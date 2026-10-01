# 让 GhostLock 在联发科机型上运行

> English: [MEDIATEK.md](MEDIATEK.md)

本文面向尚自定提取配置但未成功运行 GhostLock 的联发科（MediaTek）用户。若设备已在内置支持列表中按 `uname -r` 精确匹配且运行正常，则无需阅读本文。

联发科内核需要两个 GhostLock 无法自行推断的物理地址：`kernel_phys_load` 与 `kernel_phys_offset`。二者缺失或填写错误时，攻击会在 W1 阶段失败：连续重试至上限后终止。

```
[*] W1 attempt 1/15
...
[*] W1 attempt 15/15
[-] Write 1 failed
```

## 开始之前

`/proc/iomem` 与 `/proc/kallsyms` 仅 root 可读，因此设备需能提供 root shell（userdebug/eng 构建下的 `adb root`，或等效的 root），无需任何管理器应用。

**请勿**使用 KernelSU、ReSukiSU 或 KowSU，**尤其不要使用通过修补内核启用 KernelSU 的方式**：被 KernelSU 修补过的内核会干扰攻击。脚本会自动检查 root 与 KernelSU，不满足时给出提示并停止。

## 第 1 步 —— 用脚本取得两个取值

`tools/mtk-phys/` 会读取设备，并以醒目颜色打印两个取值。请按你的环境选择下列方式。

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

若连接了多台设备，先指定其一：Windows 上 `set ANDROID_SERIAL=<serial>`，macOS/Linux 上在命令前加 `ANDROID_SERIAL=<serial>`。

## 第 2 步 —— 把脚本输出抄进 App

脚本执行完成后，会打印两个取值：

```
kernel_phys_load = <值>
kernel_phys_offset = <值>
```

打开 App 的 **「高级参数覆盖」** 页，把这两个字段填成上述取值。保存后即对下次运行生效，无需重新构建。

## 第 3 步 —— 运行并验证

执行一次攻击，随后查看调试日志的开头若干行
（`Download/ghostlock-debug-log/<时间>/*.log.txt`）：

```
[*] soc: mtk; kernel_phys_load=<值>
[+] p0 profile ... phys_offset=<值> kernel_phys_load=<值>
```

若 W1 仍持续失败，说明取值仍然不正确，请复核脚本输出。

## 收录为内置 profile（维护者）

如需将该内核收录为内置，维护者按 [README_ZH.md](README_ZH.md) 操作，并以 `--phys` 运行提取器（优先使用镜像内嵌 kallsyms，不要传入设备的 `/proc/kallsyms`）。提取器暂不输出 `kernel_phys_offset`，需手工填写。
