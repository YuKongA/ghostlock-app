# GhostLock-App

> 中文: [README_ZH.md](README_ZH.md)

## Documentation

- [Kernel Profile Porting Guide](docs/profile/README.md) - add support for a new kernel. GhostLock matches kernels by exact `uname -r` and rejects unsupported builds, showing the status at the top. Built-in profiles live in `app/src/main/assets/profile/`: one HOCON file per release, `index.conf` as the runtime index, and `<major.minor>-template.conf` version-family templates.
- [Supported devices](docs/profile/SUPPORTED_DEVICES.md) - the built-in kernel list.
- [Shared execution defaults](docs/profile/defaults.md) - every execution-tuning field, its default, and why.
- [Profile schema](docs/profile/PROFILE_SCHEMA.md) - full profile structure and data flow.
- [Adding a component](docs/development/adding-a-component.md) - developer guide for a new native backend / terminal / route (Chinese).

For the complete device-porting workflow, kernel-family template links, and tuning rationale, see the [Kernel Profile Porting Guide](docs/profile/README.md).

Rows explicitly marked **Shizuku required** run through a shell UserService. Start Shizuku with ADB and tap the status card to grant access; all other rows use the app's normal execution path.

## Android application architecture

The current architecture authority is the [backend root-execution plan](docs/plan/backend-root-execution-unification-plan.md), with the canonical Class, Sequence, State, and result-projection diagrams in [full-process-uml.md](docs/development/full-process-uml.md).

The Kotlin application uses a neutral Clean Architecture boundary. Domain models contain no Android, Compose, concrete-backend, or persistence type. Android storage and platform effects live in data adapters; application wiring lives in AppContainer; screens and ViewModels consume immutable state plus action contracts.

BackendRegistry is the immutable same-APK membership authority. A public BackendSpec contains only neutral identity, configuration schema, parameter requirements, execution domain, abort policy, and capability. The internal BackendPlugin adds resource-free readiness and a fresh one-shot Flow factory; it never crosses UI, profile, persistence, or AIDL boundaries.

App-process and Shizuku-shell execution use the same typed RunHost request, event, snapshot, and lifecycle contract. RunInputFreezer captures the selected backend, profile-v3 snapshot, Payload, and Finish once. The host owns staging, workspace, script publication, Flow lifetime, terminal snapshot, and cleanup. Product status comes from the typed six-layer RunReport; raw uid 0, process exit 0, stdout, and diagnostic logs cannot upgrade a run to product success.

Payload and Finish are independent version-3 documents. The common renderer publishes one root script; a concrete backend is responsible only for reaching its root execution point, dispatching that exact artifact, and reporting backend-private cleanup facts. R4 removed the caller-zero legacy SessionPhase. Automatic log export is a product capability, not dead residue: removing the capability together with its old object was the mistake. R4B keeps the obsolete root-level object retired and uses one neutral run-scoped sink, one bounded channel, and one `data/run` MediaStore worker; RunCoordinator commits, formats, and enqueues under the same ordering lock without an opening protocol, second queue, timeout, or completion monitor. Explicit diagnostic-report creation, manual sharing, and automatic export remain diagnostic sinks and never become run-result authorities.

Current membership deliberately contains no selectable production-root backend. CUSTOM is disabled until an approved operation catalog exists; DEBUG-PROBE is available only in debug builds and has diagnostic-only capability. The retained 43284-Glued, 43499-Glued, and 98260 native trees are frozen future carrier candidates, not registered support.

## Quick Start

On the current refactor branch, a release build has no selectable production-root backend. In a debug build, select Debug Probe to inspect the typed App/Shizuku transport and report projection. A completed probe remains diagnostic-only and never means that root, Payload, or Finish succeeded.

A real backend becomes selectable only through its own approved carrier plan and target-device gate. R2-R4B source changes are present, including the simplified automatic-export settings-to-MediaStore chain, but R4B compile, instrumentation, AVD, Shizuku, and device gates remain NOT_RUN by explicit user instruction. Source presence is not runtime proof; do not treat this branch as a supported-device release.

## Command-Line Debugging

The retired core CLI and its load-prebuilt-profile, attack-selection, and JNI bridge flags are no longer current entry points. Production execution is initiated through the Android application and the typed RunHost contract; backend choice is an immutable registry/profile decision, never an ad-hoc command-line override.

The retained native executable trees are frozen carrier candidates and are not a supported public CLI. Use Debug Probe in a debug APK for transport diagnostics. A future desktop extractor may expose its own CLI, but that interface is independent of backend execution and root-product success.

## Offset Extraction

`tools/extract_rs` derives offsets from a `boot.img` (plus optional `xbl_config.img`), a full OTA ZIP, or an `http(s)` URL pointing at one. kallsyms come from `--kallsyms` or are recovered from the image's embedded table. `pselect_waiter_shift` and `off_slide_loggers_0_1` are derived by the built-in arm64 disassembler. MediaTek images have no `xbl_config.img` and usually no BTF: the physical load address is derived from kallsyms `_text` (override with `--phys`).

```powershell
Push-Location tools/extract_rs
cargo build --release
Pop-Location
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract.exe OTA.zip --format conf --out profile.conf
```

`--format conf` is the extractor output: a flattened, self-contained profile (no `include` lines, the shared 6.x credential/KernelSnitch constants inlined, the route selected from `--analysis` evidence unless `--route` overrides it). The extractor emits every field the image actually yields and omits the rest; it never fills gaps from a neighbouring kernel family's guesses (unverified-family 6.6, the default `-2`, the 5.15 multicast constants, or a phys default). Every output is an **unverified candidate**: importable and parseable, with missing or invalid fields blocked by the app's pre-execution validation, so a successful run never implies device support. On 5.x it also derives the credential reference repair from `init_cred` and the multicast geometry from BTF (see `docs/analysis/extractor-5x-derivation-plan.md`). `--format json` stays for the v1 import path. To add a built-in profile, complete and validate the matching version-family template, save it as a standalone `.conf` profile, and add it to `profile/index.conf`. The old C `offsets.h` registry is deprecated and removed.

### MediaTek

MediaTek images have no `xbl_config.img` and usually no embedded BTF, so the
extractor cannot derive the two physical addresses (`kernel_phys_load`,
`kernel_phys_offset`) from the image and leaves them `null`. The runtime then
falls back to the SoC formula, which fails at W1 on MediaTek. Fill both by
running the separate `tools/mtk-phys/` extractor on a rooted device (it reads
`/proc/iomem`) and pasting the values into the app's advanced overrides. See
[MEDIATEK.md](docs/profile/MEDIATEK.md).

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
Push-Location tools/extract_rs
cargo build --release --target aarch64-linux-android
Pop-Location
adb push build/extract/aarch64-linux-android/release/ghostlock-extract /data/local/tmp/
adb shell /data/local/tmp/ghostlock-extract /sdcard/OTA.zip
```

### Importing offsets without rebuilding the app

New kernels no longer need an app rebuild: tap **Import offsets.conf (HOCON)**
and pick the extractor's flattened `.conf`, or use **Import offsets.json (v1)**
for an older JSON report. v1 JSON is converted in-app, so nothing has to be
pushed to the device: native always starts from the GLKv3 document (MessagePack root map with
`schema == 3`; the removed v2 binary is rejected) the app sends on stdin, and matches the current
`uname -r` against the resolved profile
before rejecting the kernel. Imports merge across files; a release already
stored prompts before overwrite.

The app can also generate the profile itself — **Parse OTA link** (full OTA ZIP
URL) and **Parse image** (`boot.img` + optional `xbl_config.img`) run the
extractor in-process and write a flattened `.conf` into the app data dir on
success:

```hocon
# GhostLock kernel profile (HOCON, canonical owner-qualified layout).
ghostlock {
  schema_version = 3
  release = "6.12.38-android16-5-g844001fb8721-ab14552068-4k"
  selection {
    backend  = "cve_2026_43499"
    terminal = "root_child"
  }
  common { kernel_major = 6 }
  platform {
    abi {
      kernel { kernel_phys_load = 0xC7800000 }
      task_struct { prio = 148, cred = 2304 }
    }
  }
  backend {
    cve_2026_43499 {
      steps = "mcast_rootchild"        # the ONE user-visible selection token
      route { multicast_waiter { waiter_shift = 0 } }
      cred { caps_offset = 48, copy_size = 136, caps_count = 5, caps_value = -1 }
      offset { init_task = 37801728, init_cred = 37891184 }
    }
  }
}
```

The exact field list lives in [PROFILE_SCHEMA.md](docs/profile/PROFILE_SCHEMA.md); the extractor emits this same canonical layout.

## Plugins

Plugins: to be developed.

## Credits & License

Based on the following projects, licensed under Apache License 2.0 (see [LICENSE](LICENSE)):

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)

### CVE-2026-43284 references

The 43284 backend is an independent rewrite written with reference to the following upstream
projects (consulted, not vendored). Several of them ship **no LICENSE file**, so no upstream code
is redistributed here and reusing it upstream requires their permission:

- [ankitrawatgit/DirtyFrag-Android-Root-Jailbreak](https://github.com/ankitrawatgit/DirtyFrag-Android-Root-Jailbreak) (`de2ab7b`) — IpSec + crash_dump + libc++ sentry + LKM/UMH chain
- [lsposed/lspromise](https://github.com/lsposed/lspromise) (`0258165`) — page-cache splice primitive
- [diabl0w/DFRoot](https://github.com/diabl0w/DFRoot) (`3050d5b`) — LKM/UMH (soft_reboot)
- [polygraphene/DFReroot](https://github.com/polygraphene/DFReroot) (`9edc769`) — minimal SELinux LKM
- [combeng6th/DirtyInit](https://github.com/combeng6th/DirtyInit) (`3409c35`) — unprivileged XFRM/IpSec approach

### Vendored libraries

- [ludocode/mpack](https://github.com/ludocode/mpack) — MessagePack reader/writer, MIT; vendored at [`core/lib/mpack/`](core/lib/mpack/) with its LICENSE kept alongside.
