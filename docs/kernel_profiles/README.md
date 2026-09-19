# Kernel Profile Porting Guide

> TODO(profile-suggest-01): Only kernel geometry is required. `recommend_shizuku`,
> `execution` retry counts and wait/settle timings are suggestions: a profile may
> omit or override them, and the shipped default becomes the suggestion.

## File Layout

- `app/src/main/assets/kernel_profiles/index.conf`: Stores the list of all built-in supported kernel profiles. New profiles must ultimately be added here. Matching is performed by exact `uname -r`.
- `app/src/main/assets/kernel_profiles/execution-tuning.conf`: Shared general execution tuning every profile includes; `execution-<route>.conf` carries per-route tuning (included only by profiles using that route, plus fallback). `credential-6x.conf` and `kernelsnitch-6x.conf` add the values shared by 6.x kernels. `*-template.conf` files are per-field-annotated reference templates, manually loadable from the debug picker but never auto-matched. See [Common Execution Defaults](defaults.md).
- `app/src/main/assets/kernel_profiles/<uname-r>.conf`: The complete profile corresponding to each specific kernel sub-version; the file name must match the kernel sub-version `release`.
- `docs/kernel_profiles/templates/`: Template folder. Contains kernel configuration templates for different minor versions.

> **HOCON**: every configuration file is parsed as HOCON (Typesafe Config).
> `#` comments, `${?variables}`, trailing commas and `include "file.conf"`
> (relative to the same folder, nested) are supported; JSON documents remain
> valid HOCON and need no migration.

## New Device Adaptation Workflow

1. Run `adb shell uname -r` and keep the complete string; any character difference will cause matching to fail.
2. Determine the kernel family and open the corresponding document: [5.x](templates/kernel-5.x.template.md), [6.1](templates/kernel-6.1.template.md), [6.6](templates/kernel-6.6.template.md), [6.12](templates/kernel-6.12.template.md).
3. Copy the corresponding `.conf`, name it using the full kernel sub-version `release`, and fill in the required fields.
4. Build the extraction tool under `tools`, run `ghostlock-extract --format json` to extract symbol/BTF data, and transcribe field by field.
    - `tools/extract_rs` parses offsets from `boot.img` (optionally with `xbl_config.img`), a complete OTA zip, or an `http(s)` link pointing to it. For kallsyms, pass `--kallsyms`, or omit it to directly recover the image's embedded table. `pselect_waiter_shift` and `off_slide_loggers_0_1` are derived by the built-in arm64 disassembler. MediaTek images do not have `xbl_config.img` and usually have no embedded BTF: the physical load address is derived from the kallsyms `_text` (can be overridden with `--phys`).
5. Verify that all required `off_*` addresses are non-zero, that the task/cred layout comes from the same image, and set `recommend_shizuku` (default `0`; only the 5.15 multicast profile recommends Shizuku).
6. Only override `execution` when there is actual device evidence; otherwise keep the [Common Defaults](defaults.md).
7. Add `{release,file}` to `index.conf`, run `jq` validation, Rust tests, and `./gradlew clean :app:assembleDebug`.
8. Use `./gradlew installDebug` to repeatedly debug-test on a real device under the same environment, with fixed cores, and a single route; be careful to control the device temperature before testing to prevent CPU throttling;

1. Run `adb shell uname -r` and capture the complete release string; any character difference intentionally prevents a match.
2. Determine the kernel family and open the matching guide: [5.x](templates/kernel-5.x.template.md), [6.1](templates/kernel-6.1.template.md), [6.6](templates/kernel-6.6.template.md), [6.12](templates/kernel-6.12.template.md).
3. Copy the corresponding `.template.json`, name it after the exact release, and write the `release` field.
4. Use `ghostlock-extract --format json` to extract symbol/BTF data; transcribe every field, and never reuse another firmware's `off_*` merely because the major version matches.
5. Validate that all required `off_*` addresses are nonzero, that task/cred layouts come from the same image, and set `requires_shizuku` according to the runtime identity.
6. Override `execution` only with measured device evidence; otherwise keep the [shared defaults](defaults.md).
7. Add `{release,file}` to `index.json`, validate JSON with `jq`, run Rust tests, and run `./gradlew clean :app:assembleDebug`.
8. Device-test repeatedly under low temperature, fixed CPU cores, and a single route; record App/Shizuku, W1/W2/W3, fallback, and cleanup outcomes.

## Merge Order

`defaults.json` → built-in release JSON → user sparse override → explicit UI CPU selection. Later layers win. Kotlin emits a single `active-profile.json`; Native no longer searches for or merges configuration sources.

## Safety Rules

- `off_* = 0` in a template means "must be extracted", never a runnable default.
- Incorrect task/cred/multicast layouts can corrupt arbitrary kernel memory, cause a black screen, or reboot the device.
- Increasing `execution` attempt counts or shortening waits can significantly increase heat and reduce the success rate.
- Do not submit a new profile as "supported" before it passes the device gate.
