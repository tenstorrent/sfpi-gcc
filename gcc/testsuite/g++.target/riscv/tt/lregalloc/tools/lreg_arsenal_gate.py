#!/usr/bin/env python3
# Lane DS acceptance arsenal: the allocator gate.
#
# Runs every arsenal kernel through a compiler and checks the verdict
# against VERDICTS.tsv.  Two modes:
#   --mode today   : the pre-allocator contract (validates the arsenal
#                    itself against the current pin).
#   --mode future  : the LREG-allocator acceptance contract.  Lane DP
#                    runs this with their compiler; every deviation is
#                    a gate FAIL.
#
# Checks per row:
#   1. verdict: compile vs refuse-by-name (alternatives with '|').
#   2. label: tools/lreg_pressure_oracle.py --expect <label> (the
#      pressure model is pre-allocator and must not move).
#   3. compile-noop rows (--base-gxx given): .text byte-identity of the
#      candidate vs baseline object ("no-op below 9 live" claim).
# Bit-exactness rows name their golden; the CRAQ procedure and recorded
# goldens live in ARSENAL.md + tools/ladder_golden.py (host-exact for
# the integer ladders).
#
# Usage:
#   SFPI=... tools/lreg_arsenal_gate.py --mode today \
#       --gxx <driver> [--bdir <dir>] [--base-gxx <driver> [--base-bdir <dir>]]
#
# sfpi rows need $SFPI and, when the driver has no own libstdc++,
# --cxxflags-extra with the -isystem lines (see ARSENAL.md).

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.dirname(HERE)
DGOPT_RE = re.compile(r'dg-options\s+"([^"]*)"')


def dg_options(path):
    with open(path, errors="replace") as f:
        for line in f:
            m = DGOPT_RE.search(line)
            if m:
                return m.group(1).replace("[SFPI]",
                                          os.environ.get("SFPI", ""))
    return "-mcpu=tt-bh-tensix -O2"


def compile_one(gxx, bdir, extra, src, obj=None):
    cmd = [gxx]
    if bdir:
        cmd += ["-B", bdir]
    cmd += dg_options(src).split() + extra
    if obj:
        cmd += ["-c", src, "-o", obj]
    else:
        cmd += ["-S", src, "-o", os.devnull]
    return subprocess.run(cmd, capture_output=True, text=True,
                          cwd=tempfile.gettempdir())


def text_bytes(objcopy, obj):
    out = subprocess.run(
        [objcopy, "-O", "binary", "--only-section=.text", obj,
         "/dev/stdout"], capture_output=True)
    return out.stdout


def classify(stderr):
    if "internal compiler error" in stderr:
        return "ice"
    names = sorted(set(re.findall(r"\(([a-z0-9-]*lreg[a-z0-9-]*)\)",
                                  stderr)))
    if "error" in stderr and not names:
        # Unnamed error: still a refusal, but an unnamed one.
        return "refuse:UNNAMED"
    if names:
        return "refuse:" + ",".join(names)
    return "compile"


def verdict_ok(expected, got):
    for alt in expected.split("|"):
        if alt.startswith("compile"):
            if got == "compile":
                return True
        elif alt.startswith("refuse:"):
            want = alt.split(":", 1)[1]
            if got.startswith("refuse:") and want in got:
                return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["today", "future"], required=True)
    ap.add_argument("--gxx", required=True)
    ap.add_argument("--bdir")
    ap.add_argument("--base-gxx")
    ap.add_argument("--base-bdir")
    ap.add_argument("--objcopy", default="riscv-tt-elf-objcopy")
    ap.add_argument("--cxxflags-extra", default="")
    ap.add_argument("--skip-labels", action="store_true")
    args = ap.parse_args()

    extra_sfpi = args.cxxflags_extra.split()
    rows = []
    with open(os.path.join(SUITE, "VERDICTS.tsv")) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            rows.append(line.rstrip("\n").split("\t"))

    fails = 0
    for test, dialect, label, today, future, golden in rows:
        src = os.path.join(SUITE, test)
        expected = today if args.mode == "today" else future
        extra = extra_sfpi if dialect == "sfpi" else []
        if dialect == "sfpi" and not os.environ.get("SFPI"):
            print("SKIP %-32s (SFPI unset)" % test)
            continue

        p = compile_one(args.gxx, args.bdir, extra, src)
        got = classify(p.stderr)
        ok = verdict_ok(expected, got)

        noop_note = ""
        if (ok and args.base_gxx and got == "compile"
                and "compile-noop" in expected):
            with tempfile.TemporaryDirectory() as wd:
                o1 = os.path.join(wd, "cand.o")
                o2 = os.path.join(wd, "base.o")
                compile_one(args.gxx, args.bdir, extra, src, o1)
                compile_one(args.base_gxx, args.base_bdir, extra, src, o2)
                if (not os.path.exists(o1)) or (not os.path.exists(o2)):
                    ok, noop_note = False, " noop-gate=OBJ-MISSING"
                elif (text_bytes(args.objcopy, o1)
                      != text_bytes(args.objcopy, o2)):
                    ok, noop_note = False, " noop-gate=TEXT-DIFFERS"
                else:
                    noop_note = " noop-gate=byte-identical"

        label_note = ""
        if ok and label != "-" and not args.skip_labels:
            oracle = os.path.join(HERE, "lreg_pressure_oracle.py")
            r = subprocess.run(
                [sys.executable, oracle, "--gxx", args.gxx]
                + (["--bdir", args.bdir] if args.bdir else [])
                + (["--flags", dg_options(src) + " "
                    + " ".join(extra)] if extra else [])
                + ["--expect", label, src],
                capture_output=True, text=True)
            if r.returncode != 0:
                ok, label_note = False, " label-gate=MISMATCH"
            else:
                label_note = " label=%s ok" % label

        print("%s %-32s expected=%s got=%s%s%s"
              % ("PASS" if ok else "FAIL", test, expected, got,
                 noop_note, label_note))
        fails += 0 if ok else 1

    print("\n%s: %d row(s) failed" % (args.mode.upper(), fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
