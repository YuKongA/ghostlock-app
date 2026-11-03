#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BASELINE="$ROOT/BASELINE_FILE/ShizukuBackendClient.kt"
TARGET=${1:?usage: ROLLBACK.sh target-copy}
test -f "$BASELINE"
test -f "$TARGET"
python3 -c 'import pathlib,sys; pathlib.Path(sys.argv[2]).write_bytes(pathlib.Path(sys.argv[1]).read_bytes())' "$BASELINE" "$TARGET"
cmp -s "$BASELINE" "$TARGET"
printf 'ROLLBACK_RESTORED=yes sha256=%s\n' "$(python3 -c 'import hashlib,pathlib,sys; print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())' "$TARGET")"
