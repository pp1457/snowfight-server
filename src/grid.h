#ifndef GRID_H
#define GRID_H

#include <unordered_set>
#include <memory>
#include <vector>
#include <shared_mutex>

#include "game_object.h"
#include "profiler.h"

struct Cell {
    std::unordered_set<std::shared_ptr<GameObject>> objects;
    std::unique_ptr<std::shared_mutex> mtx;

    // Constructor to initialize the mutex pointer.
    Cell() : mtx(std::make_unique<std::shared_mutex>()) {}

    void Insert(const std::shared_ptr<GameObject>& obj) {
        LockTimer timer("Cell::Insert");
        std::unique_lock<std::shared_mutex> lock(*mtx);
        objects.insert(obj);
    }

    void Remove(const std::shared_ptr<GameObject>& obj) {
        LockTimer timer("Cell::Remove");
        std::unique_lock<std::shared_mutex> lock(*mtx);
        objects.erase(obj);
    }
};

class Grid {

private:
    int height_, width_;
    int cell_size_;
    int rows_, cols_;
    std::vector<std::vector<std::unique_ptr<Cell>>> cells_;

public:
    Grid(int height, int width, int cell_size);

    ~Grid();

    [[nodiscard]] int get_height() const { return height_; }
    [[nodiscard]] int get_width() const { return width_; }
    [[nodiscard]] int get_cell_size() const { return cell_size_; } // Fixed: was returning width_

    void Insert(const std::shared_ptr<GameObject>& obj);
    void Remove(const std::shared_ptr<GameObject>& obj);
    void Update(const std::shared_ptr<GameObject>& obj, long long current_time);

    [[nodiscard]] std::vector<std::shared_ptr<GameObject>> Search(double lower_y, double upper_y, double left_x, double right_x);
};

#endif
