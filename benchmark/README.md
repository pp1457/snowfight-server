# Snowfight Server Performance Testing & Profiling Guide

## Overview
This guide provides comprehensive instructions for profiling and performance testing your Snowfight multiplayer game server in realistic conditions.

## Table of Contents
1. [Quick Start](#quick-start)
2. [Load Testing with k6](#load-testing)
3. [Server-Side Profiling](#server-profiling)
4. [macOS Instruments Profiling](#instruments)
5. [Interpreting Results](#interpreting-results)
6. [Performance Optimization Tips](#optimization)

---

## Quick Start

### Prerequisites
```bash
# Install k6 for load testing
brew install k6

# Ensure Python 3 is installed (for analysis scripts)
python3 --version

# Make scripts executable
chmod +x benchmark/benchmark.sh
chmod +x benchmark/analyze_results.py
```

### Run Your First Benchmark
```bash
# Terminal 1: Build and run your server
cd /Users/liaoyunyang/Projects/Game/IO/snowfight/server
make clean && make
./server

# Terminal 2: Run a quick benchmark
cd /Users/liaoyunyang/Projects/Game/IO/snowfight/server
./benchmark/benchmark.sh quick
```

---

## Load Testing with k6

### Test Profiles

#### 1. Quick Test (Development)
- **Duration:** 30 seconds
- **Max Users:** 10
- **Use Case:** Quick validation during development
```bash
./benchmark/benchmark.sh quick
```

#### 2. Standard Test (Normal Load)
- **Duration:** 5 minutes
- **Max Users:** 50
- **Use Case:** Typical game load, good for baseline metrics
```bash
./benchmark/benchmark.sh standard
```

#### 3. Stress Test (High Load)
- **Duration:** 10 minutes
- **Max Users:** 200
- **Use Case:** Testing limits and finding breaking points
```bash
./benchmark/benchmark.sh stress
```

#### 4. Endurance Test (Stability)
- **Duration:** 30 minutes
- **Max Users:** 100
- **Use Case:** Memory leak detection, long-term stability
```bash
./benchmark/benchmark.sh endurance
```

### Custom k6 Tests

Edit `benchmark/load_test.js` to customize:
- User behavior patterns
- Message frequencies
- Snowball throw rates
- Connection patterns

Run directly with k6:
```bash
k6 run --vus 100 --duration 10m benchmark/load_test.js
```

---

## Server-Side Profiling

### Option 1: Built-in Profiler (Recommended)

Add profiling to your server code by including the profiler header.

**Step 1:** Add profiler to your includes in `server_worker.cpp`:
```cpp
#include "profiler.h"
```

**Step 2:** Add profiling macros to critical functions:
```cpp
void ServerWorker::HandleMessage(auto *ws, std::string_view str_message, uWS::OpCode opCode) {
    PROFILE_FUNCTION();  // <-- Add this line
    
    json message = json::parse(str_message);
    // ... rest of code
}

void ServerWorker::handleMovement(auto *ws, const json &message, std::shared_ptr<Player> player_ptr) {
    PROFILE_SCOPE("handleMovement");  // <-- Add this line
    
    // ... rest of code
}
```

**Step 3:** Add profiling to grid operations in `grid.cpp`:
```cpp
void Grid::Search(double lower_y, double upper_y, double left_x, double right_x) {
    PROFILE_FUNCTION();  // <-- Add this line
    SystemMonitor::instance().increment_grid_ops();  // Track operations
    
    // ... rest of code
}
```

**Step 4:** Print profiling report periodically in `main.cpp`:
```cpp
int main() {
    // ... server setup ...
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        // Print profiling report every 60 seconds
        Profiler::instance().print_report();
        SystemMonitor::instance().print_stats();
        Profiler::instance().reset();  // Reset for next interval
    }
}
```

**Step 5:** Rebuild and run:
```bash
make clean && make
./server
```

During load testing, you'll see profiling reports every 60 seconds showing:
- Function call counts
- Average, min, max execution times
- Total time spent in each function
- Active connections and message counts

### Option 2: Compile with Profiling Tools

#### Using gprof
```bash
# Modify Makefile CFLAGS to add: -pg
make clean
make CFLAGS="-pg -O2 -std=c++20"
./server

# After running load test, generate report
gprof ./server gmon.out > analysis.txt
```

#### Using Valgrind (if available on macOS)
```bash
# Check for memory leaks
valgrind --leak-check=full --show-leak-kinds=all ./server

# Profile with callgrind
valgrind --tool=callgrind ./server
kcachegrind callgrind.out.*
```

---

## macOS Instruments Profiling

### Using Xcode Instruments (Best for macOS)

#### CPU Time Profiler
```bash
# Start your server
./server &
SERVER_PID=$!

# Record CPU profile for 60 seconds
xcrun xctrace record \
    --template 'Time Profiler' \
    --attach $SERVER_PID \
    --output cpu_profile.trace \
    --time-limit 60s

# While profiling, run load test in another terminal
./benchmark/benchmark.sh quick

# Open results in Instruments
open cpu_profile.trace
```

**What to look for:**
- Hot spots (functions consuming most CPU)
- Call tree depth
- Time spent in JSON parsing
- Grid search operations
- WebSocket handling overhead

#### Memory Allocations Profiler
```bash
xcrun xctrace record \
    --template 'Allocations' \
    --attach $SERVER_PID \
    --output memory_profile.trace \
    --time-limit 60s
```

**What to look for:**
- Memory leaks
- Growing heap over time
- Allocation patterns
- Object lifecycle issues

#### System Trace
```bash
xcrun xctrace record \
    --template 'System Trace' \
    --attach $SERVER_PID \
    --output system_trace.trace \
    --time-limit 30s
```

**What to look for:**
- Thread utilization
- Lock contention
- System calls
- I/O patterns

---

## Interpreting Results

### Key Metrics to Monitor

#### 1. **Latency Metrics**
- **Ping Latency:** Should be < 50ms (P95)
- **Message Processing:** Should be < 100ms (P95)
- **WebSocket Connect:** Should be < 2000ms

#### 2. **Throughput Metrics**
- **Messages/Second:** Higher is better
- **Active Connections:** Should match expected load
- **Grid Operations/Second:** Monitor for bottlenecks

#### 3. **Resource Usage**
- **CPU:** Should not max out all cores constantly
  - 4 workers × 100% = 400% max theoretical
  - Target: < 300% under normal load
- **Memory:** Should be stable, not growing
  - Watch for memory leaks in endurance tests
- **Threads:** Should match worker count + OS overhead
  - Expected: ~10-20 threads total

#### 4. **Error Rates**
- **Connection Failures:** Should be < 0.1%
- **Message Errors:** Should be near 0%
- **Timeouts:** Should be < 1%

### Sample Good Results
```
Standard Test (50 users):
- Ping Latency (P95): 45ms
- Message Latency (P95): 85ms
- CPU Usage: 180% average
- Memory: Stable at 250MB
- Messages/sec: ~5000
- Connection Success: 100%
```

### Warning Signs
- **High Latency:** P95 > 200ms → Bottleneck in processing
- **Growing Memory:** Indicates memory leak
- **High CPU:** Sustained > 90% per core → Need optimization
- **Lock Contention:** Threads blocking on `grid` mutex
- **Connection Drops:** Network issues or server overload

---

## Performance Optimization Tips

### 1. **Grid System Optimization**
```cpp
// Current: Shared grid with mutex locking
// Consider: 
// - Increase cell size to reduce search operations
// - Use read-write locks (already done with shared_mutex)
// - Implement spatial hash with better granularity
// - Pre-allocate grid cells to avoid dynamic allocation
```

### 2. **Message Processing**
```cpp
// Profile JSON parsing - it can be slow
// Consider:
// - Use simdjson for faster parsing
// - Binary protocol instead of JSON (e.g., MessagePack, Protobuf)
// - Message batching to reduce overhead
```

### 3. **Thread Pool Sizing**
```cpp
// Current: 4 workers
// Experiment with:
int workers_num = std::thread::hardware_concurrency();
// Or profile to find optimal count for your workload
```

### 4. **Update Rate Optimization**
```cpp
// Current: 10ms for players, 10ms for objects
// Consider:
// - Adaptive update rates based on load
// - Prioritize nearby players (already done with grid search)
// - Skip updates for off-screen objects
```

### 5. **Memory Management**
```cpp
// Use object pools for frequently allocated objects
// Pre-allocate common structures
// Consider arena allocators for game objects
```

---

## Benchmark Workflow

### Recommended Testing Sequence

1. **Baseline** (No Load)
   ```bash
   # Measure idle resource usage
   ps aux | grep server
   ```

2. **Quick Validation**
   ```bash
   ./benchmark/benchmark.sh quick
   # Verify basic functionality
   ```

3. **Standard Load**
   ```bash
   ./benchmark/benchmark.sh standard
   # Establish baseline metrics
   ```

4. **Profile Hot Spots**
   ```bash
   # Run standard test + profile simultaneously
   # Identify bottlenecks
   ```

5. **Stress Test**
   ```bash
   ./benchmark/benchmark.sh stress
   # Find breaking point
   ```

6. **Endurance Test**
   ```bash
   ./benchmark/benchmark.sh endurance
   # Check for memory leaks
   ```

7. **Analyze & Optimize**
   ```bash
   python3 benchmark/analyze_results.py benchmark/results/*.json
   # Review bottlenecks, implement fixes
   ```

8. **Regression Test**
   ```bash
   # Re-run standard test
   # Compare before/after metrics
   ```

---

## Analyzing Results

```bash
# View k6 summary
k6 inspect benchmark/results/standard_*.json

# Detailed analysis with Python script
python3 benchmark/analyze_results.py \
    benchmark/results/standard_*.json \
    benchmark/results/resources_*.csv

# Open Instruments traces
open benchmark/results/*.trace
```

---

## Common Issues & Solutions

### Issue: High Message Latency
**Symptoms:** P95 > 200ms
**Solutions:**
- Profile `HandleMessage` function
- Optimize JSON parsing (use simdjson)
- Check grid search performance
- Reduce lock contention

### Issue: Memory Growth
**Symptoms:** Memory continuously increasing
**Solutions:**
- Check `thread_objects` cleanup
- Verify `grid->Remove()` is called
- Use Instruments Leaks template
- Ensure shared_ptr cycles are broken

### Issue: CPU Saturation
**Symptoms:** All cores at 100%
**Solutions:**
- Profile CPU-intensive functions
- Optimize grid search algorithm
- Reduce update frequency
- Batch operations

### Issue: Connection Drops
**Symptoms:** k6 reports connection failures
**Solutions:**
- Check system `ulimit -n`
- Increase max file descriptors
- Review WebSocket timeout settings
- Check SSL certificate validity

---

## Next Steps

1. **Run initial benchmark suite**
   ```bash
   ./benchmark/benchmark.sh all
   ```

2. **Add profiling to your code**
   - Include `profiler.h`
   - Add `PROFILE_FUNCTION()` macros
   - Rebuild and test

3. **Establish baselines**
   - Document current performance
   - Set target metrics

4. **Iterative optimization**
   - Profile → Identify bottleneck → Optimize → Test → Repeat

5. **Production monitoring**
   - Set up continuous monitoring
   - Alert on performance degradation
   - Regular load tests

---

## Additional Resources

- [k6 Documentation](https://k6.io/docs/)
- [uWebSockets Performance Tips](https://github.com/uNetworking/uWebSockets)
- [C++ Performance Optimization](https://www.agner.org/optimize/)
- [Game Server Architecture](https://gafferongames.com/)

---

## Support

For questions or issues:
1. Check profiling output for bottlenecks
2. Review k6 test results
3. Analyze Instruments traces
4. Compare against baseline metrics

Happy profiling! 🚀
