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
    "docs/plan/43499-c11-header-contract-plan.md",
    "app/src/main/cpp/gl43499/core/abi.h",
    "app/src/main/cpp/gl43499/core/result.h",
    "app/src/main/cpp/gl43499/core/placement.h",
    "app/src/main/cpp/gl43499/core/page.h",
    "app/src/main/cpp/gl43499/core/primitive.h",
    "app/src/main/cpp/gl43499/core/actor.h",
    "app/src/main/cpp/gl43499/core/api.h",
)
for relative in created:
    path = root / relative
    if path.exists():
        if path.stat().st_size != 0:
            raise SystemExit(f"rollback retained non-empty created file: {path}")
        path.unlink()
PY
