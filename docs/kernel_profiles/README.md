# Kernel Profile Porting Guide

This guide explains how to make GhostLock support a new kernel.

GhostLock matches kernels by exact `uname -r`. When nothing matches, the app
refuses to run. In most cases you don't need to change code — you only add a
profile (a HOCON config file) for that kernel.

> Only kernel geometry (symbol and struct offsets) is required.
> `recommend_shizuku` and the retry/wait values under `execution` are
> suggestions: a profile may omit them, and the shipped default becomes the
> suggestion (see [Shared Execution Defaults](defaults.md)).

## Before you start

- A target device reachable over `adb`, or a firmware package matching it.
- A working build: `./gradlew :app:assembleDebug` succeeds.
- The extraction tool builds ([`tools/extract_rs`](../../tools/extract_rs), Rust).

## Where the files live

Every built-in profile lives in `app/src/main/assets/kernel_profiles/`:

- `index.conf` — the list of built-in kernels. New profiles must be added here.
- `<uname-r>.conf` — one complete profile per kernel; the file name must match
  `uname -r` exactly.
- `execution-tuning.conf` — tuning shared by every kernel.
- `execution-<route>.conf` — tuning for a single attack route. The resolver
  loads these presets; device profiles don't `include` them.
- `credential-6x.conf`, `kernelsnitch-6x.conf` — credential template and
  KernelSnitch values shared by 6.x kernels.
- `*-template.conf` — annotated reference templates. You can load them manually
  from the debug page, but they never participate in device matching.
- `docs/kernel_profiles/templates/` — templates grouped by kernel family.

All of these are parsed as HOCON (Typesafe Config): `#` comments, `${?variables}`,
trailing commas, and `include "file.conf"` (relative to the same directory,
nestable) are supported. JSON is still valid HOCON, so nothing needs converting.

For what each field means, the required-field matrix, and the validation rules,
see the [Profile schema](PROFILE_SCHEMA.md). This guide covers the workflow only
and doesn't repeat the field reference.

## Adaptation steps

1. **Identify the kernel version.**
   Run `adb shell uname -r` and keep the complete string; any character
   difference makes matching fail.

2. **Pick a template.**
   Open the document for the kernel family:
   [5.x](templates/kernel-5.x.template.md),
   [6.1](templates/kernel-6.1.template.md),
   [6.6](templates/kernel-6.6.template.md),
   [6.12](templates/kernel-6.12.template.md).

3. **Copy it into a new profile.**
   Copy the template to `app/src/main/assets/kernel_profiles/<uname-r>.conf`.
   The file name must match `release`, then fill in the required fields.

4. **Extract the offsets.**
   Build and run the extractor:

   ```sh
   cargo build --release --manifest-path tools/extract_rs/Cargo.toml
   build/extract/release/ghostlock-extract boot.img --format conf --out profile.conf
   ```

   `--format conf` writes a flattened, self-contained profile (no `include`
   lines, the 6.x credential/KernelSnitch constants inlined); it can seed the
   new profile directly. On 5.x it also derives the credential reference
   repair and the multicast geometry from the image and its BTF. The tool accepts a `boot.img` (optionally with
   `xbl_config.img`), a complete OTA zip, or an `http(s)` URL pointing at one.
   Pass `--kallsyms` to supply a symbol table explicitly, or omit it to recover
   the image's embedded table. `pselect_waiter_shift` and
   `off_slide_loggers_0_1` are derived by the built-in arm64 disassembler, and
   the route is suggested from kernel evidence (`--route` overrides it).
   MediaTek images have no `xbl_config.img` and usually no embedded BTF: the
   physical load address is derived from kallsyms `_text`, and you can override
   it with `--phys`.

5. **Transcribe field by field, then check your work.**
   Field meanings and requirements are in the
   [Profile schema](PROFILE_SCHEMA.md). Confirm every required `off_*` is
   non-zero and that the task/cred layout comes from the same image. Set
   `recommend_shizuku` as needed (default `0`; only the 5.15 multicast profile
   recommends it).

6. **Change `execution` only with device evidence.**
   Otherwise keep the [Shared Defaults](defaults.md). You can also try values on the
   in-app parameter override page first (see below) before writing them back.

7. **Register it in `index.conf`.**
   Add a `{release, file}` entry, then validate and build:

   ```sh
   jq . app/src/main/assets/kernel_profiles/index.conf
   cargo test --manifest-path tools/extract_rs/Cargo.toml
   ./gradlew clean :app:assembleDebug
   ```

8. **Verify on a real device.**
   Install with `./gradlew installDebug` and test repeatedly in the same
   environment, on fixed cores, with a single attack route. Let the device cool
   down first so CPU throttling doesn't skew the results. Don't mark a new
   profile as "supported" until it passes on-device verification.

## Editing configuration inside the app

You don't have to edit assets and reinstall the app for every tweak. The app
offers two override layers; saving applies them immediately and they take part
in the next run:

- **Parameter override page (general overrides)** - adjust `execution.*` tuning,
  such as attempt counts, wait/timeout values, and core selection.
- **Advanced override page** - change `route` / `fallback.to` and any numeric
  path (stored as sparse HOCON); handy for trying new offsets or geometry.
- **Export config** - write the fully merged HOCON to a folder you pick (SAF)
  and use it as the starting point for a new profile.

Overrides are saved per release and take priority over the built-in profile and
imported offsets. See section 7 of the
[Profile schema](PROFILE_SCHEMA.md) for the layer order and storage locations.
Adding a kernel to the built-in set still follows the steps above and commits to
assets.

## Safety notes

- `off_* = 0` in a template is a symbol offset relative to the kernel image
  base; you must extract the real value.
- A wrong task/cred/multicast layout can corrupt arbitrary kernel memory, blank
  the screen, or reboot the device.
- Raising `execution` attempt counts or shortening waits increases heat and can
  lower the success rate.
- Don't submit a new profile as "supported" before it passes on-device
  verification.
