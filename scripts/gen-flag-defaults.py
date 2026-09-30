#!/usr/bin/env python3
"""Regenerate docs/TENSIX-FLAG-DEFAULTS.md from riscv.opt + the reviewed ON set.

The Init() values are spread over 440 lines of riscv.opt with no comment
saying which ones ship, which is how a 36-pass gap between "measured" and
"shipped" went unnoticed.  This keeps the answer in one generated place.
"""
import re, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OPT = ROOT / "gcc/config/riscv/riscv.opt"
HARNESS = pathlib.Path(
    sys.argv[1] if len(sys.argv) > 1
    else ROOT.parent.parent / "craq-sfpi/dashboard/sweep_2x2.py")

def reviewed_on():
    if not HARNESS.exists():
        return set()
    s = HARNESS.read_text()
    i = s.index("ON_FLAGS = ("); j = s.index("\n)\n", i)
    return set(re.findall(r"-mtt-tensix-(?:optimize-)?([a-z0-9-]+)", s[i:j]))

def flags():
    out, name, ln = [], None, 0
    for k, l in enumerate(OPT.read_text().splitlines(), 1):
        if l.startswith("mtt-tensix"):
            name, ln = l.strip(), k
        elif name and l.startswith("Target"):
            m = re.search(r"Init\((\d+)\)", l)
            out.append((name, ln, m.group(1) if m else "-"))
            name = None
    return out

def main():
    on = reviewed_on()
    fl = flags()
    short = lambda n: re.sub(r"^mtt-tensix-(optimize-)?", "", n).rstrip("=")
    init1 = [f for f in fl if f[2] == "1"]
    on39  = [f for f in fl if short(f[0]) in on]
    gap   = [f for f in on39 if f[2] == "0"]
    opt   = [f for f in fl if f[2] == "0" and short(f[0]) not in on]
    print(f"Init(1)={len(init1)} reviewedON={len(on39)} "
          f"promotion-backlog={len(gap)} optin={len(opt)}")

if __name__ == "__main__":
    main()
