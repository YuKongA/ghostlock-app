#!/bin/sh
set -eu
python3 - "$1" "$2" <<'PYROLLBACK'
from pathlib import Path
import hashlib, sys
current = Path(sys.argv[1])
baseline = Path(sys.argv[2])
expected_modified = "6d9fe3f2c150961da501cd9aca8ac63de5f16b3d0bc3366c0219ebc84cc1fd26"
expected_baseline = "28fc640908d6d831480a505dd772c87322e5d0e86a6d7451d54a77d5e79d3985"
if hashlib.sha256(current.read_bytes()).hexdigest() != expected_modified:
    raise SystemExit("ROLLBACK_REFUSED: active file hash differs")
content = baseline.read_bytes()
if hashlib.sha256(content).hexdigest() != expected_baseline:
    raise SystemExit("ROLLBACK_REFUSED: baseline hash differs")
current.write_bytes(content)
print("ROLLBACK_RESTORED: " + hashlib.sha256(current.read_bytes()).hexdigest())
PYROLLBACK
