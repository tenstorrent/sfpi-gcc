#!/usr/bin/env python3
# Tensix SFPU makespan oracle (lane DT, list-scheduler acceptance arsenal).
#
# Judges any SFPU instruction schedule against the modeled OPTIMUM, not
# against another compiler pass: it parses the compiler's own per-insn
# typed-effect dump (assembly compiled with -mtt-tensix-dump-effects),
# rebuilds the dependence DAG under the same fail-closed vocabulary the
# in-compiler consumers use, and reports
#
#   (a) the critical-path LOWER BOUND on the region/function makespan,
#       derived from the AUDITED xtt_result_latency facts
#       (gcc/config/riscv/tt/rvtt-cost.md D3 audit table; the .s
#       annotations are generated from exactly those attributes, so the
#       annotation IS the audited table, per-insn, post-RA), and
#   (b) the ACTUAL modeled makespan of the schedule as emitted (issue
#       order of the .s), counting modeled interlock stalls (BH
#       scoreboard) or verifying explicit SFPNOP padding (WH), plus the
#       architectural next-slot ACCEPTANCE stall.
#
# So a schedule produced by -mtt-tensix-optimize-list-schedule (or any
# other pass, or a hand kernel) is judged as: lower-bound <= schedule,
# with the gap named.  A schedule that beats the bubble-fill passes but
# sits far from the bound is visible as exactly that.
#
# Fail-closed rules mirrored from the pass vocabulary (rvtt-cost.md,
# rtl-rvtt-schedule.cc):
#   - latency=-1 in the dump means UNAUDITED: the oracle never guesses.
#     The lower bound charges 0 for it (a true lower bound must
#     under-approximate) and the makespan count REFUSES BY NAME: the
#     function's makespan is reported as ">= N" with the unaudited
#     subunit/mnemonic classes listed, never as an exact number.
#   - opaque effect sets (raw asm, .ttinsn words) are full barriers:
#     ordered against everything, latency unknown -> same named refusal.
#   - Dst traffic is ordered conservatively (no address disambiguation:
#     dst=r/w vs dst=r/w in program order is an edge), RWC steps are
#     ordered against each other AND against Dst traffic (a counter step
#     changes every later Dst-relative address), config accesses are
#     full barriers, CC writes order against every CC reader/writer.
#   - The one mnemonic-keyed fact: SFPSWAP's architectural next-slot
#     acceptance stall (xtt_next_slot_stall in rvtt-cost.md: on the next
#     cycle only SFPNOP is accepted; hardware stalls otherwise).  The
#     dump does not carry this attribute, so the oracle keys it on the
#     SFPSWAP mnemonic with this provenance note.  This is a measurement
#     tool, not a compiler decision: the charter's no-op-names rule
#     binds compiler decisions; the oracle only restates an audited
#     table fact next to its citation.
#
# Model (identical to the list scheduler's issue-distance rules):
#   RAW, WAW edge:  issue(j) >= issue(i) + words(i) + latency(i)
#   WAR edge:       issue(j) >= issue(i) + words(i)
#   each instruction occupies words(i) issue slots (every annotated .s
#   line is one Tensix word; a multi-word materialization appears as
#   multiple annotated lines, so words(i) == 1 per parsed node).
#
# Usage:
#   tensix-makespan-oracle.py [--arch bh|wh] [--function NAME] FILE.s
#   tensix-makespan-oracle.py --compare BASE.s CAND.s [--function NAME]
#
# Exit status: 0 on success, 1 on parse/usage error, 2 if --check-wh-nops
# finds a WH latency violation (an omitted required NOP).

import argparse
import re
import sys
from collections import namedtuple

FX_RE = re.compile(
    r"#\s*xtt-effects:\s*(?:(opaque)|subunit=(\S+)\s+latency=(-?\d+)"
    r"\s+lreg-read=0x([0-9a-f]+)\s+lreg-write=0x([0-9a-f]+)"
    r"\s+port=(\S+)\s+cc=(\S+)\s+config=0x([0-9a-f]+)"
    r"\s+rwc=(\S+)\s+dst=(\S+)\s+encodable=(\S+))")

LABEL_RE = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
INSN_RE = re.compile(r"^\s+([A-Za-z_.][\w.]*)\s*(.*)$")

Node = namedtuple(
    "Node",
    "idx mnem text opaque subunit lat rd wr cc config rwc dst")


def parse_nodes(path):
    """Return {function: [Node,...]} for every annotated Tensix insn."""
    funcs = {}
    cur = None
    pending = None  # last insn line awaiting its xtt-effects comment
    with open(path) as f:
        for raw in f:
            line = raw.rstrip("\n")
            m = LABEL_RE.match(line)
            if m and not m.group(1).startswith(".L"):
                cur = m.group(1)
                funcs.setdefault(cur, [])
                pending = None
                continue
            fx = FX_RE.search(line)
            if fx and cur is not None and pending is not None:
                nodes = funcs[cur]
                if fx.group(1):  # opaque
                    nodes.append(Node(len(nodes), pending[0], pending[1],
                                      True, "opaque", -1, 0, 0,
                                      "unknown", 1, "unknown", "rw"))
                else:
                    nodes.append(Node(
                        len(nodes), pending[0], pending[1], False,
                        fx.group(2), int(fx.group(3)),
                        int(fx.group(4), 16), int(fx.group(5), 16),
                        fx.group(7), int(fx.group(8), 16),
                        fx.group(9), fx.group(10)))
                pending = None
                continue
            if re.match(r"\s*#", line):
                # A non-effects comment (e.g. the READ/WRITE lreg
                # markers): zero-length ghost, never a word.  Clear
                # pending so its own opaque annotation is dropped.
                pending = None
                continue
            im = INSN_RE.match(line)
            if im and not im.group(1).startswith("."):
                pending = (im.group(1), line.strip())
            elif im and im.group(1) == ".ttinsn":
                pending = (".ttinsn", line.strip())
    return funcs


def dep_kind(a, b):
    """Dependence a->b (a earlier).  2 = latency-carrying (RAW/WAW),
    1 = order-only (WAR/structural), 0 = none.  Fail-closed."""
    if a.opaque or b.opaque:
        return 1  # full barrier, order-only (latency refused by name)
    # LREG value edges.
    if a.wr & b.rd or a.wr & b.wr:
        return 2
    if a.rd & b.wr:
        return 1
    # CC lane-state: any write orders against every reader and writer.
    a_ccw = "w" in a.cc
    b_ccw = "w" in b.cc
    a_ccr = "r" in a.cc
    b_ccr = "r" in b.cc
    if (a_ccw and (b_ccr or b_ccw)) or (a_ccr and b_ccw):
        return 1
    # Config space: full barrier among config-touching insns and
    # against everything (macro state can redirect any later write).
    if a.config or b.config:
        return 1
    # Dst memory: conservative, no address disambiguation.
    a_dst = a.dst != "none"
    b_dst = b.dst != "none"
    a_rwc = a.rwc not in ("none",)
    b_rwc = b.rwc not in ("none",)
    if (a_dst or a_rwc) and (b_dst or b_rwc):
        return 1
    return 0


# Architectural next-slot acceptance stall (rvtt-cost.md
# xtt_next_slot_stall; SFPSWAP.md: next cycle only SFPNOP accepted).
def acceptance_stall_p(node):
    return node.mnem.upper().startswith("SFPSWAP")


def load_audit_table(path):
    """Cited extra audited facts (see dl-latency-audit-20260820.tsv):
    applied ONLY where the dump says unaudited; never overrides."""
    table = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                table[parts[0]] = int(parts[1])
    return table


def apply_audit_table(nodes, table):
    out = []
    for x in nodes:
        if x.lat < 0 and not x.opaque:
            key_s = f"subunit:{x.subunit}"
            key_m = f"mnem:{x.mnem.upper()}"
            lat = table.get(key_m, table.get(key_s))
            if lat is not None:
                x = x._replace(lat=lat)
        out.append(x)
    return out


def analyze(nodes, arch):
    n = len(nodes)
    words = n  # one Tensix word per annotated line
    unaudited = sorted({f"{x.subunit}/{x.mnem}" for x in nodes if x.lat < 0})

    def lat(x):
        return max(x.lat, 0)

    # Critical path (longest path, edge weight words + latency).
    cp = [0] * n
    has_succ = [False] * n
    for i in range(n - 1, -1, -1):
        best = 1 + lat(nodes[i])  # own word + trailing shadow (drain)
        for j in range(i + 1, n):
            k = dep_kind(nodes[i], nodes[j])
            if not k:
                continue
            has_succ[i] = True
            via = cp[j] + 1 + (lat(nodes[i]) if k == 2 else 0)
            if via > best:
                best = via
        cp[i] = best
    # Sink-drain term: the last-issued word must be a DAG sink, and its
    # trailing shadow drains at block end, so the word count alone
    # under-bounds by the smallest sink latency.
    sink_drain = min((lat(nodes[i]) for i in range(n) if not has_succ[i]),
                     default=0)
    lower = max(words + sink_drain, max(cp, default=0))

    # As-emitted in-order issue simulation.
    issue = [0] * n
    done = [0] * n
    t = 0
    stalls = 0
    acc_stalls = 0
    viol = []
    for j in range(n):
        ready = t
        for i in range(j):
            k = dep_kind(nodes[i], nodes[j])
            if not k:
                continue
            need = done[i] + (lat(nodes[i]) if k == 2 else 0)
            if need > ready:
                ready = need
        if j and acceptance_stall_p(nodes[j - 1]) \
           and not nodes[j].mnem.upper().startswith("SFPNOP"):
            if t + 1 > ready:
                ready = t + 1
            acc_stalls += 1
        if arch == "wh" and ready > t:
            viol.append((nodes[j].idx, nodes[j].mnem, ready - t))
            ready = t  # WH does not stall; record the violation
        stalls += max(0, ready - t)
        issue[j] = ready
        done[j] = ready + 1
        t = done[j]
    # Block-end drain: every node's trailing shadow counts (the same
    # conservative exit term the schedule pass applies to baseline and
    # candidate identically).
    makespan = t
    for j in range(n):
        if done[j] + lat(nodes[j]) > makespan:
            makespan = done[j] + lat(nodes[j])
    return dict(words=words, lower=lower, makespan=makespan,
                stalls=stalls, acc_stalls=acc_stalls,
                unaudited=unaudited, wh_violations=viol)


def report(name, r, arch):
    exact = not r["unaudited"]
    ms = f"{r['makespan']}" if exact else f">= {r['makespan']}"
    print(f"function {name}: [{arch}]")
    print(f"  words={r['words']} lower-bound={r['lower']}")
    print(f"  as-emitted makespan={ms} "
          f"(modeled-stalls={r['stalls']} acceptance-stalls={r['acc_stalls']})")
    if r["unaudited"]:
        print("  REFUSED-EXACT: unaudited latency classes present, "
              "counts exclude their unknown stalls: "
              + ", ".join(r["unaudited"]))
    if r["wh_violations"]:
        for uid, mnem, k in r["wh_violations"]:
            print(f"  WH-LATENCY-VIOLATION: node {uid} {mnem} issues {k} "
                  f"slot(s) early (required NOP missing)")
    gap = r["makespan"] - r["lower"]
    print(f"  gap-to-lower-bound={gap}"
          + ("" if exact else " (lower bound only; exact refused)"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--arch", choices=["bh", "wh"], default="bh")
    ap.add_argument("--function", action="append", default=None)
    ap.add_argument("--compare", action="store_true",
                    help="two files: baseline then candidate")
    ap.add_argument("--audit-table", default=None,
                    help="cited extra audited-latency facts (tsv); "
                         "applied only where the dump says unaudited")
    args = ap.parse_args()
    audit = load_audit_table(args.audit_table) if args.audit_table else None

    if args.compare and len(args.files) != 2:
        ap.error("--compare needs exactly two .s files")

    status = 0
    parsed = [parse_nodes(p) for p in args.files]
    names = args.function or sorted(
        set().union(*[set(p) for p in parsed]))
    for name in names:
        results = []
        for p, path in zip(parsed, args.files):
            if name not in p or not p[name]:
                continue
            nodes = p[name]
            if audit:
                nodes = apply_audit_table(nodes, audit)
            r = analyze(nodes, args.arch)
            results.append((path, r))
            if r["wh_violations"]:
                status = 2
        if not results:
            continue
        if args.compare and len(results) == 2:
            (bp, br), (cp_, cr) = results
            print(f"function {name}: [{args.arch}] compare")
            for tag, (path, r) in zip(("base", "cand"), results):
                exact = "" if not r["unaudited"] else ">= "
                print(f"  {tag} {path}: makespan={exact}{r['makespan']} "
                      f"lower-bound={r['lower']}")
            if br["unaudited"] or cr["unaudited"]:
                print("  verdict: REFUSED-EXACT (unaudited classes: "
                      + ", ".join(sorted(set(br["unaudited"])
                                         | set(cr["unaudited"]))) + ")")
            else:
                d = br["makespan"] - cr["makespan"]
                lb = cr["lower"]
                print(f"  verdict: cand {'improves' if d > 0 else 'does not improve'} "
                      f"by {d}; cand gap-to-lower-bound="
                      f"{cr['makespan'] - lb}")
        else:
            for path, r in results:
                if len(args.files) > 1:
                    print(f"== {path}")
                report(name, r, args.arch)
    return status


if __name__ == "__main__":
    sys.exit(main())
