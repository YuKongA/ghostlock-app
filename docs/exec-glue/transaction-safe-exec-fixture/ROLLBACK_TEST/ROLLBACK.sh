#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${ADB_SERIAL:?set ADB_SERIAL to the device serial}"
ADB_BIN=${ADB:-adb}
BASELINE_APK=${BASELINE_APK:-"$SCRIPT_DIR/BASELINE_FILE/GhostLock-source-updated-before-fixture.apk"}
test -f "$BASELINE_APK"
"$ADB_BIN" -s "$ADB_SERIAL" install -r "$BASELINE_APK"
