#!/bin/sh
set -eu
python3 - "$1" "$2" <<'PYROLLBACK'
from pathlib import Path
import hashlib, sys
current=Path(sys.argv[1]); baseline=Path(sys.argv[2])
if hashlib.sha256(current.read_bytes()).hexdigest() != "dbd16e189415fbd6d51444bea6b0445c3c1b3df98bb4a3cb173eb149f2370079": raise SystemExit("ROLLBACK_REFUSED: modified hash mismatch")
data=baseline.read_bytes()
if hashlib.sha256(data).hexdigest() != "6d9fe3f2c150961da501cd9aca8ac63de5f16b3d0bc3366c0219ebc84cc1fd26": raise SystemExit("ROLLBACK_REFUSED: baseline hash mismatch")
current.write_bytes(data)
print("ROLLBACK_RESTORED: " + hashlib.sha256(current.read_bytes()).hexdigest())
PYROLLBACK
