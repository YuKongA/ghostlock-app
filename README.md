# GhostLock-App

> 中文: [README_ZH.md](README_ZH.md)

## Documentation

- [Kernel Profile Porting Guide](docs/kernel_profiles/README.md) - add support for a new kernel. GhostLock matches kernels by exact `uname -r` and rejects unsupported builds, showing the status at the top. Built-in profiles live in `app/src/main/assets/kernel_profiles/`: one HOCON file per release, `index.conf` as the runtime index, and `<major.minor>-template.conf` version-family templates.
- [Supported devices](docs/kernel_profiles/SUPPORTED_DEVICES.md) - the built-in kernel list.
- [Shared execution defaults](docs/kernel_profiles/defaults.md) - every execution-tuning field, its default, and why.
- [Profile schema](docs/kernel_profiles/PROFILE_SCHEMA.md) - full profile structure and data flow.
- [Adding a component](docs/development/adding-a-component.md) - developer guide for a new native middleware / backend / frontend (Chinese).

For the complete device-porting workflow, kernel-family template links, and tuning rationale, see the [Kernel Profile Porting Guide](docs/kernel_profiles/README.md).

Rows explicitly marked **Shizuku required** run through a shell UserService. Start Shizuku with ADB and tap the status card to grant access; all other rows use the app's normal execution path.

## Quick Start

Open **GhostLock** and tap **Run**. KernelSU (`me.weishu.kernelsu`), ReSukiSU (`com.resukisu.resukisu`), or KowSU (`com.kowx712.supermanager`) provides `ksud` for module loading; without it, W1/W2 still grant uid 0 but no module is loaded.

The execution chain is a pipeline of three components: a frontend (`root_child` startup/handoff), a backend (the CVE-2026-43499 futex primitive), and a middleware route. The catalogued combinations are instantiated at build time; the resolved profile selects which one runs. The route races two cores: on the 6.6/6.12 tree-waiter kernels the main thread hammers `select` while a consumer thread perturbs the waiter's priority; on the 6.1 compact-waiter kernels it drives `getsockopt(TCP_ZEROCOPY_RECEIVE)` through a punched-hole page; the 5.15 kernels use the multicast waiter. The CPU pair also comes from the resolved profile; the legacy no-argument entry still honors `GHOSTLOCK_TCP_ROUTE=0` (force pselect) and `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE`.

## Command-Line Debugging

adb/shell has no seccomp filter, so W3 is skipped - handy for quick verification:

```powershell
make -C src ghostlock
adb push build/native/ghostlock /data/local/tmp/ghostlock
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock
```

## Offset Extraction

`tools/extract_rs` derives offsets from a `boot.img` (plus optional `xbl_config.img`), a full OTA ZIP, or an `http(s)` URL pointing at one. kallsyms come from `--kallsyms` or are recovered from the image's embedded table. `pselect_waiter_shift` and `off_slide_loggers_0_1` are derived by the built-in arm64 disassembler. MediaTek images have no `xbl_config.img` and usually no BTF: the physical load address is derived from kallsyms `_text` (override with `--phys`).

```powershell
cargo build --release --manifest-path tools/extract_rs/Cargo.toml
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format json --out offsets.json
build/extract/release/ghostlock-extract.exe OTA.zip --format json --out offsets.json
```

Use `--format json` for extractor output. To add a built-in profile, complete and validate the matching version-family template, save it as a standalone `.conf` profile, and add it to `kernel_profiles/index.conf`. The old C `offsets.h` registry is deprecated and removed.

### Preflight

The extractor disassembles `remove_waiter()` before extracting offsets. Kernels with the fix are rejected with exit code `6`; only vulnerable kernels continue.

### On-device analysis

A full OTA can be analyzed entirely on the phone: `boot` plus `xbl_config`
are extracted automatically. Pass `--work-dir` an app-writable dir when
running inside the app sandbox. Cross-compile and push:

```powershell
rustup target add aarch64-linux-android
$ndk = "$env:ANDROID_HOME\ndk\<version>\toolchains\llvm\prebuilt\windows-x86_64\bin"
$env:CC_aarch64_linux_android = "$ndk\aarch64-linux-android35-clang.cmd"
$env:AR_aarch64_linux_android = "$ndk\llvm-ar.exe"
$env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = $env:CC_aarch64_linux_android
cargo build --release --target aarch64-linux-android --manifest-path tools/extract_rs/Cargo.toml
adb push build/extract/aarch64-linux-android/release/ghostlock-extract /data/local/tmp/
adb shell /data/local/tmp/ghostlock-extract /sdcard/OTA.zip
```

### Importing offsets without rebuilding the app

New kernels no longer need an app rebuild: tap **Import offsets.json** and
pick the extractor's JSON (single object or array), or push it to
`<GHOSTLOCK_HOME>/offsets.json` (default `/data/local/tmp`). At startup native
matches the current `uname -r` against imported entries before rejecting the
kernel. Imports merge across files; a release already stored prompts before
overwrite.

The app can also generate the JSON itself — **Parse OTA link** (full OTA ZIP
URL) and **Parse image** (`boot.img` + optional `xbl_config.img`) run the
extractor in-process and write `offsets.json` into the app data dir on
success.

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

## Credits & License

Based on the following projects, licensed under Apache License 2.0 (see [LICENSE](LICENSE)):

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
