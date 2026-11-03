#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET=${1:?usage: ROLLBACK.sh TARGET_COPY_ROOT}
python3 - "$ROOT/manifest.tsv" "$TARGET" <<'PYROLLBACK'
from pathlib import Path
import sys
manifest, target = map(Path, sys.argv[1:])
removed=0
for line in manifest.read_text().splitlines()[1:]:
    kind, rel, *_ = line.split('\t')
    dest=target/rel
    if kind == 'added' and dest.exists():
        dest.unlink(); removed += 1
print(f'ROLLBACK_APPLIED restored=0 removed={removed}')
PYROLLBACK
