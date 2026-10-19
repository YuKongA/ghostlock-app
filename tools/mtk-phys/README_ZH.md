# mtk-phys

> English: [README.md](README.md)

在已 root 的设备上取得 profile 所需的两个物理地址：

- `kernel_phys_load`：`_text` 的物理加载地址。
- `kernel_phys_offset`：DRAM 基址（linear-map `PHYS_OFFSET`）。

主提取器的 `--vendor-boot` 或 `--iomem` 输入无法提供所需取值时，
再使用本脚本从已 root 的设备取值。这些数据源的用法见
[联发科指南](../../docs/kernel_profiles/MEDIATEK_ZH.md)。本脚本读取
`/proc/iomem` 与 `/proc/kallsyms`，因此取值对应它运行所在的设备与内核。

## 文件

| 文件 | 运行位置 | 作用 |
|---|---|---|
| `mtk-phys.sh` | 设备（root shell） | 核心逻辑，打印两个地址 |
| `extract_phys.sh` | 你的电脑（POSIX shell + adb） | push `mtk-phys.sh` 并以 root 执行 |
| `extract_phys.bat` | 你的电脑（Windows cmd + adb） | 同上，供 cmd.exe |

## 用法

在设备上（root shell，可来自终端 App 或先 push）：

```sh
adb push tools/mtk-phys/mtk-phys.sh /data/local/tmp/
adb shell su -c 'sh /data/local/tmp/mtk-phys.sh'
```

在你的电脑上经 adb 运行：

```sh
tools/mtk-phys/extract_phys.sh      # POSIX shell
tools\mtk-phys\extract_phys.bat     # Windows
```

若连接了多台设备，请先用 `ANDROID_SERIAL` 指定其一。

三种方式都会以醒目颜色打印两个取值，并给出可直接粘贴到高级参数覆盖页的取值。

## 前置要求

- root shell：userdebug/eng 构建下的 `adb root`，或等效的 root。无需任何管理器应用。
- 内核未被 KernelSU 修补（无论以模块加载还是编入内核）。被 KernelSU 修补过的
  内核会干扰攻击。脚本会检查 root，非 root 即停止；但 KernelSU 检查当前已禁用
  （`mtk-phys.sh:15-23`），请自行确认。
- 为读取地址，脚本在运行期间把 `kernel.kptr_restrict` 设为 0，退出时恢复原值。

## 计算方式

```
kernel_phys_load   = Kernel code 起始 - (_stext - _text)
kernel_phys_offset = System RAM 起始
```

KASLR slide 在 `_stext - _text` 中相互抵消。只比较虚拟 `_stext`/`_text` 的低
32 位，物理地址为 32 位。

`kernel_phys_offset` 先取最低的 `System RAM` 范围。若其下方存在 `reserved`
范围，且该范围起始不低于 1 GiB、按 64 MiB 对齐、与第一个 `System RAM` 范围
相距不超过 64 MiB，则下移到该范围。部分设备在 DRAM 底部保留 carveout，导致
第一个可用的 `System RAM` 从真实基址之上开始。若你的设备布局不同，请对照
`/proc/iomem` 手工核对打印值。
