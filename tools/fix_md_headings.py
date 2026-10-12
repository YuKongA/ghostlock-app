#!/usr/bin/env python3
"""Insert the blank lines markdownlint wants around headings that precede a fence."""

import pathlib
import sys

BLANK = chr(10)


def main() -> int:
    for name in sys.argv[1:]:
        path = pathlib.Path(name)
        lines = path.read_text().split(BLANK)
        out = []
        for index, line in enumerate(lines):
            out.append(line)
            following = lines[index + 1] if index + 1 < len(lines) else ""
            if line.startswith("#") and following.startswith("```"):
                out.append("")
        path.write_text(BLANK.join(out))
        print("fixed", name, len(lines), "->", len(out), "lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
