#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET_ROOT=${1:?Usage: ROLLBACK.sh <isolated-target-root>}
python3 - "$SCRIPT_DIR/BASELINE_FILE" "$SCRIPT_DIR/manifest.tsv" "$TARGET_ROOT" <<'PYCODE'
from pathlib import Path
import sys
baseline, manifest, target = map(Path, sys.argv[1:4])
target = target.resolve()
restored = 0
removed = 0
for line in manifest.read_text().splitlines():
    if not line: continue
    rel, baseline_status, *_ = line.split('	')
    dest = target / rel
    if baseline_status == 'BASELINE=PRESENT':
        src = baseline / rel
        if not src.is_file(): raise SystemExit('baseline file missing: ' + rel)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(src.read_bytes())
        restored += 1
    else:
        if dest.exists():
            dest.unlink()
            removed += 1
print(f'ROLLBACK_APPLIED files={restored} removed={removed}')
PYCODE
