# 04 - Monitoring a live channel

Everything ESHM will tell you about a channel while it is running: who is
attached, whether they are still alive, and how much has moved in each
direction. A **generator** (master) makes traffic; a **watcher** (slave)
attaches and prints a stats line twice a second. Both exist in C++ and Python.

## What the API exposes

| Call | C++ | Python |
|---|---|---|
| Full statistics | `eshm_get_stats(handle, &stats)` | `conn.get_stats()` → dict |
| Negotiated role | `eshm_get_role(handle, &role)` | `conn.get_role()` |
| Peer not stale | `eshm_check_remote_alive(handle, &alive)` | `conn.is_remote_alive()` |
| Manual heartbeat | `eshm_update_heartbeat(handle)` | — |
| Error text | `eshm_error_string(rc)` | exception message |

### Every field of `ESHMStats`

| Field | Means |
|---|---|
| `master_heartbeat`, `slave_heartbeat` | Monotonic counters, bumped every `ESHM_HEARTBEAT_INTERVAL_MS` (1 ms default) by each side's heartbeat thread |
| `master_heartbeat_delta`, `slave_heartbeat_delta` | Advance **since your last `get_stats` call** — the call resets them |
| `master_pid`, `slave_pid` | PIDs of the attached processes, `0` if that side is absent |
| `master_alive`, `slave_alive` | Set in `eshm_init`, cleared in `eshm_destroy` |
| `stale_threshold` | Missed heartbeats before the monitor thread declares the peer stale |
| `m2s_write_count`, `m2s_read_count` | Master→slave channel traffic |
| `s2m_write_count`, `s2m_read_count` | Slave→master channel traffic |

## The liveness trap

`eshm_check_remote_alive()` does **not** mean "a peer is attached". It reports
the inverse of "has the peer been detected as stale", and nothing is stale
before it has ever existed — so on a brand new, empty channel it answers
*alive*. Pick the signal that matches your question:

| Question | Use |
|---|---|
| Has a peer ever attached? | `stats.slave_alive` / `stats.master_alive` |
| Is the attached peer still running? | `*_heartbeat_delta > 0`, or `eshm_check_remote_alive` once a peer has been seen |
| Which side am I? | `eshm_get_role` (matters under `ESHM_ROLE_AUTO`, see [05](../05_reconnection/)) |

The heartbeat delta is the strongest signal: a counter that has stopped moving
is a peer that has stopped, whatever the flags still say.

## Build and run

```bash
cmake -S . -B build && cmake --build build
```

```bash
# C++ generates, Python watches
./build/channel_monitor generate demo     # terminal 1
python3 peer.py watch demo                # terminal 2

# Python generates, C++ watches
python3 peer.py generate demo             # terminal 1
./build/channel_monitor watch demo        # terminal 2
```

```
#    role   | m.beat   s.beat   | m.pid  s.pid  | m2s w/r   s2m w/r   | peer
--------------------------------------------------------------------------------------------
0    SLAVE  | 1243     18       | 4711   4736   |   24/24      0/0    | master slave
     heartbeat delta since last sample: master +1243, slave +18 (stale threshold 100)
```

Stop the generator with Ctrl-C and watch `master_heartbeat_delta` fall to 0
while `master_alive` stays set for a moment — that gap is exactly what the
monitor thread uses to declare a peer stale, and what drives the reconnection
logic in example [05](../05_reconnection/).
