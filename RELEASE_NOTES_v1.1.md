# ESHM v1.1.0 Release Notes

## Overview

v1.1.0 replaces the read path's 100 µs polling loop with **push wakeup**: a
blocking read now parks on a futex inside the shared segment and is woken by
the peer's write. On the round-trip benchmark that is the difference between
**5,492 and 1,981,817 round trips per second** between two C++ peers,
and 5,235 vs 205,882 between C++ and Python.

It also reorganises the examples into nine numbered directories that each pair
a C++ program with a Python peer, and fixes five bugs that this work
uncovered — including one that meant **C++ → Python decoding had never worked**
despite being documented as supported.

## Upgrading from 1.0.0

| | Changed? | What it means |
|---|---|---|
| **C ABI** | No | New symbols only; `SOVERSION` stays 1. Anything that merely links against ESHM keeps working without recompilation. |
| **Shared-memory protocol** | **Yes** — v2 → v3 | **Both ends of a channel must be rebuilt together.** A v2 peer and a v3 peer will refuse to attach. |
| **Default behaviour** | Yes | Blocking reads park instead of polling. Opt out with `eshm_set_wakeup_mode(handle, ESHM_WAKEUP_POLL)`. |
| **Attach validation** | Yes | Mismatched `version` or `ESHM_MAX_DATA_SIZE` now fails at `eshm_init()` with a diagnostic, where it previously succeeded and crashed later. |

The protocol version is tracked separately from the library version precisely
because they moved independently here: `ESHM_VERSION` went 2 → 3 while the C
ABI stayed compatible.

## Push wakeup

```c
// Nothing to do - push wakeup is on by default.
ESHMHandle* h = eshm_init(&config);

// Wait until data arrives, however long that takes.
eshm_read_ex(h, buf, sizeof(buf), &n, ESHM_TIMEOUT_INFINITE);

// Or opt out and drive your own loop.
eshm_set_wakeup_mode(h, ESHM_WAKEUP_POLL);
```

```python
from eshm import ESHM, ESHMRole, ESHMWakeupMode, TIMEOUT_INFINITE

with ESHM("channel", role=ESHMRole.SLAVE) as conn:
    print(conn.wakeup_mode)                 # ESHMWakeupMode.PUSH
    data = conn.read(timeout_ms=TIMEOUT_INFINITE)
    conn.wakeup_mode = ESHMWakeupMode.POLL  # opt out
```

### Measured

All figures from `examples/08_benchmark` and `test/functional/test_wakeup.cpp`
on one machine (WSL2). Ratios travel; absolute numbers do not — run it on your
own hardware.

| | Push (default) | Poll (previous) |
|---|---|---|
| Round trips/sec, C++ ↔ C++ | **1,981,817** | 5,492 |
| Per round trip, C++ ↔ C++ | **0.5 µs** | 182.1 µs |
| Round trips/sec, C++ ↔ Python | **205,882** | 5,235 |
| Per round trip, C++ ↔ Python | **4.9 µs** | 191.0 µs |
| CPU used over a 300 ms idle wait | **0.5 ms** | 18.2 ms |
| Context switches, same wait | **6** | 1,702 |

The round-trip gap is that large because the old path slept 100 µs on *every*
miss, so a request/response pair paid two sleeps per exchange. Streaming
throughput, where the reader rarely misses, is unaffected.

### How it works

- One futex word per direction, in `ESHMChannel`, carved from existing padding.
- The writer bumps it and calls `FUTEX_WAKE` **only if a reader is registered**,
  so a hot producer/consumer pair makes no syscall on the write side.
- The reader spins briefly before parking, so a hot pair makes none on the read
  side either — it never registers as a waiter.
- Parks are capped at 50 ms internally, so a peer that dies surfaces through the
  existing stale-detection path rather than hanging.
- A parked reader is woken before reconnection unmaps the segment.

An eventfd cannot do this job: an fd is an index into one process's table and
cannot be published through shared memory, and sharing one between unrelated
processes needs `SCM_RIGHTS` over a Unix socket — a second IPC channel to
bootstrap the first. A `pthread_cond_t` in shared memory can, but hangs when
its peer dies, which is the exact failure ESHM's lock-free design exists to
survive. A futex waiter whose waker dies simply times out.

## Protocol v3

Four fields, all carved from existing padding so **no struct changed size**
(`static_assert`s enforce it):

| Field | Where | Purpose |
|---|---|---|
| `wake_seq` | `ESHMChannel` | The futex word |
| `waiters` | `ESHMChannel` | Parked reader count; lets the writer skip the syscall |
| `features` | `ESHMHeader` | Capability bitmask for future additive features |
| `layout_size` | `ESHMHeader` | `sizeof(ESHMData)` of the creating build |

`eshm_init()` now validates magic, `version` and `layout_size` on attach.
Before this, `version` was written once at creation and **never read by
anything**.

## Bugs fixed

**C++ → Python DER decoding never worked.** The C++ encoder emits the SEQUENCE
tag as `0x30` (correct DER, constructed bit set); the Python decoder compared
against a bare `0x10` and rejected it. Python → C++ worked because the C++
decoder masks with `& 0x1F`, which is why this went unnoticed — but the README
advertised the reverse direction as working with "0 decode errors". Fixed in
the Python decoder; the wire format is unchanged.

**Mismatched `ESHM_MAX_DATA_SIZE` caused SIGBUS.** The larger build mapped its
own `sizeof(ESHMData)` over a smaller segment and faulted past end-of-file on
first access. Now:

```
[ESHM] Cannot attach to '/eshm_mismatch': memory layout mismatch
       (segment is 8576 bytes, this build expects 16768 -
        ESHM_MAX_DATA_SIZE differs between the two builds)
```

**Reconnection delivered no data.** On reattach the monitor reset every counter
except `last_read_write_count`, which still held the dead master's final write
count. A new master's channel starts at 0, so everything it wrote was discarded
until it passed the old total — the slave logged `RECONNECTED`, then went
silent.

**BINARY was dropped by the native Python path.** `ESHMData.write_data()`
raised on `DataType.BINARY` and `read_data()` decoded it to `None`, though the
C API marshals it correctly.

**A writer killed mid-write hung every subsequent reader, permanently.**
`seqlock_read_begin()` spins while the sequence number is odd; a writer killed
between `seqlock_write_begin()` and `seqlock_write_end()` leaves it odd forever,
and the reader then spun at 100% CPU until its own process was killed. That is
precisely the failure the lock-free design is meant to survive. The read path
now bounds the wait at 100 ms and falls through to the caller's timeout, so
stale detection and reconnection can do their job. Pre-existing; found while
benchmarking this release.

## Examples

`examples/` is now nine numbered directories. Each is self-contained, builds
standalone against an installed ESHM, and pairs a C++ program with a `peer.py`
that talks to it **in both directions**.

| # | Directory | Covers |
|---|---|---|
| 01 | `hello_channel` | init/attach/write/read/timeouts/cleanup |
| 02 | `structured_data` | ASN.1 records, all five wire types |
| 03 | `c_api` | The ABI-stable C surface, compiled as C |
| 04 | `monitoring` | All 13 `ESHMStats` fields, role, liveness |
| 05 | `reconnection` | Every recovery knob, `disconnect_behavior`, `ROLE_AUTO` |
| 06 | `large_payload` | Payloads beyond `ESHM_MAX_DATA_SIZE` |
| 07 | `rich_types` | `Event`/`FunctionCall`/`ImageFrame` (C++ only) |
| 08 | `benchmark` | Round-trip rate; `--poll` A/Bs push against polling |
| 09 | `integration` | `find_package` / submodule / FetchContent |

```bash
./examples/run_all.sh      # every C++/Python pairing, both directions
```

## Two API traps worth knowing

**`0` means opposite things in the two halves of the API.** In `ESHMConfig`,
`reconnect_wait_ms = 0` and `max_reconnect_attempts = 0` mean *unlimited*. In
the read functions, `timeout_ms = 0` means *do not wait at all*. To block until
data arrives, pass `ESHM_TIMEOUT_INFINITE`.

**`eshm_check_remote_alive()` does not mean "a peer is attached."** It reports
the inverse of "detected as stale", and nothing is stale before it has ever
existed — so on a new, empty channel it answers *alive*. Use `slave_alive` /
`master_alive` from `eshm_get_stats()` to ask whether a peer is actually there.

## Verification

- 8/8 ctest tests pass, including the new `WakeupTest`
- 9/9 example pairings pass in both directions (`examples/run_all.sh`)
- All 9 example directories build standalone against an installed ESHM
- Reconnection verified across 3 master restarts in both language directions
- Layout sizes verified for `ESHM_MAX_DATA_SIZE` 4096 and 8192

## Requirements

Unchanged, except that push wakeup uses the Linux `futex` syscall — already
implied by ESHM's use of POSIX shared memory and `/dev/shm`.

- Linux, CMake 3.10+, C++17 (GCC 7+/Clang 5+), pthread and rt
