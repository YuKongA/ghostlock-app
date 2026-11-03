#!/bin/sh
set -eu
TARGET=${1:?usage: ROLLBACK.sh <created-report-path>}
python3 - "$TARGET" <<'PYCODE'
from pathlib import Path
import sys
target = Path(sys.argv[1])
if target.exists():
    target.unlink()
print(f"ROLLBACK=ABSENT {target}")
PYCODE
