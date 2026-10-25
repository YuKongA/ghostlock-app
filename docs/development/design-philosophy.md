English | [简体中文](design-philosophy_ZH.md)

# GhostLock design principles

Read this document before making changes. See the [engineering standards](engineering-standards.md) for practices and verification requirements.

## 1. Confirm device support with device evidence

Host tests check whether code meets its specification. Device tests confirm actual results on the device. Neither replaces the other. Mark judgments without device evidence as assumptions. Do not mark a new profile `supported` until it passes device verification.

## 2. Make contracts explicit

The profile selects the route explicitly. Do not infer it from the kernel version. Preserve the distinction between an absent value and an explicit `0`. GLK1 expresses presence through whether a key exists. Types or interfaces must make the owners, borrowers, and lifetime endpoints of files, threads, and child processes clear.

The route lifecycle is fixed at `prepare → execute → disarm → destroy`. Required conventions must point to a type, field, or test. They cannot depend on verbal agreement.

## 3. Keep one authority for each piece of data

The profile is the configuration authority. The execution layer consumes an immutable `TargetProfile`. `ResolvedAddresses` is read-only after publication. Agreement tests constrain the Native and Kotlin registries. Global state is limited to the session singleton and the upper bound that is read-only after startup.

Before adding state, check whether it belongs in an existing object. State its owner and lifetime.

## 4. Check at compile time where possible

Use types, `constexpr`, and compile-time checks to reject invalid combinations. Keep data structures host-compilable. Machine-code comparison must prove that abstractions in the attack window add no cost. If that cannot be proved, use direct code and explain why. New code must produce zero warnings and zero clang-tidy findings.

## 5. Change only the current batch's scope

Before making changes, list the affected files and the parts that must remain unchanged. Freeze everything outside that list. Do not rewrite ported `kernelsnitch/` code incidentally. Keep legacy v1 separate from new paths. Before a rewrite, define the goal and verification method. Obtain approval before an L-level change.

Split large changes into independently verifiable batches. Verify and record each batch before starting the next.

## 6. Define verification during design

Every L-level design must include a verification matrix that explains how to observe results, compare the baseline, and reproduce conditions. Device records include the commit, device, run count, and log names. Archive failures on the same terms as passes.

## 7. Report uncertainty

One success or failure does not establish a pattern. Attributing `KERNEL-PANIC-01` to code requires reproduction with the same build, a cold-device rerun, and stack evidence. Label unconfirmed results as unverified. Do not hide failures or overstate what passed.

## 8. Validate boundaries and make cleanup explicit

Check profile-driven sizes and offsets for overflow and range before use. Validation failure must prevent publication of the active profile. Do not introduce exceptions, allocations, or indirect calls in the attack window. New parameters must have boundary checks and defined rejection behavior.

Design cleanup separately from disarm. Keep state until process exit when safe reclamation cannot be proved. `dirty` is a valid terminal state. Do not free resources early to make cleanup appear complete.

## 9. Keep decisions and evidence

Before making changes, use `git log --all -- <path>` to find existing designs. Record the context, decision, reasons, alternatives, and consequences. Plans describe intended work. Logs describe what actually happened. Keep them separate.

Historical documents must remain searchable after archival in git. Retain failed experiments as well as successful records so later changes can check them.

## 10. Automate agreement checks

Use builds, tests, or scripts for rules that can be checked mechanically, such as agreement between fields on both sides, clang-tidy, and attack-function shape comparison. When adding an agreement requirement, provide a test, script, or checklist with it. Have someone other than the author review the parts that require judgment.

## 11. Take responsibility for changes

- Read the code and history before defining the scope and the parts to preserve.
- Run the checks that are available. Explain why any check cannot be run.
- Do not include unrelated refactors or adopt new features incidentally. Prove that the change preserves existing behavior.
- Leave decisions, results, and failure records for later maintainers.

## Appendix: mapping principles to practices

| Principle in this document | Practice in `engineering-standards.md` |
|---|---|
| §1 Device-support evidence | §8 Verification and evidence standards: gates, attribution discipline, and V&V layers |
| §2 Explicit contracts | §3 Data flow: presence and a single authority, §2.2 Explicit route selection, §6 Naming and interface contracts |
| §3 Single authority | §3.1 Configuration data flow, §2.4 Global state, §5.1 Wire authority |
| §4 Compile-time checks | §2.3 Compilation boundaries, §5.2 Type design, §8.2 cmp_disasm |
| §5 Change scope | §1.4 Batches and explicit preservation, §2.5 Upstream and legacy code |
| §6 Verification design | §1.3 Requirements, `documentation-standards.md`: plan and gate templates |
| §7 Uncertainty | §8.4 Attribution discipline, §1.5 Commit standards |
| §8 Boundaries and cleanup | §3.2 Overflow and range validation, §4.2 PI-window rules, §4.3 Error-handling layers |
| §9 Decisions and evidence | `documentation-standards.md`: document categories, ADRs, archival, and current-state notes |
| §10 Automated checks | Agreement tests on both sides, clang-tidy policy, `cmp_disasm`, and route templates |
| §11 Responsibility for changes | §9 Agent checklist and prohibited patterns |

## References

Standards: SWEBOK v4.0, ISO/IEC/IEEE 12207, ISO/IEC 25010, IEEE 1012, and ISO/IEC/IEEE 24765.
Practices: [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines),
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
[Google SRE Book Ch.15](https://sre.google/sre-book/postmortem-culture/),
and [Software Engineering at Google](https://github.com/laofo/SoftwareEngineeringAtGoogle).
See the appendix of `engineering-standards.md` for local sources.
