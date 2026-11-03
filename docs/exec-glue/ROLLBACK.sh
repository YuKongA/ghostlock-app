#!/bin/sh
set -eu
TARGET_ROOT=${1:?Usage: ROLLBACK.sh TARGET_ROOT}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 - "$SCRIPT_DIR/DIFF_FILE.patch" "$TARGET_ROOT" <<'PYTHON'
from pathlib import Path
import shlex
import subprocess
import sys

patch_path = Path(sys.argv[1])
root = Path(sys.argv[2]).resolve()
patch_bytes = patch_path.read_bytes()
blocks = []
for raw_block in patch_bytes.split(b"diff --git ")[1:]:
    lines = raw_block.splitlines()
    header = shlex.split("diff --git " + lines[0].decode("utf-8"))
    old_path, new_path = header[2][2:], header[3][2:]
    if b"new file mode " in b"\n".join(lines):
        blocks.append((old_path, new_path, "added"))
    elif b"deleted file mode " in b"\n".join(lines):
        blocks.append((old_path, new_path, "deleted"))
    else:
        blocks.append((old_path, new_path, "modified"))
result = subprocess.run(
    ["git", "apply", "--reverse", "--binary", "--unsafe-paths", "-p1"],
    input=patch_bytes,
    cwd=root,
    capture_output=True,
)
if result.returncode:
    sys.stderr.buffer.write(result.stdout + result.stderr)
    raise SystemExit(result.returncode)
removed_added = 0
for old_path, new_path, kind in blocks:
    if kind != "added":
        continue
    path = (root / new_path).resolve()
    if root not in path.parents:
        raise SystemExit(f"rollback path escapes target: {new_path}")
    if path.is_file() or path.is_symlink():
        path.unlink()
        removed_added += 1
artifact_paths = (
    "docs/exec-glue/DIFF_FILE.patch",
    "docs/exec-glue/VERIFICATION.txt",
    "docs/exec-glue/ROLLBACK.sh",
)
removed_artifacts = 0
for rel in artifact_paths:
    path = root / rel
    if path.is_file():
        path.unlink()
        removed_artifacts += 1
try:
    (root / "docs/exec-glue").rmdir()
except OSError:
    pass
print(
    "ROLLBACK_APPLIED: git apply --reverse --binary; "
    f"patch_blocks={len(blocks)}; added_files_removed={removed_added}; "
    f"artifacts_removed={removed_artifacts}"
)
PYTHON
