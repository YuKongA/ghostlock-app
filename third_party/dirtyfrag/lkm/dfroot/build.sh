#!/bin/bash

set -euo pipefail

ENGINE="${ENGINE:-podman}"

if [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
    LLVM_OBJCOPY="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy"
else
    LLVM_OBJCOPY="llvm-objcopy"
fi

cd "$(dirname "$0")"

for version in \
    android12-5.10 \
    android13-5.10 \
    android13-5.15 \
    android14-5.15 \
    android15-6.6  \
    android16-6.12 \
    android17-6.18 \
; do
    "$ENGINE" run --rm --pid=host --network=none \
        -v "$(pwd)":/src -w /src \
        ghcr.io/ylarod/ddk-min:"$version" \
        make "$@"
    cp dirtyfrag.ko dirtyfrag-"$version".ko
    # Size diet: this ko is written through the exploit page by page.
    # --strip-unneeded keeps only load-relevant symbols (undefined imports etc.);
    # the -R removals drop loader-ignored sections. Removing the empty .hyp.*
    # sections also repacks the file, reclaiming ~2.2 KiB of alignment padding.
    # (13.4 KiB -> ~7.8 KiB, i.e. 4 pages -> 2 pages.)
    "$LLVM_OBJCOPY" --strip-unneeded \
      -R .comment -R .note.gnu.build-id -R .note.gnu.property -R .note.Linux -R .note.GNU-stack \
      -R .BTF -R .BTF.base -R .llvm_addrsig \
      -R .hyp.text -R .hyp.bss -R .hyp.rodata -R .hyp.event_ids \
      -R .hyp.patchable_function_entries -R .hyp.data \
      dirtyfrag-"$version".ko

    mv dirtyfrag-"$version".ko ../app/src/main/jni/ko/dirtyfrag-"$version".ko
    echo "==============================="
    echo "Compiled $version LKM"
    echo "==============================="
    echo ""
done
rm dirtyfrag.ko
