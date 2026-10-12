#!/usr/bin/env python3
"""PI-span guard: inside PI the window does no logging, no allocation, no stdio, no indirect call.

Why this is span-level and no longer function-level (2026-10-12 user ruling): the prohibition
binds INSIDE PI -- the interval a thread spends on the chain the kernel walks -- and not the
whole window body. A window function may print again once its PI span is over; the values a
syscall produced inside PI are carried out in shared_ fields and printed after the last actor
stopped (seam/multicast_route.hpp report_pi_readings).

The ledger below is this scan's definition of PI:
  - (function, None, None): the whole body runs inside PI;
  - (function, start, end): only the text between the two anchors is inside PI, and each anchor
    must be found exactly once. An anchor that is gone means the span moved: the scan refuses to
    guess rather than silently widen or narrow itself.

judge is deliberately NOT in the ledger: its only call site is after stop_threads(), so it runs
outside PI and may print. Its name is still checked, so renaming it forces this ledger to be
revisited instead of quietly shrinking the scan.

pi_window.hpp:12 is the authority for the prohibitions; docs/analysis/pi-window-constraints.md
section 2-3 is the syscall whitelist the spans are the first machine cut of.

Falsification (AGENTS 8): all three cases run on a COPY, never on the tree --
  A. a printf between waiter_main's anchors      -> WINDOW-BODY-DIRTY (FAIL)
  B. a printf after waiter_main's PI-OUT anchor  -> PASS (the old function-level guard failed)
  C. the start anchor's statement rewritten      -> PI-ANCHOR-MISSING (FAIL)
"""

import pathlib
import re
import sys

# The target is a parameter so the guard can be falsified against a copy: mutating the real
# file to prove a guard fails is how a tree gets poisoned (this project has the scar).
TARGET = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                      "core/src/backend/cve_2026_43499/seam/multicast_route.hpp")

# The ledger: what runs inside PI. Anchors are whole statements, and the end anchors are the
# explicit PI-OUT comments that mark where each function leaves PI (IO allowed below them).
PI_SPANS = (
    ("trigger", "/* PI-IN-TRIGGER", "/* PI-OUT-TRIGGER"),
    ("consumer_main", "/* PI-IN-CONSUMER", "/* PI-OUT-CONSUMER"),
    ("waiter_main", "/* PI-IN", "/* PI-OUT"),
    ("owner_main", "/* PI-IN", "/* PI-OUT"),
)

# Named exemptions: outside PI by call site, checked so the exemption stays deliberate.
OUTSIDE_PI = ("judge",)
# Each exemption carries its own anchor comment in the code, so the exemption has a citable
# source (and removing that anchor -- or renaming the function -- fails this scan).
EXEMPT_ANCHORS = ("/* PI-EXEMPT-JUDGE",)

# The seam's log() is banned by name: an earlier guard that only knew the log_step* names passed
# a mutated window body (found by falsifying on a copy).
BANNED = re.compile(
    r"\blog\b|\blogging\b|"
    r"\blog_step\b|\blog_step_value\b|\blog_step_values\b|\blog_hex\b|spdlog|printf|"
    r"std::string|std::vector|std::function|\bnew\b|malloc|calloc|realloc|\bvirtual\b"
)


def slice_functions(text: str) -> dict:
    """Return {function name: body} for every function the ledger names."""
    names = [entry[0] for entry in PI_SPANS] + list(OUTSIDE_PI)
    found = {}
    pattern = re.compile(
        r"(?:void|bool|int|std::int32_t)\s+\*?\s*(" + "|".join(names) + r")\(\)[^\n]*\n"
    )
    for match in pattern.finditer(text):
        name = match.group(1)
        start = match.end() - 1
        index = match.start()
        cursor = text.find("{", index)
        depth = 0
        while cursor < len(text):
            if text[cursor] == "{":
                depth += 1
            elif text[cursor] == "}":
                depth -= 1
                if depth == 0:
                    break
            cursor += 1
        found[name] = text[start:cursor]
    return found


def span_of(body: str, start, end):
    """The text inside PI, or a named refusal when an anchor is missing or not unique."""
    if start is None or end is None:
        return body, None
    if body.count(start) != 1:
        return None, "start anchor x" + str(body.count(start)) + ": " + start
    if body.count(end) != 1:
        return None, "end anchor x" + str(body.count(end)) + ": " + end
    return body[body.index(start):body.index(end)], None


def main() -> int:
    text = TARGET.read_text()
    bodies = slice_functions(text)
    missing = [name for name, _, _ in PI_SPANS if name not in bodies]
    missing += [name for name in OUTSIDE_PI if name not in bodies]
    if missing:
        print("PI-SPAN-MISSING:", ", ".join(missing))
        return 1
    gone = [anchor for anchor in EXEMPT_ANCHORS if anchor not in text]
    if gone:
        print("PI-EXEMPT-MISSING:", ", ".join(gone))
        return 1
    dirty = 0
    for name, start, end in PI_SPANS:
        span, why = span_of(bodies[name], start, end)
        if why is not None:
            print("PI-ANCHOR-MISSING: " + name + " " + why)
            return 1
        for number, line in enumerate(span.split(chr(10)), start=1):
            stripped = line.split("//")[0]
            if BANNED.search(stripped):
                print("WINDOW-BODY-DIRTY: " + name + " PI line " + str(number) + ": " +
                      line.strip()[:88])
                dirty += 1
    if dirty != 0:
        print("WINDOW-BODY-DIRTY: " + str(dirty))
        return 1
    print("check: inside PI, no logging, allocation, stdio or indirect dispatch")
    print("check: " + ", ".join(name for name, _, _ in PI_SPANS) +
          " scanned by span; " + ", ".join(OUTSIDE_PI) + " exempt (outside PI)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
