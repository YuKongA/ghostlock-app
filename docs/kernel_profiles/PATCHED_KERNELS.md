# Patched kernels

Builds that already contain the CVE-2026-43499 fix. A profile cannot make the
exploit work on them: the rtmutex use-after-free it builds on is absent, so the
extractor refuses the image with exit code 6 and `Native` has no primitive to
reach W1/W2. The app reports these releases on the kernel card as
**Kernel patched** instead of *Not supported*, so nobody spends time hunting
offsets that cannot help.

The list lives in
[`app/src/main/assets/kernel_profiles/patched_kernels.conf`](../../../app/src/main/assets/kernel_profiles/patched_kernels.conf)
and is matched by exact `uname -r`, the same rule the built-in profiles use.

## How a build is proven patched

`ghostlock-extract` decides this itself; a release is only listed here after the
extractor and a disassembly check agree:

```sh
build/extract/release/ghostlock-extract boot.img --format conf --out /tmp/p.conf
# kernel already fixed the vulnerability: remove_waiter()@<addr> never reads
# current (no mrs sp_el0); rtmutex UAF fix is present
echo $?    # 6
```

The check is concrete: the vulnerable `remove_waiter()` reads `current` through
`sp_el0` (`mrs xN, s3_0_c4_c1_0`) and then locks `current->pi_lock`. The fix
([`3bfdc63936dd`](https://github.com/torvalds/linux/commit/3bfdc63936dd4773109b7b8c280c0f3b5ae7d349),
first in 6.6.140) locks the `pi_lock` of `waiter::task` instead, so the function
contains no `mrs …, sp_el0` at all.

## Listed builds

### `6.6.118-android15-8-gf72a02f47f4b-abogki551678766-4k`

Xiaomi 15 (`dada`, 24129PN74C), HyperOS 4.0.3.0.XOCCNXM and 4.0.4.0.XOCCNXM
(Android 17, SPL 2026-10-01), locked bootloader. Same kernel build on OS 4.0.3.0
and 4.0.4.0.

- Extractor: `vulnerable` check fails with exit code 6, message
  `remove_waiter()@0x106da4c never reads current (no mrs sp_el0)`.
- Disassembly of `remove_waiter` (`@0x106da4c`) contains no `mrs …, sp_el0`
  instruction in the whole function; the equivalent 6.6.118 build from HyperOS 3
  (`6.6.118-android15-8-gc44b714366cc-abogki519650608-4k`, `@0x106b260`) reads
  `current` at `+0x38` (`mrs x20, s3_0_c4_c1_0` → `add x21, x20, #0x90c` →
  `spin_lock`).
- HyperOS 4's version instead loads the waiter's task and locks that:
  `ldr x20, [x23, #0x50]` (`waiter::task`) → `add x21, x20, #0x90c` →
  `spin_lock` — i.e. the upstream fix backported into the vendor tree.
- `--analysis` on the same image derives the geometry and all three routes
  (`select_stack`, `multicast_waiter`, `tcp_zerocopy`) as available but reports
  `primitive unavailable`, which is the expected shape for a patched kernel.

Xiaomi 15 users on HyperOS 4 therefore need a different route to root; GhostLock
does not apply, and a profile would only produce a run that aborts in W1/W2.

## Adding an entry

1. Confirm the extractor exits 6 on the image for that release.
2. Confirm no `mrs …, sp_el0` in `remove_waiter()` (see
   `tools/extract_rs/examples/disasm_func.rs`).
3. Add a `{release, vendor, devices, upstream_fix, why}` entry to
   `patched_kernels.conf`. Keep `why` to one line; it is shown in the app.
4. Reference the evidence here.
