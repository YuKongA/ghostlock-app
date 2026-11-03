#!/bin/sh
set -eu
exec python3 - "$0" "$@" <<'PY'
from pathlib import Path
import shutil
import sys
script=Path(sys.argv[1]).resolve()
if len(sys.argv)!=3:
    raise SystemExit("usage: ROLLBACK.sh TARGET_COPY_OR_WORKTREE")
target=Path(sys.argv[2]).resolve()
baseline=script.parent/"BASELINE_FILE"
paths=[
 "docs/exec-glue/transaction-safe-exec-fixture/MODIFIED_FILE/exec_noop_fixture.c",
 "app/src/androidTest/kotlin/com/ghostlock/app/backend/core/GluedExecFixtureSmokeTest.kt",
 "docs/plan/exec-glue-plan.md",
]
for rel in paths:
 source=baseline/rel
 destination=target/rel
 if not source.is_file():
  raise SystemExit("baseline missing: "+rel)
 destination.parent.mkdir(parents=True,exist_ok=True)
 shutil.copyfile(source,destination)
print(f"ROLLBACK_OK restored={len(paths)}")
PY
