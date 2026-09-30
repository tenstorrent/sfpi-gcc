#!/usr/bin/env python3
# Lane DS acceptance arsenal: host-exact golden for the integer
# pressure ladders.
#
# The ladder bodies are XOR-only over int32 lane values, so the golden
# is exact on the host -- no floating-point rounding model, no
# simulator required to DEFINE it (the CRAQ run must merely reproduce
# it).  The golden is the FULL final state vector: the N output rows
# (LADDER_OUT(i)) written by ladder-body.h -- identical by construction
# for ladder-spilled-body.h (same DAG, exact INT32 round-trips) and
# REQUIRED of any correct exact-only Dst-spilling allocator compiling
# the rung.
#
# Stimulus contract (shared with the CRAQ probe): input row i (at byte
# LADDER_ROW(i)) holds in lane l (0..31) the splitmix32 hash of
# (i * 37 + l).  A hashed stimulus is deliberately NON-linear in i and
# l: no XOR combination of rows telescopes, so every output row is
# sensitive to every input row it depends on (a linear pattern made the
# folded golden degenerate -- lane DS finding, 2026-08-20).
#
# Usage: ladder_golden.py --n N [--twist {0,1}] [--trips T] [--lanes 32]

import argparse

M = 0xFFFFFFFF


def splitmix32(x):
    x = (x + 0x9E3779B9) & M
    z = x
    z = ((z ^ (z >> 16)) * 0x85EBCA6B) & M
    z = ((z ^ (z >> 13)) * 0xC2B2AE35) & M
    return (z ^ (z >> 16)) & M


def stim(i, lane):
    return splitmix32(i * 37 + lane)


def golden_lane(n, twist, trips, lane):
    a = [stim(i, lane) for i in range(n)]
    for _ in range(trips):
        if twist:
            # down-ring, sequential: a[0]^=a[n-1](old); a[i]^=a[i-1](new)
            a[0] ^= a[n - 1]
            for i in range(1, n):
                a[i] ^= a[i - 1]
        else:
            # up-ring, sequential: a[i]^=a[i+1](old, i<n-1); a[n-1]^=a[0](new)
            for i in range(n - 1):
                a[i] ^= a[i + 1]
            a[n - 1] ^= a[0]
    return a


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, required=True)
    ap.add_argument("--twist", type=int, default=0)
    ap.add_argument("--trips", type=int, default=8)
    ap.add_argument("--lanes", type=int, default=32)
    args = ap.parse_args()
    print("# ladder n=%d twist=%d trips=%d: output row i at byte "
          "%d+2*i; one line per output row, lanes 0..%d (hex)"
          % (args.n, args.twist, args.trips,
             256 if args.twist else 192, args.lanes - 1))
    finals = [golden_lane(args.n, args.twist, args.trips, l)
              for l in range(args.lanes)]
    for i in range(args.n):
        print("row%02d " % i
              + " ".join("%08x" % finals[l][i] for l in range(args.lanes)))


if __name__ == "__main__":
    main()
