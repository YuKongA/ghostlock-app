#!/bin/sh
set -eu

transaction_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_project_root=$(CDPATH= cd -- "$transaction_root/../../.." && pwd)
requested_root=${1:-$default_project_root}
project_root=$(CDPATH= cd -- "$requested_root" && pwd)
case "$project_root" in
    "$default_project_root"|"$transaction_root"/ROLLBACK_TEST/*) ;;
    *) printf 'ROLLBACK_REFUSED=project-root-outside-transaction\n' >&2; exit 2 ;;
esac

python3 - "$project_root" "$transaction_root" <<'PY'
from hashlib import sha256
from pathlib import Path
import shutil
import sys

root = Path(sys.argv[1]).resolve()
transaction = Path(sys.argv[2]).resolve()
changes = []

def replace_once(relative, old, new=b""):
    path = root / relative
    data = path.read_bytes()
    count = data.count(old)
    if count != 1:
        raise SystemExit(f"ROLLBACK_REFUSED=anchor-count:{relative}:{count}")
    updated = data.replace(old, new, 1)
    path.write_bytes(updated)
    changes.append((relative, len(data), len(updated), sha256(updated).hexdigest()))

def remove_between(relative, start, end):
    path = root / relative
    data = path.read_bytes()
    if data.count(start) != 1 or data.count(end) != 1:
        raise SystemExit(f"ROLLBACK_REFUSED=range-anchor:{relative}")
    first = data.index(start)
    last = data.index(end, first + len(start))
    updated = data[:first] + data[last:]
    path.write_bytes(updated)
    changes.append((relative, len(data), len(updated), sha256(updated).hexdigest()))

def restore_exact(relative, backup_relative, expected_current, expected_restored):
    path = root / relative
    backup = transaction / backup_relative
    current = path.read_bytes()
    original = backup.read_bytes()
    current_hash = sha256(current).hexdigest()
    original_hash = sha256(original).hexdigest()
    if current_hash != expected_current:
        raise SystemExit(f"ROLLBACK_REFUSED=current-hash:{relative}:{current_hash}")
    if original_hash != expected_restored:
        raise SystemExit(f"ROLLBACK_REFUSED=preimage-hash:{backup_relative}:{original_hash}")
    path.write_bytes(original)
    restored_hash = sha256(path.read_bytes()).hexdigest()
    if restored_hash != expected_restored:
        raise SystemExit(f"ROLLBACK_FAILED=restore-hash:{relative}:{restored_hash}")
    changes.append((relative, len(current), len(original), restored_hash))

gradle = "app/build.gradle.kts"
replace_once(gradle, b'//TODO(m0-marker-20261029): temporary marker fixture build gate; remove after M0 device validation.\nval m0MarkerEnabled = providers.gradleProperty("m0Marker").orNull == "true"\n')
remove_between(
    gradle,
    b'/** Builds the isolated M0 source copy and its A301SO-specific KO under the real build root. */\n',
    b'/** The repository\'s SINGLE NDK authority',
)
replace_once(
    gradle,
    b'        externalNativeBuild {\n            cmake {\n'
    b'                arguments += "-DGHOSTLOCK_M0_MARKER=${if (m0MarkerEnabled) "ON" else "OFF"}"\n'
    b'                if (m0MarkerEnabled) {\n'
    b'                    arguments += "-DGHOSTLOCK_M0_SOURCE_DIR=${m0BuildDirectory.get().asFile.absolutePath}"\n'
    b'                }\n            }\n        }\n',
)
replace_once(gradle, b'            buildConfigField("boolean", "M0_MARKER", m0MarkerEnabled.toString())\n')
replace_once(
    gradle,
    b'if (m0MarkerEnabled) {\n'
    b'    tasks.named("preBuild") { dependsOn(buildM0MarkerKo) }\n'
    b'    tasks.configureEach {\n'
    b'        if (name.contains("CMake", ignoreCase = true)) dependsOn(buildM0MarkerKo)\n'
    b'    }\n}\n\n',
)

cmake = "app/src/main/cpp/43284glued/CMakeLists.txt"
remove_between(
    cmake,
    b'#TODO(m0-marker-20261029): isolate the approved marker-only device fixture; remove this\n',
    b'set(DFROOT_SOURCE_DIR',
)

environment = "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironment.kt"
replace_once(environment, b'import com.ghostlock.app.BuildConfig\n')
replace_once(
    environment,
    b' * @return the allowed 43499 path variables, the M0-only 43284 run path, or no additions.\n',
    b' * @return only the allowed 43499 path variables; other backends receive no additions.\n',
)
replace_once(
    environment,
    b'     * @return three path variables for 43499-Glued, the M0 run path for 43284, or an empty map.\n',
    b'     * @return three path variables for 43499-Glued, or an empty map for other backends.\n',
)
replace_once(
    environment,
    b'        if (backendKey == "43284Glued" && BuildConfig.M0_MARKER) {\n'
    b'            return mapOf("GHOSTLOCK_HOME" to home)\n'
    b'        }\n',
)
replace_once(
    environment,
    b'        val home = runDirectory.absolutePath\n'
    b'        if (backendKey != "43499-Glued") return emptyMap()\n'
    b'        return mapOf(\n',
    b'        if (backendKey != "43499-Glued") return emptyMap()\n'
    b'        val home = runDirectory.absolutePath\n'
    b'        return mapOf(\n',
)

host = "app/src/main/kotlin/com/ghostlock/app/backend/core/ExecutableShizukuHost.kt"
replace_once(host, b'import com.ghostlock.app.BuildConfig\n')
replace_once(host, b'import java.io.FileInputStream\n')
replace_once(
    host,
    b'        val runId = if (BuildConfig.M0_MARKER) UUID.randomUUID().toString().replace("-", "")\n'
    b'        else UUID.randomUUID().toString()\n'
    b'        val directory = File(root, runId)\n',
    b'        val directory = File(root, UUID.randomUUID().toString())\n',
)

restore_exact(
    "app/src/main/cpp/43284glued/exp.c",
    "BASELINE_FILE/app/src/main/cpp/43284glued/exp.c",
    "79bee37b591002b576c92720bf44c4d1889f7faf0482bda9e37d302b7b98c00b",
    "afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39",
)
restore_exact(
    "app/src/main/cpp/43284glued/libcxx.S",
    "BASELINE_FILE/app/src/main/cpp/43284glued/libcxx.S",
    "362c4d33d6c35a852c718d57b4519cf7c4755d262a47672843fde2f0b0588096",
    "d423e769a9b127f52f545e4c1bc5874aa0643a1457a3b9ac69471480ad9812be",
)
restore_exact(
    "app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironmentTest.kt",
    "BASELINE_FILE/app/src/test/kotlin/com/ghostlock/app/backend/core/ExecutableRunEnvironmentTest.kt",
    "1615e872978250149a4e0d84267331fc91bc640679a17fc23da1599b165beb6a",
    "11f9fda8bd42cc983c4771dfffb904f8e99552ebd4fa52427663c7efc93bfb06",
)
replace_once(
    host,
    b'            if (BuildConfig.M0_MARKER && binaryName == "lib43284Glued.so") {\n'
    b'                val worker = File(directory, "m0-worker")\n'
    b'                FileInputStream(stagedBinary).use { input ->\n'
    b'                    FileOutputStream(worker).use { output -> input.copyTo(output) }\n'
    b'                }\n'
    b'                check(worker.setReadable(false, false)) { "stage:m0-worker-read-reset" }\n'
    b'                check(worker.setWritable(false, false)) { "stage:m0-worker-write-reset" }\n'
    b'                check(worker.setExecutable(false, false)) { "stage:m0-worker-exec-reset" }\n'
    b'                check(worker.setReadable(true, true)) { "stage:m0-worker-read-owner" }\n'
    b'                check(worker.setWritable(true, true)) { "stage:m0-worker-write-owner" }\n'
    b'                check(worker.setExecutable(true, true)) { "stage:m0-worker-exec-owner" }\n'
    b'            }\n',
)

test = root / "app/src/androidTest/kotlin/com/ghostlock/app/backend/43284glued/M0MarkerGateTest.kt"
if test.exists():
    test.unlink()
    changes.append((str(test.relative_to(root)), -1, 0, "absent"))

for relative in (
    "app/src/main/cpp/43284glued/m0",
    "build/app/m0/fixture/43284glued",
    "app/.cxx/Debug/5h1x94h2",
    "build/app/intermediates/cxx/Debug/5h1x94h2",
):
    target = root / relative
    if target.exists():
        if target.is_symlink() or not target.resolve().is_relative_to(root):
            raise SystemExit(f"ROLLBACK_REFUSED=unsafe-directory:{relative}")
        shutil.rmtree(target)
        changes.append((relative, -1, 0, "absent"))

expected_artifacts = {
    "build/app/outputs/apk/debug/GhostLock-v2.0.12(1342)-arm64-v8a-debug.apk":
        "0c46ab51427a911c3103eb3737da2525cee3fbdb835a9d86a7b3ebb2d47984d7",
    "build/app/outputs/apk/androidTest/debug/GhostLock-v2.0.12(1342)-debug-androidTest.apk":
        "3a5ca664cd9e3e9740d8cf939f85733551cb6a1684ee316d8a2604bf2836dea8",
}
for relative, expected in expected_artifacts.items():
    artifact = root / relative
    if artifact.exists():
        actual = sha256(artifact.read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f"ROLLBACK_REFUSED=artifact-hash:{relative}:{actual}")
        artifact.unlink()
        changes.append((relative, -1, 0, "absent"))

for relative, before, after, digest in changes:
    print(f"ROLLBACK_FILE={relative} bytes={before}->{after} sha256={digest}")
print(f"ROLLBACK_RESULT=restored M0-only source delta; changed_paths={len(changes)}")
PY
