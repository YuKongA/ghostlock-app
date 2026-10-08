# 附件 A1：P2 依赖面全表（48 头 / 59 文件）

> 分析类（**≤8192 B**；与 [A2](contract-layer-p2-spec-annex-a2.md) 由原单件附件按配额拆分）。**配套**：[spec](contract-layer-p2-spec.md) §1 · 判据 = D1。
> 上层已全禁用（`*.disabled` = **194**）；本表是 **P2 冻结输入**：新契约层按本表提供**同名头 + 最小符号集**。

## §A1 依赖面全表（生成物 · 冻结输入）

| 上层头（P2 必须提供同名文件） | 被 backend 包含次数 | 头内职责（原文首句） |
|---|---|---|
| `session/core_session.hpp` | 12 | Process-level state container (ADR-0002). Framework fields stay neutral; 43499-specific state lives in an opaque slot co |
| `support/log.hpp` | 11 | Neutral logging / platform macro layer (A3-2). Moved verbatim out of kernelsnitch/utils.h so that the kernelsnitch provi |
| `contract/identity.hpp` | 10 | ADR-0004 identity vocabulary and interface contracts. This is the contract layer: the stable component ids, the only ava |
| `memory/payload_builder.h` | 9 | Immutable description of one kernel write. `preserve_child` selects the one-child erase layout; false selects the leaf/z |
| `support/decls.hpp` | 7 | Neutral support surface only. The 43499 heap prepare / leak / spray declarations moved to backend/cve_2026_43499/spray.h |
| `support/native_resource.hpp` | 7 |  |
| `memory/target.h` | 6 | Compiled-in target geometry, functionized into ghostlock::memory. The values fall into three groups: address-layout and  |
| `profile/document.hpp` | 6 | Neutral framing result for the GLK1 v2 object-section wire (ADR-0003). Document carries exactly what the container can d |
| `contract/capabilities.hpp` | 5 | Umbrella for the contract capability vocabulary (alpha batch). Authority: docs/analysis/contract-design.md sections 5 an |
| `profile/schema.hpp` | 5 | Owner side of the neutral profile container (ADR-0003 / S4 R1). A Schema declares, per owner, the FieldSpecs it owns and |
| `support/status.hpp` | 5 | Generic success/failure status for functions whose return carries no data. true == success, false == failure. using Stat |
| `contract/stage_result.hpp` | 4 | Outcome of one orchestration stage. Failed maps to exit code 1, Continue proceeds to the next stage and Done stops with  |
| `platform/device_facts.hpp` | 4 | Device/firmware facts the CVE-2026-43284 endgame needs, expressed as an injectable probe contract (B5-7). The backend ne |
| `profile/runtime_struct_offsets.h` | 4 | Active-profile seam (A2-4-3). The frozen transport is owned by the selected backend, which defines these two accessors ( |
| `race/pi_race.h` | 4 | Owns one PI race attempt: the two PI futexes the waiter walks, the three worker threads and the route outcome. The atomi |
| `session/runtime_config.h` | 4 | One runtime snapshot captured once from the environment. Paths own their storage; callers convert to c_str() only at sys |
| `support/timing.hpp` | 4 | The single process-wide exploit timeline. Inline so the hot attack paths keep the exact code shape they had when this li |
| `memory/constants.hpp` | 3 | `select_stack_route.h` keeps a host-safe fallback macro under this name, so a TU that includes it before this header mus |
| `memory/offset.h` | 3 |  |
| `platform/runtime.hpp` | 3 | Apply a rooted run's cached /proc/iomem (System RAM span) to the direct-map end. `home_dir` locates the cache; `release` |
| `support/run_state.hpp` | 3 | Attack step status reporting over stdio, enabled by --enable-status-record. The native process never writes the app's st |
| `memory/direct_map.hpp` | 2 | Direct-map end, built-in or narrowed by a measured iomem dump (defined in memory/address_space.cpp). extern std::uint64_ |
| `memory/heap_context.h` | 2 | Owns the per-attempt mm-shaping child pid and memfd arrays. The vectors free their storage on destruction, but they neve |
| `platform/abi.hpp` | 2 | platform::abi owner schema/view (ADR-0003 / ADR-0004 R5, A2-4-3). Platform ABI: the task_struct layout offsets, the stru |
| `profile/glkv3.hpp` | 2 | GLKv3 wire codec (MessagePack / MPack, no magic prefix). Design: docs/analysis/wire-transport-model.md. GLKv3 is a bare  |
| `profile/registry.hpp` | 2 | S4 R1 schema registry: selection -> owner schemas + default-aware bind. SchemaRegistry<Schemas...> is a compile-time own |
| `race/threads.hpp` | 2 | Thread entry points registered with PiRace::start_threads(). void \*waiter_thread(void \*arg); void \*owner_thread(void \*ar |
| `support/fatal_error.hpp` | 2 | Thrown only on pre-attack fatal errors where execution must stop. The throwing site logs the details; the top-level hand |
| `terminal/handoff_probe.hpp` | 2 | Post-exploit KernelSU handoff probe results. The field semantics stay the legacy booleans so the caller keeps the exact  |
| `terminal/root_program.hpp` | 2 | Compatibility header: the neutral RootProgram spec is identity/interface vocabulary and now lives in contract/identity.h |
| `terminal/rooted_child.hpp` | 2 | Neutral, move-only handle to one rooted child handed from a backend to a terminal action (ADR-0004 R10). The backend tra |
| `terminal/terminal_input.hpp` | 2 | Concrete terminal input payloads. The neutral base (TerminalInput, ActivationContext, RootProgram) is identity/interface |
| `contract/abi/glk_contract_abi.h` | 1 | GhostLock countermeasure plugin ABI, version 1 (CM-1). This header is the complete, stable contract between the GhostLoc |
| `contract/address_discovery.hpp` | 1 | Optional address-discovery capability (ADR-0004 T3 / ADR-0001 section 17). Discovery runs before any attack write (R13)  |
| `contract/model.hpp` | 1 | Neutral frozen data vocabulary (ADR-0004 R1 / A2-4-4). Physically relocated from profile/model.h. kernel_offsets, Target |
| `contract/step_catalog.hpp` | 1 | Step vocabulary for the step-queue design (docs/analysis/step-queue-design.md section 4.1; M1). M1 SCOPE (hard boundary) |
| `memory/address_space.h` | 1 | Neutral bootstrap inputs the address space needs, decoupled from the backend profile type (ADR-0004 R1: memory must not  |
| `plugin/controller.hpp` | 1 | Invoke fn<P>() for every policy in the caller-supplied list. The list is a std::tuple of policy types; the mechanism its |
| `plugin/host.hpp` | 1 | P1 step 2: the plugin host -- register -> window-gated load -> stage dispatch -> unload (design docs/analysis/plugin-run |
| `plugin/host_ops.hpp` | 1 | delta-4: adapt the per-run contract::Capabilities view into the C glk_contract_ops function table an out-of-tree plugin  |
| `plugin/kernel_channel.hpp` | 1 | LKM versioned request channel (delta batch). Authority: docs/analysis/contract-design.md sections 3.12, 3.12.1, 3.12.2 a |
| `plugin/registry.hpp` | 1 | CM-3: runtime registry for out-of-tree countermeasure hooks. TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无 |
| `session/runtime_paths.h` | 1 | Host-safe runtime path normalization shared by RuntimeConfig. Only <string>/<string_view> are used, so the exact legacy  |
| `support/sha256.hpp` | 1 | Single SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104) implementation. S4 R8: the tree used to carry two copies -- plugi |
| `support/time.h` | 1 |  |
| `terminal/root_script.hpp` | 1 | Write the root-side KernelSU handoff script to the configured path. safe_mode comes from the caller (ADR-0006 T1): the t |
| `terminal/umh_command.hpp` | 1 | Neutral UMH argv spec (ADR-0004 R1/R19). The cve_2026_43284 backend builds it (lkm::build_late_load_command) and the fut |
| `terminal/umh_forward.hpp` | 1 | umh_forward terminal execution policy (B5-8 / B6-T5). Read-only readiness terminal. The cve_2026_43284 backend fills ter |

合计：**48 个上层头**，被 **59 个 backend 文件** 包含。

- **生成方法**：见 [A2 §A3](contract-layer-p2-spec-annex-a2.md)（解析 `backend/**` 的 `#include` 后聚合）。
- **口径（U-1，**已结清**）**：上层头 = **48** —— 去重 include **102** 个，其中非 backend/lib **52** 个，减去位于 `backend/cve_2026_43499/leak/` 内部的 4 个自有头 = **48**；命令见 [A2 §A3](contract-layer-p2-spec-annex-a2.md)。
