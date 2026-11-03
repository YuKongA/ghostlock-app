#!/bin/sh
set -eu
TARGET_ROOT=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --target-root) TARGET_ROOT=${2:?missing target root}; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 64 ;;
  esac
done
[ -n "$TARGET_ROOT" ] || { echo "--target-root is required" >&2; exit 64; }
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$SCRIPT_DIR" "$TARGET_ROOT" <<'PY'
from pathlib import Path
import hashlib, json, sys
base, target = Path(sys.argv[1]), Path(sys.argv[2])
manifest = json.loads((base / 'PREIMAGE_HASHES.json').read_text())
paths = [Path(x) for x in manifest['modified_sha256']]
for rel in paths:
    file = target / rel
    if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != manifest['modified_sha256'][rel.as_posix()]:
        raise SystemExit(f'modified hash mismatch: {file}')
for rel in paths:
    file = target / rel
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes((base / 'BASELINE_FILE' / rel).read_bytes())
    if hashlib.sha256(file.read_bytes()).hexdigest() != manifest['baseline_sha256'][rel.as_posix()]:
        raise SystemExit(f'rollback verification failed: {file}')
print('ROLLBACK_OK files=3 baseline_hashes_match=yes')
PY
