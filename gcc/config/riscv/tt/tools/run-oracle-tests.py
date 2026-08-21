#!/usr/bin/env python3
# Self-test runner for tensix-makespan-oracle.py (lane DT tool, lane EI
# RecMII loop extension).  Hand-solved fixtures under oracle-tests/;
# every assertion is a substring of the oracle's own report so the
# expected numbers are auditable next to the fixture's derivation
# comments.  Exit 0 = all pass.

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE = os.path.join(HERE, "tensix-makespan-oracle.py")
TESTS = os.path.join(HERE, "oracle-tests")

CASES = [
    # (fixture, extra args, required substrings)
    ("loop-recurrence.s", [],
     ["loop .L2 nodes[0:3): ResMII=3 RecMII=6 MII=6",
      "achieved-II=6 round-chain-stall/iter=0",
      "RecMII-circuit:"]),
    ("loop-twochain.s", [],
     ["loop .L3 nodes[0:6): ResMII=6 RecMII=6 MII=6",
      "achieved-II=10 round-chain-stall/iter=4"]),
    ("loop-unaudited.s", [],
     ["RecMII=>= ", "achieved-II=>= ",
      "REFUSED-EXACT: unaudited latency classes present: shft2/sfpshft2"]),
    # --loops mode prints only the loop table.
    ("loop-twochain.s", ["--loops"],
     ["function kernel_twochain: [bh] loops",
      "achieved-II=10 round-chain-stall/iter=4"]),
    # Straight-line report is unchanged by the extension: a loop-free
    # function still reports words/lower-bound/makespan lines.
    ("straightline.s", [],
     ["words=3 lower-bound=4",
      "as-emitted makespan=5",
      "gap-to-lower-bound=1"]),
]


def main():
    failures = 0
    for fixture, extra, needles in CASES:
        path = os.path.join(TESTS, fixture)
        out = subprocess.run(
            [sys.executable, ORACLE, "--arch", "bh", path] + extra,
            capture_output=True, text=True)
        text = out.stdout + out.stderr
        for needle in needles:
            if needle in text:
                print(f"PASS {fixture} {extra}: {needle!r}")
            else:
                print(f"FAIL {fixture} {extra}: missing {needle!r}")
                print("---- oracle output ----")
                print(text)
                failures += 1
    if failures:
        print(f"{failures} assertion(s) FAILED")
        return 1
    print("all oracle self-tests PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
