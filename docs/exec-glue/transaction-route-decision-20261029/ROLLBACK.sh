#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
  printf '%s\n' 'usage: ROLLBACK.sh TARGET BASELINE' >&2
  exit 2
fi
python3 -c 'from pathlib import Path; import sys; Path(sys.argv[1]).write_bytes(Path(sys.argv[2]).read_bytes())' "$1" "$2"
