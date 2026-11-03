#!/bin/sh
set -eu
SERIAL=${1:-adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp}
ADB_BIN=${ADB_BIN:-adb}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASELINE="$ROOT/BASELINE_FILE/base.apk"
"$ADB_BIN" -s "$SERIAL" install -r "$BASELINE"
REMOTE=$($ADB_BIN -s "$SERIAL" shell pm path com.ghostlock.app | sed -n 's/^package://p' | tr -d '\r' | head -1)
if [ -z "$REMOTE" ]; then echo 'ROLLBACK_VERIFY=package-path-missing' >&2; exit 1; fi
python3 - "$BASELINE" "$ADB_BIN" "$SERIAL" "$REMOTE" <<'PYVERIFY'
import hashlib, pathlib, subprocess, sys
baseline, adb, serial, remote = sys.argv[1:]
expected = hashlib.sha256(pathlib.Path(baseline).read_bytes()).hexdigest()
actual_bytes = subprocess.check_output([adb, '-s', serial, 'exec-out', 'cat', remote])
actual = hashlib.sha256(actual_bytes).hexdigest()
if actual != expected:
    print(f'ROLLBACK_VERIFY=FAIL expected={expected} actual={actual}', file=sys.stderr)
    raise SystemExit(1)
print(f'ROLLBACK_VERIFY=PASS sha256={actual}')
PYVERIFY
