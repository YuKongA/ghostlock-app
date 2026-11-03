#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
device=$1
baseline=$2
expected=$3
adb -s "$device" install -r "$baseline"
remote=$(adb -s "$device" shell pm path com.ghostlock.app | sed -n '1s/^package://p' | tr -d '\r')
test -n "$remote"
out="$root/ROLLBACK_TEST/installed-base.apk"
adb -s "$device" pull "$remote" "$out" >/dev/null
actual=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$out")
test "$actual" = "$expected"
printf 'ROLLBACK_VERIFIED: PASS device=%s sha256=%s\n' "$device" "$actual"
