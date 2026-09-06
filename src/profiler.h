#ifndef PROFILER_H
#define PROFILER_H

#include <chrono>
#include <climits>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <memory>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

class Profiler {
public:
    struct Stats {
        long long total_time_us = 0;
        long long min_time_us = LLONG_MAX;
        long long max_time_us = 0;
        long long call_count = 0;

        void add_sample(long long time_us) {
            total_time_us += time_us;
            min_time_us = std::min(min_time_us, time_us);
            max_time_us = std::max(max_time_us, time_us);
            call_count++;
        }

        void merge(const Stats& other) {
            if (other.call_count == 0) return;
            total_time_us += other.total_time_us;
            min_time_us = std::min(min_time_us, other.min_time_us);
            max_time_us = std::max(max_time_us, other.max_time_us);
            call_count += other.call_count;
        }

        double avg_time_us() const {
            return call_count > 0 ? static_cast<double>(total_time_us) / call_count : 0.0;
        }
    };

    static Profiler& instance() {
        static Profiler inst;
        return inst;
    }

    void record(const std::string& name, long long duration_us) {
        ThreadStats& ts = thread_stats();
        std::lock_guard<std::mutex> lock(ts.mtx);
        ts.stats[name].add_sample(duration_us);
    }

    void print_report() {
        std::unordered_map<std::string, Stats> merged = collect();

        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "PROFILING REPORT\n";
        std::cout << std::string(80, '=') << "\n\n";

        std::vector<std::pair<std::string, Stats>> sorted_stats;
        long long total_lock_wait_time = 0;
        long long total_lock_calls = 0;

        for (const auto& [name, stat] : merged) {
            sorted_stats.push_back({name, stat});
            if (name.substr(0, 10) == "LOCK_WAIT:") {
                total_lock_wait_time += stat.total_time_us;
                total_lock_calls += stat.call_count;
            }
        }
        std::sort(sorted_stats.begin(), sorted_stats.end(),
                  [](const auto& a, const auto& b) {
                      return a.second.total_time_us > b.second.total_time_us;
                  });

        std::cout << std::left << std::setw(30) << "Function"
                  << std::right << std::setw(12) << "Calls"
                  << std::setw(12) << "Total(ms)"
                  << std::setw(12) << "Avg(us)"
                  << std::setw(12) << "Min(us)"
                  << std::setw(12) << "Max(us)" << "\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& [name, stat] : sorted_stats) {
            std::cout << std::left << std::setw(30) << name
                      << std::right << std::setw(12) << stat.call_count
                      << std::setw(12) << std::fixed << std::setprecision(2)
                      << (stat.total_time_us / 1000.0)
                      << std::setw(12) << std::fixed << std::setprecision(2)
                      << stat.avg_time_us()
                      << std::setw(12) << stat.min_time_us
                      << std::setw(12) << stat.max_time_us << "\n";
        }

        std::cout << "\n" << std::string(80, '=') << "\n";

        if (total_lock_calls > 0) {
            std::cout << "\n=== LOCK CONTENTION SUMMARY ===\n";
            std::cout << "Total Lock Operations: " << total_lock_calls << "\n";
            std::cout << "Total Time Waiting for Locks: "
                      << std::fixed << std::setprecision(2)
                      << (total_lock_wait_time / 1000.0) << " ms\n";
            std::cout << "Average Lock Wait Time: "
                      << std::fixed << std::setprecision(2)
                      << (static_cast<double>(total_lock_wait_time) / total_lock_calls)
                      << " μs\n";
            std::cout << "===============================\n";
        }

        std::cout << "\n" << std::string(80, '=') << "\n\n";
    }

    void reset() {
        std::shared_lock<std::shared_mutex> lock(registry_mtx_);
        for (const auto& ts : threads_) {
            std::lock_guard<std::mutex> tlock(ts->mtx);
            ts->stats.clear();
        }
    }

private:
    struct ThreadStats {
        std::unordered_map<std::string, Stats> stats;
        mutable std::mutex mtx;
    };

    std::unordered_map<std::string, Stats> collect() {
        std::unordered_map<std::string, Stats> merged;
        std::shared_lock<std::shared_mutex> lock(registry_mtx_);
        for (const auto& ts : threads_) {
            std::lock_guard<std::mutex> tlock(ts->mtx);
            for (const auto& [name, stat] : ts->stats) {
                merged[name].merge(stat);
            }
        }
        return merged;
    }

    ThreadStats& create_thread_stats() {
        std::unique_lock<std::shared_mutex> lock(registry_mtx_);
        threads_.push_back(std::make_unique<ThreadStats>());
        return *threads_.back();
    }

    ThreadStats& thread_stats() {
        thread_local ThreadStats& local = create_thread_stats();
        return local;
    }

    std::vector<std::unique_ptr<ThreadStats>> threads_;
    std::shared_mutex registry_mtx_;
};

class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& name)
        : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        Profiler::instance().record(name_, duration);
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

class LockTimer {
public:
    explicit LockTimer(const std::string& lock_name)
        : name_(lock_name), start_(std::chrono::high_resolution_clock::now()) {}

    ~LockTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        Profiler::instance().record("LOCK_WAIT:" + name_, duration);
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

#define PROFILE_CONCAT_IMPL(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_IMPL(a, b)

#ifdef ENABLE_PROFILING
#define PROFILE_SCOPE(name) ScopedTimer PROFILE_CONCAT(_profile_timer_, __LINE__)(name)
#define PROFILE_FUNCTION() ScopedTimer PROFILE_CONCAT(_profile_timer_, __LINE__)(__FUNCTION__)
#define PROFILE_LOCK(name) LockTimer PROFILE_CONCAT(_profile_lock_, __LINE__)(name)
#else
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_FUNCTION() ((void)0)
#define PROFILE_LOCK(name) ((void)0)
#endif

class SystemMonitor {
public:
    struct SystemStats {
        size_t active_connections = 0;
        size_t total_objects = 0;
        size_t grid_operations = 0;
        size_t messages_processed = 0;
        size_t messages_sent = 0;
    };

    static SystemMonitor& instance() {
        static SystemMonitor inst;
        return inst;
    }

    void increment_connections() { active_connections_.fetch_add(1, std::memory_order_relaxed); }
    void decrement_connections() { active_connections_.fetch_sub(1, std::memory_order_relaxed); }
    void set_total_objects(size_t count) { total_objects_.store(count, std::memory_order_relaxed); }
    void increment_grid_ops() { grid_operations_.fetch_add(1, std::memory_order_relaxed); }
    void increment_msg_processed() { messages_processed_.fetch_add(1, std::memory_order_relaxed); }
    void increment_msg_sent() { messages_sent_.fetch_add(1, std::memory_order_relaxed); }

    SystemStats get_stats() const {
        SystemStats s;
        s.active_connections = active_connections_.load(std::memory_order_relaxed);
        s.total_objects = total_objects_.load(std::memory_order_relaxed);
        s.grid_operations = grid_operations_.load(std::memory_order_relaxed);
        s.messages_processed = messages_processed_.load(std::memory_order_relaxed);
        s.messages_sent = messages_sent_.load(std::memory_order_relaxed);
        return s;
    }

    void print_stats() const {
        auto s = get_stats();
        std::cout << "\n=== SYSTEM STATISTICS ===\n"
                  << "Active Connections: " << s.active_connections << "\n"
                  << "Total Objects: " << s.total_objects << "\n"
                  << "Grid Operations: " << s.grid_operations << "\n"
                  << "Messages Processed: " << s.messages_processed << "\n"
                  << "Messages Sent: " << s.messages_sent << "\n"
                  << "=========================\n\n";
    }

    void reset() {
        active_connections_.store(0, std::memory_order_relaxed);
        total_objects_.store(0, std::memory_order_relaxed);
        grid_operations_.store(0, std::memory_order_relaxed);
        messages_processed_.store(0, std::memory_order_relaxed);
        messages_sent_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<size_t> active_connections_{0};
    std::atomic<size_t> total_objects_{0};
    std::atomic<size_t> grid_operations_{0};
    std::atomic<size_t> messages_processed_{0};
    std::atomic<size_t> messages_sent_{0};
};

#endif // PROFILER_H
