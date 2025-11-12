#include "server_worker.h"
#include "profiler.h"

using json = nlohmann::json;

ServerWorker::ServerWorker() {}

// Sends a pong response for a "ping" message.
void ServerWorker::handlePing(auto *ws, const json &message, uWS::OpCode opCode) {
    PROFILE_SCOPE("handlePing");
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
void ServerWorker::handleJoin(auto * /*ws*/, const json &message, const std::shared_ptr<Player>& player_ptr) {
    PROFILE_SCOPE("handleJoin");
    // Set the player's ID and attributes using default values if keys are missing.
    player_ptr->set_id(message.value("id", "unknown"));
    player_ptr->set_username(message.value("username", "unknown"));

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
    player_ptr->set_life_length((long long)(4e18));

    // Insert the player into the grid.
    grid->Insert(player_ptr);
}

// Processes a "movement" message.
void ServerWorker::handleMovement(auto * /*ws*/, const json &message, const std::shared_ptr<Player>& player_ptr) {
    PROFILE_SCOPE("handleMovement");
    if (!message.contains("objectType")) return;
    if (message["objectType"] == "player") {
        // Handle player movement.
        long long time_update = message.value("timeUpdate", 0LL);
        
        double new_x = player_ptr->get_x();
        double new_y = player_ptr->get_y();

        if (message.contains("position") &&
            message["position"].contains("x") &&
            message["position"].contains("y")) {
            new_x = message["position"]["x"].get<double>();
            new_y = message["position"]["y"].get<double>();
        }

        player_ptr->set_x(new_x);
        player_ptr->set_y(new_y);
        player_ptr->set_time_update(time_update);

        grid->Update(player_ptr, 0);

    } else if (message["objectType"] == "snowball") {
        // Handle snowball movement.
        std::string snowball_id = message.value("id", "unknown");
        bool is_new = false;
        std::shared_ptr<Snowball> snowball_ptr;

        if (!thread_objects.count(snowball_id)) {
            snowball_ptr = std::make_shared<Snowball>(snowball_id, "snowball");
            thread_objects[snowball_id] = snowball_ptr;
            is_new = true;
        } else {
            snowball_ptr = std::static_pointer_cast<Snowball>(thread_objects[snowball_id]);
        }

        // std::cout << "thread_object's size: " << thread_objects_.size() << std::endl;

        double x = 0.0, y = 0.0, vx = 0.0, vy = 0.0;
        double size = message.value("size", 1.0);
        long long time_update = message.value("timeUpdate", 0LL);
        long long life_length = message.value("lifeLength", static_cast<long long>(4e18));
        int damage = message.value("damage", 0);
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

//------------------------------------------------------------------------------
// Refactored HandleMessage implementation
//------------------------------------------------------------------------------
void ServerWorker::HandleMessage(auto *ws, std::string_view str_message, uWS::OpCode opCode) {
    PROFILE_FUNCTION();
    SystemMonitor::instance().increment_msg_processed();
    
    // Fast path: check for ping without full JSON parse
    if (str_message.find("\"ping\"") != std::string_view::npos) {
        json message = json::parse(str_message);
        handlePing(ws, message, opCode);
        return;
    }
    
    json message = json::parse(str_message);
    std::string type = message.value("type", "");

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

std::string ExtractPlayerId(const std::string& snowballId) {
    size_t firstUnderscore = snowballId.find('_');
    size_t secondUnderscore = snowballId.find('_', firstUnderscore + 1);

    if (firstUnderscore == std::string::npos || secondUnderscore == std::string::npos) {
        return "not_snowball";
    }

    return snowballId.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1);
}

void UpdatePlayerView(auto *ws, auto player_ptr) {
    PROFILE_SCOPE("UpdatePlayerView");

    double lower_y = player_ptr->get_y() - (constants::FIXED_VIEW_HEIGHT);
    double upper_y = lower_y + 2 * constants::FIXED_VIEW_HEIGHT;
    double left_x = player_ptr->get_x() - (constants::FIXED_VIEW_WIDTH);
    double right_x = left_x + 2 * constants::FIXED_VIEW_WIDTH;
    
    // Get neighbors - use auto to allow move semantics/RVO
    auto neighbors = grid->Search(lower_y, upper_y, left_x, right_x);
    
    // Build batch message with MessagePack
    auto now = std::chrono::system_clock::now();
    long long current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Use thread_local buffer to avoid repeated allocations
    thread_local msgpack::sbuffer buffer;
    buffer.clear(); // Reuse the buffer
    msgpack::packer<msgpack::sbuffer> pk(&buffer);
    
    {
        PROFILE_SCOPE("UpdatePlayerView_BuildMsgPack");
        
        // Pack batch message as map: {messageType: "batch_update", timestamp: xxx, updates: [...]}
        pk.pack_map(3);
        
        pk.pack("messageType");
        pk.pack("batch_update");
        
        pk.pack("timestamp");
        pk.pack(current_time);
        
        pk.pack("updates");
        
        // First pass: collect valid objects and handle collisions
        std::vector<std::shared_ptr<GameObject>> valid_objects;
        valid_objects.reserve(neighbors.size()); // Reserve to avoid reallocation
        for (auto obj : neighbors) {
            // Skip the player themselves
            if (obj->get_id() == player_ptr->get_id()) {
                continue;
            }
            
            // Skip dead objects that expired (beyond grace period for death notification)
            // Allow recently dead objects to be sent so clients can see death state
            if (obj->get_is_dead() && obj->Expired(current_time)) {
                continue;
            }
            
            // Handle collision with damaging objects
            if (obj->get_damage() && ExtractPlayerId(obj->get_id()) != player_ptr->get_id() && obj->Collide(player_ptr)) {
                player_ptr->Hurt(ws, obj->get_damage());
                // Don't send this object (it just collided)
            } else {
                valid_objects.push_back(obj);
            }
        }
        
        // Pack array of updates with correct count
        pk.pack_array(valid_objects.size());
        
        // Second pass: pack all valid objects
        for (auto obj : valid_objects) {
            obj->ToMsgPack(pk, current_time);
        }
    }
    
    // Send binary message
    if (buffer.size() > 0) {
        PROFILE_SCOPE("UpdatePlayerView_WebSocketSend");
        ws->send(std::string_view(buffer.data(), buffer.size()), uWS::OpCode::BINARY);
        SystemMonitor::instance().increment_msg_sent();
    }
}

void HandleThreadClients(struct us_timer_t * /*t*/) {
    PROFILE_SCOPE("HandleThreadClients");
    auto clients_copy = thread_clients;
    auto now = std::chrono::system_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    for (auto *ws : clients_copy) {
        auto player_ptr = ws->getUserData()->player;
        if (player_ptr->get_is_dead()) {
            thread_clients.erase(ws);
            return;
        }
        if (player_ptr->Expired(current_time)) {
            grid->Remove(player_ptr);
        } else {
            UpdatePlayerView(ws, player_ptr);
        }
    }
}
void HandleThreadObjects(struct us_timer_t * /*t*/) {
    PROFILE_SCOPE("HandleThreadObjects");
    
    // Get current time once, outside the loop
    auto now = std::chrono::system_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Update total objects count
    SystemMonitor::instance().set_total_objects(thread_objects.size());
    
    // Collect objects to remove (avoid modifying map while iterating)
    std::vector<std::string> to_remove;
    to_remove.reserve(32); // Pre-allocate for typical cleanup size
    
    // Iterate through objects and collect those that need removal
    for (auto &[id, obj] : thread_objects) {
        if (!obj) {
            to_remove.push_back(id);
            continue;
        }
        
        if (obj->get_is_dead()) {
            to_remove.push_back(id);
            grid->Remove(obj);
        } else if (obj->Expired(current_time)) {
            to_remove.push_back(id);
            grid->Remove(obj);
        } else {
            grid->Update(obj, current_time);
        }
    }
    
    // Remove dead/expired objects from map
    for (const auto& id : to_remove) {
        thread_objects.erase(id);
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
            SystemMonitor::instance().increment_connections();
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cout << "Client connected!" << std::endl;
        },
        .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
            HandleMessage(ws, message, opCode);
        },
        .close = [](auto *ws, int /*code*/, std::string_view /*message*/) {
            grid->Remove(ws->getUserData()->player);
            thread_clients.erase(ws);
            SystemMonitor::instance().decrement_connections();
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
    us_timer_set(playerTimer, HandleThreadClients, 20, 10);  // 10ms (100Hz) - MessagePack optimization allows faster updates

    // Timer for snowball position updates (every 250ms)
    struct us_timer_t *objectTimer = us_create_timer(loop, 0, 0);
    us_timer_set(objectTimer, HandleThreadObjects, 250, 30);  // Keep objects at 30ms

    sslApp.run();
}
