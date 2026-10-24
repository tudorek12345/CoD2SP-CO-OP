"""Bounded loopback UDP relay for reproducible event retry/deduplication tests.

usage: python udp_impairment.py listen-port host-port result.json
Drops the first host Event datagram, duplicates the next Event, and exits after
four seconds. Carries fixture packets only; it has no game/Steam integration.
"""
import json
import selectors
import socket
import sys
import time
from pathlib import Path


def main():
    listen, target = int(sys.argv[1]), int(sys.argv[2])
    if not 1 <= listen <= 65535 or not 1 <= target <= 65535 or listen == target:
        raise SystemExit("invalid relay ports")
    server = ("127.0.0.1", target)
    client = None
    stats = {"test": "loopback fixture UDP impairment", "dropped_events": 0,
             "duplicated_events": 0, "forwarded_datagrams": 0}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", listen))
        sock.setblocking(False)
        with selectors.DefaultSelector() as ready:
            ready.register(sock, selectors.EVENT_READ)
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                for _, _ in ready.select(.02):
                    data, sender = sock.recvfrom(1201)
                    if len(data) > 1200:
                        continue
                    if sender == server:
                        if client is None:
                            continue
                        event = len(data) >= 32 and data[:8] == b"C2COOP1\0" and data[10:12] == b"\0\6"
                        if event and stats["dropped_events"] == 0:
                            stats["dropped_events"] += 1
                            continue
                        sock.sendto(data, client)
                        stats["forwarded_datagrams"] += 1
                        if event and stats["duplicated_events"] == 0:
                            sock.sendto(data, client)
                            stats["duplicated_events"] += 1
                    elif client is None or client == sender:
                        client = sender
                        sock.sendto(data, server)
                        stats["forwarded_datagrams"] += 1
    stats["pass"] = stats["dropped_events"] == 1 and stats["duplicated_events"] == 1
    Path(sys.argv[3]).write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")
    return 0 if stats["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
