# Running GhostLock on a MediaTek device

> 中文版: [MEDIATEK_ZH.md](MEDIATEK_ZH.md)

This guide is for MediaTek users who extract the offsets by themselves but have not yet obtained a working run. If
the device is listed with an exact `uname -r` match and runs, this guide does
not apply.

MediaTek kernels require two physical addresses that GhostLock cannot infer on
its own: `kernel_phys_load` and `kernel_phys_offset`. When they are missing or
incorrect, the attack fails during W1: it is retried to its limit and then
fails.

```
[*] W1 attempt 1/15
...
[*] W1 attempt 15/15
[-] Write 1 failed
```

## Before you start

`/proc/iomem` and `/proc/kallsyms` are readable only by root, so the device must
give you a root shell (`adb root` on a userdebug/eng build, or an equivalent
root). No manager app is needed.

Do **not** use KernelSU, ReSukiSU, or KowSU, including the form that patches the
kernel: a KernelSU-patched kernel interferes with the exploit. The script checks
root and KernelSU for you and stops with a message if either is not satisfied.

## Step 1 — get the two values with the script

`tools/mtk-phys/` reads the device and prints both values in coloured text.
Pick the section for your setup.

### Android (on the phone)

If `mtk-phys.sh` is already on the phone, run it from a root shell (a terminal
app with `su`):

```sh
su -c 'sh /data/local/tmp/mtk-phys.sh'
```

If it is not on the phone, use the entry for your computer below instead.

### Windows (on your PC)

`adb` must be on PATH and the phone connected.

```bat
tools\mtk-phys\extract_phys.bat
```

### macOS / Linux (on your PC)

`adb` must be on PATH and the phone connected.

```sh
tools/mtk-phys/extract_phys.sh
```

If more than one device is connected, select one first: `set ANDROID_SERIAL=<serial>`
on Windows, or prefix the command with `ANDROID_SERIAL=<serial>` on macOS/Linux.

## Step 2 — copy the script output into the app

When the script finishes, it prints the two values:

```
kernel_phys_load = <value>
kernel_phys_offset = <value>
```

Open the app's **Advanced parameter overrides** page and set the two fields to
those values. Saving applies them to the next run; no rebuild is required.

## Step 3 — run and verify

Run the exploit once, then read the first lines of the debug log
(`Download/ghostlock-debug-log/<time>/*.log.txt`):

```
[*] soc: mtk; kernel_phys_load=<value>
[+] p0 profile ... phys_offset=<value> kernel_phys_load=<value>
```

If W1 keeps failing, one of the values is still incorrect; recheck the script
output.

## Attach vendor_boot.img (no root)

Some MediaTek devices only need boot.img and work right away. On others, the
attack still fails during W1 because `kernel_phys_load` / `kernel_phys_offset`
are missing. GhostLock can recover them from the stock **vendor_boot.img**, with
no root and no /proc access.

1. Open GhostLock → **Advanced options** → **Parameter configuration** →
   **Load configuration** → **Parse local image.**
2. Select **boot.img**.
3. Tap **Attach vendor_boot.img (MediaTek only)** and
   pick the stock vendor_boot.img.

The Attach vendor_boot.img entry is shown only on MediaTek devices.
 
vendor_boot.img always yields kernel_phys_load. kernel_phys_offset is
filled in only when the image's alignment pattern proves it; otherwise it stays empty and you should fall back to the
mtk-phys route above to read it from /proc/iomem.

## Adding the device as a built-in (maintainers)

To ship a built-in profile for this kernel, follow [README.md](README.md) and
run the extractor with `--phys` (prefer the image's embedded kallsyms; do not
pass a device `/proc/kallsyms`). The extractor does not emit
`kernel_phys_offset`; fill it by hand.
