# 6.6 kernel profile template guide

> 中文: [kernel-6.6.template_ZH.md](kernel-6.6.template_ZH.md)

[kernel-6.6.template.conf](kernel-6.6.template.conf) is the starting point for a 6.6 device. Every geometry value is `null` on purpose: the profile stays invalid until you fill it in, so the editor and the Run gate flag unfinished work. Don't replace `null` with `0` to silence validation. Zero is a real value for some fields and changes behavior.

Field meanings, the required-field matrix, and validation rules are shared across kernel families. This page covers only what changes on 6.6.

- [Kernel profile structure](../PROFILE_SCHEMA.md): field groups, required-field matrix, validation
- [Shared execution defaults](../defaults.md): every `execution` value, all advisory

## Route selection

Shipped 6.6 profiles all declare `route.select_stack` with `waiter_shift = -2` and `fallback.to = "none"`. Every shipped profile uses the same shift. A different value moves the forged waiter off the target stack slot.

The template also shows the `tcp_zerocopy` branch. No shipped 6.6 profile uses it. Keep one branch and delete the rest.

## Credential layout

Most shipped 6.6 profiles include `credential-6x.conf` without local overrides.

| Field | Value |
|---|---|
| `cred.copy_size` | `136` |
| `cred.usage_value` | `1` |
| `cred.caps_offset` | `48` |
| `cred.caps_count` | `5` |
| `cred.caps_value` | `-1` |
| `cred.ref_count` | `null` |

The two self-contained 6.6 profiles in the assets spell the same block out inline. Either form works.

## KernelSnitch

Shipped 6.6 profiles set `kernelsnitch.collisions = 4` and leave `kernelsnitch.mm_struct_sz = null`. Native then uses its fallback of `0x500` (`src/core/kernel/constants.hpp`). This family does not override the size.

## Provenance

The values above come from the shipped 6.6 profiles. This page does not re-measure them. They are not a stable ABI across builds: revalidate against the same target image. A wrong value can change target selection, structure bounds, or race timing, and cause failure, deadlock, memory corruption, a black screen, or reboot. Native validates no geometry. Kotlin `validateProfileFields` records missing or out-of-range fields in `invalidPaths` and blocks Run until you fix them.

## Register the profile

1. Copy the template to `app/src/main/assets/kernel_profiles/<release>.conf`.
2. Set `release` to the exact `uname -r` string. The file name and the match must agree.
3. Add `include "credential-6x.conf"` and `include "kernelsnitch-6x.conf"` in place of the inline `cred` and `kernelsnitch` blocks, delete the unused route branches, and fill every `null`.
4. Add `{ release = "<release>", file = "<release>.conf" }` to `profiles` in `index.conf`.

Don't copy execution tuning into the profile. The resolver loads it from `execution-*.conf`.
