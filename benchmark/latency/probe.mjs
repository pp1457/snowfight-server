// Lightweight ping probe with microsecond wall-clock timestamps.
// usage: node probe.mjs <port> <seconds> <skip_seconds> <join|nojoin>
const [port, secs, skip, mode] = [process.argv[2], +process.argv[3], +process.argv[4], process.argv[5]];
const wall_ms = () => performance.timeOrigin + performance.now();   // ms with µs precision
const rtt = [], up = [], down = [];
let updates = 0;
const ws = new WebSocket(`wss://localhost:${port}`);
ws.binaryType = 'arraybuffer';
const t0 = wall_ms();
ws.onopen = () => {
  let x = 800, y = 800;
  if (mode === 'join') ws.send(JSON.stringify({ type: 'join', id: 'probe', username: 'probe', position: { x, y }, health: 100, size: 20, timeUpdate: Date.now() }));
  setInterval(() => {
    if (mode === 'join') ws.send(JSON.stringify({ type: 'movement', objectType: 'player', id: 'probe', position: { x, y }, timeUpdate: Date.now() }));
    ws.send(JSON.stringify({ type: 'ping', clientTime: wall_ms() }));
  }, 100);
  setTimeout(() => { ws.close(); report(); process.exit(0); }, secs * 1000);
};
ws.onmessage = (ev) => {
  if (typeof ev.data !== 'string') { updates++; return; }
  const now = wall_ms();
  const j = JSON.parse(ev.data);
  if (j.messageType !== 'pong' || now - t0 < skip * 1000) return;
  rtt.push(now - j.clientTime); up.push(j.serverUs / 1000 - j.clientTime); down.push(now - j.serverUs / 1000);
};
function q(v, p) { const s = [...v].sort((a, b) => a - b); return s[Math.min(s.length - 1, Math.floor(p * s.length))]; }
function report() {
  const f = (name, v) => `${name} med=${q(v, .5).toFixed(3)} p95=${q(v, .95).toFixed(3)} p99=${q(v, .99).toFixed(3)} max=${q(v, 1).toFixed(3)} min=${q(v, 0).toFixed(3)}`;
  console.log(`PROBE mode=${mode} n=${rtt.length} updates=${updates}`);
  console.log('  ' + f('rtt ', rtt)); console.log('  ' + f('up  ', up)); console.log('  ' + f('down', down));
}
