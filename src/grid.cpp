#include "grid.h"


Grid::Grid(int height, int width, int cell_size)
    : height_(height), width_(width), cell_size_(cell_size),
      rows_((height_ - 1) / cell_size_ + 1), cols_((width_ - 1) / cell_size_ + 1),
      cells_(rows_) {
          for (auto &row: cells_) {
              for (int i = 0; i < cols_; i++) {
                  row.push_back(std::make_unique<Cell>());
              }
          }
}

Grid::~Grid() {}

void Grid::Insert(std::shared_ptr<GameObject> obj) {
    int row = static_cast<int>(obj->get_y()) / cell_size_;
    int col = static_cast<int>(obj->get_x()) / cell_size_;

    if (row >= rows_ || col >= cols_ || row < 0 || col < 0) return;

    obj->set_row(row);
    obj->set_col(col);

    cells_[row][col]->Insert(obj);
}


void Grid::Remove(std::shared_ptr<GameObject> obj) {
    int row = obj->get_row();
    int col = obj->get_col();

    if (row >= rows_ || col >= cols_ || row < 0 || col < 0) return;

    cells_[row][col]->Remove(obj);
}

void Grid::Update(std::shared_ptr<GameObject> obj, long long current_time) {
    int old_row = obj->get_row();
    int old_col = obj->get_col();
    int cur_y = static_cast<int>(obj->get_cur_y(current_time));
    int cur_x = static_cast<int>(obj->get_cur_x(current_time));
    int new_row = cur_y / cell_size_;
    int new_col = cur_x / cell_size_;

    if (new_row < 0 || new_col < 0 || new_row >= rows_ || new_col >= cols_) return; 

    if ((old_row != new_row) || (old_col != new_col)) {
        Remove(obj);
        obj->set_row(new_row); obj->set_col(new_col);
        obj->set_x(cur_x); obj->set_y(cur_y);
        obj->set_life_length(obj->get_life_length() - (current_time - obj->get_time_update()));
        obj->set_time_update(current_time);
        Insert(obj);
    }
}


std::vector<std::shared_ptr<GameObject>> Grid::Search(double lower_y, double upper_y, 
                                                      double left_x, double right_x) { 
    int lower_row = static_cast<int>(lower_y) / cell_size_;
    int upper_row = static_cast<int>(upper_y) / cell_size_;
    int left_col = static_cast<int>(left_x) / cell_size_;
    int right_col = static_cast<int>(right_x) / cell_size_;

    std::vector<std::shared_ptr<GameObject>> all;

    for (int r = lower_row; r <= upper_row; r++) {
        for (int c = left_col; c <= right_col; c++) {
            if (r >= rows_ || c >= cols_ || r < 0 || c < 0) continue;  // Boundary check
            std::shared_lock<std::shared_mutex> lock(*(cells_[r][c]->mtx));
            auto& cell_objs = cells_[r][c]->objects;
            all.insert(all.end(), cell_objs.begin(), cell_objs.end());
        }
    }

    return all;
}
