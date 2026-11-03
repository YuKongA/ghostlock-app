#!/bin/sh
set -eu
TXN_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 1 ]; then
    printf '%s\n' "usage: $0 PROJECT_ROOT" >&2
    exit 2
fi
PROJECT_ROOT=$1
python3 - "$TXN_DIR" "$PROJECT_ROOT" <<'PYROLLBACK'
from pathlib import Path
import hashlib,sys
transaction=Path(sys.argv[1])
root=Path(sys.argv[2])
modified=transaction/'MODIFIED_FILE'
baseline=transaction/'BASELINE_FILE'
paths=[]
for source in modified.rglob('*'):
    if source.is_file():
        paths.append(source.relative_to(modified))
def digest(data): return hashlib.sha256(data).hexdigest()
for rel in paths:
    target=root/rel
    expected=(modified/rel).read_bytes()
    if not target.is_file() or digest(target.read_bytes())!=digest(expected):
        raise SystemExit(f'ROLLBACK_GUARD_FAILED path={rel}')
for rel in paths:
    target=root/rel
    original=(baseline/rel).read_bytes()
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(original)
    actual=target.read_bytes()
    if digest(actual)!=digest(original):
        raise SystemExit(f'ROLLBACK_VERIFY_FAILED path={rel}')
    print(f'ROLLBACK_FILE={rel} sha256={digest(actual)}')
print(f'ROLLBACK_RESULT=restored pre-transaction bytes; changed_paths={len(paths)}')
PYROLLBACK
