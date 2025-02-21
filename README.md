## How to install
1. ```git clone --recurse-submodules git@github.com:pp1457/snowfight-server.git```
2. ``` cd snowfight-server/src/uWebSockets ```
3. ``` WITH_OPENSSL=1 make examples ``` (build uWebSocket with OPENSSL enable)
4. ```cp uSockets/uSockets.a uSockets/libuSockets.a``` (let compiler be able to find uSockets library)
