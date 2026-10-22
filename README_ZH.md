# GhostLock-App

[English](README.md)

GhostLock 利用 CVE-2026-43499 在支持的 Android 内核上获取临时 root，并加载 KernelSU。

## 使用

1. 从 [KernelSU](https://github.com/tiann/KernelSU)、[BakaSU/ReSukiSU](https://github.com/Baka-SU/BakaSU) 或 [KowSU](https://github.com/KOWX712/KernelSU) 下载并安装管理器 APK。
2. 安装 GhostLock APK 并打开。
3. 确认内核受支持后，点击 **执行**。

内核必须精确匹配 `uname -r`。请在[支持设备列表](docs/kernel_profiles/SUPPORTED_DEVICES_ZH.md)中核对内核版本。

如果启用了 Shizuku，请先通过 ADB 启动，再运行 GhostLock。点击 GhostLock 的状态卡授权。

任何自带 `libksud.so` 的 root 管理器都可提供 `ksud` 用于加载模块。内置列表（KernelSU、ReSukiSU、BakaSU、KowSU）会自动检测；其他管理器可在 **设置 → 根管理器** 中手动选择。缺 `ksud` 时 W1/W2 仍可拿到 uid 0，但不会加载模块。

## 添加内核 profile

无需重新打包 App 即可导入 profile：

- **解析完整包链接**：输入完整 OTA ZIP 的链接。
- **解析本地镜像**：选择 `boot.img`，可附加 `xbl_config.img`。
- **导入 offsets.conf（v2）**：选择 HOCON profile。旧 JSON 报告通过 **导入 offsets.json（v1）** 在 App 内转换。

提取结果是尚未验证的候选 profile。App 会在执行前检查缺失或无效字段，生成成功不代表设备已获支持。验证和内置登记流程见[适配指南](docs/kernel_profiles/README_ZH.md)。

联发科的镜像地址提取和已 root 设备补充取值流程见[联发科指南](docs/kernel_profiles/MEDIATEK_ZH.md)。

## 命令行工具

<details>
<summary>导出 profile 并运行原生程序</summary>

需要 Android NDK、`make`、Gradle 和 ADB。通过 adb shell 运行时会跳过 App 的 seccomp 绕过阶段。

```sh
make -C src ghostlock
./gradlew exportKernelProfiles
adb push build/native/ghostlock /data/local/tmp/ghostlock
adb push "build/kernel-profiles/<release>.bin" /data/local/tmp/profile.bin
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock --load-prebuilt-profile /data/local/tmp/profile.bin
```

</details>

<details>
<summary>编译并运行偏移提取器</summary>

需要 Rust。提取器支持 boot 镜像、完整 OTA ZIP 或 HTTP(S) 链接。

```sh
(cd tools/extract_rs && cargo build --release)
build/extract/release/ghostlock-extract boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract OTA.zip --format conf --out profile.conf
```

在 App 中导入 `profile.conf`。没有 `xbl_config.img` 时省略 `--xbl-config`。若 `remove_waiter()` 已修复，提取器会以退出码 `6` 拒绝该内核。

</details>

## 参考

- [Profile 结构](docs/kernel_profiles/PROFILE_SCHEMA_ZH.md)与[执行默认值](docs/kernel_profiles/defaults_ZH.md)
- [原生核心](src/core/README_ZH.md)
- [新增组件](docs/development/adding-a-component_ZH.md)、[工程规范](docs/development/engineering-standards_ZH.md)和[文档规范](docs/development/documentation-standards_ZH.md)

## 来源与许可证

采用 [Apache License 2.0](LICENSE)，基于以下项目：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
