# 10 - Named triggers: run a function on the other side

Register a function by name; the peer calls it. Register an event handler; the
peer emits to it. Works C++ → Python and Python → C++, both directions at once.

A trigger carries **a name and nothing else** — no arguments, no return value.
Values travel through whatever data structure the two sides already share, and
the trigger only says *go look*:

```
write the data  ->  fire the trigger  ->  handler reads the data
```

| | C++ | Python |
|---|---|---|
| Open | `eshm_rpc_create("demo", role)` | `Rpc("demo", role=...)` |
| Register a call | `eshm_rpc_on_call(rpc, "process", fn, ctx)` | `@rpc.on_call("process")` |
| Register an event | `eshm_rpc_on_event(rpc, "ready", fn, ctx)` | `@rpc.on_event("ready")` |
| Run the dispatcher | `eshm_rpc_start(rpc)` | `rpc.start()` / `with rpc:` |
| Ask the peer to run something | `eshm_rpc_call(rpc, "process")` | `rpc.call("process")` |
| Tell the peer something happened | `eshm_rpc_emit(rpc, "ready")` | `rpc.emit("ready")` |
| Close | `eshm_rpc_destroy(rpc)` | `rpc.close()` |

## Call vs event

| | Handlers per name | Unknown name | Means |
|---|---|---|---|
| **call** | exactly one (re-registering replaces) | logged as an error | "run this" |
| **event** | zero or more, in registration order | silently ignored | "this happened" |

## Build and run

```bash
cmake -S . -B build && cmake --build build
```

```bash
# C++ drives, Python works
./build/trigger_master demo               # terminal 1
python3 peer.py worker demo               # terminal 2

# Python drives, C++ works
python3 peer.py master demo               # terminal 1
./build/trigger_worker demo               # terminal 2
```

```
master: data on 'demo', triggers on 'demo_ctl'
master: waiting for a worker...
   [event] worker says it is ready
-> wrote sample 0 (20.0 C), calling 'process'
   [call ] worker finished a round (1 so far)
```

```
worker: attached to 'demo' and 'demo_ctl', waiting for triggers
<- [call ] process: sample 0, 20.0 C, label "round-0"
<- [event] master is shutting down after 3 round(s)
```

Note the traffic goes both ways: the master calls `process`, and the worker
calls `done` back **from inside its handler**. That is supported.

## Two channels, and why

`eshm_rpc_create("demo", ...)` opens `demo_ctl`, never `demo`. Triggers get a
channel of their own, always:

- The dispatcher is the **single reader** of its channel. Only one thread may
  read a handle, so sharing with application reads would corrupt both.
- A channel holds **one value per direction**. A trigger written onto your data
  channel would overwrite the very data it was announcing.

Your data channel stays completely ordinary — plain `eshm_write`/`eshm_read_ex`,
with whatever layout you like. Here it is a small `struct Reading`, matched by a
`struct.Struct("<Qd32s")` on the Python side.

## The rule that matters: handlers are level-triggered

A trigger means *"the state changed, go look"* — never *"an event occurred,
count it"*. Handlers must be **idempotent** and must read current state.

This is not a style preference. The channel holds one value per direction, so
if the sender outruns the dispatcher, triggers **coalesce**. Under level
semantics that is correct: the handler runs once and reads the latest state,
which is what every coalesced firing wanted. `test_rpc` pins the behaviour —
fire 200, and you might see:

```
fired 200, handled 104, reported missed 96
```

`rpc.missed` (`eshm_rpc_missed`) reports the shortfall from sequence-number
gaps, so loss is always visible, never silent. Handled + missed always accounts
for everything fired.

### The sharp edge

Coalescing is harmless for repeated firings of the **same** name. It is *not*
harmless for **different** names fired back to back:

```c
eshm_rpc_call(rpc, "step_one");
eshm_rpc_call(rpc, "step_two");   // may overwrite step_one before it is read
```

Two names are two distinct signals, and nothing queues them. If you need both
delivered, either wait for the first to be acknowledged (the master/worker
`done` call here is exactly that handshake), or put the sequencing in your data
structure and use a single trigger to announce it.

## Priming: receive the very first message

Both workers do a throwaway read on the data channel **before** announcing
readiness:

```cpp
Reading discard;
size_t ignored = 0;
eshm_read_ex(data, &discard, sizeof(discard), &ignored, 0);   // take the baseline
eshm_rpc_emit(rpc, "worker_ready");                            // now say hello
```

A reader only sees writes made after its first read — the read path takes its
baseline on the first call. Priming while the channel is still empty means the
baseline is *zero writes*, so the master's very first sample is delivered rather
than swallowed as the baseline. Announce readiness only after priming, and the
race closes completely.

## Where the work happens

Python never touches the control channel's shared memory. A **C++ dispatcher
thread inside libeshm** owns it, decodes each trigger, and calls up into Python
through a `ctypes` callback. One implementation of the wire format, in C++ —
which is what makes the two sides impossible to drift apart.

Handlers therefore run **on the dispatcher thread**, not your main thread.
ctypes acquires the GIL for the call, so touching Python objects is safe, but a
slow handler delays every later trigger — hand off to your own queue if the work
is long.

The wire record is three scalar fields — `k` (kind), `n` (name), `s` (sequence)
— built from the five types both codecs already speak. `DataHandler`'s
`FUNCTION_CALL` and `EVENT` tags are deliberately *not* used: they exist only in
C++ ([07](../07_rich_types/)), and with no arguments they would carry nothing a
string does not.

## Cost

An idle dispatcher parks on a futex: roughly 0.5 ms of CPU per 300 ms of
waiting ([08](../08_benchmark/)). Leaving a listener running all the time is
cheap enough not to think about.
