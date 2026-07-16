#!/usr/bin/env python3
"""Acceptance-battery runner for the mathsolver CLI.

Implements exactly the conventions in tests/acceptance/README.md:

  - Tab-separated cases: id, subcommand, input, extra_args, expected_kind,
    expected, expects_exit.  Lines starting with '#' and blank lines are
    comments.
  - Invocation: mathsolver <subcommand> "<input>" <extra_args...>, where
    extra_args of "-" means none (otherwise split on spaces).
  - Compare against stdout when expects_exit is 0, stderr otherwise.
  - The exit code must equal expects_exit in every case.
  - expected of "-" means: assert the exit code only, ignore the text.
  - expected_kind:
      exact    -- stream with trailing whitespace/newline stripped must equal
                  expected byte-for-byte.
      contains -- expected must occur as a literal substring of the stream.
      regex    -- Python re.search(pattern, stream, re.MULTILINE | re.DOTALL).
      approx   -- expected is one decimal number or a comma-separated list;
                  every numeric token is extracted from the stream with
                  [-+]?\\d+\\.?\\d*([eE][-+]?\\d+)? and each expected value
                  must be within tolerance of AT LEAST ONE extracted number
                  (subset semantics; extra numbers in the output are fine).
                  Tolerance: |actual - expected| <= 1e-6 * max(1, |expected|).

Usage:
  run_acceptance.py [--binary PATH] TSV [TSV ...]

Exits nonzero if any case fails (or any TSV line is malformed).
"""

import argparse
import re
import subprocess
import sys

NUM_TOKEN = re.compile(r"[-+]?\d+\.?\d*(?:[eE][-+]?\d+)?")
KINDS = ("exact", "regex", "contains", "approx")


def extract_numbers(stream):
    out = []
    for tok in NUM_TOKEN.findall(stream):
        try:
            out.append(float(tok))
        except ValueError:
            pass
    return out


def check_approx(expected_field, stream):
    """Return (ok, detail)."""
    actual = extract_numbers(stream)
    missing = []
    for part in expected_field.split(","):
        part = part.strip()
        want = float(part)
        tol = 1e-6 * max(1.0, abs(want))
        if not any(abs(a - want) <= tol for a in actual):
            missing.append(part)
    if missing:
        return False, ("expected value(s) %s not within tolerance of any "
                       "extracted number; extracted: %s"
                       % (", ".join(missing), actual))
    return True, ""


def run_case(binary, case):
    cid, sub, inp, extra, kind, expected, expects_exit = case
    argv = [binary, sub, inp]
    if extra != "-":
        argv += extra.split(" ")
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return False, "timeout after 60s"
    except OSError as e:
        return False, "failed to launch %r: %s" % (argv, e)

    want_exit = int(expects_exit)
    if proc.returncode != want_exit:
        return False, ("exit code %d != expected %d (stdout=%r stderr=%r)"
                       % (proc.returncode, want_exit,
                          proc.stdout, proc.stderr))

    if expected == "-":
        return True, ""

    stream = proc.stdout if want_exit == 0 else proc.stderr

    if kind == "exact":
        if stream.rstrip() != expected:
            return False, "exact mismatch: expected %r, got %r" % (
                expected, stream.rstrip())
    elif kind == "contains":
        if expected not in stream:
            return False, "substring %r not found in %r" % (expected, stream)
    elif kind == "regex":
        if re.search(expected, stream, re.MULTILINE | re.DOTALL) is None:
            return False, "regex %r did not match %r" % (expected, stream)
    elif kind == "approx":
        ok, detail = check_approx(expected, stream)
        if not ok:
            return False, "%s (stream=%r)" % (detail, stream)
    else:
        return False, "unknown expected_kind %r" % kind
    return True, ""


def load_cases(path):
    cases = []
    errors = []
    with open(path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 7:
                errors.append("%s:%d: expected 7 tab-separated columns, got %d"
                              % (path, lineno, len(cols)))
                continue
            if cols[4] not in KINDS:
                errors.append("%s:%d: unknown expected_kind %r"
                              % (path, lineno, cols[4]))
                continue
            if cols[6] not in ("0", "1", "2"):
                errors.append("%s:%d: bad expects_exit %r"
                              % (path, lineno, cols[6]))
                continue
            cases.append(cols)
    return cases, errors


def main():
    ap = argparse.ArgumentParser(description="Run mathsolver acceptance TSVs")
    ap.add_argument("--binary", required=True, help="path to mathsolver binary")
    ap.add_argument("tsvs", nargs="+", help="TSV case files")
    args = ap.parse_args()

    total = passed = 0
    failures = []

    for path in args.tsvs:
        cases, errors = load_cases(path)
        for e in errors:
            failures.append(("<format>", e))
            print("FAIL <format>  %s" % e)
        for case in cases:
            total += 1
            ok, detail = run_case(args.binary, case)
            if ok:
                passed += 1
                print("PASS %-8s (%s)" % (case[0], path))
            else:
                failures.append((case[0], detail))
                print("FAIL %-8s (%s): %s" % (case[0], path, detail))

    print()
    print("summary: %d/%d passed, %d failed" % (passed, total, len(failures)))
    if failures:
        print("failing cases:")
        for cid, detail in failures:
            print("  %s: %s" % (cid, detail))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
