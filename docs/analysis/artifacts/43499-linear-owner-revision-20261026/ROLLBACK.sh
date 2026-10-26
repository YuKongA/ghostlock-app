#!/bin/sh
set -eu
ROOT=${1:-.}
SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$ROOT"
patch -p1 -R --forward < "$SELF_DIR/DIFF_FILE"
