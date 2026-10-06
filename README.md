# GhostLock-App

> 中文: [README_ZH.md](README_ZH.md)

## Documentation

- [Kernel Profile Porting Guide](docs/kernel_profiles/README.md) - add support for a new kernel. GhostLock matches kernels by exact `uname -r` and rejects unsupported builds, showing the status at the top. Built-in profiles live in `app/src/main/assets/kernel_profiles/`: one HOCON file per release, `index.conf` as the runtime index, and `<major.minor>-template.conf` version-family templates.
- [Supported devices](docs/kernel_profiles/SUPPORTED_DEVICES.md) - the built-in kernel list.
- [Shared execution defaults](docs/kernel_profiles/defaults.md) - every execution-tuning field, its default, and why.
- [Profile schema](docs/kernel_profiles/PROFILE_SCHEMA.md) - full profile structure and data flow.
- [Adding a component](docs/development/adding-a-component.md) - developer guide for a new native backend / terminal / route (Chinese).

For the complete device-porting workflow, kernel-family template links, and tuning rationale, see the [Kernel Profile Porting Guide](docs/kernel_profiles/README.md).

Rows explicitly marked **Shizuku required** run through a shell UserService. Start Shizuku with ADB and tap the status card to grant access; all other rows use the app's normal execution path.

## Quick Start

Open **GhostLock** and tap **Run**. KernelSU (`me.weishu.kernelsu`), ReSukiSU (`com.resukisu.resukisu`), or KowSU (`com.kowx712.supermanager`) provides `ksud` for module loading; without it, W1/W2 still grant uid 0 but no module is loaded.

The execution chain is `Pipeline<Backend, Terminal>`, fixed at compile time. Selection is exactly **one combination token** carried in `backend.<id>.steps`; `contract::kCombinationCatalog` is the single authority — **12 tokens = 7 available + 5 planned**. Available: the six `cve_2026_43499` tokens `{multicast_waiter, select_stack, tcp_zerocopy} x {rootchild, shizuku}` plus `cve_2026_43284 x umh`. Planned (`available=false`): `{mcast, pselect, tcp}_umh` and `cve_2026_43284 x {rootchild, shizuku}` — they parse, but the selection gate rejects them and the app greys them out. Both terminals (`root_child`, `umh_forward`) and both backends (`cve_2026_43499`, `cve_2026_43284`) are available; the other CVEs are header-only placeholders. The route (`select_stack` / `tcp_zerocopy` / `multicast_waiter`) is backend-internal policy **derived from the token**, not a separate component. The route races two cores: on the 6.6/6.12 tree-waiter kernels the main thread hammers `select` while a consumer thread perturbs the waiter's priority; on the 6.1 compact-waiter kernels it drives `getsockopt(TCP_ZEROCOPY_RECEIVE)` through a punched-hole page; the 5.15 kernels use the multicast waiter. The CPU pair also comes from the resolved profile.

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

The CLI carries transport, run control, safety and observability only (S4 R2b): `--ghostlock-app-call`, `--load-prebuilt-profile <bin>`, `--enable-status-record`, `--dump-kernel-log <dir>`, `--force-attack`, `--allow-dev-target` (relaxes only the binding-time carrier check), plus the read-only diagnostics `--probe-cve-2026-43284 <ko>` and `--plugin-probe <path.so> [--expect-sha256 <hex>]`. Selection and policy never come from the CLI: the staged entry and the `--cve43284-*` selectors were removed, and an unknown flag fails closed.

## Offset Extraction

`tools/extract_rs` derives offsets from a `boot.img` (plus optional `xbl_config.img`), a full OTA ZIP, or an `http(s)` URL pointing at one. kallsyms come from `--kallsyms` or are recovered from the image's embedded table. `pselect_waiter_shift` and `off_slide_loggers_0_1` are derived by the built-in arm64 disassembler. MediaTek images have no `xbl_config.img` and usually no BTF: the physical load address is derived from kallsyms `_text` (override with `--phys`).

```powershell
Push-Location tools/extract_rs
cargo build --release
Pop-Location
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract.exe OTA.zip --format conf --out profile.conf
```

`--format conf` is the extractor output: a flattened, self-contained profile (no `include` lines, the shared 6.x credential/KernelSnitch constants inlined, the route selected from `--analysis` evidence unless `--route` overrides it). The extractor emits every field the image actually yields and omits the rest; it never fills gaps from a neighbouring kernel family's guesses (unverified-family 6.6, the default `-2`, the 5.15 multicast constants, or a phys default). Every output is an **unverified candidate**: importable and parseable, with missing or invalid fields blocked by the app's pre-execution validation, so a successful run never implies device support. On 5.x it also derives the credential reference repair from `init_cred` and the multicast geometry from BTF (see `docs/analysis/extractor-5x-derivation-plan.md`). `--plugin-descriptor <probe-stdout.tsv>` (repeatable, `--format conf` only) lets a countermeasure's own probe description fill `plugin.<id>.extract.<key>`: the key names and types come from the descriptor, while the values are resolved from the profile being produced (R1), the image BTF (R3) or kallsyms (R2). It writes an `extract`-only profile fragment (not a wire document), appended after `countermeasure`; a `required` entry that cannot be resolved fails the run, an optional one is omitted and never replaced by a default, and without the flag the output is byte-for-byte unchanged. `--format json` stays for the v1 import path. To add a built-in profile, complete and validate the matching version-family template, save it as a standalone `.conf` profile, and add it to `kernel_profiles/index.conf`. The old C `offsets.h` registry is deprecated and removed.

### MediaTek

MediaTek images have no `xbl_config.img` and usually no embedded BTF, so the
extractor cannot derive the two physical addresses (`kernel_phys_load`,
`kernel_phys_offset`) from the image and leaves them `null`. The runtime then
falls back to the SoC formula, which fails at W1 on MediaTek. Fill both by
running the separate `tools/mtk-phys/` extractor on a rooted device (it reads
`/proc/iomem`) and pasting the values into the app's advanced overrides. See
[MEDIATEK.md](docs/kernel_profiles/MEDIATEK.md).

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

The exact field list lives in [PROFILE_SCHEMA.md](docs/kernel_profiles/PROFILE_SCHEMA.md); the extractor emits this same canonical layout.

## Plugins (P1)

Import a countermeasure `.so` from the settings page: the app copies it into its no-backup
`countermeasures/` root, hashes it locally, and reads its self-description through the read-only
native probe (`--plugin-probe`, never `dlopen` inside the JVM). Only **enabled** plugins are emitted
as `plugin.<id>.*` in the GLKv3 document (default off; `params.*` values are typed by the plugin's
own descriptor, and an enabled=false section is rejected). A reference countermeasure plugin lives in the standalone **`ghostlock-plugin-example`**
project (vendored ABI header, `build.sh android|host|abi-check`, bilingual README); this
repository keeps only `tools/plugins/README.md` as the pointer, because a plugin author
should not need the exploit repository to build one. **P1 ships the declare → validate → bind
wire layer only**: the runtime that loads the module and invokes it at its stage is not wired yet
(tracked as task-9 in the branch plan, and it needs its own L-level design and device gate).

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

- [ludocode/mpack](https://github.com/ludocode/mpack) — MessagePack reader/writer, MIT; vendored at [`src/lib/mpack/`](src/lib/mpack/) with its LICENSE kept alongside.

