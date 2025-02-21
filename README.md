## How to install
1. Clone this repository 
   ```bash
   git clone --recurse-submodules git@github.com:pp1457/snowfight-server.git
   ```
2. Move into uWebSockets repo
   ```bash
   cd snowfight-server/src/uWebSockets
   ```
3. Build uWebSockets with OPENSSL enabled
   ```bash
   WITH_OPENSSL=1 make examples
   ```
4. Let compiler be able to find uSockets library
   ```bash
   cp uSockets/uSockets.a uSockets/libuSockets.a
   ```
5. Let compiler be able to find uWebSockets header file
   ```bash
   mkdir -p src/uWebSockets; cp src/*.h src/uWebSockets
   ```
6. Go back to root
   ```bash
   cd ../../
   ```
7. Compile the server
   ```bash
   make
   ```
8. Run the server
   ```bash
   ./server
   ```

### LTO Plugin Error Fix

If you encounter errors like:
```plugin needed to handle lto object```

Follow these steps:

1. Navigate to the uSockets directory:
   ```bash
   cd src/uWebSockets/uSockets
   ```
2. Rebuild uSockets with LTO disabled:
   ```bash
   make WITH_LTO=0
   ```
3. Rename the uSockets library to lib mode 
   ```bash
   cp uSockets.a libuSockets.a
   ```
4. Back to root and compile again
