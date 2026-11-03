#!/usr/bin/env python3
"""Restore M0 tail-preflight sources in an explicitly supplied disposable or target tree."""
from pathlib import Path
import argparse
import hashlib
import sys

TRANSACTION = Path(__file__).resolve().parent
FILES = ["app/src/main/cpp/43284glued/m0/exp.c", "app/src/androidTest/kotlin/com/ghostlock/app/backend/43284glued/M0MarkerGateTest.kt", "docs/plan/exec-glue-plan.md"]

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

parser = argparse.ArgumentParser()
parser.add_argument("--target-root", required=True, help="explicit target root; no live worktree path is inferred")
args = parser.parse_args()
target_root = Path(args.target_root).resolve()
entries = []
for name in FILES:
    relative = Path(name)
    baseline = TRANSACTION / "BASELINE_FILE" / relative
    modified = TRANSACTION / "MODIFIED_FILE" / relative
    target = target_root / relative
    if not baseline.is_file() or not modified.is_file() or not target.is_file():
        print(f"ROLLBACK_RESULT=missing file: {name}", file=sys.stderr)
        sys.exit(2)
    current = target.read_bytes()
    if current != modified.read_bytes():
        print(f"ROLLBACK_RESULT=target mismatch: {name} sha256={sha256(current)}", file=sys.stderr)
        sys.exit(3)
    entries.append((name, baseline.read_bytes(), target))
for name, baseline, target in entries:
    target.write_bytes(baseline)
for name, baseline, target in entries:
    restored = target.read_bytes()
    if restored != baseline:
        print(f"ROLLBACK_RESULT=verification failed: {name}", file=sys.stderr)
        sys.exit(4)
print(f"ROLLBACK_RESULT=restored {len(entries)} baseline files; changed_paths={len(entries)}")
for name, baseline, _ in entries:
    print(f"RESTORED_SHA256={sha256(baseline)} {name}")
