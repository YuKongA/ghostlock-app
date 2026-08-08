# GhostLock-App

> English: [README.md](README.md)

## 支持的设备

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

启动时按 `uname -r` 匹配偏移表，未匹配的内核会直接拒绝运行，App 顶部显示支持状态。偏移表按精确的 `uname -r` 组织在 `src/kernels/` 下，同一构建的设备共用一行——例如 Redmi K90 与 Xiaomi Civi 5 Pro，以及 Xiaomi 17 / 17 Pro / 17 Pro Max。新增跑在已列内核上的设备时，在对应行追加设备名即可（提取器的 `--register` 会提示共用该内核，不会重复建表）；全新内核构建则新增一行。

## 快速开始

打开 **GhostLock** 应用，点击 **执行** ，软件会自动完成提权流程，
需先自行安装 KernelSU（`me.weishu.kernelsu`）软件以使用 `ksud`，
缺少 `ksud` 时 W1/W2 仍可拿到 uid 0，但不会加载 KernelSU 模块。

## 命令行调试

adb/shell 环境无 seccomp 过滤，会跳过 W3 阶段，适合快速验证：

```powershell
make ghostlock
adb push ghostlock /data/local/tmp/ghostlock
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock
```

## 偏移量提取

`tools/extract_target.py` 从 `boot.img` 和 `xbl_config.img` 解析偏移量，需 Python 3、`lz4` 模块（联发科内核为 LZ4 压缩；`pip install -r tools/requirements.txt`）及 kallsyms 来源（`--kallsyms`/`--kallsyms-finder`）；传 `--llvm-objdump` 还会自动推导 `pselect_waiter_shift` 与 `off_slide_loggers_0_1`。联发科镜像没有 `xbl_config.img` 且通常没有内嵌 BTF，LZ4 镜像会自动按 MTK 加载地址 `0x80000000` 处理（可用 `--phys` 覆盖）——符号来自 kallsyms，结构体偏移回退到 `target.h` 默认值。用 `--format c --out offsets.h` 输出独立头文件，或用 `--register` 注册到仓库（存于 `src/kernels/<uname-release>/offsets.h`，目录名即完整 `uname -r`；已注册的内核会提示共用，不会重复建表）：

```powershell
python tools/extract_target.py `
  boot.img `
  --xbl-config xbl_config.img `
  --register
```

### pselect 路线可行性

`core_sys_select` 只把 3 份 `FDS_BYTES(nfds)` 的 fd_set 拷到内核栈（nfds=320 时为 qword 0..14）。futex waiter 必须落在该区内：其 lock 字段位于 waiter 起始字 + 11，因此推导 shift（waiter 的 qword 偏移）必须 ≤ 3，否则 task/lock 会落入内核清零区，路线不可行。布局不可行时脚本会直接报错。

同一内核版本在不同 SoC 分支的 PGO/LTO 布局可能不同：小米 15（`6.6.77`，`do_pselect` 未内联）的 waiter 位于第 12 个 qword，不可行；小米 15 Pro（同 `6.6.77`，中间层被内联）waiter 位于 word 0，可用 `pselect_waiter_shift=-2`。

## 来源与许可证

基于以下项目改写，继承 Apache License 2.0（见 [LICENSE](LICENSE)）：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
