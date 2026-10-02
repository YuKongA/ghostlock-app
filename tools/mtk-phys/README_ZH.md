# mtk-phys

> English: [README.md](README.md)

在已 root 的设备上取得 profile 所需的两个物理地址：

- `kernel_phys_load` —— `_text` 的物理加载地址。
- `kernel_phys_offset` —— DRAM 基址（linear-map `PHYS_OFFSET`）。

提取器无法从镜像取得这两个值（见
[MEDIATEK_ZH.md](../../docs/kernel_profiles/MEDIATEK_ZH.md)）；本脚本改从
`/proc/iomem` 与 `/proc/kallsyms` 读取。

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

在你的电脑 上经 adb 运行：

```sh
tools/mtk-phys/extract_phys.sh      # POSIX shell
tools\mtk-phys\extract_phys.bat     # Windows
```

若连接了多台设备，请先用 `ANDROID_SERIAL` 指定其一。

三种方式都会以醒目颜色打印两个取值，并给出可直接粘贴到高级参数覆盖页的取值。

## 前置要求

- root shell：userdebug/eng 构建下的 `adb root`，或等效的 root。无需任何管理器应用。
- **内核未被 KernelSU 修补**（无论以模块加载还是编入/修补进内核）。被 KernelSU
  修补过的内核会干扰攻击，脚本检测到即拒绝运行。

## 计算方式

```
kernel_phys_load   = Kernel code 起始 - (_stext - _text)
kernel_phys_offset = System RAM 起始
```

KASLR slide 在 `_stext - _text` 中相互抵消。
