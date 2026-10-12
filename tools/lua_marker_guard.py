#!/usr/bin/env python3
"""Lua-marker guard: judging counts must read native lines only.

Why: the log seam adds a source marker on the Lua path (sandbox.cpp:438 — the marker is a
square-bracketed, three-lowercase-letter token). A script can therefore print a line that
*looks like* a named refusal. If the judging count does not exclude marked lines, then
C3 ("one grep decides") and C5 ("byte-identical to the code string") lose their evidential
value. So: exclude every marked line first, then count; and count the marked lines
separately as a control (proof the script actually reached that point).

Usage:
  python3 tools/lua_marker_guard.py <log-file> [--filter on|off] [--assert-excluded]
  python3 tools/lua_marker_guard.py <log-file> --filter off   # must FAIL on a forged log
  python3 tools/lua_marker_guard.py <log-file> --emit-filtered | grep -c '<key>'   # count a key

Exit codes:
  0  pass (filter on: marked lines excluded from the judging count)
  2  FAIL — a marked line reached the judging count (filter off), or --assert-excluded
     did not hold (no marked refusal found / a native refusal appeared)
  1  usage or read error

Falsification (AGENTS 8): point --filter off at a forged log that carries a marked line
shaped like a named refusal; the guard must then FAIL. With --filter on the same file must
pass, because the marked line never reaches the judging count.
"""

import pathlib
import re
import sys

# The marker, literally: a square-bracketed, three-lowercase-letter token. Keep this neutral
# (no escaping games): the class is written out so a reader can match it by eye.
MARKER = re.compile(r"\[[a-z][a-z][a-z]\]")

# The refusal code table is NOT re-invented here: it is parsed from its single authority,
# core/src/pipeline/step_refusal.hpp (step_refusal_name() — one verdict per case). The header
# is read-only for this guard. Narrow on purpose: a generic kebab-case shape would call any
# hyphenated word a refusal (that is how a count turns into noise).
REFUSAL_HEADER = pathlib.Path("core/src/pipeline/step_refusal.hpp")
REFUSAL_FALLBACK = ("step-target-missing", "session-frame-missing", "step-execution-missing",
                    "step-markers-missing", "step-params-inconsistent", "step-no-document",
                    "step-unknown", "prepare-region-refused", "victim-pipes-failed",
                    "victim-fork-failed", "victim-leak-disagreed", "credential-image-missing",
                    "credential-layout-missing", "alias-arithmetic-missing",
                    "init-task-offset-missing", "victim-cred-offset-missing",
                    "payload-page-unmapped", "w2-not-rooted", "target-outside-direct-map")


def refusal_codes() -> tuple:
    """Return the refusal codes, parsed from the authority header (fallback: literal list)."""
    try:
        text = REFUSAL_HEADER.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return REFUSAL_FALLBACK
    found = []
    for verdict, name in re.findall(r"case\s+StepRefusal::(\w+)\s*:\s*return\s+\"([^\"]+)\";", text):
        if verdict == "Ok":      # "won" is the success verdict, not a refusal
            continue
        found.append(name)
    return tuple(found) if found else REFUSAL_FALLBACK


REFUSALS = refusal_codes()
NAMED_REFUSAL = re.compile("|".join(re.escape(c) for c in REFUSALS))


def usage(msg: str) -> int:
    print(f"lua-marker-guard: {msg}")
    print("usage: lua_marker_guard.py <log-file> [--filter on|off] [--assert-excluded] [--emit-filtered]")
    return 1


def main(argv: list) -> int:
    args = [a for a in argv[1:]]
    if not args:
        return usage("missing <log-file>")
    log = pathlib.Path(args[0])
    filt = "on"
    assert_excluded = False
    emit_filtered = False
    i = 1
    while i < len(args):
        if args[i] == "--filter" and i + 1 < len(args):
            filt = args[i + 1].lower()
            i += 2
        elif args[i] == "--emit-filtered":
            emit_filtered = True
            i += 1
        elif args[i] == "--assert-excluded":
            assert_excluded = True
            i += 1
        else:
            return usage(f"unknown argument {args[i]!r}")
    if filt not in ("on", "off"):
        return usage("--filter takes on|off")
    if not log.is_file():
        return usage(f"not a file: {log}")

    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    marked = [ln for ln in lines if MARKER.search(ln)]
    native = [ln for ln in lines if not MARKER.search(ln)]
    marked_refusals = [ln for ln in marked if NAMED_REFUSAL.search(ln)]
    native_refusals = [ln for ln in native if NAMED_REFUSAL.search(ln)]

    if emit_filtered:
        # The judging path: native lines only, so downstream counts cannot see Lua output.
        for ln in native:
            print(ln)
        return 2 if (filt == "off" and marked_refusals) else 0

    print(f"lua-marker-guard: refusals={len(REFUSALS)} source={REFUSAL_HEADER}")
    print(f"lua-marker-guard: file={log} lines={len(lines)} filter={filt}")
    print(f"lua-marker-guard: native_lines={len(native)} marker_lines={len(marked)}")
    print(f"lua-marker-guard: native_refusals={len(native_refusals)} "
          f"marked_refusals={len(marked_refusals)}")

    if filt == "off" and marked_refusals:
        print("FAIL: a marked line reached the judging count "
              f"({len(marked_refusals)} marked refusal line(s)); the Lua path cannot be told "
              "from native output with the filter off")
        return 2

    if assert_excluded:
        if not marked_refusals:
            print("FAIL: --assert-excluded given but the file carries no marked refusal line")
            return 2
        if native_refusals:
            print(f"FAIL: --assert-excluded given but native_refusals={len(native_refusals)} "
                  "(the log is not the forged shape this check expects)")
            return 2
        print("PASS: every marked refusal line was excluded from the judging count")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
