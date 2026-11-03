#!/bin/sh
set -eu
TARGET=${1:?usage: ROLLBACK.sh TARGET_FILE}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASELINE="$SCRIPT_DIR/BASELINE_FILE/exec-glue-plan.md"
python3 - "$BASELINE" "$TARGET" <<'PYROLLBACK'
from pathlib import Path
import sys
src, dst = map(Path, sys.argv[1:])
dst.write_bytes(src.read_bytes())
print("ROLLBACK_APPLIED")
PYROLLBACK
