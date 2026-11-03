#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=${1:?usage: ROLLBACK.sh TARGET_COPY_ROOT}
python3 - "$ROOT/BASELINE_FILE" "$ROOT/manifest.tsv" "$TARGET" <<'PYROLLBACK'
from pathlib import Path
import shutil, sys
baseline, manifest, target = map(Path, sys.argv[1:])
restored = removed = 0
for line in manifest.read_text().splitlines()[1:]:
    kind, rel, *_ = line.split('\t')
    dest = target / rel
    if kind == 'removed':
        src = baseline / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        restored += 1
    elif kind == 'modified':
        src = baseline / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        restored += 1
    elif kind == 'added' and dest.exists():
        dest.unlink()
        removed += 1
print(f'ROLLBACK_APPLIED restored={restored} removed={removed}')
PYROLLBACK
