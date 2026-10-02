# GhostLock-App

> English: [README.md](README.md)

## 文档

- [Kernel Profile 适配指南](docs/kernel_profiles/README_ZH.md) —— 如何支持一款新内核。GhostLock 按精确 `uname -r` 匹配，未匹配的内核直接拒绝运行并在 App 顶部显示状态。
- [支持设备列表](docs/kernel_profiles/SUPPORTED_DEVICES_ZH.md) —— 内置内核清单。
- [公共执行默认值](docs/kernel_profiles/defaults_ZH.md) —— 每个 `execution` 字段的默认值与取舍。
- [Profile 结构文档](docs/kernel_profiles/PROFILE_SCHEMA_ZH.md) —— profile 的完整结构、字段语义与数据流。
- [新增组件指南](docs/development/adding-a-component.md) —— 为 native 添加新 middleware / backend / frontend 的开发者指南。

新增设备的完整流程、内核版本模板跳转和公共参数理由见[Kernel Profile 适配指南](docs/kernel_profiles/README_ZH.md)。

明确标记为**需要 Shizuku**的固件通过 shell UserService 执行。先使用 ADB 启动 Shizuku，再点击顶部支持状态区域授权；其余固件沿用应用内执行路径。

## 快速开始

打开 **GhostLock** 点击 **执行**。如果系统中有可用的提权运行时，可以使用它完成执行流程；否则应用会回退到直接执行路径。

执行链由三类组件构成：frontend（`root_child` 启动/交接）、backend（CVE-2026-43499 futex 原语）与 middleware 路线。**编目组合在构建期实例化，具体运行哪一路由由活动 profile 决定。**

## 命令行调试

adb/shell 环境无 seccomp 过滤，会跳过 W3，适合快速验证：

```powershell
make -C src ghostlock
./gradlew exportKernelProfiles
adb push build/native/ghostlock /data/local/tmp/ghostlock
adb push build/kernel-profiles/<release>.bin /data/local/tmp/profile.bin
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock --load-prebuilt-profile /data/local/tmp/profile.bin
```

## 偏移量提取

`tools/extract_rs` 从 `boot.img`（可加 `xbl_config.img`）、完整 OTA zip 或指向它的 `http(s)` 链接解析偏移量。kallsyms 传 `--kallsyms`，或省略以直接恢复镜像内嵌符号表。route 由内核证据推荐，也可用 `--route` 覆盖。

```powershell
Push-Location tools/extract_rs
cargo build --release
Pop-Location
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract.exe OTA.zip --format conf --out profile.conf
```

提取结果使用 `--format conf` 输出：flatten（无 `include`、凭据/KernelSnitch 常量内联）的自包含 profile。提取器把镜像实际获得的所有字段都写出，未获得的字段省略，不会用相邻内核族的猜测值补齐。

### 联发科

联发科镜像没有 `xbl_config.img`，通常也没有内嵌 BTF，提取器无法从镜像推导两个物理地址（`kernel_phys_load`、`kernel_phys_offset`），会把它们留成 `null`。运行时按 SoC 公式回退，在联发科上会在 W1 失败。请先在已 root 的设备上运行单独的 `tools/mtk-phys/` 提取器（读取 `/proc/iomem`），再把两个值填入 App 的高级参数覆盖。参见 [MEDIATEK_ZH.md](docs/kernel_profiles/MEDIATEK_ZH.md)。

### 前置检查

提取器在提取偏移量前先反汇编 `remove_waiter()`。已包含修复的内核以退出码 `6` 拒绝；仅未修复内核继续。

### 手机端运行

完整 OTA 可直接在手机上分析：传完整包时自动提取 `boot` + `xbl_config`。在 App 沙箱内运行时，`--work-dir` 必须指向 App 可写目录。交叉编译后 push：

```powershell
rustup target add aarch64-linux-android
$ndk = "$env:ANDROID_HOME\ndk\<version>\toolchains\llvm\prebuilt\windows-x86_64\bin"
$env:CC_aarch64_linux_android = "$ndk\aarch64-linux-android35-clang.cmd"
$env:AR_aarch64_linux_android = "$ndk\llvm-ar.exe"
$env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = $env:CC_aarch64_linux_android
Push-Location tools/extract_rs
cargo build --release --target aarch64-linux-android
Pop-Location
adb push build/extract/aarch64-linux-android/release/ghostlock-extract /data/local/tmp/
adb shell /data/local/tmp/ghostlock-extract /sdcard/OTA.zip
```

### 外部导入偏移，免去重新构建应用

新增内核不再需要重新打包 App：点击 **导入 offsets.conf (HOCON)** 选择提取器产出的扁平 `.conf`，旧 JSON 报告仍可通过 **导入 offsets.json (v1)** 导入。v1 JSON 会在 App 内转成当前布局，因此无需向设备推送二进制：Native 始终从 App 发送到 stdin 的 GLK1 文档开始，并在匹配当前 `uname -r` 后再决定是否拒绝该内核。多个导入文件会合并；若某个 release 已存在则会提示覆盖。

App 也能直接生成这份 profile：**解析完整包链接**（完整 OTA zip 的 `http(s)` 链接）与 **解析镜像**（`boot.img` + 可选 `xbl_config.img`）都在 App 进程内跑提取器，并在成功后把扁平 `.conf` 写到 App 数据目录：

```hocon
# GhostLock kernel profile: 6.12.38-android16-5-g844001fb8721-ab14552068-4k (HOCON, self-contained).
release = "6.12.38-android16-5-g844001fb8721-ab14552068-4k"
schema_version = 1
kernel_major = 6
recommend_shizuku = 0
kernel_phys_load = 0xC7800000
route {
  select_stack {
    waiter_shift = 0
  }
}
fallback {
  to = "none"
}
kernelsnitch {
  collisions = 4
}
task_struct {
  prio = 148
  cred = 2304
}
cred {
  caps_offset = 48
  copy_size = 136
  usage_value = 1
  caps_count = 5
  caps_value = -1
}
offset {
  init_task = 37801728
  init_cred = 37891184
}
```

## 来源与许可证

基于以下项目改写，继承 Apache License 2.0（见 [LICENSE](LICENSE)）：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
