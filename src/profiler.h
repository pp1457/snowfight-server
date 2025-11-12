#ifndef PROFILER_H
#define PROFILER_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

// Simple profiler to track function execution times
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
        
        double avg_time_us() const {
            return call_count > 0 ? static_cast<double>(total_time_us) / call_count : 0.0;
        }
    };
    
    static Profiler& instance() {
        static Profiler inst;
        return inst;
    }
    
    void record(const std::string& name, long long duration_us) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_[name].add_sample(duration_us);
    }
    
    void print_report() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "PROFILING REPORT\n";
        std::cout << std::string(80, '=') << "\n\n";
        
        // Create sorted vector by total time
        std::vector<std::pair<std::string, Stats>> sorted_stats;
        for (const auto& [name, stat] : stats_) {
            sorted_stats.push_back({name, stat});
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
        
        std::cout << "\n" << std::string(80, '=') << "\n\n";
    }
    
    void reset() {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.clear();
    }
    
private:
    std::unordered_map<std::string, Stats> stats_;
    std::shared_mutex mtx_;
};

// RAII timer for automatic profiling
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

// Macro for easy profiling
#define PROFILE_SCOPE(name) ScopedTimer _timer_##__LINE__(name)
#define PROFILE_FUNCTION() ScopedTimer _timer_##__LINE__(__FUNCTION__)

// Memory and system statistics
class SystemMonitor {
public:
    struct SystemStats {
        size_t active_connections = 0;
        size_t total_objects = 0;
        size_t grid_operations = 0;
        size_t messages_processed = 0;
        size_t messages_sent = 0;
        double cpu_usage_percent = 0.0;
        size_t memory_usage_mb = 0;
    };
    
    static SystemMonitor& instance() {
        static SystemMonitor inst;
        return inst;
    }
    
    void increment_connections() { 
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.active_connections++; 
    }
    
    void decrement_connections() { 
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.active_connections--; 
    }
    
    void set_total_objects(size_t count) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.total_objects = count;
    }
    
    void increment_grid_ops() {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.grid_operations++;
    }
    
    void increment_msg_processed() {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.messages_processed++;
    }
    
    void increment_msg_sent() {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_.messages_sent++;
    }
    
    SystemStats get_stats() {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return stats_;
    }
    
    void print_stats() {
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
        std::unique_lock<std::shared_mutex> lock(mtx_);
        stats_ = SystemStats{};
    }
    
private:
    SystemStats stats_;
    std::shared_mutex mtx_;
};

#endif // PROFILER_H
