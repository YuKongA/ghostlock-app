## Summary

`remove_waiter_uses_current()` classifies a kernel as **patched** purely by the
absence of `mrs xN, sp_el0`. That is not a valid signal: the fixed function
still reads `sp_el0` for its own preempt-count / lockdep bookkeeping. This PR
decides the verdict from the base register of the store that clears
`pi_blocked_on` instead.

## Scope

**Detection only.** This fixes a false negative in the profile gate. It does
not change exploit behaviour and makes no claim about the exploit working on
any particular kernel/family.

## The problem

```rust
pub fn remove_waiter_uses_current(dis: &[String]) -> bool {
    let mrs_current = Regex::new(r"(?i)\bmrs\s+x\d+,\s*s3_0_c4_c1_0\b").unwrap();
    dis.iter().any(|line| mrs_current.is_match(line))
}
```

Two independent defects:

1. **Patched kernels still contain `sp_el0`.** The fix moves the `pi_lock` /
   `pi_blocked_on` handling from `current` to `waiter->task`, but unrelated
   preempt/lockdep accounting keeps using `current`. Such a kernel is reported
   as vulnerable.

2. **The register is matched by encoding name only.** LLVM prints `SP_EL0`;
   only the encoded form `s3_0_c4_c1_0` is matched here.

Observed on a vivo PD2361 (`5.15.197-g9b8a3ba875bb-dirty`) that carries the
Android backport of `3bfdc63936dd` (Bug 496407345):

```
ffffffc009852e48: mov  x23, x1
ffffffc009852e64: ldr  x20, [x23, #0x30]   ; waiter->task
ffffffc009852e6c: mrs  x25, SP_EL0         ; preempt bookkeeping only
ffffffc009852e78: add  x22, x20, #0x884    ; waiter->task->pi_lock
ffffffc009852ef0: str  xzr, [x20, #0x8b0]  ; waiter->task->pi_blocked_on = NULL
ffffffc009853104: mov  x5, x20             ; 6th arg = waiter->task
ffffffc00985310c: bl   rt_mutex_adjust_prio_chain
```

The kernel is patched; the current check calls it vulnerable.

## The fix

Take the base register of the zero store that clears `pi_blocked_on`:

- **vulnerable** — the base is `current` (`mrs Xn, sp_el0`, or a `mov` copy of
  that register);
- **patched** — the base is `waiter->task` (`ldr Xn, [waiter, #task_offset]`,
  where the base of that load is `x1` or a `mov` copy of it).

`propagate_mov_aliases()` resolves the register copies compilers emit in the
prologue (`mov x23, x1` above). If neither shape is recognised the legacy `mrs`
test is kept, so kernels the current heuristic already handles keep their
previous verdict.

The `mrs` pattern now accepts both `sp_el0` and `s3_0_c4_c1_0`.

## Tests

| test | status |
|---|---|
| `patched_remove_waiter_clears_pi_blocked_on_on_waiter_task` | kept |
| `vulnerable_remove_waiter_reads_current` | kept |
| `vulnerable_remove_waiter_through_a_current_copy` | new — `mrs` → `mov` → `str xzr` |
| `patched_backport_still_reads_sp_el0` | new — the real PD2361 5.15.197 disassembly above |

Applied on top of the current `main` tree and run with `cargo test --offline`:
**46 passed, 0 failed** (all four tests above included).

## Files changed

`tools/extract_rs/src/derive.rs` only.
