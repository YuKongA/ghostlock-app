#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERIAL=${1:-emulator-5554}
ADB_BIN=${ADB_BIN:-adb}
BASELINE="$ROOT/BASELINE_FILE/base.apk"
"$ADB_BIN" -s "$SERIAL" install -r "$BASELINE"
"$ADB_BIN" -s "$SERIAL" uninstall com.ghostlock.app.test >/dev/null 2>&1 || true
python3 - "$BASELINE" "$ADB_BIN" "$SERIAL" <<'PYVERIFY'
import hashlib, pathlib, subprocess, sys
baseline, adb, serial = sys.argv[1:]
out=subprocess.check_output([adb, '-s', serial, 'shell', 'pm', 'path', 'com.ghostlock.app'], text=True)
remote=next((line.strip().removeprefix('package:') for line in out.splitlines() if line.startswith('package:')), None)
if not remote: raise SystemExit('ROLLBACK_VERIFY=package-path-missing')
expected=hashlib.sha256(pathlib.Path(baseline).read_bytes()).hexdigest()
actual=hashlib.sha256(subprocess.check_output([adb, '-s', serial, 'exec-out', 'cat', remote])).hexdigest()
if actual != expected: raise SystemExit(f'ROLLBACK_VERIFY=FAIL expected={expected} actual={actual}')
print(f'ROLLBACK_VERIFY=PASS sha256={actual}')
PYVERIFY
