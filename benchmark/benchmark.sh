#!/bin/bash

# Comprehensive benchmark script for Snowfight server
# Usage: ./benchmark.sh [test_type]
# test_type: quick, standard, stress, endurance

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
SERVER_DIR="/Users/liaoyunyang/Projects/Game/IO/snowfight/server"
RESULTS_DIR="$SERVER_DIR/benchmark/results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Create results directory
mkdir -p "$RESULTS_DIR"

echo -e "${GREEN}=== Snowfight Server Benchmark Suite ===${NC}\n"

# Function to check if server is running
check_server() {
    if lsof -Pi :12345 -sTCP:LISTEN -t >/dev/null 2>&1 ; then
        echo -e "${GREEN}✓ Server is running on port 12345${NC}"
        return 0
    else
        echo -e "${RED}✗ Server is not running on port 12345${NC}"
        return 1
    fi
}

# Function to run k6 test
run_k6_test() {
    local test_name=$1
    local vus=$2
    local duration=$3
    local output_file="$RESULTS_DIR/${test_name}_${TIMESTAMP}.json"
    
    echo -e "${YELLOW}Running $test_name test (VUs: $vus, Duration: $duration)...${NC}"
    
    k6 run \
        --vus "$vus" \
        --duration "$duration" \
        --out json="$output_file" \
        "$SERVER_DIR/benchmark/load_test.js"
    
    echo -e "${GREEN}✓ Results saved to: $output_file${NC}\n"
}

# Quick test (development/debugging)
quick_test() {
    echo -e "${YELLOW}=== QUICK TEST ===${NC}"
    echo "Duration: 30s, Max users: 10"
    run_k6_test "quick" 10 "30s"
}

# Standard test (typical load)
standard_test() {
    echo -e "${YELLOW}=== STANDARD TEST ===${NC}"
    echo "Duration: 5min, Max users: 50"
    run_k6_test "standard" 50 "5m"
}

# Stress test (high load)
stress_test() {
    echo -e "${YELLOW}=== STRESS TEST ===${NC}"
    echo "Duration: 10min, Max users: 200"
    run_k6_test "stress" 200 "10m"
}

# Endurance test (stability over time)
endurance_test() {
    echo -e "${YELLOW}=== ENDURANCE TEST ===${NC}"
    echo "Duration: 30min, Max users: 100"
    run_k6_test "endurance" 100 "30m"
}

# CPU and Memory profiling with instruments (macOS)
profile_with_instruments() {
    echo -e "${YELLOW}=== PROFILING WITH INSTRUMENTS ===${NC}"
    
    # Find server PID
    SERVER_PID=$(lsof -Pi :12345 -sTCP:LISTEN -t)
    
    if [ -z "$SERVER_PID" ]; then
        echo -e "${RED}✗ Could not find server process${NC}"
        return 1
    fi
    
    echo "Server PID: $SERVER_PID"
    echo "Starting CPU profiling for 60 seconds..."
    
    # CPU profiling
    xcrun xctrace record \
        --template 'Time Profiler' \
        --attach "$SERVER_PID" \
        --output "$RESULTS_DIR/cpu_profile_${TIMESTAMP}.trace" \
        --time-limit 60s
    
    echo -e "${GREEN}✓ CPU profile saved${NC}"
    
    # Memory profiling
    echo "Starting memory profiling for 60 seconds..."
    xcrun xctrace record \
        --template 'Allocations' \
        --attach "$SERVER_PID" \
        --output "$RESULTS_DIR/memory_profile_${TIMESTAMP}.trace" \
        --time-limit 60s
    
    echo -e "${GREEN}✓ Memory profile saved${NC}"
}

# Network statistics
network_stats() {
    echo -e "${YELLOW}=== NETWORK STATISTICS ===${NC}"
    
    # Monitor network traffic on port 12345
    echo "Monitoring network traffic for 60 seconds..."
    timeout 60 nettop -P -J bytes_in,bytes_out -p 12345 > "$RESULTS_DIR/network_${TIMESTAMP}.txt" 2>&1 &
    
    # Wait for monitoring
    sleep 60
    
    echo -e "${GREEN}✓ Network stats saved${NC}"
}

# System resource monitoring
monitor_resources() {
    echo -e "${YELLOW}=== MONITORING SYSTEM RESOURCES ===${NC}"
    
    local duration=$1
    local interval=1
    local output_file="$RESULTS_DIR/resources_${TIMESTAMP}.csv"
    
    echo "timestamp,cpu_percent,memory_mb,threads,open_files" > "$output_file"
    
    SERVER_PID=$(lsof -Pi :12345 -sTCP:LISTEN -t)
    
    if [ -z "$SERVER_PID" ]; then
        echo -e "${RED}✗ Could not find server process${NC}"
        return 1
    fi
    
    echo "Monitoring PID $SERVER_PID for ${duration} seconds..."
    
    for ((i=0; i<duration; i+=interval)); do
        timestamp=$(date +"%Y-%m-%d %H:%M:%S")
        
        # Get CPU usage
        cpu=$(ps -p "$SERVER_PID" -o %cpu= | tr -d ' ')
        
        # Get memory usage in MB
        mem=$(ps -p "$SERVER_PID" -o rss= | awk '{print $1/1024}')
        
        # Get thread count
        threads=$(ps -M -p "$SERVER_PID" | wc -l | tr -d ' ')
        
        # Get open files count
        files=$(lsof -p "$SERVER_PID" 2>/dev/null | wc -l | tr -d ' ')
        
        echo "$timestamp,$cpu,$mem,$threads,$files" >> "$output_file"
        
        sleep "$interval"
    done
    
    echo -e "${GREEN}✓ Resource monitoring saved to: $output_file${NC}"
}

# Latency breakdown test
latency_test() {
    echo -e "${YELLOW}=== LATENCY BREAKDOWN TEST ===${NC}"
    
    # Use a custom script to measure different latency components
    echo "Measuring WebSocket handshake, message processing, and response times..."
    
    # This would require a custom tool, for now we'll use k6
    run_k6_test "latency" 1 "1m"
}

# Generate HTML report
generate_report() {
    echo -e "${YELLOW}=== GENERATING REPORT ===${NC}"
    
    # Create a simple HTML report
    local report_file="$RESULTS_DIR/report_${TIMESTAMP}.html"
    
    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Snowfight Server Benchmark Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        h1 { color: #333; }
        table { border-collapse: collapse; width: 100%; margin: 20px 0; }
        th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }
        th { background-color: #4CAF50; color: white; }
        tr:nth-child(even) { background-color: #f2f2f2; }
        .metric { display: inline-block; margin: 10px; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }
        .metric-value { font-size: 24px; font-weight: bold; color: #4CAF50; }
    </style>
</head>
<body>
    <h1>Snowfight Server Benchmark Report</h1>
    <p>Generated: $(date)</p>
    
    <h2>Test Summary</h2>
    <p>Results are saved in: $RESULTS_DIR</p>
    
    <h2>Files Generated</h2>
    <ul>
$(ls -lh "$RESULTS_DIR"/*"${TIMESTAMP}"* | awk '{print "<li>" $9 " (" $5 ")</li>"}')
    </ul>
    
    <h2>Next Steps</h2>
    <ol>
        <li>Open JSON files with <code>k6 report</code> or analyze with jq</li>
        <li>Open .trace files with Instruments app</li>
        <li>Review CSV files for resource usage trends</li>
        <li>Compare results across different test runs</li>
    </ol>
</body>
</html>
EOF
    
    echo -e "${GREEN}✓ HTML report generated: $report_file${NC}"
    
    # Open report in browser
    open "$report_file"
}

# Main script logic
main() {
    local test_type=${1:-standard}
    
    # Check if server is running
    if ! check_server; then
        echo -e "${RED}Please start the server first: cd server && make run${NC}"
        exit 1
    fi
    
    # Check if k6 is installed
    if ! command -v k6 &> /dev/null; then
        echo -e "${RED}k6 is not installed. Install with: brew install k6${NC}"
        exit 1
    fi
    
    echo "Starting benchmark suite at: $(date)"
    echo "Results directory: $RESULTS_DIR"
    echo ""
    
    case "$test_type" in
        quick)
            quick_test
            ;;
        standard)
            standard_test
            monitor_resources 300 &  # 5 minutes
            wait
            ;;
        stress)
            stress_test
            monitor_resources 600 &  # 10 minutes
            wait
            ;;
        endurance)
            endurance_test
            monitor_resources 1800 &  # 30 minutes
            wait
            ;;
        profile)
            echo "Run a load test in another terminal, then this will profile..."
            sleep 5
            profile_with_instruments
            ;;
        latency)
            latency_test
            ;;
        all)
            quick_test
            sleep 30
            standard_test
            sleep 30
            latency_test
            ;;
        *)
            echo -e "${RED}Unknown test type: $test_type${NC}"
            echo "Available types: quick, standard, stress, endurance, profile, latency, all"
            exit 1
            ;;
    esac
    
    # Generate report
    generate_report
    
    echo -e "${GREEN}=== Benchmark Complete ===${NC}"
    echo "View results in: $RESULTS_DIR"
}

# Run main function
main "$@"
