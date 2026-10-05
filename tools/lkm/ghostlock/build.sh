#!/bin/bash
# Build the GhostLock minimal kernel-side payload against an Android GKI KMI.
#
# The device kernel is a GKI 5.15 build (see docs/analysis/minimal-lkm-plan.md):
# symbol CRCs are stable inside one KMI, so a module built for any patch release
# of android13-5.15 links correctly; only the vermagic string differs, which the
# 43284 chain optionally rewrites at load time (--cve43284-allow-vermagic-rewrite).
#
# Usage: ENGINE=podman KMI=android13-5.15 ./build.sh
set -euo pipefail

ENGINE="${ENGINE:-podman}"
KMI="${KMI:-android13-5.15}"
IMAGE="${IMAGE:-ghcr.io/ylarod/ddk-min:${KMI}}"

if [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
    LLVM_OBJCOPY="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"
else
    LLVM_OBJCOPY="${LLVM_OBJCOPY:-llvm-objcopy}"
fi

here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"

echo "== building against ${IMAGE} =="
"$ENGINE" run --rm --network=none -v "$here":/src -w /src "$IMAGE" make

# Size diet: every byte is written through the exploit one page at a time.
"$LLVM_OBJCOPY" --strip-unneeded \
    -R .comment -R .note.gnu.build-id -R .note.gnu.property -R .note.Linux \
    -R .note.GNU-stack -R .BTF -R .BTF.base -R .llvm_addrsig \
    -R .hyp.text -R .hyp.bss -R .hyp.rodata -R .hyp.event_ids \
    -R .hyp.patchable_function_entries -R .hyp.data \
    ghostlock.ko

mkdir -p out
mv ghostlock.ko "out/ghostlock-${KMI}.ko"
echo "== built out/ghostlock-${KMI}.ko =="
ls -l "out/ghostlock-${KMI}.ko"
shasum -a 256 "out/ghostlock-${KMI}.ko" 2>/dev/null || sha256sum "out/ghostlock-${KMI}.ko"
