#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
TARGET_ROOT=${1:-"$REPO_ROOT"}
python3 - "$SCRIPT_DIR/BASELINE_FILE" "$TARGET_ROOT" <<'PYCODE'
from pathlib import Path
import sys
source_root, target_root = map(Path, sys.argv[1:3])
count = 0
for source in source_root.rglob('*'):
    if source.is_file():
        target = target_root / source.relative_to(source_root)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        count += 1
print(f"ROLLBACK_APPLIED files={count}")
PYCODE
