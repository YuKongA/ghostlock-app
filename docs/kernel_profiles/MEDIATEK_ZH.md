# 让 GhostLock 在联发科机型上运行

> English: [MEDIATEK.md](MEDIATEK.md)

本文面向自行提取偏移、但尚未成功运行 GhostLock 的联发科（MediaTek）用户。
若设备已在内置列表中按 `uname -r` 精确匹配且运行正常，则无需阅读本文。

Profile 需要 `kernel_phys_load` 与 `kernel_phys_offset`。先在 **解析本地镜像** 中，
为 `boot.img` 附加匹配的 `vendor_boot.img`。提取器从头部读取加载地址，且仅在
对齐模式能证明 DRAM 基址时才推导该基址。可读的 `/proc/iomem` 或 `--iomem` 转储
也是取值来源。这些输入无法提供取值时，再使用下方已 root 设备上的脚本。

取值缺失或错误可能导致 W1 连续重试至上限后停止：

```
[*] W1 attempt 1/15
...
[*] W1 attempt 15/15
[-] Write 1 failed
```

## 使用脚本前

`/proc/iomem` 与 `/proc/kallsyms` 仅 root 可读，因此设备需能提供 root shell
（userdebug/eng 构建下的 `adb root`，或等效的 root），无需任何管理器应用。

**请勿**使用已被 KernelSU 修补或已加载 KernelSU 内核模块的内核，
包括 BakaSU/ReSukiSU 与 KowSU 变体。这些改动可能干扰攻击。
脚本只检查 root 权限，运行前请自行确认内核状态。

## 第 1 步：用脚本取得两个取值

`tools/mtk-phys/` 会读取设备，并以醒目颜色打印两个取值。请按你的环境选择下列
方式。

### 安卓（在手机上运行）

若 `mtk-phys.sh` 已在手机上，用 root shell（带 `su` 的终端 App）运行：

```sh
su -c 'sh /data/local/tmp/mtk-phys.sh'
```

若脚本不在手机上，请改用下方对应电脑的入口。

### Windows（在电脑上运行）

需 `adb` 在 PATH 中且手机已连接。

```bat
tools\mtk-phys\extract_phys.bat
```

### macOS / Linux（在电脑上运行）

需 `adb` 在 PATH 中且手机已连接。

```sh
tools/mtk-phys/extract_phys.sh
```

若连接了多台设备，先指定其一：Windows 上 `set ANDROID_SERIAL=<serial>`，
macOS/Linux 上在命令前加 `ANDROID_SERIAL=<serial>`。

## 第 2 步：把脚本输出抄进 App

脚本执行完成后，会打印两个取值：

```
kernel_phys_load = <值>
kernel_phys_offset = <值>
```

打开 App 的 **「高级参数覆盖」** 页，把这两个字段填成上述取值。保存后即对下次
运行生效，无需重新构建。

## 第 3 步：运行并验证

执行一次攻击，随后查看调试日志的开头若干行
（`Download/ghostlock-debug-log/<时间>/*.log.txt`）：

```
[*] soc: mtk; kernel_phys_load=<值>
[+] p0 profile ... phys_offset=<值> kernel_phys_load=<值>
```

若 W1 仍持续失败，请将两个取值与脚本输出重新核对。

## 收录为内置 profile（维护者）

如需内置该 profile，请按 [README_ZH.md](README_ZH.md) 操作。优先使用镜像内嵌
kallsyms，并通过 `--vendor-boot vendor_boot.img` 或 `--iomem iomem.txt` 提供物理地址
数据。`--phys` 仅提供 `kernel_phys_load`。提取器会输出两个地址字段，未知值写为
`null`。测试 profile 前，请取得并核实所有缺失值。
