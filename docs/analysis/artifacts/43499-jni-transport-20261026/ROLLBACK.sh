#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=${1:-/Users/nickji/Documents/Program/Analysis/ghostlock-app}

patch -d "$ROOT" -R -p1 < "$SCRIPT_DIR/DIFF_FILE"

python3 - "$ROOT" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
created = (
    "app/src/main/cpp/gl43499/core/api_unavailable.c",
    "docs/plan/43499-jni-transport-plan.md",
)
for relative in created:
    path = root / relative
    if path.exists():
        if path.stat().st_size != 0:
            raise SystemExit(f"rollback retained non-empty created file: {path}")
        path.unlink()
PY
