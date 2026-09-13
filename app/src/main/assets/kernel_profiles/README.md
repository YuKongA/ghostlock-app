# Kernel Profile Porting Guide

This directory is the sole built-in source of runtime profiles.

## Layout

- `index.json`: Exact-release allowlist mapping `uname -r` to standalone JSON files; unlisted files are not packaged into the generated support index.
- `defaults.json`: Shared execution tuning for all releases; see [hared Execution Defaults](defaults.md).
- `<uname-r>.json`: One complete profile per kernel release; filename and `release` must match.
- `templates/`: Non-runtime kernel-family templates with self-contained English guides.

## Porting a New Device

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
