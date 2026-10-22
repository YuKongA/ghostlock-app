# Kernel profile porting guide

> 中文: [README_ZH.md](README_ZH.md)

GhostLock matches kernels by exact `uname -r`. When nothing matches, the app
refuses to run. Supporting a new kernel usually needs no code change, only a
profile (a HOCON config file) for that kernel.

Only kernel geometry (symbol and struct offsets) is required.
`recommend_shizuku` and the retry/wait values under `execution` are
suggestions: a profile may omit them and the shipped default applies. See
[Shared execution defaults](defaults.md).

## Before you start

- A target device reachable over `adb`, or a firmware package matching it.
- A working build: `./gradlew :app:assembleDebug` succeeds.
- The extraction tool builds ([`tools/extract_rs`](../../tools/extract_rs), Rust).

## Where the files live

Built-in profiles live in `app/src/main/assets/kernel_profiles/`:

- `index.conf`: the list of built-in kernels. New profiles must be added here.
- `<uname-r>.conf`: one complete profile per kernel. The `release` entry in
  `index.conf` must match `uname -r` exactly, and the file is named after it.
- `execution-tuning.conf`: tuning shared by every kernel.
- `execution-<route>.conf`: tuning for one attack route. The resolver loads
  these presets; device profiles don't `include` them.
- `credential-6x.conf`, `kernelsnitch-6x.conf`: credential template and
  KernelSnitch values shared by 6.x kernels.
- `*-template.conf`: annotated reference templates. You can load them from the
  debug page, but they never participate in device matching.
- `docs/kernel_profiles/templates/`: templates grouped by kernel family.

All of these parse as HOCON (Typesafe Config): `#` comments, `${?variables}`,
trailing commas, and `include "file.conf"` (relative to the same directory,
nestable). JSON is valid HOCON, so nothing needs converting.

Field meanings, the required-field matrix, and validation rules are in the
[Profile schema](PROFILE_SCHEMA.md).

## Port a kernel

1. **Identify the kernel version.**
   Run `adb shell uname -r` and keep the complete string. Any character
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

   ```sh
   (cd tools/extract_rs && cargo build --release)
   build/extract/release/ghostlock-extract boot.img --format conf --out profile.conf
   ```

   `--format conf` writes a flattened, self-contained profile with no
   `include` lines and the 6.x credential/KernelSnitch constants inlined. It
   writes every field of the selected route and the shared geometry, using
   explicit `null` for unavailable values and never borrowing a neighbouring
   kernel family's guesses. Treat the result as a candidate
   seed: the app validates missing fields after import and blocks execution,
   so a successful generation does not mean the profile is usable. On 5.x the
   tool also derives the credential reference repair and the multicast
   geometry from the image and its BTF.

   The tool accepts a `boot.img` (optionally with `xbl_config.img`), a
   complete OTA zip, or an `http(s)` URL pointing at one. Pass `--kallsyms` to
   supply a symbol table explicitly, or omit it to recover the image's
   embedded table. `pselect_waiter_shift` and `off_slide_loggers_0_1` come
   from the built-in arm64 disassembler, and the route is suggested from
   kernel evidence (`--route` overrides it). For MediaTek physical addresses,
   pass `--vendor-boot vendor_boot.img` or `--iomem iomem.txt`. The vendor header
   supplies the load address and, when proven by alignment, the DRAM base.
   An iomem dump supplies the DRAM base and the `Kernel code` range needed
   with kallsyms to derive the load address. `_text` alone is insufficient.
   `--phys` supplies an explicit load address, not the DRAM base.

5. **Transcribe field by field, then check your work.**
   Field meanings and requirements are in the
   [Profile schema](PROFILE_SCHEMA.md). Confirm every required `off_*` is
   non-zero and that the task/cred layout comes from the same image. Set
   `recommend_shizuku` as needed (default `0`; some shipped profiles set `1`).

6. **Change `execution` only with device evidence.**
   Otherwise keep the [shared defaults](defaults.md). You can try values on
   the in-app override page before writing them back.

7. **Register it in `index.conf`.**
   Add a `{release, file}` entry, then validate and build:

   ```sh
   jq . app/src/main/assets/kernel_profiles/index.conf
   (cd tools/extract_rs && cargo test)
   ./gradlew clean :app:assembleDebug
   ```

8. **Verify on a real device.**
   Install with `./gradlew installDebug` and test repeatedly in the same
   environment, on fixed cores, with a single attack route. Let the device
   cool down first so CPU throttling doesn't skew the results. Don't mark a
   new profile as supported until it passes on-device verification.

## Edit configuration inside the app

You don't have to edit assets and reinstall the app for every tweak. Two
override layers apply immediately and take part in the next run:

- **Parameter override page**: adjust `execution.*` tuning, such as attempt
  counts, wait/timeout values, and core selection.
- **Advanced override page**: change `route` / `fallback.to` and any numeric
  path (stored as sparse HOCON). Useful for trying new offsets or geometry.

**Export config** writes the fully merged HOCON to a folder you pick (SAF), so
you can use it as the starting point for a new profile.

Overrides are saved per release and take priority over the built-in profile
and imported offsets. Section 7 of the [Profile schema](PROFILE_SCHEMA.md)
lists the layer order and storage locations. Adding a kernel to the built-in
set still follows the steps above and commits to assets.

## Safety notes

- `off_* = 0` in a template is a symbol offset relative to the kernel image
  base; you must extract the real value.
- A wrong task/cred/multicast layout can corrupt arbitrary kernel memory,
  blank the screen, or reboot the device.
- Raising `execution` attempt counts or shortening waits increases heat and
  can lower the success rate.
- Don't submit a new profile as supported before it passes on-device
  verification.
