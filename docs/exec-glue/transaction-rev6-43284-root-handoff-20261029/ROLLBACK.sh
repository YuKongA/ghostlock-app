#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
target=$1
cp "$here/BASELINE_FILE/exec-glue-plan.md" "$target"
cmp -s "$here/BASELINE_FILE/exec-glue-plan.md" "$target"
printf 'ROLLBACK_RESTORED: PASS path=%s\n' "$target"
