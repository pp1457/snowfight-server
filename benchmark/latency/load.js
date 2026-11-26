// Replica of the earlier session's perf_short.js (damage 0), plus an upstream/downstream split.
import ws from 'k6/ws';
import { Trend, Counter } from 'k6/metrics';

const pingRtt = new Trend('ping_rtt', true);
const pingUp = new Trend('ping_up', true);
const pingDown = new Trend('ping_down', true);
const updates = new Counter('batch_updates_received');

export const options = {
  stages: [
    { duration: '5s', target: +__ENV.VUS },
    { duration: '30s', target: +__ENV.VUS },
  ],
  summaryTrendStats: ['med', 'p(95)', 'p(99)', 'max'],
};

export default function () {
  const id = `p_${__VU}`;
  let x = Math.random() * 1600, y = Math.random() * 1600, n = 0;
  ws.connect(`wss://localhost:${__ENV.PORT}`, {}, function (socket) {
    socket.on('open', () => {
      socket.send(JSON.stringify({ type: 'join', id, username: id, position: { x, y }, health: 100, size: 20, timeUpdate: Date.now() }));
      socket.setInterval(() => {
        x = Math.max(0, Math.min(1600, x + (Math.random() - 0.5) * 6));
        y = Math.max(0, Math.min(1600, y + (Math.random() - 0.5) * 6));
        socket.send(JSON.stringify({ type: 'movement', objectType: 'player', id, position: { x, y }, timeUpdate: Date.now() }));
        if (Math.random() < 0.3) {
          const a = Math.random() * 6.283, s = 200 + Math.random() * 100;
          socket.send(JSON.stringify({ type: 'movement', objectType: 'snowball', id: `snowball_${id}_${n++}`, position: { x, y },
            velocity: { x: Math.cos(a) * s, y: Math.sin(a) * s }, size: 5, damage: 0, charging: false, lifeLength: 5000, timeUpdate: Date.now() }));
        }
        socket.send(JSON.stringify({ type: 'ping', clientTime: Date.now() }));
      }, 100);
    });
    socket.on('binaryMessage', () => { updates.add(1); });
    socket.on('message', (m) => {
      if (m.indexOf('pong') === -1) return;
      const now = Date.now();
      const j = JSON.parse(m);
      pingRtt.add(now - j.clientTime);
      pingUp.add(j.serverUs / 1000 - j.clientTime);
      pingDown.add(now - j.serverUs / 1000);
    });
    socket.setTimeout(() => socket.close(), 36000);
  });
}
