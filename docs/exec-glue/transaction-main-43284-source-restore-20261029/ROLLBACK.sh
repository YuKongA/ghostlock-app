#!/bin/sh
set -eu
tx_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_root=$(CDPATH= cd -- "$tx_root/../../.." && pwd)
requested=${1:-$default_root}
root=$(CDPATH= cd -- "$requested" && pwd)
case "$root" in "$default_root"|"$tx_root"/ROLLBACK_TEST/*) ;; *) echo ROLLBACK_REFUSED=outside-root >&2; exit 2 ;; esac
python3 - "$root" "$tx_root" <<'PYCODE'
from hashlib import sha256
from pathlib import Path
import sys
root=Path(sys.argv[1]); tx=Path(sys.argv[2])
files={
'app/src/main/cpp/43284glued/exp.c':('afdd68cb6b07fa9b04f93c7d4bd9f8ce0333f4dd2265416959cf9c630d55ac39','79bee37b591002b576c92720bf44c4d1889f7faf0482bda9e37d302b7b98c00b'),
'app/src/main/cpp/43284glued/libcxx.S':('d423e769a9b127f52f545e4c1bc5874aa0643a1457a3b9ac69471480ad9812be','362c4d33d6c35a852c718d57b4519cf7c4755d262a47672843fde2f0b0588096'),
}
for rel,(wanted,restored) in files.items():
 p=root/rel; b=(tx/'BASELINE_FILE'/rel).read_bytes(); current=sha256(p.read_bytes()).hexdigest(); backup=sha256(b).hexdigest()
 if current!=wanted or backup!=restored: raise SystemExit(f'ROLLBACK_REFUSED={rel}:{current}:{backup}')
 p.write_bytes(b)
 if sha256(p.read_bytes()).hexdigest()!=restored: raise SystemExit(f'ROLLBACK_FAILED={rel}')
 print(f'ROLLBACK_FILE={rel} sha256={restored}')
print('ROLLBACK_RESULT=restored pre-transaction bytes')
PYCODE
