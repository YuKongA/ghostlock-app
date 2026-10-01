# mtk-phys

> 中文: [README_ZH.md](README_ZH.md)

Derive the two physical addresses a profile needs, on a rooted device:

- `kernel_phys_load` — the `_text` physical load address.
- `kernel_phys_offset` — the DRAM base (linear-map `PHYS_OFFSET`).

The extractor cannot obtain these from the image (see
[MEDIATEK.md](../../docs/kernel_profiles/MEDIATEK.md)); the script reads them
from `/proc/iomem` and `/proc/kallsyms`.

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
- A kernel **not patched with KernelSU** (loaded as a module or built into the
  kernel). A KernelSU-patched kernel interferes with the exploit, so the script
  refuses to run when it detects one.

## Computation

```
kernel_phys_load   = Kernel code start - (_stext - _text)
kernel_phys_offset = System RAM start
```

The KASLR slide cancels in `_stext - _text`.
