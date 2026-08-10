# GhostLock-App

> English: [README.md](README.md)

## 支持的设备

| Kernel                                                 | Devices                                         |
| ------------------------------------------------------ | ----------------------------------------------- |
| `6.6.77-android15-8-g4a507830d890-ab13636293-4k`       | Xiaomi Civi 5 Pro, Redmi K90 / 4 Turbo, POCO F7 |
| `6.6.77-android15-8-g63ce7556864c-ab13994517-4k`       | Xiaomi 15                                       |
| `6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k`  | Xiaomi 15 Pro                                   |
| `6.6.89-android15-8-g096cdb6ecefc-ab14358676-4k`       | OPPO Pad 4 Pro                                  |
| `6.6.102-android15-8-gfe76d1bc97fd-ab14689815-4k`      | Xiaomi 17T                                      |
| `6.6.118-android15-8-g2e6b9c3812c5-ab15114928-4k`      | OPPO Find N5                                    |
| `6.6.118-android15-8-g93e223c276e7-abogki500782043-4k` | OnePlus 13                                      |
| `6.6.118-android15-8-g608a629fedf7-ab15154340-4k`      | Redmi K90 Ultra                                 |
| `6.6.118-android15-8-ge56cf6b09cca-ab15511674-4k`      | Redmi K90 Ultra                                 |
| `6.6.118-android15-8-ge58033dc8ea6-abogki498046332-4k` | OPPO Pad 5, OnePlus Pad 2                       |
| `6.6.118-android15-8-gebdfad32d749-ab15099304-4k`      | OPPO Find X8 / Find X8 Pro                      |
| `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` | Redmi K90 Pro Max                               |
| `6.12.23-android16-5-g75e9b1c7ae7c-abogki463945075-4k` | Xiaomi 17 / 17 Pro / 17 Pro Max / 17 Ultra      |
| `6.12.23-android16-5-g82efd98459a2-ab14457512-4k`      | OPPO Find X9 / Find X9 Pro                      |
| `6.12.23-android16-5-ga8f88ad96df3-ab13929693-4k`      | OnePlus 15                                      |
| `6.12.23-android16-5-gb2a876903b49-ab14541642-4k`      | OnePlus 15                                      |
| `6.12.23-android16-5-gf1bdb13583da-ab13761046-4k`      | Red Magic 11 Pro                                |
| `6.12.38-android16-5-g844001fb8721-ab14552068-4k`      | OnePlus 15T                                     |

启动时按精确的 `uname -r` 匹配偏移表，未匹配的内核直接拒绝运行，App 顶部显示支持状态。偏移表存放在 `src/kernels/<uname-release>/offsets.h`，同一构建的设备共用一行。为已列出的内核追加设备时，在该行补上设备名即可；新内核构建用提取器的 `--register` 添加。

## 快速开始

打开 **GhostLock** 并点击 **执行**。需先安装 KernelSU（`me.weishu.kernelsu`）或 ReSukiSU（`com.resukisu.resukisu`）以提供 `ksud`；缺少 `ksud` 时 W1/W2 仍可获取 uid 0，但不会加载 KernelSU 模块。

### CPU 核心对

提权路线是双核竞争：主线程钉在 `CORE` 上跑 pselect 爆破，consumer 线程钉在 `CONSUMER_CORE` 上扰动同一 waiter 的优先级（默认 0/1）。App 按最大频率分组列出相邻核心对，选择后通过 `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE` 传给 native（命令行：`GHOSTLOCK_CORE=6 GHOSTLOCK_CONSUMER_CORE=7 ./ghostlock`）。核心不在当前 cpuset 内时回退到 0/1 并告警。

## 命令行调试

adb/shell 环境无 seccomp 过滤，会跳过 W3 阶段，适合快速验证：

```powershell
make ghostlock
adb push ghostlock /data/local/tmp/ghostlock
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock
```

## 偏移量提取

`tools/extract_target.py` 从 `boot.img`（可加 `xbl_config.img`）解析偏移量：

- 需 Python（`pip install -r tools/requirements.txt`）及 kallsyms 来源（`--kallsyms` / `--kallsyms-finder`）。
- 传 `--llvm-objdump` 会自动推导 `pselect_waiter_shift` 与 `off_slide_loggers_0_1`。
- 联发科镜像没有 `xbl_config.img` 且通常没有内嵌 BTF：内核物理加载地址由 kallsyms 的 `_text` 推导（`_text - 0xffffffc000000000`，即 DRAM 基址；仅在 `_text` 缺失时回退 `0x80000000`，可用 `--phys` 覆盖）；符号来自 kallsyms，结构体偏移回退到 `target.h` 默认值。
- `--format c --out offsets.h` 输出独立头文件；`--register` 将表注册到 `src/kernels/<uname-release>/offsets.h`（已注册的内核提示共用，不重复建表）：

```powershell
python tools/extract_target.py `
  boot.img `
  --xbl-config xbl_config.img `
  --register
```

### pselect 路线可行性

`core_sys_select` 只把 3 份 `FDS_BYTES(nfds)` 的 fd_set 拷到内核栈（nfds=320 时为 qword 0..14）。futex waiter 必须落在该区内：lock 字段位于 waiter 起始字 + 11，因此推导出的 shift（waiter 的 qword 偏移）必须 ≤ 3，否则 task/lock 落入内核清零区，路线不可行；布局不可行时脚本直接报错。

同一内核版本在不同 SoC 分支的 PGO/LTO 布局可能不同：小米 15（`6.6.77`，`do_pselect` 未内联）waiter 位于第 12 个 qword，不可行；小米 15 Pro（同 `6.6.77`，中间层内联）waiter 位于 word 0，可用 `pselect_waiter_shift=-2`。

## 来源与许可证

基于以下项目改写，继承 Apache License 2.0（见 [LICENSE](LICENSE)）：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
