#!/usr/bin/env python3
# Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Waits until UDP actually works end to end against a slave, then exits.
#
# Run inside the master's network namespace, before handing control to the test.
# ICMP is not enough: a path can carry pings while UDP is refused or while the
# far end is still coming up.
#
# No agent is needed on the slave. We send a datagram to a port nothing binds
# there and wait for the ICMP port-unreachable its kernel answers with, which a
# connected UDP socket reports as ECONNREFUSED. That proves master to slave over
# UDP and slave to master over ICMP -- not slave to master over UDP, and not
# multicast.
#
# A probe sent before the far end exists leaves the neighbour entry INCOMPLETE,
# which makes the next probe fail too, so the first failure is never final.
# That is what the deadline is for.

import argparse
import socket
import sys
import time


def probe(host, port, timeout):
    """One probe. Returns (ready, reason)."""
    its_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    its_socket.settimeout(timeout)
    try:
        its_socket.connect((host, port))
        its_socket.send(b"\x00")
        its_socket.recv(1)
        # Something answered on a port we expect to be closed. The path works,
        # which is all we are asking about.
        return True, "unexpected reply, but the path carries UDP"
    except ConnectionRefusedError:
        return True, "ICMP port unreachable received"
    except socket.timeout:
        return False, "no answer"
    except OSError as its_error:
        return False, its_error.strerror or str(its_error)
    finally:
        its_socket.close()


def main():
    its_parser = argparse.ArgumentParser(description=__doc__)
    its_parser.add_argument("host", help="slave address on the overlay")
    its_parser.add_argument("--port", type=int, default=34599,
                            help="a port nothing binds on the slave (default: %(default)s)")
    its_parser.add_argument("--deadline", type=float, default=15.0,
                            help="seconds to keep waiting for the path (default: %(default)s)")
    its_parser.add_argument("--interval", type=float, default=1.0,
                            help="seconds between probes; keep >= 1 so the slave's ICMP "
                                 "rate limit cannot mask a working path (default: %(default)s)")
    its_parser.add_argument("--timeout", type=float, default=0.5,
                            help="seconds to wait for one answer; kept well under --interval so "
                                 "a stalled probe does not eat the deadline (default: %(default)s)")
    its_args = its_parser.parse_args()

    its_end = time.monotonic() + its_args.deadline
    its_attempts = 0
    its_reason = "not attempted"
    while True:
        its_attempts += 1
        its_ready, its_reason = probe(its_args.host, its_args.port, its_args.timeout)
        if its_ready:
            print(f"UDP path to {its_args.host} ready after {its_attempts} probe(s) "
                  f"({its_reason})")
            return 0
        if time.monotonic() >= its_end:
            break
        time.sleep(its_args.interval)

    print(f"UDP path to {its_args.host} not usable after {its_attempts} probe(s) "
          f"over {its_args.deadline:g}s: {its_reason}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
