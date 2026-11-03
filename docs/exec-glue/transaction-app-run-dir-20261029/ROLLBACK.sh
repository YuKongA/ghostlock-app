#!/bin/sh
set -eu
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
target_root=${1:?usage: ROLLBACK.sh <copy-root>}
python3 - "$script_dir" "$target_root" <<'PYCODE'
from pathlib import Path
import hashlib, sys
artifact=Path(sys.argv[1])
target=Path(sys.argv[2])
files={
 "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableBackendProcess.kt":"ExecutableBackendProcess.kt",
 "app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableProcessRoutingTest.kt":"ExecutableProcessRoutingTest.kt",
}
for relative,name in files.items():
 source=artifact/"BASELINE_FILE"/name
 destination=target/relative
 destination.parent.mkdir(parents=True,exist_ok=True)
 payload=source.read_bytes()
 destination.write_bytes(payload)
 actual=hashlib.sha256(destination.read_bytes()).hexdigest()
 expected=hashlib.sha256(payload).hexdigest()
 if actual!=expected: raise SystemExit("ROLLBACK_MISMATCH="+relative)
 print("ROLLBACK_RESTORED="+relative+" sha256="+actual)
PYCODE
