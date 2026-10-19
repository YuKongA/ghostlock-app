# 6.1 kernel profile template guide

> 中文: [kernel-6.1.template_ZH.md](kernel-6.1.template_ZH.md)

[kernel-6.1.template.conf](kernel-6.1.template.conf) is the starting point for a 6.1 device. Every geometry value is `null` on purpose: the profile stays invalid until you fill it in, so the editor and the Run gate flag unfinished work. Don't replace `null` with `0` to silence validation. Zero is a real value for some fields and changes behavior.

Field meanings, the required-field matrix, and validation rules are shared across kernel families. This page covers only what changes on 6.1.

- [Kernel profile structure](../PROFILE_SCHEMA.md): field groups, required-field matrix, validation
- [Shared execution defaults](../defaults.md): every `execution` value, all advisory

## Route selection

All shipped 6.1 profiles declare `route.tcp_zerocopy` with `compact_waiter = 1`. Twenty of them also declare a select-stack fallback:

```hocon
fallback {
  to = "select_stack"
  route {
    select_stack { waiter_shift = 1 }
  }
}
```

Three shipped profiles set `to = "none"`. Only the `tcp_zerocopy` to `select_stack` fallback is implemented. Don't declare any other target. Keep one `route` branch and delete the rest.

## Credential layout

The template spells the credential block out inline. Most shipped 6.1 profiles replace it with `include "credential-6x.conf"`, a shared 6.x template that leaves the 5.x reference slots `null` because 6.x does not repair them. Either form works.

| Field | Value |
|---|---|
| `cred.copy_size` | `136` |
| `cred.usage_value` | `1` |
| `cred.caps_offset` | `48` |
| `cred.caps_count` | `5` |
| `cred.caps_value` | `-1` |
| `cred.ref_count` | `null` |

`caps_offset` is the one value that varies. One shipped profile overrides it to `40`. Take the value from the target image rather than assuming `48`.

The two self-contained 6.1 profiles in the assets repeat the same block inline instead of including `credential-6x.conf`.

## KernelSnitch

Shipped 6.1 profiles set `kernelsnitch.collisions = 4`, and they all set `kernelsnitch.mm_struct_sz = 1024`. Most take the first value from `kernelsnitch-6x.conf` and override the size locally. The include carries `mm_struct_sz = null`, and the native fallback is `0x500` (`src/core/kernel/constants.hpp`), so the override is required for the 6.1 layout. The template has no `mm_struct_sz` row, so add it when you adapt the file.

## Provenance

The values above come from the shipped 6.1 profiles. This page does not re-measure them. They are not a stable ABI across builds: revalidate against the same target image. A wrong value can change target selection, structure bounds, or race timing, and cause failure, deadlock, memory corruption, a black screen, or reboot. Native validates no geometry. Kotlin `validateProfileFields` records missing or out-of-range fields in `invalidPaths` and blocks Run until you fix them.

## Register the profile

1. Copy the template to `app/src/main/assets/kernel_profiles/<release>.conf`.
2. Set `release` to the exact `uname -r` string. The file name and the match must agree.
3. Add `include "credential-6x.conf"` and `include "kernelsnitch-6x.conf"`, replace the inline `cred` and `kernelsnitch` blocks with the local overrides the target needs, delete the unused route branches, and fill every `null`.
4. Add `{ release = "<release>", file = "<release>.conf" }` to `profiles` in `index.conf`.

Don't copy execution tuning into the profile. The resolver loads it from `execution-*.conf`.
