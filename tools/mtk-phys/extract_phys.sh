#!/bin/sh
#
# Run on your PC (POSIX shell): push mtk-phys.sh to the device and run it as
# root over adb. Prints kernel_phys_load / kernel_phys_offset.
#
#   ANDROID_SERIAL=<serial> tools/mtk-phys/extract_phys.sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ADB=${ADB:-adb}
if [ -n "${ANDROID_SERIAL:-}" ]; then
    ADB="$ADB -s $ANDROID_SERIAL"
fi

$ADB get-state >/dev/null 2>&1 || { echo "error: no adb device" >&2; exit 1; }
$ADB root >/dev/null 2>&1 || true
$ADB wait-for-device

dev=/data/local/tmp/ghostlock-mtk-phys.sh
$ADB push "$here/mtk-phys.sh" "$dev" >/dev/null

uid=$($ADB shell id -u | tr -d '\r')
if [ "$uid" = "0" ]; then
    $ADB shell "sh $dev"
else
    $ADB shell "su -c 'sh $dev'"
fi
