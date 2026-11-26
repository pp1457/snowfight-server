// Microbenchmark for the spatial grid's locking strategy.
//
// Mirrors src/grid.{h,cpp}: each cell holds an unordered_set<shared_ptr<Obj>>,
// Search() copies shared_ptrs out of every overlapping cell, and an object that
// changes cell is a Remove() + Insert(). Only the lock layout changes:
//
//   cell-rw       one std::shared_mutex per cell   (what src/grid.h does)
//   cell-mutex    one std::mutex per cell
//   global-rw     one std::shared_mutex for the whole grid
//   global-mutex  one std::mutex for the whole grid
//
// Each thread owns a slice of the objects (as each worker loop owns its
// players' snowballs) and loops: with probability --write it moves one of its
// objects, otherwise it runs a player view query (the AoI rectangle from
// server_worker.cpp) and collision-checks the results outside the lock.
//
//   ./grid_lock_bench locks    [--n 32000] [--world density|fixed] [--threads 1,2,4,8]
//                              [--write 0,0.5,0.9] [--policy all] [--seconds 1] [--reps 3] [--cell 100]
//   ./grid_lock_bench examined [--counts 250,1000,4000,16000,32000]
//
// Output is CSV on stdout.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace {

// From src/constants.h and src/main.cpp.
constexpr double kViewHalfW = 1600 / 2.0 + 200;
constexpr double kViewHalfH = 900 / 2.0 + 200;
int kCellSize = 100;  // src/main.cpp; --cell overrides
constexpr int kFixedWorld = 1600;
constexpr double kPlayerSize = 30;

// Objects per unit^2 for --world density. Chosen so that a view query
// examines ~120 objects once the world is larger than the view.
constexpr double kDefaultDensity = 4.1e-5;

struct Obj {
    std::atomic<double> x{0}, y{0};  // written by the owning thread, read by searchers
    double size = 10;
    int cell = 0;                    // only touched by the owning thread
};
using ObjPtr = std::shared_ptr<Obj>;

template <typename M>
struct ReadGuard {
    M &m;
    explicit ReadGuard(M &mtx) : m(mtx) {
        if constexpr (std::is_same_v<M, std::shared_mutex>) m.lock_shared(); else m.lock();
    }
    ~ReadGuard() {
        if constexpr (std::is_same_v<M, std::shared_mutex>) m.unlock_shared(); else m.unlock();
    }
};

template <typename M, bool kGlobal>
class Grid {
    struct Cell {
        std::unordered_set<ObjPtr> objs;
        M mtx;
    };
    int side_, n_;
    std::vector<std::unique_ptr<Cell>> cells_;  // separately allocated, like src/grid.h
    M global_;

    int clamp_idx(int v) const { return std::min(std::max(v, 0), n_ - 1); }

public:
    explicit Grid(int side) : side_(side), n_((side - 1) / kCellSize + 1) {
        cells_.reserve(static_cast<size_t>(n_) * n_);
        for (int i = 0; i < n_ * n_; i++) cells_.push_back(std::make_unique<Cell>());
    }

    int cell_of(double x, double y) const {
        return clamp_idx(static_cast<int>(y) / kCellSize) * n_ + clamp_idx(static_cast<int>(x) / kCellSize);
    }

    void Insert(const ObjPtr &o) {
        o->cell = cell_of(o->x.load(std::memory_order_relaxed), o->y.load(std::memory_order_relaxed));
        std::unique_lock<M> lk(kGlobal ? global_ : cells_[o->cell]->mtx);
        cells_[o->cell]->objs.insert(o);
    }

    // Like Grid::Update: the grid is only touched when the object changes cell.
    bool Move(const ObjPtr &o, double nx, double ny) {
        o->x.store(nx, std::memory_order_relaxed);
        o->y.store(ny, std::memory_order_relaxed);
        int nc = cell_of(nx, ny);
        if (nc == o->cell) return false;
        if constexpr (kGlobal) {
            std::unique_lock<M> lk(global_);
            cells_[o->cell]->objs.erase(o);
            cells_[nc]->objs.insert(o);
        } else {
            { std::unique_lock<M> lk(cells_[o->cell]->mtx); cells_[o->cell]->objs.erase(o); }
            { std::unique_lock<M> lk(cells_[nc]->mtx);      cells_[nc]->objs.insert(o); }
        }
        o->cell = nc;
        return true;
    }

    // Returns the number of objects examined (everything in the overlapping cells).
    size_t Search(double ly, double uy, double lx, double rx, std::vector<ObjPtr> &out) {
        int r0 = clamp_idx(static_cast<int>(std::max(ly, 0.0)) / kCellSize);
        int r1 = clamp_idx(static_cast<int>(std::max(uy, 0.0)) / kCellSize);
        int c0 = clamp_idx(static_cast<int>(std::max(lx, 0.0)) / kCellSize);
        int c1 = clamp_idx(static_cast<int>(std::max(rx, 0.0)) / kCellSize);
        size_t examined = 0;
        auto copy_cell = [&](Cell &cell) {
            examined += cell.objs.size();
            out.insert(out.end(), cell.objs.begin(), cell.objs.end());
        };
        if constexpr (kGlobal) {
            ReadGuard<M> g(global_);
            for (int r = r0; r <= r1; r++)
                for (int c = c0; c <= c1; c++) copy_cell(*cells_[r * n_ + c]);
        } else {
            for (int r = r0; r <= r1; r++)
                for (int c = c0; c <= c1; c++) {
                    Cell &cell = *cells_[r * n_ + c];
                    ReadGuard<M> g(cell.mtx);
                    copy_cell(cell);
                }
        }
        return examined;
    }
};

struct alignas(64) Counters {
    std::atomic<uint64_t> searches{0}, moves{0}, crossings{0}, examined{0};
};

struct RunResult {
    double ops_per_s, searches_per_s, moves_per_s, crossing_frac, examined_avg;
};

std::vector<ObjPtr> make_objects(int n, int side, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> pos(0, side - 1);
    std::vector<ObjPtr> objs;
    objs.reserve(n);
    for (int i = 0; i < n; i++) {
        auto o = std::make_shared<Obj>();
        o->x.store(pos(rng));
        o->y.store(pos(rng));
        objs.push_back(std::move(o));
    }
    return objs;
}

template <typename G>
RunResult run_once(int n, int side, int threads, double write_frac, double seconds, double step, uint64_t seed) {
    G grid(side);
    auto objs = make_objects(n, side, seed);
    for (auto &o : objs) grid.Insert(o);

    std::vector<Counters> ctr(threads);
    std::atomic<bool> go{false}, stop{false};
    std::atomic<uint64_t> sink{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; t++) {
        ts.emplace_back([&, t] {
            std::mt19937_64 rng(seed * 7919 + t);
            std::uniform_real_distribution<double> u01(0, 1);
            size_t lo = objs.size() * t / threads, hi = objs.size() * (t + 1) / threads;
            std::vector<ObjPtr> out;
            out.reserve(4096);
            uint64_t hits = 0;
            Counters &c = ctr[t];
            while (!go.load(std::memory_order_acquire)) {}
            while (!stop.load(std::memory_order_relaxed)) {
                if (hi > lo && u01(rng) < write_frac) {
                    const ObjPtr &o = objs[lo + rng() % (hi - lo)];
                    double a = u01(rng) * 2 * M_PI;
                    double nx = std::clamp(o->x.load(std::memory_order_relaxed) + step * std::cos(a), 0.0, side - 1.0);
                    double ny = std::clamp(o->y.load(std::memory_order_relaxed) + step * std::sin(a), 0.0, side - 1.0);
                    bool crossed = grid.Move(o, nx, ny);
                    c.moves.store(c.moves.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                    if (crossed) c.crossings.store(c.crossings.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                } else {
                    double px = u01(rng) * side, py = u01(rng) * side;
                    out.clear();
                    size_t ex = grid.Search(py - kViewHalfH, py + kViewHalfH, px - kViewHalfW, px + kViewHalfW, out);
                    // Collision check outside the lock, as HandleThreadClients does.
                    for (const auto &p : out) {
                        double dx = p->x.load(std::memory_order_relaxed) - px;
                        double dy = p->y.load(std::memory_order_relaxed) - py;
                        double r = p->size + kPlayerSize;
                        hits += (dx * dx + dy * dy < r * r);
                    }
                    c.searches.store(c.searches.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                    c.examined.store(c.examined.load(std::memory_order_relaxed) + ex, std::memory_order_relaxed);
                }
            }
            sink.fetch_add(hits);
        });
    }

    auto snapshot = [&] {
        Counters s;
        for (auto &c : ctr) {
            s.searches += c.searches.load(); s.moves += c.moves.load();
            s.crossings += c.crossings.load(); s.examined += c.examined.load();
        }
        return std::array<uint64_t, 4>{s.searches.load(), s.moves.load(), s.crossings.load(), s.examined.load()};
    };

    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // warm-up
    auto a = snapshot();
    auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    auto b = snapshot();
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    stop.store(true);
    for (auto &th : ts) th.join();

    double s = double(b[0] - a[0]), m = double(b[1] - a[1]);
    return {(s + m) / dt, s / dt, m / dt, m > 0 ? double(b[2] - a[2]) / m : 0.0, s > 0 ? double(b[3] - a[3]) / s : 0.0};
}

RunResult run_policy(const std::string &policy, int n, int side, int threads, double wf, double secs, double step, uint64_t seed) {
    if (policy == "cell-rw")      return run_once<Grid<std::shared_mutex, false>>(n, side, threads, wf, secs, step, seed);
    if (policy == "cell-mutex")   return run_once<Grid<std::mutex, false>>(n, side, threads, wf, secs, step, seed);
    if (policy == "global-rw")    return run_once<Grid<std::shared_mutex, true>>(n, side, threads, wf, secs, step, seed);
    if (policy == "global-mutex") return run_once<Grid<std::mutex, true>>(n, side, threads, wf, secs, step, seed);
    std::fprintf(stderr, "unknown policy %s\n", policy.c_str());
    std::exit(1);
}

template <typename T>
std::vector<T> parse_list(const std::string &s) {
    std::vector<T> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::stringstream is(item);
        T x;
        is >> x;
        v.push_back(x);
    }
    return v;
}

int side_for(const std::string &world, int n, double density) {
    if (world == "fixed") return kFixedWorld;
    return std::max(kFixedWorld, static_cast<int>(std::sqrt(n / density)));
}

// Objects examined per view query: grid vs scanning every object.
void examined_mode(const std::vector<int> &counts, double density) {
    std::printf("n,world_side,grid_examined_avg,grid_us_per_query,scan_examined,scan_us_per_query\n");
    for (int n : counts) {
        int side = side_for("density", n, density);
        Grid<std::shared_mutex, false> grid(side);
        auto objs = make_objects(n, side, 42);
        for (auto &o : objs) grid.Insert(o);
        std::mt19937_64 rng(1);
        std::uniform_real_distribution<double> u01(0, 1);
        const int q = 2000;
        std::vector<ObjPtr> out;
        size_t ex = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < q; i++) {
            double px = u01(rng) * side, py = u01(rng) * side;
            out.clear();
            ex += grid.Search(py - kViewHalfH, py + kViewHalfH, px - kViewHalfW, px + kViewHalfW, out);
        }
        double grid_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / q;
        // No grid: test every object against the view rectangle.
        size_t found = 0;
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < q; i++) {
            double px = u01(rng) * side, py = u01(rng) * side;
            out.clear();
            for (const auto &o : objs) {
                double x = o->x.load(std::memory_order_relaxed), y = o->y.load(std::memory_order_relaxed);
                if (std::fabs(x - px) <= kViewHalfW && std::fabs(y - py) <= kViewHalfH) out.push_back(o);
            }
            found += out.size();
        }
        double scan_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / q;
        std::printf("%d,%d,%.1f,%.2f,%d,%.2f\n", n, side, double(ex) / q, grid_us, n, scan_us);
        if (found == size_t(-1)) std::puts("");  // keep the scan from being optimized away
    }
}

}  // namespace

int main(int argc, char **argv) {
    std::string mode = argc > 1 ? argv[1] : "locks";
    std::string world = "density", policies = "all", threads_s = "1,2,4,8", write_s = "0,0.5,0.9";
    std::string counts_s = "250,1000,4000,16000,32000";
    int n = 32000, reps = 3;
    double seconds = 1.0, step = 10.0, density = kDefaultDensity;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--n") n = std::stoi(v);
        else if (k == "--world") world = v;
        else if (k == "--policy") policies = v;
        else if (k == "--threads") threads_s = v;
        else if (k == "--write") write_s = v;
        else if (k == "--seconds") seconds = std::stod(v);
        else if (k == "--reps") reps = std::stoi(v);
        else if (k == "--step") step = std::stod(v);
        else if (k == "--density") density = std::stod(v);
        else if (k == "--counts") counts_s = v;
        else if (k == "--cell") kCellSize = std::stoi(v);
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 1; }
    }

    if (mode == "examined") {
        examined_mode(parse_list<int>(counts_s), density);
        return 0;
    }

    std::vector<std::string> pols = policies == "all"
        ? std::vector<std::string>{"global-mutex", "global-rw", "cell-mutex", "cell-rw"}
        : parse_list<std::string>(policies);
    int side = side_for(world, n, density);
    std::printf("policy,threads,n,world_side,write_frac,ops_per_s,searches_per_s,moves_per_s,crossing_frac,examined_avg\n");
    for (double wf : parse_list<double>(write_s))
        for (int th : parse_list<int>(threads_s))
            for (const auto &p : pols) {
                std::vector<RunResult> rs;
                for (int r = 0; r < reps; r++) rs.push_back(run_policy(p, n, side, th, wf, seconds, step, 100 + r));
                std::sort(rs.begin(), rs.end(), [](auto &a, auto &b) { return a.ops_per_s < b.ops_per_s; });
                const RunResult &m = rs[rs.size() / 2];  // median of reps
                std::printf("%s,%d,%d,%d,%.2f,%.0f,%.0f,%.0f,%.3f,%.1f\n", p.c_str(), th, n, side, wf,
                            m.ops_per_s, m.searches_per_s, m.moves_per_s, m.crossing_frac, m.examined_avg);
                std::fflush(stdout);
            }
    return 0;
}
