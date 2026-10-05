#!/usr/bin/env python3
# UDP fault proxy for two local game processes: delay, jitter, loss, duplication and reordering, repeatable by seed.
# SPDX-License-Identifier: GPL-2.0-or-later
"""Sits between two games on one PC and damages the UDP traffic between them in a repeatable way.

    game A (UDP port 51001)  <->  [a-port 52001 | proxy | b-port 52002]  <->  game B (UDP port 51002)

Game A is told its peer is at 127.0.0.1:52001 and game B that its peer is at 127.0.0.1:52002.
Each game only ever sees the proxy socket that faces it, so its network library finds the address
it dialed on every packet that comes back, and both games can dial at the same time.

    python tools/net_fault_proxy.py --a-game 127.0.0.1:51001 --b-game 127.0.0.1:51002 \
        --a-port 52001 --b-port 52002 --delay 40 --jitter 5 --loss 1 --dup 0.5 --reorder 2 --seed 7

--delay is one way, so the round trip the games measure is about twice that. Every fault can be set
for one direction only with the --ab-* and --ba-* forms (A to B, B to A), for asymmetric links.

Repeatable by seed: which packets are lost, doubled or held back is decided by the seed and each
packet's position in its direction's stream, not by the clock. Two runs with the same seed apply
the same faults to the 1st, 2nd, 3rd ... packet of each direction. (What those packets contain
still depends on the games' own timing.)
"""
import argparse
import heapq
import random
import select
import socket
import sys
import time


def parse_endpoint(text):
    host, _, port = text.rpartition(":")
    if not host or not port.isdigit():
        raise argparse.ArgumentTypeError("expected ADDRESS:PORT, got %r" % text)
    return (host, int(port))


def parse_outage(text):
    start, _, length = text.partition(":")
    try:
        return (float(start), float(length))
    except ValueError:
        raise argparse.ArgumentTypeError("expected START:LENGTH in seconds, got %r" % text)


class Direction:
    """One way through the proxy, with its own random stream and its own counters."""

    def __init__(self, name, seed, delay, jitter, loss, dup, reorder, reorder_extra):
        self.name = name
        self.rng = random.Random(seed)
        self.delay = delay / 1000.0
        self.jitter = jitter / 1000.0
        self.loss = loss / 100.0
        self.dup = dup / 100.0
        self.reorder = reorder / 100.0
        self.reorder_extra = reorder_extra / 1000.0
        self.received = self.forwarded = self.lost = self.doubled = self.held = self.blacked_out = 0
        self.bytes = 0

    def plan(self, in_outage):
        """Returns the delays (seconds) at which copies of the next packet leave; empty = lost.

        The same number of random draws is made for every packet, whatever is decided, so one
        packet's fate never shifts the faults of the packets after it.
        """
        self.received += 1
        r_loss, r_dup, r_reorder = self.rng.random(), self.rng.random(), self.rng.random()
        j_first, j_second = self.rng.uniform(-1.0, 1.0), self.rng.uniform(-1.0, 1.0)
        if in_outage:
            self.blacked_out += 1
            return []
        if r_loss < self.loss:
            self.lost += 1
            return []
        first = max(0.0, self.delay + self.jitter * j_first)
        if r_reorder < self.reorder:
            # Held long enough for the packets behind it to overtake.
            first += self.reorder_extra
            self.held += 1
        out = [first]
        if r_dup < self.dup:
            out.append(max(0.0, self.delay + self.jitter * j_second))
            self.doubled += 1
        return out

    def summary(self):
        return ("%s: %d in, %d out, %d lost, %d doubled, %d held back, %d in outages, %d bytes" %
                (self.name, self.received, self.forwarded, self.lost, self.doubled, self.held, self.blacked_out, self.bytes))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a-port", type=int, required=True, help="UDP port that faces game A (A dials this)")
    ap.add_argument("--b-port", type=int, required=True, help="UDP port that faces game B (B dials this)")
    ap.add_argument("--a-game", type=parse_endpoint, help="game A's own ADDRESS:PORT; learned from its first packet when omitted")
    ap.add_argument("--b-game", type=parse_endpoint, help="game B's own ADDRESS:PORT; learned from its first packet when omitted")
    ap.add_argument("--bind", default="127.0.0.1", help="address the proxy listens on (default 127.0.0.1)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--duration", type=float, default=0.0, help="stop after this many seconds (0 = until Ctrl+C)")
    ap.add_argument("--report", type=float, default=0.0, help="print the counters every this many seconds (0 = only at the end)")
    ap.add_argument("--outage", type=parse_outage, action="append", default=[],
                    help="START:LENGTH in seconds since the first packet: everything is dropped in that window (repeatable)")
    faults = (("delay", 0.0, "one-way delay in ms"), ("jitter", 0.0, "plus or minus this many ms, uniform"),
              ("loss", 0.0, "percent of packets dropped"), ("dup", 0.0, "percent of packets sent twice"),
              ("reorder", 0.0, "percent of packets held back"), ("reorder-ms", 30.0, "how long a held packet waits, in ms"))
    for name, default, text in faults:
        ap.add_argument("--" + name, type=float, default=default, help=text + " (both directions)")
        ap.add_argument("--ab-" + name, type=float, default=None, help="A to B only")
        ap.add_argument("--ba-" + name, type=float, default=None, help="B to A only")
    args = ap.parse_args()

    def pick(prefix, name):
        value = getattr(args, (prefix + name).replace("-", "_"))
        return getattr(args, name.replace("-", "_")) if value is None else value

    def direction(label, prefix, seed):
        return Direction(label, seed, pick(prefix, "delay"), pick(prefix, "jitter"), pick(prefix, "loss"),
                         pick(prefix, "dup"), pick(prefix, "reorder"), pick(prefix, "reorder-ms"))

    # Two streams from one seed, so changing one direction's settings never moves the other's faults.
    ab = direction("A to B", "ab-", args.seed * 2 + 1)
    ba = direction("B to A", "ba-", args.seed * 2 + 2)

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_a.bind((args.bind, args.a_port))
    sock_b.bind((args.bind, args.b_port))
    sock_a.setblocking(False)
    sock_b.setblocking(False)
    if sys.platform == "win32":
        # A UDP send to a port nobody listens on yet comes back on Windows as a reset on the next
        # receive. The games start at different moments, so that is normal here: switch it off.
        sio_udp_connreset = 0x9800000C
        for s in (sock_a, sock_b):
            try:
                s.ioctl(sio_udp_connreset, False)
            except (OSError, ValueError, AttributeError):
                pass

    game = {"a": args.a_game, "b": args.b_game}
    queue = []       # (release time, order, socket, destination key, data, direction)
    order = 0
    started = None   # the first packet starts the clock the outages count from
    began = time.monotonic()
    next_report = began + args.report if args.report > 0 else None
    print("net_fault_proxy: A <-> %s:%d | %s:%d <-> B, seed %d" % (args.bind, args.a_port, args.bind, args.b_port, args.seed), flush=True)

    def in_outage(now):
        if started is None:
            return False
        t = now - started
        return any(start <= t < start + length for start, length in args.outage)

    try:
        while True:
            now = time.monotonic()
            if args.duration > 0 and now - began >= args.duration:
                break
            # Release what is due. Ties leave in arrival order.
            while queue and queue[0][0] <= now:
                _, _, out_sock, to_key, data, way = heapq.heappop(queue)
                target = game[to_key]
                if target is None:
                    continue   # the other game has not shown its address yet
                try:
                    out_sock.sendto(data, target)
                    way.forwarded += 1
                except OSError:
                    pass
            if next_report is not None and now >= next_report:
                print(ab.summary(), flush=True)
                print(ba.summary(), flush=True)
                next_report = now + args.report
            wait = 0.05
            if queue:
                wait = min(wait, max(0.0, queue[0][0] - now))
            readable, _, _ = select.select([sock_a, sock_b], [], [], wait)
            for s in readable:
                while True:
                    try:
                        data, source = s.recvfrom(65535)
                    except (BlockingIOError, ConnectionResetError):
                        break
                    except OSError:
                        break
                    now = time.monotonic()
                    if started is None:
                        started = now
                    from_key, to_key, way, out_sock = ("a", "b", ab, sock_b) if s is sock_a else ("b", "a", ba, sock_a)
                    if game[from_key] is None:
                        game[from_key] = source
                        print("net_fault_proxy: game %s is at %s:%d" % (from_key.upper(), source[0], source[1]), flush=True)
                    elif source != game[from_key]:
                        continue   # only the two games pass through
                    way.bytes += len(data)
                    for delay in way.plan(in_outage(now)):
                        order += 1
                        heapq.heappush(queue, (now + delay, order, out_sock, to_key, data, way))
    except KeyboardInterrupt:
        pass
    finally:
        print(ab.summary(), flush=True)
        print(ba.summary(), flush=True)
        sock_a.close()
        sock_b.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
