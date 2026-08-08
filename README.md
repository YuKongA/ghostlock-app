# GhostLock-App

> 中文: [README_ZH.md](README_ZH.md)

## Supported Devices

| Kernel                                                 | Devices                                        |
| ------------------------------------------------------ | ---------------------------------------------- |
| `6.6.77-android15-8-g4a507830d890-ab13636293-4k`       | Redmi K90 (SM8750), Xiaomi Civi 5 Pro (SM8735) |
| `6.6.77-android15-8-g63ce7556864c-ab13994517-4k`       | Xiaomi 15 (SM8750)                             |
| `6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k`  | Xiaomi 15 Pro (SM8750)                         |
| `6.6.89-android15-8-g096cdb6ecefc-ab14358676-4k`       | OPPO Pad 4 Pro (SM8750)                        |
| `6.6.118-android15-8-g2e6b9c3812c5-ab15114928-4k`      | OPPO Find N5 (SM8750)                          |
| `6.6.118-android15-8-g608a629fedf7-ab15154340-4k`      | Redmi K90 Ultra (SM8750)                       |
| `6.6.118-android15-8-ge58033dc8ea6-abogki498046332-4k` | OPPO Pad 5 (MT6991Z)                           |
| `6.6.118-android15-8-gebdfad32d749-ab15099304-4k`      | OPPO Find X8 / Find X8 Pro (MT6991)            |
| `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` | Redmi K90 Pro Max (SM8850)                     |
| `6.12.23-android16-5-g75e9b1c7ae7c-abogki463945075-4k` | Xiaomi 17 / 17 Pro / 17 Pro Max (SM8850)       |
| `6.12.23-android16-5-gb2a876903b49-ab14541642-4k`      | OnePlus 15 (SM8850)                            |

At startup the kernel is matched via `uname -r`; unsupported kernels are rejected immediately and the app shows the status at the top. Tables live under `src/kernels/` keyed by the exact `uname -r`, so devices on the same build share one row — Redmi K90 & Xiaomi Civi 5 Pro, and Xiaomi 17 / 17 Pro / 17 Pro Max. To add a device on a listed kernel, append it to that row (the extractor's `--register` reports the kernel as shared instead of duplicating it); a new kernel build gets a new row.

## Quick Start

Open the **GhostLock** app and tap **Run**; the exploit runs automatically.

Install the KernelSU app (`me.weishu.kernelsu`) first so `ksud` is available. Without `ksud`, stages W1/W2 still grant uid 0, but the KernelSU module will not be loaded.

## Command-Line Debugging

The adb/shell environment has no seccomp filter, so the W3 stage is skipped - handy for quick verification:

```powershell
make ghostlock
adb push ghostlock /data/local/tmp/ghostlock
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock
```

## Offset Extraction

`tools/extract_target.py` parses offsets from `boot.img` and `xbl_config.img`. It needs Python 3, the `lz4` module (MediaTek kernels are LZ4-compressed; `pip install -r tools/requirements.txt`), and a kallsyms source (`--kallsyms`/`--kallsyms-finder`); passing `--llvm-objdump` also auto-derives `pselect_waiter_shift` and `off_slide_loggers_0_1`. MediaTek images have no `xbl_config.img` and usually no embedded BTF, the MTK load address (`0x80000000`) is assumed automatically for LZ4 images (override with `--phys`) — symbols come from kallsyms and struct offsets fall back to `target.h` defaults. Dump a standalone header with `--format c --out offsets.h`, or register the table in the repo with `--register` (stored under `src/kernels/<uname-release>/offsets.h`, directory name = exact `uname -r`; an already-registered kernel is reported as shared instead of duplicating the table):

```powershell
python tools/extract_target.py `
  boot.img `
  --xbl-config xbl_config.img `
  --register
```

### pselect route feasibility

`core_sys_select` copies 3 x `FDS_BYTES(nfds)` of fd_set data onto the kernel stack (qwords 0..14 for nfds=320). The futex waiter must land inside that zone: its lock field sits at waiter word + 11, so the derived shift (waiter offset in qwords) must be <= 3, or task/lock fall into the kernel-zeroed tail and the route cannot work. The script errors out when the layout is infeasible.

The same kernel version can differ across SoC branches due to PGO/LTO: Xiaomi 15 (`6.6.77`, non-inlined `do_pselect`) puts the waiter at qword 12 (infeasible), while Xiaomi 15 Pro (same `6.6.77`, inlined middle layer) puts it at word 0 and works with `pselect_waiter_shift=-2`.

## Credits & License

Based on the following projects, licensed under Apache License 2.0 (see [LICENSE](LICENSE)):

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
