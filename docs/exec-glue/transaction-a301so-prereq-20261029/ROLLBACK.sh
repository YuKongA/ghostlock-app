#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=${1:?usage: ROLLBACK.sh <copy-to-restore>}
python3 - "$ROOT/BASELINE_FILE/exec-glue-plan.md" "$TARGET" <<'PYCODE'
from pathlib import Path
import hashlib, sys
source = Path(sys.argv[1])
target = Path(sys.argv[2])
data = source.read_bytes()
target.write_bytes(data)
print(f"ROLLBACK_OK bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}")
PYCODE
