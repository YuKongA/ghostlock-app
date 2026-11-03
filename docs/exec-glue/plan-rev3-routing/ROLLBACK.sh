#!/bin/sh
set -eu
target="$1"
if [ -z "$target" ]; then echo "usage: ROLLBACK.sh <modified-copy>" >&2; exit 2; fi
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$target" "$dir/BASELINE_FILE" "$dir/MODIFIED_FILE" <<'PYCODE'
from pathlib import Path
import hashlib,sys
target,baseline,modified=map(Path,sys.argv[1:])
actual=hashlib.sha256(target.read_bytes()).hexdigest()
expected=hashlib.sha256(modified.read_bytes()).hexdigest()
if actual!=expected: raise SystemExit("ROLLBACK_REFUSED sha256="+actual+" expected="+expected)
target.write_bytes(baseline.read_bytes())
print("ROLLBACK_APPLIED")
PYCODE
