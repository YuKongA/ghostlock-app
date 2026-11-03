#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
TARGET=${1:-"$REPO_ROOT/docs/plan/exec-glue-plan.md"}
python3 - "$SCRIPT_DIR/BASELINE_FILE" "$TARGET" <<'PYCODE'
from pathlib import Path
import sys
source, target = map(Path, sys.argv[1:3])
target.write_bytes(source.read_bytes())
print("ROLLBACK_APPLIED")
PYCODE
