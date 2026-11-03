#!/bin/sh
set -eu
target="${1:?target path required}"
baseline="${2:?baseline path required}"
python3 - "$target" "$baseline" <<'PY'
from pathlib import Path
import hashlib, os, sys
target, baseline = map(Path, sys.argv[1:])
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
expected_current = "301b1117e371fdb3ff18f8edda0aefda82e2f9434ac1583096a22e1ddb2bb7a7"
expected_baseline = "aa3a273b074ca1c8e7e9d90f552a3d805ae521142d90ca7377a5b14541c3e886"
if sha(target) != expected_current:
    raise SystemExit("ROLLBACK_REFUSED: target hash mismatch")
if sha(baseline) != expected_baseline:
    raise SystemExit("ROLLBACK_REFUSED: baseline hash mismatch")
tmp = target.with_name(target.name + ".rollback-tmp")
tmp.write_bytes(baseline.read_bytes())
os.replace(tmp, target)
actual = sha(target)
if actual != expected_baseline:
    raise SystemExit("ROLLBACK_FAILED: restored hash mismatch")
print(f"ROLLBACK_VERIFIED: PASS sha256={actual}")
PY
