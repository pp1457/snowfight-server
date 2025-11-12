# Snowfight Server Profiling - Quick Reference

## 🚀 Quick Start Commands

```bash
# 1. Start your server
cd /Users/liaoyunyang/Projects/Game/IO/snowfight/server
make clean && make
./server

# 2. In another terminal, run load test
./benchmark/benchmark.sh quick    # 30s, 10 users - Quick validation
./benchmark/benchmark.sh standard # 5m, 50 users - Normal load
./benchmark/benchmark.sh stress   # 10m, 200 users - High load
./benchmark/benchmark.sh endurance # 30m, 100 users - Stability test
```

## 📊 What Each Test Measures

### Quick Test (30s, 10 users)
- ✅ Basic functionality validation
- ✅ Smoke test before deeper testing
- ✅ Quick iteration during development

### Standard Test (5m, 50 users)
- ✅ Realistic game load
- ✅ Baseline performance metrics
- ✅ Typical player behavior

### Stress Test (10m, 200 users)
- ✅ Server limits and breaking points
- ✅ Resource exhaustion scenarios
- ✅ Maximum throughput

### Endurance Test (30m, 100 users)
- ✅ Memory leak detection
- ✅ Long-term stability
- ✅ Resource degradation over time

## 🔍 Key Metrics to Watch

### Latency (Lower is Better)
- **Ping Response:** < 50ms (P95)
- **Message Processing:** < 100ms (P95)
- **WebSocket Connect:** < 2000ms

### Resource Usage
- **CPU:** < 300% (with 4 workers)
- **Memory:** Stable, not growing
- **Connections:** Should match test load

### Throughput (Higher is Better)
- **Messages/Second:** Monitor trends
- **Grid Operations:** Check for bottlenecks

## 🛠️ Profiling Tools

### 1. Built-in Profiler (Add to your code)
```cpp
#include "profiler.h"

void MyFunction() {
    PROFILE_FUNCTION();  // Times this function
    // your code
}
```

### 2. macOS Instruments
```bash
# CPU profiling
xcrun xctrace record --template 'Time Profiler' \
    --attach $(lsof -Pi :12345 -sTCP:LISTEN -t) \
    --output cpu.trace --time-limit 60s

# Memory profiling  
xcrun xctrace record --template 'Allocations' \
    --attach $(lsof -Pi :12345 -sTCP:LISTEN -t) \
    --output mem.trace --time-limit 60s

# Open results
open cpu.trace
```

## 📈 Analyzing Results

```bash
# View k6 summary
k6 inspect benchmark/results/standard_*.json

# Detailed Python analysis
python3 benchmark/analyze_results.py benchmark/results/standard_*.json

# Compare multiple runs
python3 benchmark/analyze_results.py \
    benchmark/results/before_optimization.json \
    benchmark/results/after_optimization.json
```

## 🎯 Performance Targets

### Good Performance
```
Latency:
  Ping (P95): < 50ms
  Message (P95): < 100ms
  
Resources:
  CPU: 150-250% (avg)
  Memory: Stable
  
Throughput:
  Messages/sec: > 1000
  Connection Success: > 99%
```

### Warning Signs
- ⚠️ Latency P95 > 200ms → Bottleneck
- ⚠️ Memory growing → Leak
- ⚠️ CPU > 90% per core → Need optimization
- ⚠️ Connections dropping → Overload

## 🔧 Common Optimizations

### If High Latency
1. Profile `HandleMessage` function
2. Optimize JSON parsing (consider simdjson)
3. Check grid search performance
4. Reduce mutex lock contention

### If Memory Growing
1. Check `thread_objects` cleanup
2. Verify `grid->Remove()` calls
3. Use Instruments Leaks template
4. Break shared_ptr cycles

### If CPU Saturated
1. Profile CPU-intensive functions
2. Optimize grid search algorithm
3. Reduce update frequency
4. Batch operations

### If Connection Issues
1. Check `ulimit -n` (file descriptors)
2. Review WebSocket settings
3. Verify SSL certificates
4. Check network capacity

## 📝 Workflow

1. **Establish Baseline**
   ```bash
   ./benchmark/benchmark.sh standard
   # Save results as baseline
   ```

2. **Make Changes**
   ```bash
   # Edit code, optimize
   ```

3. **Rebuild & Test**
   ```bash
   make clean && make
   ./server
   ./benchmark/benchmark.sh standard
   ```

4. **Compare Results**
   ```bash
   python3 benchmark/analyze_results.py \
       baseline.json new_results.json
   ```

5. **Repeat**
   - Profile hotspots
   - Optimize bottlenecks
   - Validate improvements

## 📚 File Locations

```
server/
├── benchmark/
│   ├── README.md            # Full documentation
│   ├── benchmark.sh         # Main test runner
│   ├── monitor.sh           # Real-time dashboard
│   ├── analyze_results.py   # Results analysis
│   ├── load_test.js         # k6 test script
│   └── results/             # Test outputs
└── src/
    └── profiler.h           # Built-in profiler
```

## 🆘 Troubleshooting

### Server won't start
```bash
# Check if port is in use
lsof -i :12345

# Check SSL certificates
ls -la server/private/*.pem

# Monitor server process
ps aux | grep server
```

### k6 connection fails
```bash
# Test SSL connection
openssl s_client -connect localhost:12345

# Check server logs
# Look for "Listening on port 12345"
```

### No results generated
```bash
# Check k6 is installed
k6 version

# Run with verbose output
k6 run --verbose benchmark/load_test.js
```

## 💡 Pro Tips

1. **Always run baseline first** before making changes
2. **Run tests multiple times** for consistent results
3. **Monitor during tests** with `monitor.sh`
4. **Profile before optimizing** - don't guess!
5. **Test realistic scenarios** - match actual gameplay
6. **Check memory over time** - run endurance tests
7. **Version your results** - track improvements

## 🎓 Learn More

- Full guide: `server/benchmark/README.md`
- k6 docs: https://k6.io/docs/
- uWebSockets: https://github.com/uNetworking/uWebSockets

---

**Happy Profiling! 🚀**
