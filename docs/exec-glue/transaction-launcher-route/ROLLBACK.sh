#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
  printf '%s\n' 'usage: ROLLBACK.sh TARGET_ROOT' >&2
  exit 2
fi
ARTIFACT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$ARTIFACT_ROOT/BASELINE_FILE" "$1" <<'PY'
from pathlib import Path
import sys
source=Path(sys.argv[1])
target=Path(sys.argv[2])
files={
 'app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableBackendProcess.kt':'ExecutableBackendProcess.kt',
 'app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableProcessRoutingTest.kt':'ExecutableProcessRoutingTest.kt',
 'docs/development/full-process-uml.md':'full-process-uml.md',
 'docs/plan/exec-glue-plan.md':'exec-glue-plan.md',
}
for relative,name in files.items():
    src=source/name
    dst=target/relative
    if not src.is_file() or not dst.parent.is_dir():
        raise SystemExit(f'rollback target unavailable: {relative}')
    dst.write_bytes(src.read_bytes())
print(f'restored {len(files)} paths')
PY
