#!/bin/sh
set -eu
serial="${1:?usage: ROLLBACK.sh <adb-serial> <baseline-apk> <expected-sha256>}"
baseline="${2:?baseline APK path required}"
expected="${3:?expected SHA-256 required}"
python3 - "$baseline" "$expected" <<'ROLLPY'
from pathlib import Path
from hashlib import sha256
import sys
p=Path(sys.argv[1])
actual=sha256(p.read_bytes()).hexdigest()
if actual != sys.argv[2]:
    raise SystemExit(f"ROLLBACK_REFUSED: baseline hash mismatch actual={actual}")
ROLLPY
adb -s "$serial" install -r "$baseline"
remote="$(adb -s "$serial" shell pm path com.ghostlock.app | python3 -c 'import sys; print(next(x.strip()[len("package:"):] for x in sys.stdin if x.strip().startswith("package:")))')"
adb -s "$serial" pull "$remote" docs/exec-glue/deployment-a301so-20261029/ROLLBACK_TEST/installed-base.apk
python3 - "$serial" <<'ROLLPY'
from pathlib import Path
from hashlib import sha256
import sys
expected="6ffe0fe821b0f8c14c2b734143a188fba338e1a7c8edeb345b80c26455f0f7b0"
p=Path("docs/exec-glue/deployment-a301so-20261029/ROLLBACK_TEST/installed-base.apk")
actual=sha256(p.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f"ROLLBACK_FAILED: installed hash mismatch actual={actual}")
print(f"ROLLBACK_VERIFIED: PASS device={sys.argv[1]} sha256={actual}")
ROLLPY
