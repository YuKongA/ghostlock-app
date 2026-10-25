# Running GhostLock on a MediaTek device

> 中文版: [MEDIATEK_ZH.md](MEDIATEK_ZH.md)

This guide is for MediaTek users who extract the offsets themselves but have not
yet had a working run. If the device has an exact `uname -r` match in the
built-in list and runs, this guide does not apply.

Profiles need `kernel_phys_load` and `kernel_phys_offset`. In **Parse local image**,
attach the matching `vendor_boot.img` to `boot.img` first. The extractor reads
the load address from the header and derives the DRAM base only when the
alignment proves it. A readable `/proc/iomem` or a `--iomem` dump is another source.
Use the rooted-device script below when those inputs do not supply the values.

Missing or incorrect values can cause W1 to retry to the limit and stop:

```
[*] W1 attempt 1/15
...
[*] W1 attempt 15/15
[-] Write 1 failed
```

## Before using the script

`/proc/iomem` and `/proc/kallsyms` are readable only by root, so the device must
give you a root shell (`adb root` on a userdebug/eng build, or an equivalent
root). No manager app is needed.

Do **not** use a kernel with KernelSU patches or a loaded KernelSU module.
This includes BakaSU/ReSukiSU and KowSU variants. These changes can interfere
with the exploit. The script checks root access only, so confirm the kernel
yourself before running.

## Step 1: get the two values with the script

`tools/mtk-phys/` reads the device and prints both values in coloured text.
Pick the section for your setup.

### Android (on the phone)

If `mtk-phys.sh` is already on the phone, run it from a root shell (a terminal
app with `su`):

```sh
su -c 'sh /data/local/tmp/mtk-phys.sh'
```

If it is not on the phone, use the entry for your computer below.

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

## Step 2: copy the script output into the app

The script prints the two values:

```
kernel_phys_load = <value>
kernel_phys_offset = <value>
```

Open the app's **Advanced parameter overrides** page and set the two fields to
those values. Saving applies them to the next run; no rebuild is required.

## Step 3: run and verify

Run the exploit once, then read the first lines of the debug log
(`Download/ghostlock-debug-log/<time>/*.log.txt`):

```
[*] soc: mtk; kernel_phys_load=<value>
[+] p0 profile ... phys_offset=<value> kernel_phys_load=<value>
```

If W1 keeps failing, recheck both values against the script output.

## Adding the device as a built-in (maintainers)

To ship a built-in profile, follow [README.md](README.md). Prefer the image's
embedded kallsyms. Supply `--vendor-boot vendor_boot.img` or `--iomem iomem.txt`
for the physical data. `--phys` supplies only `kernel_phys_load`.
The extractor emits both address fields, using `null` for unknown values.
Obtain and verify any missing value before testing the profile.
