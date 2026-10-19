# mtk-phys

> 中文: [README_ZH.md](README_ZH.md)

Derive the two physical addresses a profile needs, on a rooted device:

- `kernel_phys_load`: the `_text` physical load address.
- `kernel_phys_offset`: the DRAM base (linear-map `PHYS_OFFSET`).

Use this rooted-device fallback when the main extractor's `--vendor-boot` or
`--iomem` inputs do not supply the needed values. See the
[MediaTek guide](../../docs/kernel_profiles/MEDIATEK.md) for those sources.
This script reads `/proc/iomem` and `/proc/kallsyms`, so the values describe
the device and kernel it runs on.

## Files

| File | Runs on | Purpose |
|---|---|---|
| `mtk-phys.sh` | the device (root shell) | the logic; prints the two addresses |
| `extract_phys.sh` | your PC (POSIX shell + adb) | pushes `mtk-phys.sh` and runs it as root |
| `extract_phys.bat` | your PC (Windows cmd + adb) | same, for cmd.exe |

## Usage

On the device (root shell), for example from a terminal app or after pushing:

```sh
adb push tools/mtk-phys/mtk-phys.sh /data/local/tmp/
adb shell su -c 'sh /data/local/tmp/mtk-phys.sh'
```

On your PC over adb:

```sh
tools/mtk-phys/extract_phys.sh      # POSIX shell
tools\mtk-phys\extract_phys.bat     # Windows
```

If more than one device is connected, select one first with `ANDROID_SERIAL`.

All forms print the values in coloured text plus the two lines to paste into the
app's advanced override page.

## Requirements

- A root shell: `adb root` on a userdebug/eng build, or an equivalent root. No
  manager app is needed.
- A kernel not patched with KernelSU (loaded as a module or built into the
  kernel). A KernelSU-patched kernel interferes with the exploit. The script
  checks root and stops otherwise, but its KernelSU check is disabled
  (`mtk-phys.sh:15-23`), so confirm this yourself.
- To read the addresses the script sets `kernel.kptr_restrict` to 0 for the run
  and restores the previous value on exit.

## Computation

```
kernel_phys_load   = Kernel code start - (_stext - _text)
kernel_phys_offset = System RAM start
```

The KASLR slide cancels in `_stext - _text`. Only the low 32 bits of the
virtual `_stext`/`_text` are compared, and physical addresses are 32-bit.

`kernel_phys_offset` starts at the lowest `System RAM` range. It is lowered to
a `reserved` range below it when that range starts at or above 1 GiB, is
64 MiB aligned, and lies within 64 MiB of the first `System RAM` range. Some
devices reserve a carveout at the bottom of DRAM, so the first available
`System RAM` starts above the real base. Check the printed value against
`/proc/iomem` if your device uses a different layout.
