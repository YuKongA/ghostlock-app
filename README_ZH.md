# GhostLock-App

大规模重构中，详见 [静态分析与计划书](analysis/README.md)

> English: [README.md](README.md)

参见[支持设备列表](docs/kernel_profiles/SUPPORTED_DEVICES_ZH.md)。


按精确 `uname -r` 匹配偏移表，未匹配的内核直接拒绝运行，App 顶部显示支持状态。内置配置位于 `app/src/main/assets/kernel_profiles/`：每个 release 一个 JSON，`index.json` 保存运行索引，`templates/` 提供各内核大版本模板。

新增设备的完整流程、内核版本模板跳转和公共参数理由见[Kernel Profile 适配指南](docs/kernel_profiles/README_ZH.md)。

明确标记为**需要 Shizuku**的固件通过 shell UserService 执行。先使用 ADB 启动 Shizuku，再点击顶部支持状态区域授权；其余固件沿用应用内执行路径。

## 快速开始

打开 **GhostLock** 点击 **执行**。需先装 KernelSU（`me.weishu.kernelsu`）、ReSukiSU（`com.resukisu.resukisu`）或 KowSU（`com.kowx712.supermanager`）以提供 `ksud`；缺 `ksud` 时 W1/W2 仍可拿到 uid 0，但不会加载模块。

路线是双核竞争。6.6/6.12 树形 waiter 内核上主线程跑 `select` 爆破、consumer 线程扰动 waiter 优先级；6.1 紧凑 waiter 内核上主线程改走 `getsockopt(TCP_ZEROCOPY_RECEIVE)` 打洞页写入（`GHOSTLOCK_TCP_ROUTE=0` 强制回退 pselect 路线）。核心对默认取大核（不可用时回退 0/1），可用 `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE` 覆盖。

## 命令行调试

adb/shell 环境无 seccomp 过滤，会跳过 W3，适合快速验证：

```powershell
make ghostlock
adb push ghostlock /data/local/tmp/ghostlock
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock
```

## 偏移量提取

`tools/extract_rs` 从 `boot.img`（可加 `xbl_config.img`）、完整 OTA zip 或指向它的 `http(s)` 链接解析偏移量。kallsyms 传 `--kallsyms`，或省略以直接恢复镜像内嵌表。`pselect_waiter_shift` 与 `off_slide_loggers_0_1` 由内置 arm64 反汇编器推导。联发科镜像没有 `xbl_config.img` 且通常无内嵌 BTF：物理加载地址由 kallsyms `_text` 推导（可用 `--phys` 覆盖）。

```powershell
cargo build --release --manifest-path tools/extract_rs/Cargo.toml
tools/extract_rs/target/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format json --out offsets.json
tools/extract_rs/target/release/ghostlock-extract.exe OTA.zip --format json --out offsets.json
```

提取结果使用 `--format json` 输出；新增内置配置时以对应大版本模板为基础补齐和验证字段，再将独立 JSON 登记到 `kernel_profiles/index.json`。旧 C `offsets.h` 注册表已经弃用并移除。

### 前置检查

提取器在提取偏移量前先反汇编 `remove_waiter()`。已包含修复的内核以退出码 `6` 拒绝；仅未修复内核继续。

### 手机端运行

完整 OTA 可直接在手机上分析：传完整包时自动提取 `boot` + `xbl_config`。在 App 沙箱内运行时，`--work-dir` 必须指向 App 可写目录。交叉编译后 push：

```powershell
rustup target add aarch64-linux-android
$ndk = "$env:ANDROID_HOME\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin"
$env:CC_aarch64_linux_android = "$ndk\aarch64-linux-android35-clang.cmd"
$env:AR_aarch64_linux_android = "$ndk\llvm-ar.exe"
$env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = $env:CC_aarch64_linux_android
cargo build --release --target aarch64-linux-android --manifest-path tools/extract_rs/Cargo.toml
adb push tools/extract_rs/target/aarch64-linux-android/release/ghostlock-extract /data/local/tmp/
adb shell /data/local/tmp/ghostlock-extract /sdcard/OTA.zip
```

### 外部导入偏移，免去重新构建应用

新增内核不再需要重新打包 App：点击 **导入 offsets.json** 选择提取器产出的 JSON（单对象或数组均可），或推送到 `<GHOSTLOCK_HOME>/offsets.json`（默认 `/data/local/tmp`）。启动时 native 会先按当前 `uname -r` 匹配导入条目，匹配成功则顶部状态变为受支持。多次导入会合并；新文件含已存内核时，App 会先询问是否覆盖。

App 也能直接生成这份 JSON：**解析完整包链接**（完整 OTA zip 的 `http(s)` 链接）与 **解析镜像**（`boot.img` + 可选 `xbl_config.img`）都在 App 进程内跑提取器，成功后把 `offsets.json` 写入 App 数据目录。

```json
[
  {
    "release": "6.12.38-android16-5-g844001fb8721-ab14552068-4k",
    "kernel_phys_load": 3347054592,
    "pselect_waiter_shift": 0,
    "symbols": { "off_init_task": 37801728, "off_init_cred": 37891184 },
    "struct_fields": { "task_prio": 148, "task_cred": 2304 }
  }
]
```

## 来源与许可证

基于以下项目改写，继承 Apache License 2.0（见 [LICENSE](LICENSE)）：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
