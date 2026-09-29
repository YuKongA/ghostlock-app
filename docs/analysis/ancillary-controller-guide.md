# Ancillary behavior guide (`AncillaryController`)

Audience: the developer implementing the `vr.ko` behavior and their agent.

This document describes **the architecture and how to adapt a behavior to it**. The
concrete `vr.ko` bypass logic, its parameters and their verification are **not**
described here on purpose — they are agreed in the pull-request discussion.

---

## 1. What the controller is

`AncillaryController` hosts behaviors that run **outside the vulnerability path**:
vendor anti-root mitigations, environment fixups and similar. It is deliberately
separate from the `frontend × backend × middleware` component axes:

- those axes describe the **exploit path** (the primitive and how a kernel write is
  realized);
- an ancillary behavior describes **extra steps around that path** and must not change
  how the path writes.

Project-wide constraints apply (`../../AGENTS.md`): compile-time composition, no virtual
dispatch, no mutable globals, and configuration is never inferred from the kernel
version.

## 2. Where the pieces live

| Path | Role |
|---|---|
| `../../src/core/session/ancillary/ancillary_policy.hpp` | `AncillaryKind`, `AncillaryStage`, `AncillaryContext`, `AncillaryPolicyDefaults`, and the `AncillaryPolicyFor` concept |
| `../../src/core/session/ancillary/ancillary_controller.hpp` | the `AncillaryPolicyList` registry and the header-only dispatcher `AncillaryController<Middleware>::apply` |
| `../../src/core/session/ancillary/ancillary_controller.cpp` | Android translation unit that pulls the header through the device build |
| `../../src/core/session/ancillary/vr_guard.hpp` | the `vr.ko` behavior (`VrGuardPolicy`) |
| `../../src/core/tests/ancillary_test.cpp` | host test for the registry, the profile gate and the dispatch signature |

## 3. The behavior contract

A behavior is a policy type satisfying `AncillaryPolicyFor<P, Middleware>`:

- `static constexpr AncillaryKind kind` — a stable id (append one to the enum);
- `static bool enabled(const profile::TargetProfile &)` — the profile gate. Fail
  closed: if the resolved profile does not enable the behavior, return `false`;
- `template <class Middleware> static Status apply(AncillaryStage, ExploitSession &, AncillaryContext &)` — the stage entry.

`AncillaryStage` selects **when** the behavior runs relative to the exploit steps:
`PreSpawn` (setup done, before a victim exists), `PostSpawn` (a victim exists),
`PreHandoff` (before the frontend handoff). `AncillaryContext` carries what the backend
injects: whether the kernel write primitive and the read-back primitive are available,
plus the runtime applicability decision (§5).

## 4. Adding a behavior — the only touch points

1. Append an id to `AncillaryKind`.
2. Create `src/core/session/ancillary/<name>.hpp` (and `.cpp` when the body needs the
   device/attack primitives):
   - keep the **plan** — pure functions that turn the resolved profile layout and
     already-read values into an ordered list of writes — in the header; it must be
     host-compilable and unit-tested with fixed vectors;
   - keep the **execution** — anything that touches the kernel — in the `.cpp`, using
     the primitives injected through `AncillaryContext`.
3. Append the policy type to `AncillaryPolicyList` in `ancillary_controller.hpp`.
   **This is the single registration point.** No route, frontend or backend edits.
4. Add a host test and register it in `../../src/Makefile`.
5. Add the behavior's profile contract on both sides (§6).

The backend calls `AncillaryController<Middleware>::apply` at the fixed stages; that
call site is wired **once** and never changes when behaviors are added.

## 5. Applicability is a runtime decision

`enabled()` is only a **pre-set from the profile**. It is not proof that the behavior
is relevant on the running device:

- the same GKI release is shipped by multiple vendors, so the kernel string / release
  is **not** a device identity;
- therefore a behavior that targets a vendor component must **confirm at runtime** that
  the component is actually present, and **do nothing** when it is not (fail safe);
- record that decision in `AncillaryContext` at the controller entry, not in the route.

Do not key a behavior on `uname -r` or on the profile release. The profile gate and the
runtime applicability are two separate facts and both must hold.

## 6. Profile contract (GLK1 v2)

Behavior configuration travels in the resolved profile, never in environment variables
(`../../AGENTS.md`):

- a **gate** field records whether the support list enables the behavior for this
  profile;
- an optional **layout** section carries the behavior-specific parameters;
- shared kernel symbol offsets live in the existing `offset` section.

Add the fields to the native model (`profile/model.h`, `profile/binary.cpp`) and mirror
them in `profile-core` (`NativeProfile.kt`, the resolver, the exporter and the app-side
injection); the key names must match verbatim and the golden fixtures must be
recomputed. The exact field set for `vr.ko` is defined by the PR, not by this document.

## 7. Constraints checklist

- [ ] No mutable global state; do not add fields to `ExploitSession` — its layout is
      fixed so attack-function stack offsets do not move.
- [ ] No indirect/virtual dispatch on the attack path.
- [ ] Header-only parts compile on the host.
- [ ] New code is warning-free under `-Wall -Wextra -Wconversion -Wsign-conversion`.
- [ ] Configuration comes from the profile; no configuration-class environment
      variables.

## 8. Verification

- `make -C src native-host-tests`
- NDK build with **zero warnings**
- `make -C src lint-tidy`
- If the behavior touches the attack path: `python3 tools/cmp_disasm.py <baseline> build/native/ghostlock`
  **and** the on-device gate (cold boot, fixed CPU pair, single route, KernelSU not
  loaded), archived under `docs/analysis/device-gates/`.

## 9. The `vr.ko` behavior

`VrGuardPolicy` (id `VrGuard`) is the first registered behavior. On this branch it is a
**skeleton**: declared and registered, but gated off. Implement it by following §4–§6;
the concrete neutralization logic and its parameters are agreed in the PR review.
