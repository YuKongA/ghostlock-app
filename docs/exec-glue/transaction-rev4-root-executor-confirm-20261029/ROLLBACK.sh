#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
BASELINE="$SCRIPT_DIR/BASELINE_FILE/exec-glue-plan.md"
TARGET="$*"
if [ -z "$TARGET" ]; then TARGET="$REPO_ROOT/docs/plan/exec-glue-plan.md"; fi
python3 - "$BASELINE" "$TARGET" <<'PYCODE'
from pathlib import Path
import sys
source, target = map(Path, sys.argv[1:3])
target.write_bytes(source.read_bytes())
print("ROLLBACK_APPLIED")
PYCODE
