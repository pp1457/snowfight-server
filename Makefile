# Compiler and flags
CC = clang++
ifdef TSAN
SANITIZER_FLAGS = -fsanitize=thread
endif
CFLAGS = -Iinclude -I/usr/local/include -I./src/uWebSockets/src -I./src/uWebSockets/uSockets/src -I./src/msgpack -std=c++20 -Wall -Wextra -O2 $(SANITIZER_FLAGS)
LDFLAGS = -L/usr/local/lib -L./src/uWebSockets/uSockets $(SANITIZER_FLAGS)
LIBS = -luSockets -lssl -lz -lcrypto -lpthread

ifdef PROFILE
CFLAGS += -DENABLE_PROFILING
endif

# Directories
SRC_DIR = src
BUILD_DIR = build

# Find all source files and corresponding object files
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRC_FILES))

# Final executable
TARGET = server

# Default target
all: $(TARGET)

# Link the final binary
$(TARGET): $(OBJ_FILES)
	$(CC) $(CFLAGS) $(OBJ_FILES) $(LDFLAGS) $(LIBS) -o $(TARGET)

# Compile each source file
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure the build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Run the server
run: all
	./$(TARGET)

# Grid locking microbenchmark (see benchmark/GRID_LOCKS.md)
bench: bin/grid_lock_bench

bin/grid_lock_bench: benchmark/grid_lock_bench.cpp
	mkdir -p bin
	$(CC) -std=c++20 -Wall -Wextra -O2 -pthread $< -o $@

.PHONY: all clean run bench
