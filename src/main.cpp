#include <iostream>
#include <vector>
#include <memory>

#include "server_worker.h"
#include "profiler.h"

std::shared_mutex output_mtx;
std::shared_ptr<Grid> grid;

thread_local std::unordered_set<uWS::WebSocket<true, true, PointerToPlayer>*> thread_clients;
thread_local std::unordered_map<std::string, std::shared_ptr<GameObject>> thread_objects;

int main(int argc, char *argv[]) {
    int workers_num = 4;
    int grid_height = 1600, grid_width = 1600, grid_cell_size = 100;
    int port = 12345;

    std::vector<std::shared_ptr<ServerWorker>> workers;
    grid = std::make_shared<Grid>(grid_height, grid_width, grid_cell_size);

    for (int i = 0; i < workers_num; i++) {
        workers.push_back(std::make_shared<ServerWorker>());
        workers[i]->Start(port);
    }

    // Profiling report loop
    int report_interval = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        report_interval++;
        
        // Print profiling report every 60 seconds
        if (report_interval % 6 == 0) {
            std::unique_lock<std::shared_mutex> lock(output_mtx);
            std::cout << "\n";
            Profiler::instance().print_report();
            SystemMonitor::instance().print_stats();
            Profiler::instance().reset();
        }
    }

    return 0;
}
