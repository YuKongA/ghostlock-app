# Real-device evidence — v8

KernelSU manager v3.3.0 (32601-2, LKM) reporting **working / jailbreak mode** after the
exploit chain ran to completion on the target device. The log tail shows the module
`late-load`, `enforce=1 (enforcing)` after KernelSU took over, `KernelSU ready` and
`result: exploit completed`.

| Item | Value |
|---|---|
| Image | `vrko-v8-real-device-kernelsu.jpg` |
| Device | iQOO Neo10 Pro+ (vivo PD2463) |
| Kernel | `6.6.89-android15-8-g1f71897ac249-abogki467805059-4k` |
| Fingerprint | `vivo/PD2463/PD2463:16/.../BP2A.250605.031.A3_VC...` |
| KernelSU | 32601-2, LKM, working |

## What this does and does not prove

- It shows the **whole chain plus the KernelSU late-load succeeding on hardware** for the
  code in this source tree (v8).
- It is **not** a per-item device-gate record: it does not include `tools/cmp_disasm.py`
  output, a cold boot, a fixed CPU pair, or a KernelSU-free clean start — so this tree must
  not be marked "supported" on the strength of it alone.
- Versions after v8 (W2 bounded waits, W2 diagnostics, the kernel_phys notice split) were
  **never device-tested** and are deliberately not part of this source.
- One observed issue stays open: W2 retried 15 times without succeeding on a *different*
  (unreleased) build. That run's log was never captured, so it is neither explained nor
  claimed fixed here.
