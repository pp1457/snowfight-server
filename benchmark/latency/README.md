# Ping latency study (2026-10-18)

The question: where does the ~7 ms p95 ping RTT at 50 clients come from?

## How to reproduce

```
git apply benchmark/latency/instrumentation.patch     # adds per-worker stats + "serverUs" to pong; don't commit it
make
./server 12510 2> inst.log &
PORT=12510 VUS=50 k6 run --insecure-skip-tls-verify benchmark/latency/load.js
NODE_TLS_REJECT_UNAUTHORIZED=0 node benchmark/latency/probe.mjs 12510 25 0 nojoin   # in parallel, during the steady phase
grep INST inst.log
git apply -R benchmark/latency/instrumentation.patch
```

- `load.js` replicates the earlier 50-client run (damage 0, a ping every 100 ms). It also splits each ping into two halves:
  - **up**: client → server handled;
  - **down**: server → client.
- `probe.mjs` is a separate, lightly loaded client with microsecond timestamps.
- The patch prints, every 5 s, for each worker: thread CPU %, the share of wall time spent in ticks and in message handling, tick duration, and the gap between ticks.

## Results (Apple M3, macOS, server and k6 on the same machine)

| Clients | Busy worker CPU | Tick (p50 / p95) | Ticks per s | Ping up p95 | Ping down p95 | k6 RTT p95 | Probe RTT p95 |
|---|---|---|---|---|---|---|---|
| 25 | 30% | 5.8 / 8.2 ms | 50 | 3.7 ms | 0.1 ms | 4 ms | 7.3 ms |
| 50 | 37% | 7.0 / 7.6 ms | 50 | 6.2 ms | ~0 ms | 6 ms | 8.0 ms |
| 100 | 99% | 28.6 / 32.0 ms | 34 | 50.3 ms | ~0 ms | 50 ms | 50.6 ms |

## Findings

1. **Every connection landed on one worker.** At each load level, one worker had all the clients and the other three sat at 0.1% CPU. On macOS, SO_REUSEPORT does not spread connections across sockets the way Linux does, so "4 worker event loops" is effectively 1 on this machine.
2. **The tail is server-side queueing.**
   - All of the delay is in the upstream half; the server-to-client half stays under 0.3 ms.
   - The lightly loaded probe sees the same tail as k6, so the client isn't the cause.
   - The cause: a ping that arrives during a 7 ms tick waits for the tick to finish.
   - The math fits. The tick runs 7 ms out of every 20, so ρ = 0.35. The top 5% of pings fall inside the last 86% of a tick, which gives about 6 ms of waiting plus about 0.8 ms of base cost ≈ 6.8 ms. That matches the measured p95 of 6.2–8.0 ms.
3. **The knee at 100 clients is saturation.**
   - The tick takes about 29 ms, longer than its 20 ms period. The worker sits at 99% CPU, ticks back-to-back, and drops ticks: 34 Hz, which matches the 32.5 Hz updates per client.
   - Pings wait behind whole ticks, so RTT goes to 30 ms (median) and 50 ms (p95).
   - The tick cost grows roughly with the square of the client count, because each client is sent every other client's state: 50 → 100 clients gives about 4× the tick time.
4. **Probing on a fixed period is biased.**
   - The probe pings every 100 ms, which is an exact multiple of the 20 ms tick, so each probe always hits the same phase of the tick cycle. In one run the probe that joined the game had a 4.8 ms median.
   - k6's 50 clients start at random phases, so together they sample the tick cycle fairly.
   - The fix is to randomize the probe interval (Poisson arrivals).
