#include "server_worker.h"

using json = nlohmann::json;

ServerWorker::ServerWorker() {}

// Sends a pong response for a "ping" message.
void ServerWorker::handlePing(auto *ws, const json &message, uWS::OpCode opCode) {
    long long clientTime = message.value("clientTime", 0LL);
    auto now = std::chrono::system_clock::now();
    auto serverTime = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    json pongMsg = {
        {"messageType", "pong"},
        {"serverTime", serverTime},
        {"clientTime", clientTime}
    };

    ws->send(pongMsg.dump(), opCode);
}

// Processes a "join" message.
void ServerWorker::handleJoin(auto * /*ws*/, const json &message, std::shared_ptr<Player> player_ptr) {
    // Set the player's ID and attributes using default values if keys are missing.
    player_ptr->set_id(message.value("id", "unknown"));
    player_ptr->set_player_id(message.value("id", "unknown"));

    double x = 0.0, y = 0.0;
    int health = message.value("health", 100);
    double size = message.value("size", 20.0);
    long long time_update = message.value("timeUpdate", 0LL);

    // Extract position if provided.
    if (message.contains("position") &&
        message["position"].contains("x") &&
        message["position"].contains("y")) {
        x = message["position"]["x"].get<double>();
        y = message["position"]["y"].get<double>();
    }

    if (x < 0 || y < 0 || x > grid->get_width() || y > grid->get_height()) {
        return;
    }

    player_ptr->set_health(health);
    player_ptr->set_x(x);
    player_ptr->set_y(y);
    player_ptr->set_size(size);
    player_ptr->set_time_update(time_update);

    // Insert the player into the grid.
    grid->Insert(player_ptr);
}

std::string ExtractPlayerId(const std::string& snowballId) {
    size_t firstUnderscore = snowballId.find('_');
    size_t secondUnderscore = snowballId.find('_', firstUnderscore + 1);

    if (firstUnderscore == std::string::npos || secondUnderscore == std::string::npos) {
        return "unknown";
    }

    return snowballId.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1);
}

// Processes a "movement" message.
void ServerWorker::handleMovement(auto * /*ws*/, const json &message, std::shared_ptr<Player> player_ptr) {
    if (!message.contains("objectType")) return;
    if (message["objectType"] == "player") {
        // Handle player movement.
        
        double vx = 0, vy = 0;
        long long time_update = message.value("timeUpdate", 0LL);

        if (message.contains("direction")) {
            bool x_change = false, y_change = false;
            if (message["direction"]["left"].get<bool>()) {
                vx -= 1; x_change |= 1;
            }
            if (message["direction"]["right"].get<bool>()) {
                vx += 1; x_change |= 1;
            }
            if (message["direction"]["up"].get<bool>()) {
                vy -= 1; y_change |= 1;
            }
            if (message["direction"]["down"].get<bool>()) {
                vy += 1; y_change |= 1;
            }

            vx *= constants::PLAYER_SPEED;
            vy *= constants::PLAYER_SPEED;

            if (x_change && y_change) {
                vx /= constants::SQRT_2;
                vy /= constants::SQRT_2;
            }
        }

        player_ptr->set_time_update(time_update);
        player_ptr->set_vx(vx);
        player_ptr->set_vy(vy);

    } else if (message["objectType"] == "snowball") {
        // Handle snowball movement.
        std::string snowball_id = message.value("id", "unknown");
        bool is_new = false;
        std::shared_ptr<Snowball> snowball_ptr;

        if (!thread_objects.count(snowball_id)) {
            snowball_ptr = std::make_shared<Snowball>(snowball_id, "snowball");
            snowball_ptr->set_player_id(ExtractPlayerId(snowball_id));
            snowball_ptr->set_is_penetrable(true);
            thread_objects[snowball_id] = snowball_ptr;
            is_new = true;
        }
        else {
            snowball_ptr = std::static_pointer_cast<Snowball>(thread_objects[snowball_id]);
        }

        // std::cout << "thread_object's size: " << thread_objects_.size() << std::endl;

        double x = 0.0, y = 0.0, vx = 0.0, vy = 0.0;
        double size = message.value("size", 1.0);
        long long time_update = message.value("timeUpdate", 0LL);
        long long life_length = message.value("lifeLength", static_cast<long long>(4e18));
        int damage = message.value("damage", 5);
        bool charging = message.value("charging", false);

        if (message.contains("position") &&
            message["position"].contains("x") &&
            message["position"].contains("y")) {
            x = message["position"]["x"].get<double>();
            y = message["position"]["y"].get<double>();
        }
        if (message.contains("velocity") &&
            message["velocity"].contains("x") &&
            message["velocity"].contains("y")) {
            vx = message["velocity"]["x"].get<double>();
            vy = message["velocity"]["y"].get<double>();
        }

        snowball_ptr->set_x(x);
        snowball_ptr->set_y(y);
        snowball_ptr->set_vx(vx);
        snowball_ptr->set_vy(vy);
        snowball_ptr->set_size(size);
        snowball_ptr->set_time_update(time_update);
        snowball_ptr->set_life_length(life_length);
        snowball_ptr->set_charging(charging);
        snowball_ptr->set_damage(damage);

        if (is_new) {
            grid->Insert(snowball_ptr);
        }
    }
}

void ServerWorker::HandleMessage(auto *ws, std::string_view str_message, uWS::OpCode opCode) {
    json message = json::parse(str_message);
    std::string type = message.value("type", "");

    { 
        std::unique_lock<std::shared_mutex> lock(output_mtx);
        std::cout << message.dump(4) << std::endl;
    }

    // Handle ping separately.
    if (type == "ping") {
        handlePing(ws, message, opCode);
        return;
    }

    // Retrieve the player's pointer from user data.
    auto player_ptr = ws->getUserData()->player;

    if (type == "join") {
        handleJoin(ws, message, player_ptr);
    }
    else if (type == "movement") {
        handleMovement(ws, message, player_ptr);
    }
}

//------------------------------------------------------------------------------
// Minimal implementations for Start, UpdatePlayerView, StartServer, and main.
// Adjust these as needed for your application.
//

void ServerWorker::Start(int port) {
    worker_thread_ = std::thread(&ServerWorker::StartServer, this, port);
}


void UpdatePlayerView(auto *ws, auto player_ptr) {

    double lower_y = player_ptr->get_y() - (constants::FIXED_VIEW_HEIGHT);
    double upper_y = lower_y + 2 * constants::FIXED_VIEW_HEIGHT;
    double left_x = player_ptr->get_x() - (constants::FIXED_VIEW_WIDTH);
    double right_x = left_x + 2 * constants::FIXED_VIEW_WIDTH;
    
    std::vector<std::shared_ptr<GameObject>> neighbors = grid->Search(lower_y, upper_y, left_x, right_x);

    auto now = std::chrono::system_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    double x_cand[3], y_cand[3];
    int valid_xy = 0;
    x_cand[0] = player_ptr->get_cur_x(current_time); 
    y_cand[0] = player_ptr->get_cur_y(current_time);
    x_cand[1] = x_cand[1]; 
    y_cand[1] = player_ptr->get_y();
    x_cand[2] = player_ptr->get_x();
    y_cand[2] = y_cand[0];
     
    for (auto obj : neighbors) if (obj->get_id() != player_ptr->get_id()) {
        if (!obj->get_is_penetrable() && obj->Touch(player_ptr)) {
            while (valid_xy < 3) {
                if (obj->Closer(player_ptr)) {
                    valid_xy++;
                } else {
                    break;
                }
            }
            if (valid_xy == 3) break;
        }
    }
    
    if (valid_xy < 3) {
        player_ptr->set_x(x_cand[valid_xy]);
        player_ptr->set_y(y_cand[valid_xy]);
    }
    player_ptr->set_time_update(current_time);

    for (auto obj : neighbors) {
        if (obj->get_damage() && obj->get_player_id() != player_ptr->get_id()) {
            if (obj->Collide(player_ptr, current_time)) {
                player_ptr->Hurt(ws, obj->get_damage());
            }
        } else {
            obj->SendMessageToClient(ws, "movement");
        }
    }

    player_ptr->SendMessageToClient(ws, "movement");
    grid->Update(player_ptr);
}

void HandleThreadClients(struct us_timer_t * /*t*/) {
    auto clients_copy = thread_clients;
    for (auto *ws : clients_copy) {
        auto player_ptr = ws->getUserData()->player;
        if (player_ptr->get_is_dead()) {
            grid->Remove(player_ptr);
            thread_clients.erase(ws);
            return;
        }
        UpdatePlayerView(ws, player_ptr);
    }
}

void HandleThreadObjects(struct us_timer_t * /*t*/) {
    auto objects_copy = thread_objects;
    
    for (auto &[id, obj] : objects_copy) {
        if (obj) { 
            auto now = std::chrono::system_clock::now();
            auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
                
            if (obj->get_is_dead()) {
                thread_objects.erase(id);
            } else if (obj->Expired(current_time)) {
                thread_objects.erase(id);
                grid->Remove(obj);
            } else {
                obj->set_x(obj->get_cur_x(current_time));
                obj->set_y(obj->get_cur_y(current_time));
                obj->set_life_length(obj->get_life_length() - (current_time - obj->get_time_update()));
                obj->set_time_update(current_time);
                grid->Update(obj);
            }
        }
    }
}

void ServerWorker::StartServer(int port) {
    // Create an SSL app with required certificate and key file options.
    uWS::SSLApp sslApp = uWS::SSLApp({
        .key_file_name = "private/key.pem",
        .cert_file_name = "private/cert.pem"
    })
    .ws<PointerToPlayer>("/*", {
        .open = [](auto *ws) {
            ws->getUserData()->player = std::make_shared<Player>();
            ws->getUserData()->player->set_type("player");
            thread_clients.insert(ws);
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cout << "Client connected!" << std::endl;
        },
        .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
            HandleMessage(ws, message, opCode);
        },
        .close = [](auto *ws, int /*code*/, std::string_view /*message*/) {
            grid->Remove(ws->getUserData()->player);
            thread_clients.erase(ws);
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cout << "Client disconnected!" << std::endl;
        }
    })
    .listen(port, [&](auto *listenSocket) {
        if (listenSocket) {
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cout << "Listening on port " << port << std::endl;
        } else {
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cerr << "Failed to start the server" << std::strerror(errno) << std::endl;
        }
    });

    struct us_loop_t *loop = (struct us_loop_t *) uWS::Loop::get();
    struct us_timer_t *playerTimer = us_create_timer(loop, 0, 0);
    us_timer_set(playerTimer, HandleThreadClients, 20, 10);

    // Timer for snowball position updates (every 250ms)
    struct us_timer_t *objectTimer = us_create_timer(loop, 0, 0);
    us_timer_set(objectTimer, HandleThreadObjects, 250, 10);

    sslApp.run();
}
