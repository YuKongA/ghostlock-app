#!/bin/sh
set -eu
exec python3 - "$0" "$@" <<'PY'
from pathlib import Path
import shutil
import sys
script = Path(sys.argv[1]).resolve()
if len(sys.argv) != 3:
    raise SystemExit("usage: ROLLBACK.sh TARGET_COPY_OR_WORKTREE")
target = Path(sys.argv[2]).resolve()
transaction = script.parent
baseline = transaction / "BASELINE_FILE"
paths = [
    "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableBackendProcess.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableShizukuHost.kt",
    "docs/development/full-process-uml.md",
    "docs/plan/exec-glue-plan.md",
]
for relative in paths:
    source = baseline / relative
    destination = target / relative
    if not source.is_file():
        raise SystemExit(f"baseline missing: {relative}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
for relative in [
    "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironment.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironmentTest.kt",
]:
    destination = target / relative
    if destination.exists():
        destination.unlink()
print(f"ROLLBACK_OK restored={len(paths)} removed=2")
PY
