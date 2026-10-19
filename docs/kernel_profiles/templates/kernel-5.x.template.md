# 5.x kernel profile template guide

> 中文: [kernel-5.x.template_ZH.md](kernel-5.x.template_ZH.md)

[kernel-5.x.template.conf](kernel-5.x.template.conf) is the starting point for a 5.15 device. Every geometry value is `null` on purpose: the profile stays invalid until you fill it in, so the editor and the Run gate flag unfinished work. Don't replace `null` with `0` to silence validation. Zero is a real value for some fields and changes behavior.

Field meanings, the required-field matrix, and validation rules are shared across kernel families. This page covers only what changes on 5.x.

- [Kernel profile structure](../PROFILE_SCHEMA.md): field groups, required-field matrix, validation
- [Shared execution defaults](../defaults.md): every `execution` value, all advisory

## Route selection

Shipped 5.15 profiles all declare `route.multicast_waiter` and set `fallback.to = "none"`. The template also shows the `tcp_zerocopy` and `select_stack` branches. Keep one branch, delete the rest. Native rejects a profile with no route or an unknown route.

Shipped values for the multicast branch:

| Field | Value |
|---|---|
| `route.multicast_waiter.waiter_off` | `96` |
| `route.multicast_waiter.buffer_size` | `264` |
| `route.multicast_waiter.task_offset` | `48` |
| `route.multicast_waiter.lock_offset` | `56` |
| `route.multicast_waiter.compact_waiter` | `1` |

The multicast route needs fields that the other routes do not: `offset.empty_zero_page`, `kernelsnitch.mm_struct_sz`, and a `cred.ref_count` greater than zero. Validation also requires `cred.copy_size ≥ 0xa0` and `waiter_off + lock_offset + 8 ≤ buffer_size`.

## Credential layout

5.x repairs the credential reference fields, so the reference slots are in use. 6.x leaves them `null`.

| Field | Value |
|---|---|
| `cred.copy_size` | `176` |
| `cred.usage_value` | `256` |
| `cred.caps_offset` | `48` |
| `cred.caps_count` | `3` |
| `cred.caps_value` | `2199023255551` |
| `cred.ref_count` | `4` |
| `cred.ref0_offset` to `cred.ref3_offset` | `128`, `136`, `144`, `152` |
| `cred.ref0_image` to `cred.ref3_image` | per-device image offsets |

The `refN_image` values differ per device. Take them from the same target image as the other offsets. Validation requires each `refN_image` to be non-zero and each `refN_offset + 8` to fit inside `copy_size`.

## KernelSnitch

Shipped 5.15 profiles set `kernelsnitch.collisions = 8` and `kernelsnitch.mm_struct_sz = 1024`. The native fallback for a missing `mm_struct_sz` is `0x500` (`src/core/kernel/constants.hpp`), so set the field rather than relying on it.

## Provenance

The values above come from the shipped 5.15 profiles. This page does not re-measure them. They are not a stable ABI across builds: revalidate against the same target image. A wrong value can change target selection, structure bounds, or race timing, and cause failure, deadlock, memory corruption, a black screen, or reboot. Native validates no geometry. Kotlin `validateProfileFields` records missing or out-of-range fields in `invalidPaths` and blocks Run until you fix them.

## Register the profile

1. Copy the template to `app/src/main/assets/kernel_profiles/<release>.conf`.
2. Set `release` to the exact `uname -r` string. The file name and the match must agree.
3. Delete the unused route branches and fill every `null`.
4. Add `{ release = "<release>", file = "<release>.conf" }` to `profiles` in `index.conf`.

Don't copy execution tuning into the profile. The resolver loads it from `execution-*.conf`.
