#ifndef GAME_OBJECT_H
#define GAME_OBJECT_H

#include <string>
#include <memory>
#include <chrono>
#include <uWebSockets/App.h>

#include "nlohmann/json.hpp"
#include "msgpack.hpp"

using json = nlohmann::json;

class Player;

struct PointerToPlayer {
    std::shared_ptr<Player> player;
};

class GameObject {
public:
    // Constructors and destructor
    GameObject()
        : type_("unknown"), id_("unknown"), username_("unknown"),
          x_(0), y_(0), vx_(0), vy_(0), size_(1),
          row_(0), col_(0), health_(100), damage_(0),
          time_update_(0), life_length_(1000),
          is_dead_(false) {}

    GameObject(std::string id, std::string type)
        : type_(std::move(type)), id_(std::move(id)), username_("unknown"),
          x_(0), y_(0), vx_(0), vy_(0), size_(1),
          row_(0), col_(0), health_(100), damage_(0),
          time_update_(0), life_length_(1000),
          is_dead_(false) {}

    virtual ~GameObject() = default;

    // Getters - methods defined in class body are implicitly inline
    const std::string& get_type() const { return type_; }
    const std::string& get_id() const { return id_; }
    const std::string& get_username() const { return username_; }
    double get_x() const { return x_; }
    double get_y() const { return y_; }
    double get_vx() const { return vx_; }
    double get_vy() const { return vy_; }
    double get_size() const { return size_; }
    int get_row() const { return row_; }
    int get_col() const { return col_; }
    int get_health() const { return health_; }
    int get_damage() const { return damage_; }
    long long get_time_update() const { return time_update_; }
    long long get_life_length() const { return life_length_; }
    bool get_is_dead() const { return is_dead_; }

    // Virtual functions for current position calculations
    virtual double get_cur_x(long long /*current_time*/) const { return x_; }
    virtual double get_cur_y(long long /*current_time*/) const { return y_; }

    // Setters - pass strings by value and move (copy elision optimization)
    void set_type(std::string type) { type_ = std::move(type); }
    void set_id(std::string id) { id_ = std::move(id); }
    void set_username(std::string username) { username_ = std::move(username); }
    void set_x(double x) { x_ = x; }
    void set_y(double y) { y_ = y; }
    void set_vx(double vx) { vx_ = vx; }
    void set_vy(double vy) { vy_ = vy; }
    void set_size(double size) { size_ = size; }
    void set_row(int row) { row_ = row; }
    void set_col(int col) { col_ = col; }
    void set_health(int health) { health_ = health; }
    void set_damage(int damage) { damage_ = damage; }
    void set_time_update(long long time_update) { time_update_ = time_update; }
    void set_life_length(long long life_length) { life_length_ = life_length; }
    void set_is_dead(bool is_dead) { is_dead_ = is_dead; }

    // Default implementation of get_charging; can be overridden by derived classes.
    virtual bool get_charging() const { return false; }

    // Other member functions (pass shared_ptr by const reference to avoid refcount overhead)
    [[nodiscard]] bool Expired(long long current_time);
    [[nodiscard]] bool Collide(const std::shared_ptr<GameObject>& obj);
    void Hurt(uWS::WebSocket<true, true, PointerToPlayer>* ws, int damage);
    [[nodiscard]] json ToJson(long long current_time, std::string messageType = "movement");
    void ToMsgPack(msgpack::packer<msgpack::sbuffer>& pk, long long current_time) const;
    virtual void SendMessageToClient(uWS::WebSocket<true, true, PointerToPlayer>* ws, std::string type);

protected:
    std::string type_, id_, username_;
    double x_, y_, vx_, vy_, size_;
    int row_, col_, health_, damage_;
    long long time_update_, life_length_;
    bool is_dead_;
};

class Player : public GameObject {
    // Additional Player-specific members and methods can be declared here.
};

class Snowball : public GameObject {
public:
    Snowball(const std::string& id, const std::string& type)
        : GameObject(id, type), charging_(false) {}

    // Override to compute current position based on elapsed time.
    virtual double get_cur_x(long long current_time) const override {
        long long elapsed_time = current_time - get_time_update();
        return get_x() + get_vx() * (elapsed_time / 1000.0);
    }

    virtual double get_cur_y(long long current_time) const override {
        long long elapsed_time = current_time - get_time_update();
        return get_y() + get_vy() * (elapsed_time / 1000.0);
    }

    // Override get_charging to return the snowball's charging state.
    bool get_charging() const override { return charging_; }
    void set_charging(bool charging) { charging_ = charging; }

private:
    bool charging_;
};

#endif // GAME_OBJECT_H
