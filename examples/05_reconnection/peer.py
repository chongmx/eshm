#!/usr/bin/env python3
"""Python side of example 05 - surviving a master restart.

    python3 peer.py publish [channel] [--up S] [--down S] [--cycles N]
    python3 peer.py consume [channel] [--attempts N] [--wait MS]
                                      [--interval MS] [--stale MS]
                                      [--behavior immediately|on-timeout|never]
                                      [--auto]

Pairs with the C++ programs in this directory in either direction:

    ./build/flaky_publisher demo   +   python3 peer.py consume demo
    python3 peer.py publish demo   +   ./build/resilient_consumer demo

Demonstrates: max_reconnect_attempts, reconnect_wait_ms,
reconnect_retry_interval_ms, stale_threshold_ms, disconnect_behavior and
ESHMRole.AUTO - the same knobs resilient_consumer.cpp exposes.
"""

import sys
import time
from pathlib import Path

try:
    from eshm import ESHM, ESHMRole, ESHMDisconnectBehavior
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "py"))
    from eshm import ESHM, ESHMRole, ESHMDisconnectBehavior

CHANNEL_DEFAULT = "reconnect"
BUFFER = 4096

BEHAVIORS = {
    "immediately": ESHMDisconnectBehavior.IMMEDIATELY,
    "on-timeout": ESHMDisconnectBehavior.ON_TIMEOUT,
    "never": ESHMDisconnectBehavior.NEVER,
}


def parse(argv):
    """Return (channel, options dict) from a flag list."""
    channel, opts, i = CHANNEL_DEFAULT, {}, 0
    while i < len(argv):
        arg = argv[i]
        if arg.startswith("--"):
            name = arg[2:]
            if name == "auto":
                opts["auto"] = True
            else:
                i += 1
                opts[name] = argv[i]
        else:
            channel = arg
        i += 1
    return channel, opts


def publish(channel: str, opts) -> int:
    """Create the channel, publish, tear it down, repeat - a restarting master."""
    up = float(opts.get("up", 5))
    down = float(opts.get("down", 3))
    cycles = int(opts.get("cycles", 0))

    print(f"publisher: '{channel}', {up:g}s up / {down:g}s down"
          f"{f', {cycles} cycle(s)' if cycles else ''} (Ctrl-C to stop)\n")

    total = 0
    cycle = 0
    while cycles == 0 or cycle < cycles:
        cycle += 1
        # auto_cleanup unlinks the segment on close: a real restart, which
        # bumps master_generation and makes the slave's monitor react.
        with ESHM(channel, role=ESHMRole.MASTER, auto_cleanup=True) as conn:
            print(f"[cycle {cycle}] up", flush=True)
            until = time.monotonic() + up
            while time.monotonic() < until:
                total += 1
                conn.write(f"cycle {cycle} message {total}".encode() + b"\x00")
                time.sleep(0.2)
            print(f"[cycle {cycle}] down", flush=True)

        time.sleep(down)

    print(f"\npublisher: {total} message(s) over all cycles")
    return 0


def consume(channel: str, opts) -> int:
    """Attach with a configurable recovery policy and report every outage."""
    behavior = BEHAVIORS[opts.get("behavior", "on-timeout")]
    kwargs = dict(
        role=ESHMRole.AUTO if opts.get("auto") else ESHMRole.SLAVE,
        auto_cleanup=False,
        disconnect_behavior=behavior,
        max_reconnect_attempts=int(opts.get("attempts", 50)),
        reconnect_wait_ms=int(opts.get("wait", 5000)),
        reconnect_retry_interval_ms=int(opts.get("interval", 100)),
        stale_threshold_ms=int(opts.get("stale", 100)),
    )

    print(f"consumer: channel '{channel}'")
    print(f"  requested role  {kwargs['role'].name}")
    print(f"  attempts        {kwargs['max_reconnect_attempts']}"
          f"{'  (unlimited)' if kwargs['max_reconnect_attempts'] == 0 else ''}")
    print(f"  wait            {kwargs['reconnect_wait_ms']} ms"
          f"{'  (unlimited)' if kwargs['reconnect_wait_ms'] == 0 else ''}")
    print(f"  retry interval  {kwargs['reconnect_retry_interval_ms']} ms")
    print(f"  stale threshold {kwargs['stale_threshold_ms']} ms")
    print(f"  on stale peer   {behavior.name}\n")

    conn = None
    for _ in range(100):
        try:
            conn = ESHM(channel, **kwargs)
            break
        except RuntimeError:
            time.sleep(0.1)

    if conn is None:
        print(f"consumer: could not join '{channel}' - is a publisher running?",
              file=sys.stderr)
        return 1

    print(f"consumer: joined as {conn.get_role().name} (Ctrl-C to stop)\n")

    messages = timeouts = outages = 0
    connected = False

    with conn:
        while True:
            try:
                data = conn.read(buffer_size=BUFFER, timeout_ms=500)
            except TimeoutError:
                timeouts += 1
                alive = conn.is_remote_alive()
                if connected and not alive:
                    print("[down] publisher went away - reconnecting in the background",
                          flush=True)
                    connected = False
                    outages += 1
                # Sleep, do not spin. While the library is detached and
                # retrying, the read raises TimeoutError *immediately* rather
                # than waiting out timeout_ms - there is no segment to wait on.
                # Without this the loop burns a full core for the whole outage.
                if not alive:
                    time.sleep(0.02)
                continue
            except RuntimeError as exc:
                # Reconnection gave up, or the master is stale under the
                # IMMEDIATELY behaviour. This is what --attempts/--wait change.
                print(f"[end]  read failed: {exc}")
                break

            if not connected:
                print(f"[up]   publisher is back (outage {outages} over)")
                connected = True
            messages += 1
            if messages % 10 == 1:
                print("<- " + data.rstrip(b"\x00").decode(), flush=True)

    print(f"\nconsumer: {messages} message(s), {outages} outage(s), {timeouts} timeout(s)")
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] not in ("publish", "consume"):
        print(__doc__)
        return 2

    channel, opts = parse(argv[2:])
    try:
        return publish(channel, opts) if argv[1] == "publish" else consume(channel, opts)
    except KeyboardInterrupt:
        print("\nstopped")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
