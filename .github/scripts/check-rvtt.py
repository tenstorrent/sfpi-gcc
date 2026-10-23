#!/usr/bin/env python3
"""Require a completed, nonempty and clean DejaGNU g++ summary."""

import argparse
from collections import Counter
from pathlib import Path
import re


SUMMARY_NAMES = {
    "expected passes": "PASS",
    "unexpected failures": "FAIL",
    "unexpected successes": "XPASS",
    "expected failures": "XFAIL",
    "unresolved testcases": "UNRESOLVED",
    "untested testcases": "UNTESTED",
    "unsupported tests": "UNSUPPORTED",
}


def check_summary(text, runner_exit):
    records = Counter()
    reported = {}
    errors = []
    summaries = 0
    for line in text.splitlines():
        result = re.match(r"^(PASS|FAIL|XPASS|XFAIL|UNRESOLVED|UNTESTED|UNSUPPORTED|ERROR):", line)
        if result:
            records[result[1]] += 1
        if re.fullmatch(r"\s*=== g\+\+ Summary ===\s*", line):
            summaries += 1
        count = re.fullmatch(r"# of (.*?)\s+(\d+)\s*", line)
        if count:
            name, value = count.groups()
            kind = SUMMARY_NAMES.get(name)
            if kind is None or kind in reported or summaries != 1:
                errors.append(f"invalid summary record: {line}")
            else:
                reported[kind] = int(value)
    if summaries != 1:
        errors.append(f"expected one completed g++ summary, found {summaries}")
    for kind in SUMMARY_NAMES.values():
        if records[kind] != reported.get(kind, 0):
            errors.append(f"{kind}: records={records[kind]}, summary={reported.get(kind, 0)}")
    if not records["PASS"]:
        errors.append("no passing checks executed")
    for kind in ("FAIL", "XPASS", "UNRESOLVED", "UNTESTED", "ERROR"):
        if records[kind]:
            errors.append(f"{records[kind]} {kind} records")
    if runner_exit:
        errors.append(f"test runner exited with status {runner_exit}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--runner-exit", type=int, required=True)
    args = parser.parse_args()
    errors = check_summary(args.summary.read_text(), args.runner_exit)
    for error in errors:
        print(f"ERROR: {error}")
    if not errors:
        print("rvtt.exp completed with no unexpected results")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
