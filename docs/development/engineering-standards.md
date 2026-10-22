English | [简体中文](engineering-standards_ZH.md)

# GhostLock engineering and design standards

> Practices and requirements. See [`design-philosophy.md`](design-philosophy.md) for the reasons behind them. These rules apply to human developers and
> AI agents in this repository. Before any change, you must read this document and complete the design process in section 1.
> Terms follow RFC 2119: MUST / SHOULD / MAY.
> Precedence: the user's current requirements > `AGENTS.md` > this document > other documentation. If this document conflicts with code, the code takes precedence. Correct this document with the change.

---

## 1. Development process: design before changes, required

### 1.1 Four-stage process

```
Explore (read-only investigation) → Design (design artifacts) → Implement (minimal implementation) → Verify (verification by level)
```

- **Explore**: read the relevant code and documentation before changing anything. Use `git log`/`git show` to review historical designs and evidence.
  This repository's `docs/analysis/**` and device-gates are fully retained in git history. Keep the investigation narrow. State assumptions where uncertain.
- **Design**: produce a reviewable design according to the table below. Do not start implementation before the design is complete.
- **Implement**: make the smallest change, touching only the files listed in the design. Do not make changes outside that scope.
- **Verify**: run the checks required by §1.3 and keep the evidence: command output, logs, and comparison results. Evidence takes precedence over verbal assertions.

### 1.2 Change levels and design artifacts

| Level | Criteria | Minimum design artifact |
|---|---|---|
| **S** | Comments, spelling, or formatting within one file; documentation typos | None; change directly |
| **M** | Ordinary bug fixes, features or refactors outside the attack path, added tests, structural documentation updates | A paragraph stating the motivation, affected files, behavioral differences, and verification plan; this may be in the task description or commit message |
| **L** | Critical attack path: waiter/race/payload/route/exec flow; configuration file format or wire protocol; cross-Native↔Kotlin contracts; public data structures; build system; new routes | A **plan document**, using the template in `documentation-standards.md`: current state and baseline commit, goals and constraints, per-file change list, data-flow/control-flow differences, compatibility and rollback, verification matrix, and explicit preservation list |

For L-level changes, you must present the design and obtain the user's approval before writing code.

### 1.3 Verification requirements by level

| Change | Host tests | NDK build | lint-tidy | `cmp_disasm` | Device gate |
|---|---|---|---|---|---|
| S/M outside the attack path | Required | Required, zero warnings | Required, 0 findings | May skip | Not required |
| M touching the attack path | Required | Required, zero warnings | Required, 0 findings | **Required**, 8 functions | Required; see §8.3 |
| L | Required | Required, zero warnings | Required, 0 findings | **Required** | Required, with archived evidence |

See `AGENTS.md` and §8 for commands.

### 1.4 Batches and explicit preservation

- Split large changes into batches, each covering one kind of work. Each batch independently completes "implement → all required checks → record verification conclusions".
  Do not start the next batch before the previous one passes verification.
- Every plan must include an "Explicit preservation: out of scope" list to prevent incidental rewrites. Examples include ported kernel code in `futex_hash.h`,
  `put32/put64`, the absence of a `PayloadPage` destructor, and the `FIELD` macro. Code outside the change list is frozen.

### 1.5 Commits and branches

- Branch flow: `very-not-stable-dev` → upstream `main`. Do not commit, push, or create a PR without an explicit user request.
- Use Conventional Commits: `type(scope): summary`,
  `type ∈ {feat,fix,docs,refactor,test,chore,build,revert}`. Put behavioral changes and invariants in the body. Historical example:
  `fix(profile): resolve imported v1 reports and edit them per their own release`.
- Each commit expresses one intent and contains no unrelated formatting. Split large changes into a sequence of small commits aligned with the batches.
- Review: changes to the main repository should have at least one reviewer other than the author. For L-level changes, the plan and gate evidence are the review record.
  A new feature without tests is incomplete.

---

## 2. Architecture standards

### 2.1 Three layers and dependency direction

```
app/ (Kotlin, UI/domain/data/shizuku)
  │  ProcessBuilder (not JNI): write GLK1 to stdin, collect logs from stdout/stderr, report the result through the exit code
  ▼
src/core/ (C++23 executable ELF)     tools/extract_rs/ (Rust offline extractor)
```

- Cross-layer contracts are limited to processes, files, stdin/stdout, exit codes, and GLK1/HOCON profile text. New cross-layer state must go through
  a profile or file. Do not invent environment variables; see §3.1.
- Kotlin layers: `ui → domain → data`. Platform I/O is allowed only in `data/`. `shizuku/` is an execution-entry variant,
  not a place for business rules.
- Native namespace layers: `ghostlock::{route,session,memory,attack,race,support,profile}`.
  All includes are relative to `src/core`, as in `#include "route/..."`, so ownership is visible at the call site.

### 2.2 Route structure, the project's main extension point

Each route has three parts. Do not use virtual base classes because indirect calls are forbidden in the PI race window:

1. **Policy**, in `route/route_policy.hpp`: compile-time capability declarations, `kind`, `supported()`, and `run()`.
   It must be host-compilable because host tests reference every Policy.
2. **Backend steps**, in `session/backend/cve_2026_43499_backend.*`: the W1/W2/W3 flow,
   templated on the middleware policy. Do not copy shared steps or retries into a route.
3. **Route class**: the fixed `prepare → execute → disarm → destroy` lifecycle, whose concept is in `route_lifecycle.hpp`.
   Report results only through `status`. Call `execute` only when `prepare` returns 0.

`route/pipeline.hpp` runs the backend first, then hands off to the frontend.
The sole authority for the complete list of places to update when adding middleware, a backend, or a frontend is
`docs/development/adding-a-component.md`. Check every item.

### 2.3 Compilation boundaries and conditional compilation

- Policies, data structures, and pure functions must compile on a non-Android host. Put device-only implementations in
  `#if defined(__ANDROID__)` blocks or include them only in Android builds. Route entry points, backend steps, and
  middleware hook definitions are Android execution code.
- Register host tests in `NATIVE_HOST_TESTS` in `src/Makefile`. Test-only probes must not link into production binaries.

### 2.4 Global state

- `g_exploit_session` is the only process singleton. `g_direct_map_end`, read-only after startup, is also allowed.
  New code must not introduce mutable globals or process-wide reference aliases. See `docs/analysis/native-global-state.md` in git history for the audit criteria.
- Route-level statics are a restricted exception. They must use an anonymous namespace and a singleton accessor, reset to zero after `stop()`, and be explained in the plan.

### 2.5 Ported upstream code and legacy v1

- `kernelsnitch/`, including `futex_hash.h` and the `pr_*` macros in `utils.h`, is ported unchanged from upstream.
  Do not rewrite or modernize it incidentally. Any necessary change requires separate scope and a stated reason.
- Only Kotlin converts v1, the old `offsets.json`, through `LegacyProfileConverter.kt`. Native no longer parses v1.
  New routes and fields must not modify legacy conversion. Convert compatibility input once.

---

## 3. Data-flow standards

### 3.1 Configuration data flow: a single authority

```
assets HOCON (index.conf + <uname-r>.conf + execution-*.conf + shared sections)
   → Kotlin parsing/merging (expand includes, explicit route selection)
   → user override layer (stored sparsely per release, higher priority than built-in and imported profiles)
   → GLK1 binary (passed to native through stdin; exportKernelProfiles generates .bin)
   → native TargetProfile read-only snapshot (the execution layer uses only this from this point on)
```

- The profile is the sole configuration authority. The execution layer may read only process/path environment variables:
  `GHOSTLOCK_HOME`, `TMPDIR`, and `GHOSTLOCK_KSU_LOG`. Extend the profile when new state is needed. Never add configuration environment variables.
- Match `uname -r` exactly. The profile declares the route explicitly. Do not infer it from the kernel version or device name.
- Differences between imported and built-in profiles must be traceable. Compare fields before offering an override.
  A profile's release name and filename must match exactly.

### 3.2 Native data flow

```
TargetProfile (immutable, owns values) → ResolvedAddresses::init() → ResolvedAddresses (authoritative snapshot)
   → WriteRequest (immutable write request: target/mode/preserve-child) → Payload encoding (span bounds)
   → uintptr_t write into the kernel
```

- Distinguish optional fields from explicit 0. GLK1 expresses presence through whether a key exists. Native stores optional fields in `std::optional`.
  Implicit "0 = absent" compatibility is allowed only in a documented, one-time derivation entry point.
- Every profile-driven buffer-size or offset calculation must check for overflow safely and validate the range before use.
  Follow the existing `MulticastGeometry`/`Fields` pattern.

### 3.3 Resource ownership, RAII types

| Type | Purpose | Rule |
|---|---|---|
| `UniqueFd` | Own an fd | Close on scope exit; `BorrowedFd` expresses a borrow and never closes |
| `MappedRegion` | mmap | Own the mapping; extend its lifetime only through an explicit `release_to_process_lifetime()` |
| `PthreadOwner` | Thread | Make join semantics explicit; mark dirty on failure instead of dropping it |
| `ChildProcess` | Child process | Explicit ownership through `VictimContext`; never signal on scope exit; retirement = SIGKILL+reap |
| `ScopeExit` | General cleanup | Use only when there is no more specific owner |
| `PayloadPage` | Kernel page | No destructor because kernel references may outlive it; HeapContext manages its lifetime explicitly |
| `std::expected`/`Result<T>` | Recoverable failure | Use `std::unexpected` for failure; call sites must handle or propagate it |

- New code must not use raw `new/delete` or raw `close/join`. See §5.4 for registered exceptions.
- The design must explain resource cleanup semantics, including user-space release versus disarming dangling kernel references.
  "dirty" is a valid terminal state. State whether resources remain until process exit or can be reclaimed.

---

## 4. Control-flow standards

### 4.1 Attack state machine

```
setup → W1 (SELinux) → W2 (credentials) → W3 (seccomp) → handoff (root child / KernelSU)
```

- Each stage returns a structured result. All retries go through `retry_write_stage()`. Do not implement separate retries in routes or stages.
- W1/W2/W3 target addresses and write modes come from immutable `WriteRequest` values. Stages do not share mutable intermediate state.
  Pass shared state explicitly through `ExploitSession`/`VictimChain`.
- Transfer child-process ownership after handoff explicitly through `release_child()`. If a probe fails, retire the child instead of writing blindly.

### 4.2 PI race-window rules

Within the `waiter_thread → owner_thread → consumer_thread → CMP_REQUEUE_PI → route` window:

- No exceptions, dynamic dispatch through virtual functions or `std::function`, locks, or blocking logging calls.
  Use the existing path with `log_sync()` semantics.
- Allow only direct calls and comparisons of Policy capabilities known at compile time through `route_capability`.
- Any incidental simplification must first pass `cmp_disasm.py`; see §8.2.

### 4.3 Error-handling layers

| Mechanism | Use | Example |
|---|---|---|
| `FatalError` | Fatal errors before the attack only; log at the throw site, exit directly at the top level | Profile validation failure |
| `std::expected`/`Result<T>` | Recoverable failure with errno semantics | I/O, parsing |
| `Status`(bool) | Success or failure with no data | Stage progression |
| `RouteStatus` | Route result: OK / fallback-safe / dirty | Report only through `status` |

- Native is designed with `-fno-exceptions` semantics. Do not throw exceptions other than `FatalError`. Do not use exceptions for recoverable errors.
- Fail early with context. Profile validation must refuse to publish the active profile on failure. KernelSnitch must reject out-of-bounds access.

---

## 5. Data-structure standards

### 5.1 Kernel ABI structures, the highest-risk area

- The GLK1 wire v2 section/key table in `profile/binary.cpp` is the contract authority. Once published, field names, types, and bit patterns must not change.
  Add a field by adding an entry to that section and synchronizing Kotlin `NativeProfile.kt`. Section/entry order does not matter.
- Tests lock down the Kotlin↔Native registries for `RouteKind` and field tables: `route_catalog_test` /
  `RouteCatalogAgreementTest`. Wire IDs, key names, and types must match. Named sections and entries are unordered.
- Give enums explicit wire values and document them in contract comments. If `0` has special semantics, do not treat it as an ordinary value.
  For example, `kRouteAuto` does not appear in Kotlin.

### 5.2 Type design

- Use `PascalCase` and `final` for new types. Use `struct` for aggregates without invariants, and `class` for types with invariants.
- Small value types, such as handles and statuses, use `noexcept` and `[[nodiscard]]`. Add a `static_assert` when they are trivially copyable,
  following the `BorrowedFd` pattern.
- Choose views and containers by purpose: `std::span` for borrowed sequences, `std::array` for fixed sizes, `std::string_view`,
  `std::optional`, and `std::expected`. Do not create variable-length VLAs. The attack path has already replaced all of them with page-bounded buffers.
- Use explicitly sized integers such as `int32_t`/`uint32_t`. Mixing signed and unsigned values requires explicit conversions and must pass `-Wsign-conversion`.

### 5.3 Immutability

- Default to `const`/`constexpr`. Do not defer to runtime what can be decided at compile time. Read-only snapshots,
  `TargetProfile`, `ResolvedAddresses`, and `WriteRequest`, must not change after publication.
- Model potentially mutable data explicitly with mutexes, atomics, or session members. Implicit mutable globals are forbidden.

### 5.4 Registered exceptions, preserve unchanged

The `reinterpret_cast` in `fdset_put/get_word` for the POSIX ABI; `put32/put64` for kernel-layout writes;
getline/free in `iomem_map_span`; no destructor for `PayloadPage`; the global session singleton; the `FIELD` macro in `binary.cpp`;
and ported `kernelsnitch` code. New exceptions must be included in a plan document with reasons.

---

## 6. Code-style standards

### 6.1 C++ (`src/core`)

- Language: C++23, `-fno-rtti`, static libc++, `-O2 -flto`, and `-Wall -Wextra -Wconversion -Wsign-conversion`.
  New code must not introduce warnings. RTTI, exceptions, and nonstandard extensions are forbidden.
- Headers: use `#ifndef GHOSTLOCK_<PATH>_<FILE>_HPP` guards, make headers self-contained, and use `#include` paths relative to `src/core`
  for project headers. Sort within groups alphabetically, following the file's existing style without unrelated reordering.
- Naming: `snake_case` for functions and variables, `PascalCase` for types, `kCamelCase` for compile-time constants, and `name_` for members.
  Macros are limited to the ported headers explicitly preserved.
- Formatting: 4-space indentation, K&R braces, and a line width of about 100. `clang-format` is not enforced, with `FormatStyle: none`,
  but keep each file consistent.
- Comments explain design intent and invariants, the reasons rather than a description of the code. Existing `/* ... */` block comments are examples.
  See the Policy description in `route_policy.hpp`.
- Interfaces: single-argument constructors and conversion operators must be `explicit`, except when intentionally providing value semantics.
  Prefer immutability and compile-time checks through `static_assert`/concepts to runtime checks.
- clang-tidy: `bugprone-*`, `performance-*`, `clang-analyzer-*`, and `--warnings-as-errors='*'`,
  with 0 findings expected. Explain category-wide exclusions in `.clang-tidy` comments. Use inline `NOLINT` for individual false positives.
- Static/global objects: only trivial destruction is allowed. Dynamic initialization requires a clear reason.

### 6.2 Kotlin (`app/`)

- Follow the official Kotlin Coding Conventions: 4 spaces, trailing commas, expression bodies, modifier order, and naming rules.
  Additional project conventions:
  - Restrict visibility with `internal`/`private` by default. Prefer `val` and immutable collections.
  - Profile-related classes follow the `data class` and `entries()/apply()/from()` pattern.
  - Constants and enums synchronized with native name the corresponding native symbol, as in `RouteKind` wire comments.
  - Use KDoc comments to state intent briefly. Do not repeat the signature with `@param`/`@return`.
- Isolate platform I/O in `data/`. Put domain logic in `domain/usecase`. Compose UI must not access processes or files directly.

### 6.3 Rust (`tools/extract_rs`)

- Use `cargo fmt` and `cargo clippy` as the default gates. Handle errors with `Result` and `?`. Do not use `unwrap` on fallible paths.
- CLI `--format conf` outputs a self-contained HOCON profile. `--format json` outputs a v1 import report.
  The default `text` format also outputs JSON. To get HOCON, you must explicitly pass `--format conf`.
  Exit codes carry meaning. For example, preflight rejection is `6`. New CLI options require updates to the README and profile-adaptation documentation.

### 6.4 Commits, branches, and review batches

See §1.5. Each batch has an independently verifiable commit sequence. Commit messages include the changes and verification conclusions.

---

## 7. Documentation standards, summary

[`documentation-standards.md`](documentation-standards.md) is the sole authority for the full rules. Three engineering requirements apply:

- Every reader document throughout the repository must have an English `.md` version and a Chinese `_ZH.md` version. Update both in the same batch.
- Changes to commands, fields, or workflows must update documentation in the same batch.
- Plans and gate records must have complete template fields, including commit and date. Archive failures and passes on the same terms.

---

## 8. Verification and evidence standards

### 8.1 Verification pyramid

1. **Host unit tests**, `make -C src native-host-tests`, 26 tests: fixed vectors, lifecycles, geometry boundaries,
   and agreement between both sides. New data structures and routes require tests registered in `NATIVE_HOST_TESTS`.
   A feature change without corresponding tests is incomplete.
2. **Kotlin unit tests**, `./gradlew :app:testDebugUnitTest`, and **Rust tests**, `cargo test --release`.
3. **Shape comparison**, for attack-path changes only; see §8.2.
4. **Device gate**, §8.3.

### 8.2 Attack-function shape comparison

```sh
python3 tools/cmp_disasm.py <baseline-binary> build/native/ghostlock
```

- Covers 8 attack functions in `TARGETS`. The result must be `IDENTICAL (strict)` or annotation differences that have been reviewed and recorded,
  such as an allowed `LAYOUT-SHIFT` single-address annotation. Add new attack functions to `TARGETS`.
- Put reports and conclusions in the commit message or plan. Historical examples list IDENTICAL for each function in the batch report.

### 8.3 Device gate, required for attack-path changes

**Prerequisites**: cold device, fixed CPU pair, one route, a clean boot without KernelSU loaded, and a record of device state, including uptime.

**Pass criteria**: `route_done status=0 clean=1/1`, `child is root!`, `exploit complete`, handoff
`KernelSU ready`, and no kernel panic. At minimum, repeat the run and record the run count.

**Archive**: use the template in `documentation-standards.md`. Logs correspond to the device's
`Download/ghostlock-debug-log/<时间>/` directory.

### 8.4 Attribution discipline

- `KERNEL-PANIC-01` is a known environment/timing issue. The same build can produce PASS/panic/PASS.
  Attributing it to code requires reproduction with the same build, a cold-device rerun, and stack evidence. Do not attribute a single panic to code.
- Do not claim support for unverified paths. The developer has verified all three paths on devices: Multicast on 5.15, TCP, and Select.
  Do not mark a new profile `supported` before it passes device verification.
- Archive failures on the same terms as passes. Keep fail/panic and pass documents together in the same format.

---

## 9. Agent checklist, design before changes

**Before starting**

- [ ] Read `AGENTS.md`, this document, and current documentation directly related to the task, such as `adding-a-component.md` and PROFILE_SCHEMA
- [ ] Search git history for similar designs and evidence with `git log --all -- <path>`
- [ ] Classify the change as S/M/L; for L, write a plan and obtain user approval first
- [ ] List affected files, changes to data flow, control flow, or data structures, and any required cross-layer synchronization
- [ ] Confirm invariants: profile authority, the global-state list, wire compatibility, and the preservation list

**During implementation**

- [ ] Make the smallest change; do not touch files outside the design, kernelsnitch, `LegacyProfileConverter.kt`, or the preservation list
- [ ] Synchronize both sides: Native↔Kotlin registries and field tables match exactly; update documentation, including `_ZH`
- [ ] Zero warnings in new code; explicit type conversions; RAII ownership; prefer immutability

**After completion**

- [ ] Run all commands required by §1.3 for the change level and keep raw output
- [ ] Attack path: `cmp_disasm`, device gate, and archived gate record
- [ ] Report evidence: commands, results, and file paths; do not write "should be fine"
- [ ] Without an explicit request: do not commit, push, or create a PR

**Prohibited patterns**, based on past failures

- Changes that mix several topics or include incidental refactors
- Changing the attack path without a design, or skipping shape comparison after a change
- Claiming verification passed without evidence, or using a single result to overturn an established attribution
- Deleting historical documents as "outdated junk" without an index; recreating abstractions that already exist

---

## Appendix: external references

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines): P.4 static type safety,
  P.5 prefer compile-time checking, P.8 do not leak resources, P.10 prefer immutability, and P.11 encapsulate messy constructs.
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html): leave ownership evidence at call sites,
  self-contained headers, and trivial-destruction requirements for static/global objects.
- [Kotlin Coding Conventions](https://kotlinlang.org/docs/coding-conventions.html).
- [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/).
- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/).
- [ADR](https://adr.github.io/), [MADR](https://adr.github.io/adr-templates/).
- [Diátaxis](https://diataxis.fr/).
- [AGENTS.md open format](https://agents.md/).
- [Software Engineering at Google](https://github.com/laofo/SoftwareEngineeringAtGoogle).
- [Anthropic: Claude Code best practices](https://www.anthropic.com/engineering/claude-code-best-practices).

Local sources, all searchable in git history: `DECOUPLING_PLAN.md`/`DECOUPLING_LOG.md`, including commit `432a05f`;
`docs/analysis/native-cpp-migration-plan.md`, `docs/development/native-modernization-plan.md`,
`docs/analysis/native-global-state.md`, `docs/analysis/native-entrypoint-plan.md`,
`docs/analysis/environment-convergence-plan.md`, `docs/analysis/device-gates/**`,
`repro/xperia-first-success/README.md`, `docs/analysis/native-warning-audit.md`, and `src/.clang-tidy`.
