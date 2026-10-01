#!/system/bin/sh
#
# Runs ON a rooted Android device. Prints kernel_phys_load and
# kernel_phys_offset. No adb, no manager app.
#
#   adb push tools/mtk-phys/mtk-phys.sh /data/local/tmp/
#   adb shell su -c 'sh /data/local/tmp/mtk-phys.sh'
#
# The kernel must NOT be patched with KernelSU (module or built-in); the script
# refuses to run when it detects one, because that interferes with the exploit.
set -eu

uid=$(id -u)
[ "$uid" = "0" ] || { echo "error: must run as root (id -u=$uid)" >&2; exit 1; }
#
#if grep -qi kernelsu /proc/modules 2>/dev/null; then
#    echo "error: KernelSU module loaded; use a kernel not patched with KernelSU" >&2
#    exit 1
#fi
#if grep -qi kernelsu /proc/kallsyms 2>/dev/null; then
#    echo "error: kernel appears KernelSU-patched; use an unpatched kernel" >&2
#    exit 1
#fi

old=$(cat /proc/sys/kernel/kptr_restrict 2>/dev/null || echo 0)
trap 'echo "$old" > /proc/sys/kernel/kptr_restrict 2>/dev/null' EXIT
echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

release=$(uname -r)
text=$(awk '$3=="_text"{print $1; exit}' /proc/kallsyms)
stext=$(awk '$3=="_stext"{print $1; exit}' /proc/kallsyms)
[ -n "$text" ] && [ -n "$stext" ] || {
    echo "error: _text/_stext not found (is kptr_restrict hiding addresses?)" >&2
    exit 1
}
kc=$(awk '/Kernel code/{split($1,a,"-"); print a[1]; exit}' /proc/iomem)
[ -n "$kc" ] || { echo "error: no 'Kernel code' range in /proc/iomem" >&2; exit 1; }

ESC=$(printf '\033')

# All parsing and formatting happens in one awk call, which converts hex to
# decimal itself (no strtonum, no shell arithmetic). Only the low 32 bits of the
# 64-bit virtual _stext/_text are needed: their difference is far below 2^32 and
# cancels the KASLR slide; physical addresses are 32-bit.
#
# kernel_phys_offset (the DRAM base / linear-map PHYS_OFFSET) is the first
# "System RAM" start, extended down through any contiguous "reserved" regions:
# some devices reserve a carveout at the very bottom of DRAM, so the first
# *available* System RAM starts above the real base.
awk -v esc="$ESC" -v release="$release" -v text="$text" -v stext="$stext" -v kc="$kc" '
function h2d(s,   i, c, v, hex) {
    hex = "0123456789abcdef"; v = 0; s = tolower(s)
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        v = v * 16 + index(hex, c) - 1
    }
    return v
}
$1 ~ /^[0-9a-f]+-[0-9a-f]+$/ {
    split($1, r, "-"); n++
    S[n] = h2d(r[1]); E[n] = h2d(r[2])
    lab = $0; sub(/^[^:]*:[ \t]*/, "", lab); L[n] = lab
}
END {
    tl = h2d(substr(text,  length(text)  - 7))
    sl = h2d(substr(stext, length(stext) - 7))
    delta = sl - tl
    phys_load = h2d(kc) - delta

    base = -1
    for (i = 1; i <= n; i++) if (L[i] ~ /System RAM/ && (base < 0 || S[i] < base)) base = S[i]

    # Lower the base to a DRAM base carveout when the first System RAM starts
    # above it: some devices (Qualcomm) mark the bottom of DRAM reserved, and a
    # small hole can follow, so accept a 64 MiB-aligned "reserved" region below
    # the first System RAM within 64 MiB.
    # 1073741824 = 0x40000000 (1 GiB), 67108864 = 0x4000000 (64 MiB). Hex
    # literals are avoided because some awk builds (BWK/toybox) parse 0x... as 0.
    for (i = 1; i <= n; i++) {
        if (L[i] ~ /reserved/ && S[i] >= 1073741824 && S[i] < base &&
            base - S[i] <= 67108864 && S[i] % 67108864 == 0)
            base = S[i]
    }
    phys_offset = base

    if (base < 0) {
        printf "error: no System RAM range in /proc/iomem\n" > "/dev/stderr"; exit 1
    }
    if (phys_load <= 0 || phys_offset <= 0) {
        printf "error: computed a zero address; /proc/iomem or /proc/kallsyms gave\n" > "/dev/stderr"
        printf "       no real addresses (check root and kptr_restrict=0).\n" > "/dev/stderr"
        exit 1
    }

    red = esc "[1;31m"; yel = esc "[1;33m"; grn = esc "[1;32m"; off = esc "[0m"
    printf "\n"
    printf "%s%s%s\n", red, "=========================================================", off
    printf "%s%s%s\n", red, "  GhostLock -- kernel physical addresses (MediaTek)", off
    printf "%s%s%s\n", red, "=========================================================", off
    printf "release               = %s\n", release
    printf "delta (_stext - _text)= %d (0x%x)\n", delta, delta
    printf "\n"
    printf "%s  =====paste these two lines into the app=====%s\n", grn, off
    printf "%s  kernel_phys_load   = %d (0x%x)%s\n", yel, phys_load, phys_load, off
    printf "%s  kernel_phys_offset = %d (0x%x)%s\n", yel, phys_offset, phys_offset, off
    printf "\n"
}' /proc/iomem
