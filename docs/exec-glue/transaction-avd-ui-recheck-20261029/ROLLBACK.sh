#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
  printf '%s\n' 'usage: ROLLBACK.sh TARGET BASELINE' >&2
  exit 2
fi
python3 - "$1" "$2" <<'PY'
from pathlib import Path
import hashlib
import sys

target = Path(sys.argv[1])
baseline = Path(sys.argv[2]).read_bytes()
target.write_bytes(baseline)
actual = target.read_bytes()
if actual != baseline:
    raise SystemExit('ROLLBACK_MISMATCH')
print(f'ROLLBACK_OK bytes={len(actual)} sha256={hashlib.sha256(actual).hexdigest()}')
PY
