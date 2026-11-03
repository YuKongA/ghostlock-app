#!/usr/bin/env python3
"""Restore the exact pre-transaction test source in an explicitly supplied copy."""
from pathlib import Path
import argparse
import hashlib
import sys

TRANSACTION = Path(__file__).resolve().parent
RELATIVE = Path("app/src/androidTest/kotlin/com/ghostlock/app/backend/43284glued/M0MarkerGateTest.kt")
BASELINE = TRANSACTION / "BASELINE_FILE" / RELATIVE
MODIFIED = TRANSACTION / "MODIFIED_FILE" / RELATIVE

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

parser = argparse.ArgumentParser()
parser.add_argument("--target-root", required=True, help="copy root to restore; the transaction workspace is never inferred")
args = parser.parse_args()
target = Path(args.target_root).resolve() / RELATIVE
if not BASELINE.is_file() or not MODIFIED.is_file() or not target.is_file():
    print("ROLLBACK_RESULT=missing baseline/modified/target", file=sys.stderr)
    sys.exit(2)
current = target.read_bytes()
expected = MODIFIED.read_bytes()
if current != expected:
    print(f"ROLLBACK_RESULT=target mismatch; current_sha256={sha256(current)}", file=sys.stderr)
    sys.exit(3)
baseline = BASELINE.read_bytes()
target.write_bytes(baseline)
restored = target.read_bytes()
if restored != baseline:
    print(f"ROLLBACK_RESULT=verification failed; restored_sha256={sha256(restored)}", file=sys.stderr)
    sys.exit(4)
print(f"ROLLBACK_RESULT=restored baseline; path={target}; sha256={sha256(restored)}")
