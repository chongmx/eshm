# 05 - Reconnection and role negotiation

Example [01](../01_hello_channel/) needs the master started first. This one is
about what happens *after* that: the master crashes, restarts, or is upgraded,
and the slave has to survive it. A **flaky publisher** cycles up and down on
purpose; a **resilient consumer** exposes every recovery knob as a flag so the
policies can be compared against the same failure.

## The knobs

| `ESHMConfig` field | Flag | Default | Meaning |
|---|---|---|---|
| `stale_threshold_ms` | `--stale` | 100 | Silence before the monitor thread calls the peer stale |
| `reconnect_retry_interval_ms` | `--interval` | 100 | Gap between reattach attempts |
| `max_reconnect_attempts` | `--attempts` | 50 | Attempts before giving up (**0 = unlimited**) |
| `reconnect_wait_ms` | `--wait` | 5000 | Total recovery budget (**0 = unlimited**) |
| `disconnect_behavior` | `--behavior` | `on-timeout` | What a read does while the peer is stale |

Both budgets apply: recovery stops at whichever of `--attempts` or `--wait`
runs out first, so "retry forever" needs **both** set to 0.

### `disconnect_behavior`

| Value | A read during an outage | Use when |
|---|---|---|
| `IMMEDIATELY` | Fails with `ESHM_ERROR_MASTER_STALE` | You want to know instantly and handle it yourself |
| `ON_TIMEOUT` (default) | Returns `ESHM_ERROR_TIMEOUT` while retrying underneath | You want transparent recovery with a bounded wait |
| `NEVER` | Keeps waiting, no error | The peer is expected back and there is nothing else to do |

`ESHM_ERROR_MASTER_STALE` only ever reaches the caller under `IMMEDIATELY`;
under the other two the library reports a plain timeout while it retries.

## Build and run

```bash
cmake -S . -B build && cmake --build build
```

Start the publisher, then a consumer — in either language:

```bash
# C++ publisher, Python consumer
./build/flaky_publisher demo --up 5 --down 3     # terminal 1
python3 peer.py consume demo --attempts 0 --wait 0

# Python publisher, C++ consumer
python3 peer.py publish demo --up 5 --down 3     # terminal 1
./build/resilient_consumer demo --attempts 0 --wait 0
```

```
[cycle 1] up
<- cycle 1 message 1
[cycle 1] down
[down] publisher went away - reconnecting in the background
[cycle 2] up
[up]   publisher is back (outage 1 over)
<- cycle 2 message 26
```

## Four policies worth comparing

Run the publisher with `--up 3 --down 6` (an outage longer than the default
5 s budget) and try each of these against it:

```bash
./build/resilient_consumer demo                              # gives up mid-outage
./build/resilient_consumer demo --attempts 0 --wait 0        # survives every outage
./build/resilient_consumer demo --behavior immediately       # reports MASTER_STALE at once
./build/resilient_consumer demo --behavior never --attempts 0 --wait 0
```

The first one ends with `[end] read failed: ...` — that is the 5000 ms budget
expiring, and it is the single most common surprise in production. The default
is deliberately *not* "retry forever".

## `ESHM_ROLE_AUTO`

Pass `--auto` to either consumer to join with `ESHM_ROLE_AUTO` instead of
`ESHM_ROLE_SLAVE`. AUTO takes whichever role is free — master if the segment
does not exist yet, slave if it does — so start order stops mattering:

```bash
python3 peer.py consume demo --auto      # started first: becomes MASTER
./build/resilient_consumer demo --auto   # started second: becomes SLAVE
```

Both programs print the role they actually got, via `eshm_get_role()` /
`conn.get_role()`. Two caveats: whoever wins is the one that owns the segment,
so leave `auto_cleanup` off on the side that might lose it, and check the role
you got before assuming which channel direction you are writing into.

## How recovery works underneath

1. Each side's heartbeat thread bumps its counter every
   `ESHM_HEARTBEAT_INTERVAL_MS` (1 ms default).
2. The monitor thread samples the peer's counter. Unchanged for
   `stale_threshold_ms` → peer marked stale.
3. The slave detaches and retries `shm_open` every
   `reconnect_retry_interval_ms`, until `max_reconnect_attempts` or
   `reconnect_wait_ms` runs out.
4. A restarted master bumps `master_generation`, so the slave can tell a
   genuine restart from a hiccup.

Example [04](../04_monitoring/) shows those counters directly — run
`channel_monitor watch` alongside this one to watch a stall happen live.
