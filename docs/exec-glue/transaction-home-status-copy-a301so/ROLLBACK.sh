#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERIAL=${1:-adb-QV770MFGJ1-XxNlpb._adb-tls-connect._tcp}
ADB_BIN=${ADB_BIN:-adb}
BASELINE="$ROOT/BASELINE_FILE/base.apk"
"$ADB_BIN" -s "$SERIAL" install -r "$BASELINE"
python3 - "$BASELINE" "$ADB_BIN" "$SERIAL" <<'PYVERIFY'
from pathlib import Path
import hashlib,subprocess,sys
baseline,adb,serial=sys.argv[1:]
out=subprocess.check_output([adb,'-s',serial,'shell','pm','path','com.ghostlock.app'],text=True)
remote=next(x.strip().removeprefix('package:') for x in out.splitlines() if x.startswith('package:'))
actual=hashlib.sha256(subprocess.check_output([adb,'-s',serial,'exec-out','cat',remote])).hexdigest()
expected=hashlib.sha256(Path(baseline).read_bytes()).hexdigest()
if actual!=expected: raise SystemExit(f'ROLLBACK_VERIFY=FAIL expected={expected} actual={actual}')
print(f'ROLLBACK_VERIFY=PASS sha256={actual}')
PYVERIFY
