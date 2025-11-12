// k6 WebSocket Load Test for Snowfight Server
// Run with: k6 run --vus 50 --duration 5m load_test.js

import ws from 'k6/ws';
import { check } from 'k6';
import { Counter, Trend } from 'k6/metrics';

// Custom metrics
const messagesReceived = new Counter('messages_received');
const messageLatency = new Trend('message_latency');
const pingLatency = new Trend('ping_latency');

// Configuration for different load profiles
export const options = {
    stages: [
        { duration: '30s', target: 10 },   // Ramp up to 10 users
        { duration: '1m', target: 50 },    // Ramp up to 50 users
        { duration: '2m', target: 100 },   // Ramp up to 100 users
        { duration: '1m', target: 100 },   // Stay at 100 users
        { duration: '30s', target: 0 },    // Ramp down
    ],
    thresholds: {
        'message_latency': ['p(95)<100', 'p(99)<200'], // 95% under 100ms, 99% under 200ms
        'ping_latency': ['p(95)<50'],
        'ws_connecting': ['max<5000'], // Connection time under 5s
    }
};

// Helper to generate random position
function randomPosition(max) {
    return Math.floor(Math.random() * max);
}

// Helper to generate random movement
function randomMovement(current, max, speed = 5) {
    const delta = (Math.random() - 0.5) * speed * 2;
    return Math.max(0, Math.min(max, current + delta));
}

export default function () {
    const playerId = `player_${__VU}_${Date.now()}`;
    const url = 'wss://localhost:12345';
    
    // Player state
    let playerX = randomPosition(1600);
    let playerY = randomPosition(1600);
    let snowballCounter = 0;
    
    const params = {
        headers: {
            'Origin': 'https://localhost'
        }
    };

    ws.connect(url, params, function (socket) {
        socket.on('open', () => {
            console.log(`[${playerId}] Connected to server`);
            
            // Send join message
            const joinMsg = {
                type: 'join',
                id: playerId,
                username: `Player_${__VU}`,
                position: { x: playerX, y: playerY },
                health: 100,
                size: 20,
                timeUpdate: Date.now()
            };
            socket.send(JSON.stringify(joinMsg));

            // Simulate realistic gameplay
            socket.setInterval(() => {
                const now = Date.now();
                
                // 1. Send movement update (player moving)
                playerX = randomMovement(playerX, 1600, 3);
                playerY = randomMovement(playerY, 1600, 3);
                
                const moveMsg = {
                    type: 'movement',
                    objectType: 'player',
                    id: playerId,
                    position: { x: playerX, y: playerY },
                    timeUpdate: now
                };
                socket.send(JSON.stringify(moveMsg));
                
                // 2. Randomly throw snowballs (30% chance each update)
                if (Math.random() < 0.3) {
                    const angle = Math.random() * 2 * Math.PI;
                    const speed = 200 + Math.random() * 100; // 200-300 speed
                    
                    const snowballMsg = {
                        type: 'movement',
                        objectType: 'snowball',
                        id: `snowball_${playerId}_${snowballCounter++}`,
                        position: { x: playerX, y: playerY },
                        velocity: {
                            x: Math.cos(angle) * speed,
                            y: Math.sin(angle) * speed
                        },
                        size: 5,
                        damage: 10,
                        charging: false,
                        lifeLength: 5000,
                        timeUpdate: now
                    };
                    socket.send(JSON.stringify(snowballMsg));
                }
                
                // 3. Send ping periodically (every 3rd update)
                if (Math.random() < 0.33) {
                    const pingTime = Date.now();
                    socket.send(JSON.stringify({
                        type: 'ping',
                        clientTime: pingTime
                    }));
                }
            }, 100); // Update every 100ms (10 updates per second)
        });

        socket.on('message', (data) => {
            messagesReceived.add(1);
            
            try {
                const msg = JSON.parse(data);
                const now = Date.now();
                
                if (msg.messageType === 'pong') {
                    const latency = now - msg.clientTime;
                    pingLatency.add(latency);
                } else {
                    // Track general message latency based on timeUpdate
                    if (msg.timeUpdate) {
                        const latency = now - msg.timeUpdate;
                        if (latency >= 0 && latency < 10000) { // Sanity check
                            messageLatency.add(latency);
                        }
                    }
                }
            } catch (e) {
                console.error(`[${playerId}] Error parsing message:`, e);
            }
        });

        socket.on('close', () => {
            console.log(`[${playerId}] Disconnected`);
        });

        socket.on('error', (e) => {
            console.error(`[${playerId}] WebSocket error:`, e);
        });

        // Keep connection alive for test duration
        socket.setTimeout(() => {
            socket.close();
        }, 60000); // Stay connected for 1 minute per VU
    });
}
