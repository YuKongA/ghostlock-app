#!/bin/sh
set -eu
BASE_PLAN=${1:?usage: ROLLBACK.sh <baseline-plan> <baseline-review-log> <target-plan> <target-review-log>}
BASE_LOG=${2:?usage: ROLLBACK.sh <baseline-plan> <baseline-review-log> <target-plan> <target-review-log>}
TARGET_PLAN=${3:?usage: ROLLBACK.sh <baseline-plan> <baseline-review-log> <target-plan> <target-review-log>}
TARGET_LOG=${4:?usage: ROLLBACK.sh <baseline-plan> <baseline-review-log> <target-plan> <target-review-log>}
python3 - "$BASE_PLAN" "$BASE_LOG" "$TARGET_PLAN" "$TARGET_LOG" <<'PYCODE'
from pathlib import Path
import hashlib, sys
bp, bl, tp, tl = map(Path, sys.argv[1:])
for source, target in ((bp, tp), (bl, tl)):
    target.write_bytes(source.read_bytes())
    print(f"ROLLBACK=BASELINE {target} sha256={hashlib.sha256(target.read_bytes()).hexdigest()}")
PYCODE
