# GhostLock-App

> English: [README.md](README.md)

## 支持的设备

| Kernel                                                 | Devices                                                          |
| ------------------------------------------------------ |------------------------------------------------------------------|
| `6.1.118-android14-11-gca0ef6d17716-ab13624819`        | Xiaomi 14                                                        |
| `6.1.138-android14-11-g0c3d559bcd85-ab14529422`        | Xiaomi 14                                                        |
| `6.1.145-android14-11-g09f1c0074ad7-ab14226177`        | Infinix Note 50s 5G                                              |
| `6.1.162-android14-11-gce140c0e5bf5-ab15450923`        | Zenfone 11 Ultra                                                 |
| `6.6.30-android15-8-g54dcbfbef792-ab12368803-4k`       | Red Magic Tablet 3 Pro                                           |
| `6.6.77-android15-8-g4a507830d890-ab13636293-4k`       | Xiaomi Civi 5 Pro, REDMI K90 / 4 Turbo, POCO F7                  |
| `6.6.77-android15-8-g63ce7556864c-ab13994517-4k`       | Xiaomi 15                                                        |
| `6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k`  | Xiaomi 15 Pro, REDMI K80 Pro / K80 Ultra                         |
| `6.6.89-android15-8-g096cdb6ecefc-ab14358676-4k`       | OPPO Pad 4 Pro                                                   |
| `6.6.89-android15-8-g0889fe95bb10-ab14402178-4k`       | POCO X8 Pro Max                                                  |
| `6.6.89-android15-8-gf4dc45704e54-abogki446052083-4k`  | OnePlus 13                                                       |
| `6.6.92-android15-8-g3637f4904cf5-ab13944661-4k`       | Red Magic Tablet 3 Pro, Red Magic 10 Pro, Red Magic 11 Air       |
| `6.6.102-android15-8-gab8eb70a71b8-ab14350911-4k`      | Nothing Phone 3                                                  |
| `6.6.102-android15-8-gb01b41c2647c-ab15574720-4k`      | Xiaomi 17T                                                       |
| `6.6.102-android15-8-gfe76d1bc97fd-ab14689815-4k`      | Xiaomi 17T                                                       |
| `6.6.118-android15-8-g2e6b9c3812c5-ab15114928-4k`      | OPPO Find N5                                                     |
| `6.6.118-android15-8-g93e223c276e7-abogki500782043-4k` | OPPO Find X8 Ultra, OnePlus 13 / ACE 5 Pro                       |
| `6.6.118-android15-8-g608a629fedf7-ab15154340-4k`      | REDMI K90 Ultra                                                  |
| `6.6.118-android15-8-gc44b714366cc-abogki519650608-4k` | REDMI K80 Pro / Turbo 5 Max, POCO X8 Pro Max, Xiaomi Pad 7 Ultra |
| `6.6.118-android15-8-ge56cf6b09cca-ab15511674-4k`      | REDMI K90 Ultra, POCO F7                                         |
| `6.6.118-android15-8-ge58033dc8ea6-abogki498046332-4k` | OPPO Pad 5, OnePlus Pad 2                                        |
| `6.6.118-android15-8-gebdfad32d749-ab15099304-4k`      | OPPO Find X8 / Find X8 Pro                                       |
| `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` | REDMI K90 Pro Max                                                |
| `6.12.23-android16-5-g75e9b1c7ae7c-abogki463945075-4k` | Xiaomi 17 / 17 Pro / 17 Pro Max / 17 Ultra                       |
| `6.12.23-android16-5-g82efd98459a2-ab14457512-4k`      | OPPO Find X9 / Find X9 Pro                                       |
| `6.12.23-android16-5-ga8f88ad96df3-ab13929693-4k`      | OnePlus 15                                                       |
| `6.12.23-android16-5-gb2a876903b49-ab14541642-4k`      | OnePlus 15                                                       |
| `6.12.23-android16-5-gf1bdb13583da-ab13761046-4k`      | Red Magic 11 Pro, Tablet 5 Pro                                   |
| `6.12.30-android16-5-g6e872b4863d6-ab13847919-4k`      | REDMI Note 15 4G, POCO M6 Pro 4G                                 |
| `6.12.38-android16-5-g844001fb8721-ab14552068-4k`      | OnePlus 15T                                                      |

按精确 `uname -r` 匹配偏移表，未匹配的内核直接拒绝运行，App 顶部显示支持状态。偏移表在 `src/kernels/<uname-release>/offsets.h`，新内核构建用提取器的 `--register` 添加。

## 快速开始

打开 **GhostLock** 点击 **执行**。需先装 KernelSU（`me.weishu.kernelsu`）或 ReSukiSU（`com.resukisu.resukisu`）以提供 `ksud`；缺 `ksud` 时 W1/W2 仍可拿到 uid 0，但不会加载模块。

路线是双核竞争：主线程跑 pselect 爆破，consumer 线程扰动 waiter 优先级。核心对默认取大核（不可用时回退 0/1），可用 `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE` 覆盖。

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
tools/extract_rs/target/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --register
tools/extract_rs/target/release/ghostlock-extract.exe OTA.zip --format json --out offsets.json
```

`--register` 将表写入 `src/kernels/<uname-release>/offsets.h`；`--format c --out offsets.h` 输出独立头文件。

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
