#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TARGET=${1:?usage: ROLLBACK.sh TARGET [BASELINE]}
BASELINE=${2:-$ROOT/docs/exec-glue/plan-rev11-20261029/BASELINE_FILE/exec-glue-plan.md}
python3 - "$ROOT" "$TARGET" "$BASELINE" <<'PY'
from pathlib import Path
import hashlib, os, sys
root, target_arg, baseline_arg = sys.argv[1:]
target = Path(target_arg)
if not target.is_absolute():
    target = Path(root) / target
baseline = Path(baseline_arg)
if not baseline.is_absolute():
    baseline = Path(root) / baseline
if target.resolve() == baseline.resolve():
    raise SystemExit('ROLLBACK_REJECTED: target_is_baseline')
data = baseline.read_bytes()
expected = '301b1117e371fdb3ff18f8edda0aefda82e2f9434ac1583096a22e1ddb2bb7a7'
actual = hashlib.sha256(data).hexdigest()
if actual != expected:
    raise SystemExit(f'ROLLBACK_REJECTED: baseline_sha256={actual}')
target.parent.mkdir(parents=True, exist_ok=True)
tmp = target.with_name(target.name + '.rollback.tmp')
if tmp.exists():
    raise SystemExit('ROLLBACK_REJECTED: temporary_file_exists')
with tmp.open('xb') as f:
    f.write(data)
    f.flush()
    os.fsync(f.fileno())
os.replace(tmp, target)
fd = os.open(target.parent, os.O_RDONLY)
try:
    os.fsync(fd)
finally:
    os.close(fd)
restored = target.read_bytes()
restored_hash = hashlib.sha256(restored).hexdigest()
if restored != data or restored_hash != expected:
    raise SystemExit(f'ROLLBACK_FAILED: sha256={restored_hash}')
print(f'ROLLBACK_VERIFIED: PASS target={target.resolve()} sha256={restored_hash}')
PY
