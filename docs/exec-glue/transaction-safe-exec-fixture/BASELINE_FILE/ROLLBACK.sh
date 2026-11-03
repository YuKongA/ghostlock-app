#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${ADB_SERIAL:?set ADB_SERIAL to the device serial}"
ADB_BIN=${ADB:-adb}
BASELINE_APK=${BASELINE_APK:-"$SCRIPT_DIR/BASELINE_FILE/GhostLock-v2.0.12-1342.apk"}
test -f "$BASELINE_APK"
"$ADB_BIN" -s "$ADB_SERIAL" install -r "$BASELINE_APK"
