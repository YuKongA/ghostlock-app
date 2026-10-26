#!/bin/sh
set -eu
ROOT=${1:?usage: ROLLBACK.sh ROOT}
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT"
patch -R -p1 < "$SELF_DIR/DIFF_FILE"
