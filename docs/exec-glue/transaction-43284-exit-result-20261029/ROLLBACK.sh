#!/usr/bin/env python3
"""Restore the saved baseline into a rollback-test copy, never the live checkout."""
from pathlib import Path
import hashlib, sys

if len(sys.argv) != 2:
    raise SystemExit("usage: ROLLBACK.sh TARGET_COPY_ROOT")
artifact_dir = Path(__file__).resolve().parent
baseline = artifact_dir / "BASELINE_FILE/app/src/main/kotlin/com/ghostlock/app/backend/43284glued/Flow43284Glued.kt"
target_root = Path(sys.argv[1]).resolve()
target = target_root / "app/src/main/kotlin/com/ghostlock/app/backend/43284glued/Flow43284Glued.kt"
test = target_root / "app/src/test/kotlin/com/ghostlock/app/backend/43284glued/Flow43284GluedResultTest.kt"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(baseline.read_bytes())
if test.exists():
    test.unlink()
print(f"RESTORED source_sha256={hashlib.sha256(target.read_bytes()).hexdigest()} test_removed={not test.exists()}")
