#!/usr/bin/env python3
# Lane DS acceptance arsenal: standalone LREG pressure oracle.
#
# Computes the maximum simultaneous SFPU-vector liveness of a kernel
# from the COMPILER'S OWN dumps, so pressure-ladder labels and any
# "allocator is a no-op below 9 live" claim are machine-checked rather
# than asserted.  Two independent sources:
#
#   1. rvtt_prgm_const's whole-function SSA pressure model
#      (-mtt-tensix-optimize-const-remat -fdump-tree-rvtt_prgm_const-details):
#        "const-remat: pressure N within the 8-LREG file; nothing to do"
#        "const-remat: pressure N exceeds the 8-LREG file"
#      The line is printed BEFORE any rematerialization transform, so it
#      is the pressure of the kernel as written.  Works on any CFG.
#
#   2. rvtt_lp_schedule's per-region point liveness
#      (-mtt-tensix-optimize-pressure-schedule -fdump-tree-rvtt_lp_schedule):
#        "SFPU pressure region: bb=B ops=K live-in=L peak=P"
#        "SFPU pressure schedule: old-peak=P new-peak=Q ..."
#      Straight-line unconditional BBs only (loops report rejected=cfg);
#      when present, old-peak/peak is the schedule-invariant point max.
#
# The oracle also compiles once with the PLAIN flags and counts
# lreg-pressure-exceeded refusals, giving the third (behavioral) signal.
#
# Usage:
#   lreg_pressure_oracle.py --gxx <xg++-or-driver> [--bdir <dir-for--B>]
#       [--incflags "<-I/-isystem flags for sfpi tests>"]
#       [--flags "<base flags>"] [--expect N] [--json] test.C ...
#
# --flags defaults to the file's own dg-options line (with [SFPI]
# resolved from $SFPI) when present, else
# "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops".
# --expect N: exit nonzero unless the prgm_const pressure == N for every
# input (the machine-check used for the ladder labels).

import argparse
import os
import re
import subprocess
import sys
import tempfile
import json

PRGM_RE = re.compile(r"const-remat: pressure (\d+) (within|exceeds) the (\d+)-LREG file")
LP_RE = re.compile(r"SFPU pressure region: bb=\d+ ops=\d+ live-in=\d+ peak=(\d+)")
LP_OLD_RE = re.compile(r"SFPU pressure schedule: old-peak=(\d+)")
REFUSAL = "lreg-pressure-exceeded"
DGOPT_RE = re.compile(r'dg-options\s+"([^"]*)"')


def dg_options(path):
    with open(path, errors="replace") as f:
        for line in f:
            m = DGOPT_RE.search(line)
            if m:
                opts = m.group(1)
                sfpi = os.environ.get("SFPI", "")
                return opts.replace("[SFPI]", sfpi)
    return None


def run_compile(gxx, bdir, flags, extra, src, workdir):
    cmd = [gxx]
    if bdir:
        cmd += ["-B", bdir]
    cmd += ["-S"] + flags + extra + [src, "-o", os.devnull]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=workdir)
    return proc


def collect_dump(workdir, suffix):
    text = ""
    for name in os.listdir(workdir):
        if suffix in name:
            with open(os.path.join(workdir, name), errors="replace") as f:
                text += f.read()
    return text


def measure(gxx, bdir, flags, src):
    src = os.path.abspath(src)
    result = {"file": os.path.basename(src)}
    with tempfile.TemporaryDirectory() as wd:
        # Source 1: prgm_const model.
        p = run_compile(gxx, bdir, flags,
                        ["-mtt-tensix-optimize-const-remat",
                         "-fdump-tree-rvtt_prgm_const-details"], src, wd)
        dump = collect_dump(wd, "rvtt_prgm_const")
        pressures = [int(m.group(1)) for m in PRGM_RE.finditer(dump)]
        result["prgm_const_pressure"] = max(pressures) if pressures else None
        result["lreg_file"] = (PRGM_RE.search(dump).group(3)
                               if pressures else None)
    with tempfile.TemporaryDirectory() as wd:
        # Source 2: lp_schedule point liveness (straight-line only).
        p = run_compile(gxx, bdir, flags,
                        ["-mtt-tensix-optimize-pressure-schedule",
                         "-fdump-tree-rvtt_lp_schedule"], src, wd)
        dump = collect_dump(wd, "rvtt_lp_schedule")
        peaks = ([int(m.group(1)) for m in LP_RE.finditer(dump)]
                 + [int(m.group(1)) for m in LP_OLD_RE.finditer(dump)])
        result["lp_schedule_peak"] = max(peaks) if peaks else None
    with tempfile.TemporaryDirectory() as wd:
        # Source 3: behavioral refusal count at the plain flags.
        p = run_compile(gxx, bdir, flags, [], src, wd)
        result["refusals"] = p.stderr.count(REFUSAL)
        result["other_errors"] = len(
            [l for l in p.stderr.splitlines()
             if "error:" in l and REFUSAL not in l])
        result["ice"] = "internal compiler error" in p.stderr
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gxx", required=True)
    ap.add_argument("--bdir", default=None)
    ap.add_argument("--flags", default=None,
                    help="override flags (default: the file's dg-options)")
    ap.add_argument("--expect", type=int, default=None)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("sources", nargs="+")
    args = ap.parse_args()

    ok = True
    out = []
    for src in args.sources:
        flags = (args.flags.split() if args.flags
                 else (dg_options(src) or
                       "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops").split())
        # Strip dump/relief flags from dg-options: the oracle adds its own.
        flags = [f for f in flags if not f.startswith("-fdump")
                 and f not in ("-mtt-tensix-optimize-const-remat",
                               "-mtt-tensix-optimize-const-residency",
                               "-mtt-tensix-optimize-pressure-schedule")]
        r = measure(args.gxx, args.bdir, flags, src)
        if args.expect is not None:
            r["expected"] = args.expect
            r["label_ok"] = (r["prgm_const_pressure"] == args.expect)
            ok = ok and r["label_ok"]
        out.append(r)

    if args.json:
        print(json.dumps(out, indent=2))
    else:
        for r in out:
            print("%-36s prgm_const=%-4s lp_peak=%-4s refusals=%-3d "
                  "other_err=%d ice=%s%s"
                  % (r["file"], r["prgm_const_pressure"],
                     r["lp_schedule_peak"], r["refusals"],
                     r["other_errors"], r["ice"],
                     ("" if "label_ok" not in r
                      else "  label_ok=%s" % r["label_ok"])))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
