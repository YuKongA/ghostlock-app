#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
device=QV770MFGJ1
backup="$project_root/build/deployment-audit/a301so-kmi-189/pre-gate-app.apk"
restore_pull="$script_dir/restored-installed-base.apk"

python3 - "$backup" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys
p=Path(sys.argv[1])
expected="37756ac1a4b7df5cbc4ed8734178fd12b1b131e4bf4bae7184d2b7e40ecdce92"
if not p.is_file(): raise SystemExit("ROLLBACK_REFUSED=pre-gate-apk-missing")
actual=sha256(p.read_bytes()).hexdigest()
print(f"PRE_GATE_APK_SHA256={actual}")
if actual != expected: raise SystemExit("ROLLBACK_REFUSED=pre-gate-apk-hash")
PY

adb -s "$device" shell am force-stop com.ghostlock.app >/dev/null
if adb -s "$device" shell pm path com.ghostlock.app.test 2>/dev/null | grep -q '^package:'; then
    adb -s "$device" uninstall com.ghostlock.app.test
fi
adb -s "$device" install -r "$backup"
adb -s "$device" shell am force-stop com.ghostlock.app

python3 - "$project_root" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys
root=Path(sys.argv[1])
artifacts={
 "build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk":"fd6f506e2f238b6c0f2d1dea197a6bf7d346bff15ec702c8f20aa0ea0aceda3d",
 "build/app/outputs/apk/androidTest/debug/GhostLock-v2.0.12(1342)-debug-androidTest.apk":"2c5f7a33145ce74c78e0d717cd9e0983afd9b9ed4cd844a1d64cef15dfecac0d",
}
for rel,expected in artifacts.items():
 p=root/rel
 if not p.exists(): continue
 actual=sha256(p.read_bytes()).hexdigest()
 if actual!=expected: raise SystemExit(f"ROLLBACK_REFUSED=fixture-artifact-hash:{rel}:{actual}")
 p.unlink(); print(f"FIXTURE_APK_REMOVED={rel} sha256={actual}")
PY

"$project_root/docs/exec-glue/transaction-m0-marker-20261029/ROLLBACK.sh" "$project_root"

python3 - "$project_root" "$script_dir" <<'PY'
from hashlib import sha256
from pathlib import Path
import json, shutil, sys
root=Path(sys.argv[1]); tx=Path(sys.argv[2])
rel="app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironmentTest.kt"
p=root/rel; modified="75cf9062652741f1b283cef34a53c7c2b28927d0553af2c3d2a4a1e6b95bad72"
pre=tx/"TEST_PREIMAGE.kt"; expected="11f9fda8bd42cc983c4771dfffb904f8e99552ebd4fa52427663c7efc93bfb06"
if sha256(p.read_bytes()).hexdigest()!=modified: raise SystemExit("ROLLBACK_REFUSED=test-source-diverged")
if sha256(pre.read_bytes()).hexdigest()!=expected: raise SystemExit("ROLLBACK_REFUSED=test-preimage-hash")
shutil.copyfile(pre,p)
print(f"USER_TEST_RESTORED={rel} sha256={expected}")
hashes=json.loads((tx/"PREIMAGE_HASHES.json").read_text())
for path,digest in hashes.items():
 actual=sha256((root/path).read_bytes()).hexdigest()
 if actual!=digest: raise SystemExit(f"ROLLBACK_REFUSED=source-hash:{path}:{actual}")
print(f"SOURCE_PREIMAGES_RESTORED={len(hashes)}")
PY

podman machine start podman-machine-default >/dev/null 2>&1 || true
./gradlew :app:clean --no-daemon --console=plain
./gradlew :app:assembleDebug --no-daemon --console=plain

install_path=$(adb -s "$device" shell pm path com.ghostlock.app | tr -d '\r' | sed 's/^package://')
adb -s "$device" pull "$install_path" "$restore_pull" >/dev/null
python3 - "$restore_pull" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys
p=Path(sys.argv[1]); actual=sha256(p.read_bytes()).hexdigest()
expected="37756ac1a4b7df5cbc4ed8734178fd12b1b131e4bf4bae7184d2b7e40ecdce92"
print(f"RESTORED_INSTALLED_APK_SHA256={actual}")
if actual!=expected: raise SystemExit("ROLLBACK_FAILED=installed-apk-hash")
p.unlink()
PY

python3 - "$project_root" <<'PY'
from pathlib import Path
import sys, zipfile, hashlib
root=Path(sys.argv[1]); apk=root/"build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk"
with zipfile.ZipFile(apk) as z:
 names=[n for n in z.namelist() if n.endswith("lib43284Glued.so")]
 if len(names)!=1: raise SystemExit(f"ROLLBACK_FAILED=default-elf-count:{len(names)}")
 data=z.read(names[0]); digest=hashlib.sha256(data).hexdigest()
 expected="42ee0ffc5581451720588c0349adb6ed5e26457ebc03e95572c4d610bc5d8079"
 print(f"RESTORED_DEFAULT_ELF_SHA256={digest}")
 if digest!=expected: raise SystemExit("ROLLBACK_FAILED=default-elf-hash")
 if b"M0_RELEASE_FINISH_CONSUMED=1" in data or b"M0_ROOT_READY=run:" in data:
  raise SystemExit("ROLLBACK_FAILED=M0-markers-remain")
 print("RESTORED_DEFAULT_BEHAVIOR=default 43284 ELF without M0 marker code")
PY

dumpsys=$(adb -s "$device" shell dumpsys package com.ghostlock.app)
printf '%s\n' "$dumpsys" | grep -E 'versionName=2.0.12|versionCode=1342' | head -2
printf '%s\n' 'ROLLBACK_RESULT=device app + source + default ELF restored'
