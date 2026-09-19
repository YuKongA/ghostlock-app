# Kernel Profile Adaptation Guide

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

## Safety Notes

- `off_* = 0` in the template is a symbol offset relative to the kernel image base address and must be extracted.
- Incorrect task/cred/multicast layout may cause arbitrary kernel memory corruption, a black screen, or a reboot.
- Increasing the number of attempts or shortening the wait in `execution` may significantly raise temperatures and reduce the success rate.
- A new profile must not be submitted as “supported” until it passes the real-device gate.