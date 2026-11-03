# 43499 上游 document key → 当前 kernel_offsets / runtime_config 映射（C1 前置物）

> 状态：v1（2026-11-02），只读分析。**唯一交付物 = 本文件**；未改代码、未跑 Gradle/设备、未 commit。
> 范围：[flow-process-launcher-annex-config-document-port.md](../plan/flow-process-launcher-annex-config-document-port.md) §2 / C1 的阻断项（映射表）+ §C2a 的行为差异表 + C1 golden 清单。
> 结论（先行）：**完整 C1 不具备写码条件**。C1-reader（mpack + `glkv3_document` + `document_pairs` + `payload_keys` + `key_catalog` + G1–G4）可以立即开工；C1-binder（Document → 当前 `kernel_offsets`/`runtime_config`）被 6 项裁决阻断（U1–U6）。按计划 §2/§6 的“无法裁决 ⇒ 停下报 Lead”，C1 应在 Lead 裁定后再写 binder。详见 §8。

---

## 0. 摘要

| 项 | 结果 |
|---|---|
| 上游键总数（本映射范围） | **104** = `cve_2026_43499` owner 101 键 + `root` owner 3 键（manifest 另含 6 个 `plugin`、9 个 `cve_2026_43284` 键，不在 43499 映射范围） |
| 当前 GLK1 wire 键 | **84**（native 定义 14 个 section，单个文档最多 12 个；83 个唯一目标，`compact_waiter` 在 `kernel` 与 `route.select_stack` 各出现一次） |
| 精确映射到现有字段 | **82/104 = 78.8%**（80 个 43499 键 + `root.kernel_major` + `root.safe_mode`；`safe_mode` 语义有争议） |
| 无争议、可直接绑定 | **81/104 = 77.9%** |
| 需裁决 | **23/104 = 22.1%**：1 别名、1 `safe_mode` 语义、15 常量键（无字段）、5 无对应、1 选择 token 歧义 |
| payload family | 上游 4 键 + 名称/kind/长度/摘要规则；**当前完全没有 payload 载体**（GLK1 only u64），需要 C2 新增，C1 无可绑定目标 |
| 高风险项 | 6 项（§5.4），最高为 **runtime 路径未合并 execution 默认值 ⇒ execution 参数（除 `route_done_timeout_ms` 的 native 300000 回退）为 0**（W1/W2/W3/handoff 全部受影响） |

---

## 1. 方法与来源

### 1.1 只读来源（git 历史）

所有历史文件均用 `git show <commit>:<path>` 读取；行号用 `git show <commit>:<path> | nl -ba` 核对，**行号属于该历史版本文件**。

| 短名 | commit | 文件 |
|---|---|---|
| KC | `2933b838`（2.0） | `core/src/profile/key_catalog.hpp`（172 行；manifest 的生成源，119 行数据 = 101 个 43499 行 + 3 个 root 行 + 15 个其它 owner 行） |
| DOC | `2933b838` | `core/src/profile/glkv3_document.{hpp,cpp}` |
| DP | `2933b838` | `core/src/profile/document_pairs.{hpp,cpp}` |
| PK | `2933b838` | `core/src/profile/payload_keys.{hpp,cpp}` |
| PL | `2933b838` | `core/src/profile/{layer.hpp,route_catalog.hpp}` |
| LFD | `2933b838` | `core/src/backend/cve_2026_43499/layout_from_document.hpp`（设计前置，**未被调用**，见 §1.4） |
| PM | `2933b838` | `core/src/backend/cve_2026_43499/steps/prepare_mapping.hpp` |
| REK | `2933b838` | `core/src/backend/cve_2026_43499/route/expected_keys.hpp` |
| RP | `2933b838` | `core/src/profile/run_plan.cpp`（C1 明确不搬，仅作契约对照） |
| PD | `2933b838` | `core/src/profile/plan_delivery.{hpp,cpp}`（C1 不搬） |
| SC | `2933b838` | `core/src/contract/step_catalog.hpp` |
| KNP | `d2778a1a`（2.0.5） | `profile-core/src/main/kotlin/com/ghostlock/app/data/NativeProfile.kt` |
| KAD | `d2778a1a` | `.../data/profile/NativeProfileGlkv3Adapter.kt` |
| KRS | `d2778a1a` | `.../data/profile/ProfileResolver.kt` |
| KMR | `d2778a1a` | `.../data/profile/ProfileMerger.kt` |
| KDEC | `d2778a1a` | `.../data/profile/Glkv3Decoder.kt` |
| KENC | `d2778a1a` | `.../data/profile/Glkv3Encoder.kt` |
| KHP | `d2778a1a` | `.../data/profile/HandoffPayload.kt` |
| KCH | `d2778a1a` | `.../data/profile/ChannelBStdin.kt` + `SessionSecretFrame.kt` |
| MAN | `d2778a1a` | `profile-core/src/main/resources/profile-manifest-v3.tsv`（与 KC 同内容，119 行数据） |
| FIX-TSV | `d2778a1a` | `profile-core/src/test/resources/glkv3-native-fixture.tsv`（90 行，含 2 行注释；8 root + 80 entry） |
| FIX-DEV | `d2778a1a` | `profile-core/src/test/resources/device-document-fixture.hex` |
| FIX-PAY | `d2778a1a` | `profile-core/src/test/resources/payload-document-fixture.hex` |
| GOLD | `d2778a1a` | `profile-core/src/test/kotlin/.../Glkv3Golden.kt`（`SELECTION_HEX`/`NO_SELECTION_HEX`） |

### 1.2 当前工作树（HEAD `b1190859`，脏树；本文件只读）

| 短名 | 文件 |
|---|---|
| BIN | `app/src/main/cpp/43499glued/profile/binary.cpp` |
| BINH | `app/src/main/cpp/43499glued/profile/binary.h` |
| MH | `app/src/main/cpp/43499glued/profile/model.h` |
| ENTRY | `app/src/main/cpp/43499glued/profile/entry.{h,cpp}` |
| RSO | `app/src/main/cpp/43499glued/kernel/runtime_struct_offsets.h` |
| TH / TC / KCONST | `app/src/main/cpp/43499glued/kernel/target.h` / `target_constants.hpp` / `constants.hpp` |
| RC | `app/src/main/cpp/43499glued/session/runtime_config.{h,cpp}` |
| ACC | `app/src/main/cpp/43499glued/profile/accessors.hpp` |
| OPS | `app/src/main/cpp/43499glued/attack/ops.cpp` |
| BACKEND43499 | `app/src/main/cpp/43499glued/session/backend/cve_2026_43499_backend.cpp` |
| FE | `app/src/main/cpp/43499glued/session/root_child_frontend.cpp` |
| ROUTEPOL | `app/src/main/cpp/43499glued/route/route_policy.hpp` |
| MCAST | `app/src/main/cpp/43499glued/route/multicast_waiter_route.cpp` |
| UTIL | `app/src/main/cpp/43499glued/support/util.cpp` |
| MAIN | `app/src/main/cpp/43499glued/main.cpp` |
| NPT | `app/src/main/kotlin/com/ghostlock/app/data/NativeProfile.kt` |
| PROF | `app/src/main/kotlin/com/ghostlock/app/data/Profile.kt` |
| PRT | `app/src/main/kotlin/com/ghostlock/app/data/profile/ProfileResolver.kt` |
| PMT | `app/src/main/kotlin/com/ghostlock/app/data/profile/ProfileMerger.kt` |
| PEX | `app/src/main/kotlin/com/ghostlock/app/data/profile/ProfileExporter.kt` |
| CODEC | `app/src/main/kotlin/com/ghostlock/app/backend/cve43499glued/Glued43499ProfileCodec.kt` |
| PARAMS | `app/src/main/kotlin/com/ghostlock/app/backend/cve43499glued/Glued43499ExecParams.kt` |
| FLOW | `app/src/main/kotlin/com/ghostlock/app/backend/cve43499glued/Flow43499GluedEntry.kt` |
| CAT | `app/src/main/kotlin/com/ghostlock/app/data/BuiltinProfileCatalog.kt` |
| ASSET | `app/src/main/assets/kernel_profiles/<release>.conf` |

### 1.3 复现命令（可核验）

```bash
# 上游键表与行号（第 56–156 行 = 101 个 43499 键；163–165 = root 键）
git show 2933b838^:core/src/profile/key_catalog.hpp | nl -ba | sed -n '56,165p'
# 上游读键语义
git show 2933b838^:core/src/profile/glkv3_document.cpp | nl -ba | sed -n '50,110p;215,305p'
git show 2933b838^:core/src/profile/document_pairs.cpp | nl -ba | sed -n '7,46p'
# 当前 GLK1 字段表 / 目标结构体
nl -ba app/src/main/cpp/43499glued/profile/binary.cpp | sed -n '61,212p'
nl -ba app/src/main/cpp/43499glued/profile/model.h | sed -n '18,147p'
# 当前 Kotlin 解析/合并
nl -ba app/src/main/kotlin/com/ghostlock/app/data/profile/ProfileResolver.kt | sed -n '12,152p'
nl -ba app/src/main/kotlin/com/ghostlock/app/data/profile/ProfileMerger.kt | sed -n '23,118p'
# fixtures 解码（本文件用一次性 python msgpack 解码，未写入仓库）
git show d2778a1a^:profile-core/src/test/resources/device-document-fixture.hex
```

### 1.4 证据边界（重要）

1. 两个参考 commit 是**迁移中间态**，彼此不完全一致；本文件把“上游键的权威”取为 `key_catalog.hpp`/manifest，把“上游读取语义”取为 `glkv3_document.*` + `document_pairs.*` + 各 consumer，并对矛盾处显式标注，不替任何一侧圆场。
2. `LFD`（`layout_from_document.hpp`）在 `2933b838^` 中**没有任何调用点**（`git grep layout_from_document` 仅命中自身）。它声明 abi.payload/abi.kernel/abi.offset “缺即拒绝”，但**实际在跑**的 `run_pipeline_temp.cpp` 对 abi.payload 缺失时使用“declared temporary source”常量（`run_pipeline_temp.cpp:437-521`）。因此“上游要求 payload 必填”是设计意图，不是当时的生产事实。
3. `d2778a1a^` 的 Kotlin 参考快照自相矛盾（静态对照，未运行 Gradle）：
   - `KAD:340-348` 令 `combination=token`、`backend/route/terminal=null`；而 `KAD` 自己的测试要求 `adapted.terminal=="root_child"`、`backend=="cve_2026_43499"`、`route=="multicast_waiter"`（`NativeProfileGlkv3AdapterTest.kt:53-58`），fixture TSV 也带这三个 root 键（FIX-TSV 第 2–9 行）。
   - `SELECTION_HEX` 有 8 个 root 键（backend/route/terminal/kernel_minor/safe_mode/release/schema/kernel_major），**没有 `combination`**；按 `KAD` 现码编码必然写出 `combination` 且缺 backend/route/terminal ⇒ 字节 golden 不可能同时成立。
   - `KDEC:49-61` 的 root 白名单没有 `combination`，`else -> return null` ⇒ `KAD` 输出无法被 `Glkv3Decoder.decode` 读回，`patchSafeMode`（`KDEC:89-92`）对自家文档返回 null。
   这三点直接影响 U1（root 选择契约），C1/C2 不能照抄。
4. 本文件中的覆盖率/计数由源文件机械统计（`grep`/`awk`/一次性 python 解码），不是估算。

---

## 2. 键空间与规则代码

### 2.1 计数

| 集合 | 数量 | 证据 |
|---|---|---|
| `cve_2026_43499` manifest 行 | 101 | KC:56-156；`awk` 计数 101 |
| `root` manifest 行 | 3 | KC:163-165 |
| 本映射范围合计 | 104 | — |
| 当前 GLK1 字段条目 | 84（83 唯一目标） | BIN:61-189；`kTask` 15、`kCred` 15、`kOffset` 10、`kMeta` 4、`kKernel` 5、execution 24（tcp/select 的 5 个 tuning 落在 route 段）、route 11 |
| 当前 Kotlin 可产出的 GLK1 section | 最多 12（10 固定 + 可选 `kernel` + 1 个 active route） | BIN:197-212；NPT:74-157 |
| payload family 键 | 4（`bin`,`sha256`,`size`,`kind`） | PK:23 |

分类（104 键）：

| 类别 | 数量 | 键 |
|---|---|---|
| A 精确映射到现有字段 | 82 | §3 中规则非 `K32/NONE/SEL/ALIAS` 的行；其中 `safe_mode` 标 ⚠（语义争议） |
| B 别名/重复 | 1 | `execution.heap.kernelsnitch_collisions` → 与 `kernel.kernelsnitch_collisions` 同目标 |
| C 只能映射到编译期常量 | 15 | `abi.payload.*`×5；`execution.heap.snitch_{round_wait_us,threads,wake_rounds}`×3；`route.multicast_waiter.{attempts,arm_sequence,arm_hold}`×3；`route.tcp_zerocopy.{chunk_bias,cred_copy_off,fake_task_off,payload_delta}`×4 |
| D 当前无对应概念 | 5 | `root.kernel_minor`、`offset.vr_sys_exit_tp`、`experimental`、`queue`、`steps` |
| E 选择 token 歧义 | 1 | `backend.cve_2026_43499.route`（str，queue-level route token） |

### 2.2 规则代码（表内使用）

| 代码 | 转换 | 当前缺失动作 | 当前未知动作 | 目标宽度 |
|---|---|---|---|---|
| `I32` | 恒等，无符号 32 位 | native→0；Kotlin 必填组见 §3（`KF`） | native 忽略（BIN:297-324） | u32 |
| `I64` | 恒等，无符号 64 位 | native→0 | 忽略 | u64 |
| `S32` | 恒等，有符号 32 位（two's complement） | native→0 | 忽略 | int32 |
| `B8` | bool/数值 → u8：0/1；消费端按 `!=0` 判真 | native→0（视作 false） | 忽略 | u8 |
| `O64/O32/O8` | 写入 `std::optional<...>`；缺 → 未提供（`has_value()==false`） | 保持未提供 | 忽略 | u64/u32/u8 |
| `K32` | 无 profile 字段；当前由编译期常量提供 | 常量 | n/a | 常量 |
| `NONE` | 当前无对应概念 | n/a | n/a | — |
| `SEL` | 只能落到 `kernel_offsets.route`（u8，route catalog），来源待裁决 | `Auto=0` | n/a | u8 |
| `ALIAS` | 与同组另一键写同一字段（后写覆盖） | 同目标 | 忽略 | 同目标 |
| `KF` | 该键（或所在组）被当前 Kotlin 校验要求存在且非零/为 Number | 缺→当前 Kotlin 校验 fail（`ConfigError`）；native 仍 0 | 忽略 | 同目标 |

缺失动作的“Kotlin 必填”以 `PRT:17-28,101-132` 为准：
`task_struct` 15 字段、`cred.{copy_size,caps_count}`（且非零）、`offset.{init_task,init_cred,root_task_group,selinux_enforcing}`（且非零）为必填；其余当前无必填校验，缺即 0/absent。
**未知动作的全局事实**：上游 native `Document` 只按名字读，**没有未知 key/section 拒绝**（DOC 全文无遍历校验）；“strict 未知拒绝”只存在于 Kotlin adapter（`KAD:175-210,290-320` 对未声明 path `error()`）。所以 C1 若要“表中未列出的 key 不得静默放行”，必须在 **C1 新 binder 里新建该检查**，不能声称从上游 native 继承。

---

## 3. 映射表

> 读法：`w/req/def` = manifest 的 wire/required/default 列（KC 全为 `required=0`）；`当前 GLK1 path` = `section.key`；`目标` = 当前结构体字段；`规则` = §2.2 代码 + 必要说明；`缺` = 当前缺失动作（`KF`/`0`/`OPT`/`n/a`，见 §2.2）；`证据` = KC 行 + 当前 file:line。所有 43499 行 `required=0`、`default=-`（manifest 语义），但消费者可能另有必填（见各节注）。
> **未知动作（所有行相同，故不逐行重复）**：当前 native 忽略未知 section/key（BIN:297-324）；C1 的“strict 未知拒绝”是**新增**行为，粒度由 U2 裁决。

### 3.0 总览

| 上游 section | 键数 | 当前 GLK1 section | 目标结构体 | 结论 |
|---|---|---|---|---|
| `root`（owner=root） | 3 | `meta` + header | `ProfileMeta` / `kernel_offsets.route` | 2 映射 1 无 |
| `abi.task_struct` | 15 | `task_struct` | `TaskStructOffsets` | 全部映射 |
| `abi.cred` | 7 | `cred` | `CredTemplate`（与 `cred` 合并） | 全部映射 |
| `abi.offset` | 7 | `offset` | `KernelOffsets` | 全部映射 |
| `abi.kernel` | 2 | `kernel` | `KernelMisc` | 映射但可选性不同 |
| `abi.payload` | 5 | — | 编译期 `target::payload::*` | 无字段（C） |
| `cred` | 8 | `cred` | `CredTemplate` | 全部映射 |
| `kernel` | 3 | `kernel` | `KernelMisc` | 全部映射（1 别名） |
| `offset` | 4 | `offset` | `KernelOffsets` | 3 映射 1 无 |
| `execution.heap` | 7 | `execution.heap` | `execution_settings` + 常量 | 3 映射 1 别名 3 常量 |
| `execution.race` | 4 | `execution.race` | `execution_settings` | 全部映射 |
| `execution.stages` | 8 | `execution.stages` | `execution_settings` | 全部映射 |
| `execution.handoff` | 5 | `execution.handoff` | `execution_settings` | 全部映射 |
| `execution.consumer` | 2 | `execution.consumer` | `execution_settings` | 全部映射 |
| `execution.recommended_cpus` | 2 | `execution.recommended_cpus` | `execution_settings` + `RuntimeConfig` | 全部映射（语义不同） |
| `route.multicast_waiter` | 7 | `route.multicast_waiter` | `RouteGeometry` | 4 映射 3 常量 |
| `route.select_stack` | 4 | `route.select_stack` | `RouteGeometry`+`KernelMisc`+`execution_settings` | 全部映射 |
| `route.tcp_zerocopy` | 7 | `route.tcp_zerocopy` | `execution_settings` + 常量 | 3 映射 4 常量 |
| `experimental/queue/route/steps` | 4 | — / header | — / `kernel_offsets.route` | 1 歧义 3 无 |

### 3.1 owner=`root`（3 个 manifest 键 + 5 个非 manifest root 键）

| 上游 key | w/req/def | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `root.kernel_major` | uint/0/- /1 | `meta.kernel_major` | `meta.kernel_major`（u8） | `I32`→u8（值域 0..6） | `0` | KC:163；BIN:62；MH:82；NPT:355 |
| `root.kernel_minor` | uint/0/- /1 | — | 无 | `NONE`；上游 KNP:32/564 有 `kernelMinor`，当前 Kotlin/native 均无此字段 | `n/a` | KC:164；NPT 无 kernelMinor；MH 无字段 |
| `root.safe_mode` ⚠ | bool/0/- /1 | `meta.safe_mode` | `meta.safe_mode`（u8） | `B8`，但当前 **Kotlin 恒写 0**（NPT:435），仅 `patchSafeMode` 能置 1 且无调用者（NPT:245；全仓 grep 无调用） | `0`（恒 false） | KC:165；BIN:65；MH:85；OPS:322,417 |
| `release`（非 manifest） | str | header `release` | `kernel_offsets.uname_r` | 恒等；GLK1 上限 0xffff（BIN:342），上游编码上限 256（KENC:120） | 空→解析拒绝（BIN:341-343） | NPT:38-40；BIN:267-274 |
| `backend`（非 manifest） | str | header `backend_id`（固定 1） | `component_ids.backend` | 上游是 token；当前是 u16 枚举且**编码端固定写 1**（NPT:53） | 固定 | NPT:53；BINH:36；BIN:263 |
| `terminal`（非 manifest） | str | —（固定 `root_child`） | `component_ids.frontend` | 上游由 combination 派生；当前固定 frontend=1（NPT:52） | 固定 | 上游 RP:70-90；BINH:34；NPT:52 |
| `route`（非 manifest） | str | header `route`（u16） | `kernel_offsets.route` | `SEL`：token→wire 1/2/3；0=Auto 被 `serialize` 拒绝（BIN:340），parse 也拒绝 0（BIN:278-282） | 解析拒绝 | 上游 KNP:437/routeKind；BIN:275-282；MH:40-54 |
| `combination`（非 manifest） | str | — | 无 | `NONE`；上游 `RP:70` 只读此键，而 `KAD:340` 才写它、golden 却写 backend/route/terminal——**U1** | n/a | RP:70；KAD:340-348；FIX-TSV:2-9 |

**节注**：当前 `meta` 还携带 `recommend_shizuku`（BIN:63）与 `fallback_route`（BIN:64），上游 manifest 没有这两个 key（见 §3.10）。

### 3.2 `backend.cve_2026_43499.abi.task_struct`（15）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `prio` | 4 | `task_struct.prio` | `task.prio` | `I32` | `KF` | KC:86；BIN:69；MH:89 |
| `normal_prio` | 4 | `task_struct.normal_prio` | `task.normal_prio` | `I32` | `KF` | KC:80；BIN:69；MH:89 |
| `sched_task_group` | 4 | `task_struct.sched_task_group` | `task.sched_task_group` | `I32` | `KF` | KC:88；BIN:70；MH:89 |
| `pi_lock` | 4 | `task_struct.pi_lock` | `task.pi_lock` | `I32` | `KF` | KC:82；BIN:71；MH:90 |
| `pi_waiters` | 4 | `task_struct.pi_waiters` | `task.pi_waiters` | `I32` | `KF` | KC:84；BIN:71；MH:90 |
| `pi_top_task` | 4 | `task_struct.pi_top_task` | `task.pi_top_task` | `I32` | `KF` | KC:83；BIN:72；MH:90 |
| `pi_blocked_on` | 4 | `task_struct.pi_blocked_on` | `task.pi_blocked_on` | `I32` | `KF` | KC:81；BIN:73；MH:90 |
| `pid` | 4 | `task_struct.pid` | `task.pid` | `I32` | `KF` | KC:85；BIN:74；MH:91 |
| `tgid` | 4 | `task_struct.tgid` | `task.tgid` | `I32` | `KF` | KC:91；BIN:74；MH:91 |
| `atomic_flags` | 4 | `task_struct.atomic_flags` | `task.atomic_flags` | `I32` | `KF` | KC:77；BIN:75；MH:91 |
| `real_cred` | 4 | `task_struct.real_cred` | `task.real_cred` | `I32` | `KF` | KC:87；BIN:76；MH:92 |
| `cred` | 4 | `task_struct.cred` | `task.cred` | `I32` | `KF` | KC:79；BIN:76；MH:92 |
| `comm` | 4 | `task_struct.comm` | `task.comm` | `I32` | `KF` | KC:78；BIN:77；MH:92 |
| `tasks` | 4 | `task_struct.tasks` | `task.tasks` | `I32` | `KF` | KC:90；BIN:77；MH:92 |
| `seccomp` | 4 | `task_struct.seccomp` | `task.seccomp` | `I32` | `KF` | KC:89；BIN:78；MH:92 |

**节注**：上游 document 读取器实际只强制 `pi_lock/pi_waiters/pi_top_task/pi_blocked_on` 四个（`abi.hpp:34-36` + `abi.cpp:38-66` 的 all-or-nothing），其余由旧 JSON 管线读取；当前 Kotlin 反而**全部 15 个必填**（PRT:20-24,101-103）。当前 native 消费：`RSO:105-169`（fake task 布局 + W2/W3 的 pid/tgid/comm/cred/seccomp）。宽度 4=u32 一致，无单位换算。

### 3.3 `abi.cred`（7）+ `cred`（8）→ 同一个当前 `cred` section

> 上游把 ABI 偏移（`abi.cred`）与凭据值（`cred`）分成两个 section；当前 GLK1 合并为一个 `cred`（BIN:81-97）。C1 binder 必须把两个上游 section 写进同一个 `CredTemplate`。

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `abi.cred.usage_offset` | 4 | `cred.usage_offset` | `credential.usage_offset` | `I32` | `0` | KC:67；BIN:83；MH:96 |
| `abi.cred.caps_offset` | 4 | `cred.caps_offset` | `credential.caps_offset` | `I32` | `0` | KC:56；BIN:85；MH:97 |
| `abi.cred.ref_count` | 4 | `cred.ref_count` | `credential.ref_count` | `I32` | `0` | KC:66；BIN:88；MH:99 |
| `abi.cred.ref0_offset` | 4 | `cred.ref0_offset` | `credential.ref0_offset` | `I32` | `0` | KC:57；BIN:89；MH:100 |
| `abi.cred.ref1_offset` | 4 | `cred.ref1_offset` | `credential.ref1_offset` | `I32` | `0` | KC:58；BIN:90；MH:100 |
| `abi.cred.ref2_offset` | 4 | `cred.ref2_offset` | `credential.ref2_offset` | `I32` | `0` | KC:59；BIN:91；MH:100 |
| `abi.cred.ref3_offset` | 4 | `cred.ref3_offset` | `credential.ref3_offset` | `I32` | `0` | KC:60；BIN:92；MH:100 |
| `cred.copy_size` | 4 | `cred.copy_size` | `credential.copy_size` | `I32`；运行时必须 >0 且容纳 usage/caps/refs（UTIL:193-221） | `KF`（非零） | KC:94；BIN:82；MH:96 |
| `cred.caps_count` | 4 | `cred.caps_count` | `credential.caps_count` | `I32` | `KF`（非零） | KC:92；BIN:86；MH:97 |
| `cred.usage_value` | 4 | `cred.usage_value` | `credential.usage_value` | `I32` | `0` | KC:99；BIN:84；MH:97 |
| `cred.caps_value` | 8 | `cred.caps_value` | `credential.caps_value` | `I64` | `0` | KC:93；BIN:87；MH:98 |
| `cred.ref0_image` | 8 | `cred.ref0_image` | `credential.ref0_image` | `I64` | `0` | KC:95；BIN:93；MH:101 |
| `cred.ref1_image` | 8 | `cred.ref1_image` | `credential.ref1_image` | `I64` | `0` | KC:96；BIN:94；MH:101 |
| `cred.ref2_image` | 8 | `cred.ref2_image` | `credential.ref2_image` | `I64` | `0` | KC:97；BIN:95；MH:101 |
| `cred.ref3_image` | 8 | `cred.ref3_image` | `credential.ref3_image` | `I64` | `0` | KC:98；BIN:96；MH:101 |

**节注**：`refN_image` 上游 manifest 为 uint 8；上游资产里出现**负数**（如 `-274698454400`，`2933b838^:app/src/main/assets/profile/5.15.189-...conf:74-77`），Kotlin 侧以 `ULong` 承载 two's complement；当前 GLK1 也是 u64 位容器，转换恒等。C1 binder 用 `uint64_t` 接收即可，但**不得按符号判负拒绝**。宽度 4/8 与当前 u32/u64 一致。

### 3.4 `abi.offset`（7）+ `offset`（4）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `abi.offset.init_task` | 8 | `offset.init_task` | `offsets.init_task` | `I64`；消费端再转 image 地址（RSO:28-33） | `KF`（非零） | KC:72；BIN:100；MH:105 |
| `abi.offset.init_cred` | 8 | `offset.init_cred` | `offsets.init_cred` | `I64` | `KF`（非零） | KC:71；BIN:101；MH:105 |
| `abi.offset.empty_zero_page` | 8 | `offset.empty_zero_page` | `offsets.empty_zero_page` | `I64` | `0` | KC:70；BIN:102；MH:105 |
| `abi.offset.root_task_group` | 8 | `offset.root_task_group` | `offsets.root_task_group` | `I64` | `KF`（非零） | KC:73；BIN:103；MH:106 |
| `abi.offset.selinux_enforcing` | 8 | `offset.selinux_enforcing` | `offsets.selinux_enforcing` | `I64` | `KF`（非零） | KC:76；BIN:104；MH:106 |
| `abi.offset.selinux_blob_sizes` | 8 | `offset.selinux_blob_sizes` | `offsets.selinux_blob_sizes` | `I64` | `0` | KC:75；BIN:105；MH:107 |
| `abi.offset.security_hook_heads` | 8 | `offset.security_hook_heads` | `offsets.security_hook_heads` | `I64`；0 合法（`TH:41` 默认 0） | `0` | KC:74；BIN:106；MH:107 |
| `offset.slide_nfulnl_logger` | 8 | `offset.slide_nfulnl_logger` | `offsets.slide_nfulnl_logger` | `I64` | `0` | KC:134；BIN:107；MH:108 |
| `offset.slide_loggers_0_1` | 8 | `offset.slide_loggers_0_1` | `offsets.slide_loggers_0_1` | `I64` | `0` | KC:133；BIN:108；MH:108 |
| `offset.slide_boot_id` | 8 | `offset.slide_boot_id` | `offsets.slide_boot_id` | `I64` | `0` | KC:132；BIN:109；MH:108 |
| `offset.vr_sys_exit_tp` | 4 | — | 无 | `NONE`；上游 KNP 只在非 0 时发射（KNP:403），manifest 宽度写 4 而 Kotlin 类型是 `ULong`（KNP 无字段类型行，见 `KernelOffsetTable.vrSysExitTp:ULong`） | `n/a` | KC:135；KNP:403；MH:104-109 |

**节注（宽度陷阱）**：上游 manifest 这 10 个符号偏移都是 **8 字节**；当前 wire 也是 u64，但运行时取值路径 `RSO:14-26` 把 `uint64_t` **截断到 `uint32_t`**（`static_cast<uint32_t>(get(*values))`）。当前所有已知 release 的符号偏移 < 4 GiB，所以未暴露；C1 若要求“宽度不匹配必须失败”，必须在 binder 里补 2^32 上限检查，不能沿用当前 accessor。

### 3.5 `abi.kernel`（2）+ `kernel`（3）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `abi.kernel.kernel_phys_load` | 8 | `kernel.kernel_phys_load` | `misc.kernel_phys_load`（`optional<u64>`） | `O64`；缺→编译期 `P0_KERNEL_PHYS_LOAD` 等分支（`memory/address_space.cpp:87-102`） | `OPT` | KC:68；BIN:113；MH:112 |
| `abi.kernel.kernel_phys_offset` | 8 | `kernel.kernel_phys_offset` | `misc.kernel_phys_offset`（`optional<u64>`） | `O64`；缺→`TH:21 P0_PHYS_OFFSET` | `OPT` | KC:69；BIN:114；MH:117 |
| `kernel.compact_waiter` | 1(bool) | `kernel.compact_waiter` | `misc.compact_waiter`（`optional<u8>`） | `B8`；消费 `value_or(0)!=0`（MH:269-271） | `OPT` | KC:129；BIN:115；MH:118 |
| `kernel.kernelsnitch_collisions` | 4 | `kernel.kernelsnitch_collisions` | `misc.kernelsnitch_collisions`（`optional<u32>`） | `I32`；`ACC:9-13` 缺省 4 | `OPT`（缺省 4） | KC:130；BIN:116；MH:119 |
| `kernel.mm_struct_sz` | 4 | `kernel.mm_struct_sz` | `misc.mm_struct_sz`（`optional<u32>`） | `I32`；`mm_struct_stride(fallback)`（MH:306-308） | `OPT`（缺省 `KCONST:15 0x500`） | KC:131；BIN:117；MH:120 |

**节注**：上游 `LFD:94-105` 把 `kernel_phys_load/offset` 当**必填**（缺→refused），当前是 **optional + 编译期回退**。这是“语义不同”而非“键缺失”。`kernel.compact_waiter` 与 `route.select_stack.compact_waiter`（§3.8）**写同一字段**，GLK1 里同时出现时按 section 顺序后写覆盖（BIN:288-324 顺序解析），当前编码端只会发一个（NPT:406 解析一次）。

### 3.6 `abi.payload`（5）——当前无字段

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `abi.payload.lock_offset` | 4 | — | `kernel::LOCK_OFF`=0x0e80 | `K32`（无 profile 字段） | 常量 | KC:61；TH:123；TC:26 |
| `abi.payload.waiter_w0_offset` | 4 | — | `kernel::W0_OFF`=0x1180 | `K32` | 常量 | KC:62；TH:124；TC:27 |
| `abi.payload.fake_task_offset` | 4 | — | `kernel::FAKE_TASK_OFF`=0x1280 | `K32`（TCP 分支另有 `TCP_FAKE_TASK_OFF`） | 常量 | KC:63；TH:128；TC:31 |
| `abi.payload.cred_copy_offset` | 4 | — | `kernel::CRED_COPY_OFF`=0x1080 | `K32`（TCP 分支另有 `TCP_CRED_COPY_OFF`） | 常量 | KC:64；TH:131；TC:32 |
| `abi.payload.fops_table_offset` | 4 | — | `kernel::FOPS_OFF`=0x0f80 | `K32` | 常量 | KC:65；TH:125；TC:28 |

**节注**：上游 `LFD:79-90` 要求这 5 个键 all-or-nothing；但实际在跑的 `run_pipeline_temp.cpp:437-521` 明说“文档没有 abi.payload 段，使用 declared temporary source”，并在缺失时替换为常量。**没有上游 fixture 覆盖这 5 个键**（FIX-DEV 与 FIX-PAY 都没有 `abi.payload`；SELECTION_HEX 也没有）。因此 C1 可选项：(a) 保持常量、文档出现时严格拒绝或忽略；(b) 新增 5 个 `optional<uint32>` 字段。见 U3。

### 3.7 `execution.*`（28）

#### 3.7.1 heap（7）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `execution.heap.prepare_max_attempts` | 4 | `execution.heap.prepare_max_attempts` | `execution.heap_prepare_max_attempts` | `I32`；消费 UTIL:704 | `0` | KC:109；BIN:126；MH:20 |
| `execution.heap.prepare_timeout_ms` | 4 | `execution.heap.prepare_timeout_ms` | `execution.heap_prepare_timeout_ms` | `I32`；消费 UTIL:709 | `0` | KC:110；BIN:127；MH:20 |
| `execution.heap.kernelsnitch_timeout_ms` | 4 | `execution.heap.kernelsnitch_timeout_ms` | `execution.heap_kernelsnitch_timeout_ms` | `I32`；消费 UTIL:563-566 | `0` | KC:108；BIN:128；MH:21 |
| `execution.heap.kernelsnitch_collisions` | 4 | —（与 `kernel.kernelsnitch_collisions` 同目标） | `misc.kernelsnitch_collisions` | `ALIAS`；**上游同时声明两个键**，当前只有一个字段 | `OPT`（缺→`ACC:9-13` 默认 4） | KC:107；BIN:116；MH:119 |
| `execution.heap.snitch_threads` | 4 | — | 无（当前用在线 CPU 数 `sysconf`，UTIL:502-506） | `K32` | 常量 | KC:112；UTIL:502-506 |
| `execution.heap.snitch_round_wait_us` | 4 | — | 无（当前 kernelsnitch 内部固定节奏） | `K32` | 常量 | KC:111；`kernelsnitch.h` 无对应入参 |
| `execution.heap.snitch_wake_rounds` | 4 | — | 无 | `K32` | 常量 | KC:113；同上 |

#### 3.7.2 race（4）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `execution.race.route_wait_ms` | 4 | `execution.race.route_wait_ms` | `execution.race_route_wait_ms` | `I32`；消费 race/threads.cpp:39 | `0` | KC:115；BIN:133；MH:22 |
| `execution.race.route_done_timeout_ms` | 4 | `execution.race.route_done_timeout_ms` | `execution.race_route_done_timeout_ms` | `I32`；**0 → native 回退 300000**（MH:240-244） | `0→300000` | KC:114；BIN:134；MH:22 |
| `execution.race.setup_settle_us` | 4 | `execution.race.setup_settle_us` | `execution.race_setup_settle_us` | `I32`；消费 race/threads.cpp:180 | `0` | KC:116；BIN:135；MH:23 |
| `execution.race.state_poll_interval_us` | 4 | `execution.race.state_poll_interval_us` | `execution.race_state_poll_interval_us` | `I32`；消费 race/threads.cpp:28,71 | `0` | KC:117；BIN:136；MH:23 |

#### 3.7.3 stages（8）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `execution.stages.w1_attempts` | 4 | `execution.stages.w1_attempts` | `execution.w1_attempts` | `I32`；消费 BACKEND43499:392-403 | `0` | KC:120；BIN:141；MH:25 |
| `execution.stages.w1_settle_us` | 4 | `execution.stages.w1_settle_us` | `execution.w1_settle_us` | `I32` | `0` | KC:122；BIN:142；MH:25 |
| `execution.stages.w1_scratch_repair_attempts` | 4 | `execution.stages.w1_scratch_repair_attempts` | `execution.w1_scratch_repair_attempts` | `I32`；消费 BACKEND43499:358 | `0` | KC:121；BIN:143；MH:25 |
| `execution.stages.w2_attempts` | 4 | `execution.stages.w2_attempts` | `execution.w2_attempts` | `I32`；消费 BACKEND43499:222-230 | `0` | KC:123；BIN:145；MH:26 |
| `execution.stages.w2_settle_us` | 4 | `execution.stages.w2_settle_us` | `execution.w2_settle_us` | `I32` | `0` | KC:124；BIN:146；MH:26 |
| `execution.stages.w3_chain_rounds` | 4 | `execution.stages.w3_chain_rounds` | `execution.w3_chain_rounds` | `I32`；消费 BACKEND43499:528 | `0` | KC:126；BIN:147；MH:27 |
| `execution.stages.w3_attempts` | 4 | `execution.stages.w3_attempts` | `execution.w3_attempts` | `I32`；消费 BACKEND43499:293-318 | `0` | KC:125；BIN:148；MH:27 |
| `execution.stages.w3_settle_us` | 4 | `execution.stages.w3_settle_us` | `execution.w3_settle_us` | `I32` | `0` | KC:127；BIN:149；MH:27 |

#### 3.7.4 handoff（5）+ consumer（2）+ recommended_cpus（2）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `execution.handoff.pre_dispatch_settle_ms` | 4 | `execution.handoff.pre_dispatch_settle_ms` | `execution.handoff_pre_dispatch_settle_ms` | `I32`；消费 FE:41（**0 = 立即 dispatch，见 FE:35-40 的 panic 沿革**） | `0` | KC:106；BIN:153；MH:32 |
| `execution.handoff.module_poll_attempts` | 4 | `execution.handoff.module_poll_attempts` | `execution.handoff_module_poll_attempts` | `I32`；FE:69-70 | `0` | KC:104；BIN:155；MH:32 |
| `execution.handoff.module_poll_interval_ms` | 4 | `execution.handoff.module_poll_interval_ms` | `execution.handoff_module_poll_interval_ms` | `I32`；FE:71-72 | `0` | KC:105；BIN:157；MH:33 |
| `execution.handoff.enforce_poll_attempts` | 4 | `execution.handoff.enforce_poll_attempts` | `execution.handoff_enforce_poll_attempts` | `I32`；FE:73-74 | `0` | KC:102；BIN:159；MH:33 |
| `execution.handoff.enforce_poll_interval_ms` | 4 | `execution.handoff.enforce_poll_interval_ms` | `execution.handoff_enforce_poll_interval_ms` | `I32`；FE:75-76 | `0` | KC:103；BIN:161；MH:34 |
| `execution.consumer.max_calls` | 4 | `execution.consumer.max_calls` | `execution.select_consumer_max_calls` | `I32`；race/threads.cpp:149 | `0` | KC:101；BIN:166；MH:31 |
| `execution.consumer.burst_calls` | 4 | `execution.consumer.burst_calls` | `execution.select_consumer_burst_calls` | `I32`；race/threads.cpp:122 | `0` | KC:100；BIN:167；MH:31 |
| `execution.recommended_cpus.main` | 4 | `execution.recommended_cpus.main` | `execution.recommended_main_cpu` → `RuntimeConfig.main_cpu` | `I32`；Kotlin 先用 `execution.selected_cpus.main` 覆盖（PRT:42-47），native `apply_profile` 再校验/回退（RC:52-64） | `0`（0/0 → cpuset 校验失败，回退旧 0/1） | KC:119；BIN:121；MH:19 |
| `execution.recommended_cpus.consumer` | 4 | `execution.recommended_cpus.consumer` | `execution.recommended_consumer_cpu` → `RuntimeConfig.consumer_cpu` | 同上（RC:52-64） | `0` | KC:118；BIN:122；MH:19 |

**节注**：`execution.consumer.*` 虽然与上游同名，但当前只被 select_stack 的消费者路径读取（`race/threads.cpp:122,149`）；tcp_zerocopy / multicast_waiter 路由不使用这两个值。C1 绑定它们不会改变这两个路由。

**节注（最重要）**：当前 **运行时** Kotlin 路径（CODEC:80-101）**不调用** `ProfileMerger.resolveMerged`（全仓只有 PEX:65 调用）；目标设备资产 `ASSET(5.15.189-android13-8-00016-...conf)` 全 69 行**没有 `execution` 块**。因此该 profile 经 CODEC 生成的 GLK1 中，`execution.*` 全部为 0（NPT:409-434 的 `vu()` 默认 0）。native 侧除 `race_route_done_timeout_ms` 有 300000 回退（MH:240-244）外全部直接用 0。→ H1。

### 3.8 `route.*`（18）

#### 3.8.1 `route.multicast_waiter`（7）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `route.multicast_waiter.waiter_off` | 4(int) | `route.multicast_waiter.waiter_off` | `geometry.mcast_waiter_off`（`optional<int32>`） | `S32`；发送端 `toLong().toULong()`（`MulticastConfig`），接收端 two's complement | `OPT`，但路由运行时要求非空（MCAST:25-34） | KC:144；BIN:185；MH:124 |
| `route.multicast_waiter.buffer_size` | 4 | `route.multicast_waiter.buffer_size` | `geometry.mcast_buffer_size`（`optional<u32>`） | `O32` | `OPT`（路由要求） | KC:141；BIN:186；MH:126 |
| `route.multicast_waiter.task_offset` | 4 | `route.multicast_waiter.task_offset` | `geometry.mcast_task_offset` | `O32` | `OPT`（路由要求） | KC:143；BIN:187；MH:127 |
| `route.multicast_waiter.lock_offset` | 4 | `route.multicast_waiter.lock_offset` | `geometry.mcast_lock_offset` | `O32` | `OPT`（路由要求） | KC:142；BIN:188；MH:128 |
| `route.multicast_waiter.attempts` | 1 | — | 无（当前 multicast 一次性，无 attempts 概念） | `K32` | 常量 | KC:140；MCAST:15-100 |
| `route.multicast_waiter.arm_sequence` | 1 | — | 无 | `K32` | 常量 | KC:139；同上 |
| `route.multicast_waiter.arm_hold` | 2 | — | 无 | `K32` | 常量 | KC:138；同上 |

#### 3.8.2 `route.select_stack`（4）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `route.select_stack.waiter_shift` | 4(int) | `route.select_stack.waiter_shift` | `geometry.pselect_waiter_shift`（`optional<int32>`） | `S32`；消费 `SelectStackLayout`（MH:288-295） | `OPT` | KC:148；BIN:178；MH:124 |
| `route.select_stack.compact_waiter` | 1(bool) | `route.select_stack.compact_waiter` | `misc.compact_waiter`（同 `kernel.compact_waiter`） | `B8`；与 kernel 键同目标（§3.5 节注） | `OPT` | KC:145；BIN:179；MH:118 |
| `route.select_stack.enter_delay_us` | 4 | `route.select_stack.enter_delay_us` | `execution.select_enter_delay_us` | `I32`；消费 select_stack_route.cpp:173-179 | `0` | KC:146；BIN:180；MH:30 |
| `route.select_stack.timeout_us` | 4 | `route.select_stack.timeout_us` | `execution.select_timeout_us` | `I32`；消费 select_stack_route.cpp:455-464 | `0` | KC:147；BIN:181；MH:30 |

#### 3.8.3 `route.tcp_zerocopy`（7）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `route.tcp_zerocopy.attempts` | 4 | `route.tcp_zerocopy.attempts` | `execution.tcp_attempts` | `I32`；消费 tcp_zerocopy_route.cpp:270,286 | `0` | KC:150；BIN:171；MH:28 |
| `route.tcp_zerocopy.arm_sequence` | 4 | `route.tcp_zerocopy.arm_sequence` | `execution.tcp_arm_sequence` | `I32`；消费 tcp_zerocopy_route.cpp:267 | `0` | KC:149；BIN:172；MH:28 |
| `route.tcp_zerocopy.post_receive_hold_iterations` | 4 | `route.tcp_zerocopy.post_receive_hold_iterations` | `execution.tcp_post_receive_hold_iterations` | `I32`；消费 tcp_zerocopy_route.cpp:269 | `0` | KC:155；BIN:174；MH:29 |
| `route.tcp_zerocopy.fake_task_off` | 8 | — | `kernel::TCP_FAKE_TASK_OFF`=0x5800 | `K32` | 常量 | KC:153；TH:136；TC:33 |
| `route.tcp_zerocopy.cred_copy_off` | 8 | — | `kernel::TCP_CRED_COPY_OFF`=0x6800 | `K32` | 常量 | KC:152；TH:138；TC:34 |
| `route.tcp_zerocopy.chunk_bias` | 8 | — | 常量 `0xe80`（TCP 分支；非 TCP 为 `SKB_FRAG_BIAS=0`） | `K32` | 常量 | KC:151；UTIL:341；KCONST:24 |
| `route.tcp_zerocopy.payload_delta` | 8(int) | — | 常量 `0`（TCP 分支；非 TCP 为 `SKB_DATA_DELTA=-0xe80`） | `K32`；上游有符号 64 位 | 常量 | KC:154；UTIL:340；KCONST:14 |

**节注**：TCP 分支由 `route::route_capability(...tcp_payload_layout)` 选择（UTIL:336-351），**不来自文档**。上游 `REK:44-50` 把 `payload_delta/chunk_bias/fake_task_off/cred_copy_off` 与 `post_receive_hold_iterations` 一起列为 tcp 路由的 expected keys（缺→route 拒绝），当前只保留了 3 个 tuning，4 个几何是常量。

### 3.9 backend 选择 / queue / steps（4）

| 上游 key | w | 当前 GLK1 path | 目标 | 规则 | 缺 | 证据 |
|---|---|---|---|---|---|---|
| `backend.cve_2026_43499.experimental` | 1(bool) | — | 无 | `NONE`；上游 NPT 只在声明时发射（KNP:314） | `n/a` | KC:128；KNP:314 |
| `backend.cve_2026_43499.queue` | array | — | 无（当前 W1→W2→W3 固定流水线） | `NONE`；上游 native `RP:60-64` 对携带 queue 的文档**直接拒绝**（queue-removed），而 Kotlin 仍会发射（KNP:316）——上游自身矛盾 | `n/a` | KC:136；RP:13,60-64；KNP:316 |
| `backend.cve_2026_43499.route`（queue-level token） | str | header `route`（u8） | `kernel_offsets.route`（候选） | `SEL`：上游把 queue 级 route 放在 `backend.cve_2026_43499.route`（KNP:309）；当前 route 只有 header 的 u8。谁提供 C1 的 route：root `route` / root `combination` / 本键，未定（U1） | `Auto=0`（然后 parse 拒绝） | KC:137；KNP:309；BIN:275-282；RP:70 |
| `backend.cve_2026_43499.steps` | str | — | 无 | `NONE`；manifest 注明 M5 已 REMOVED，出现即应拒绝（KC:156 doc 列）；上游 NPT 不再发射 | `n/a` | KC:156；KNP:305-322（不发射） |

### 3.10 仅当前有的键/字段

| 当前键/字段 | 位置 | 上游情况 | 影响 |
|---|---|---|---|
| `meta.recommend_shizuku` | BIN:63；MH:83；NPT:356 | 上游 manifest 无此 key；上游 Kotlin 也不发射（KNP:97-99 只发 kernel_major/safe_mode/kernel_minor） | 当前 native **无消费者**（仅 binary.cpp/model.h/tests 出现）；真正消费者是 UI 的 `BuiltinProfileCatalog.recommendShizuku`（CAT:42-44）。C1 可丢弃而不影响攻击，但 C2 需保留 UI 语义 |
| `meta.fallback_route` | BIN:64；MH:84；NPT:357 | 上游无 root/段键；上游 route 选择单一 | **有消费者**：`ROUTEPOL:239-247` 在首选路由失败时按 fallback 重试。C1 若只绑定 root route，W2/W3 的 fallback 行为会消失（H3） |
| `meta.safe_mode`（profile 值） | BIN:65；MH:85；NPT:435 | 上游是 root `safe_mode` bool（可缺省） | 当前恒 0，见 §3.1 |
| `kernel.compact_waiter` 与 `route.select_stack.compact_waiter` 双写 | BIN:115,179 | 上游只有一个 `kernel.compact_waiter` + 一个 `route.select_stack.compact_waiter`（同值两个声明） | 两键同目标；先后覆盖。C1 需定义优先级（U6） |
| `mcast.*` 平铺回退 | PRT:61-70 | 上游无此回退（route 段直读） | 当前 profile 两种写法都能解析；C2 若只用 `route.<branch>` 需要资产继续带 branch |
| `fallback.route.<name>.*` 查找 | PRT:56-59,66-68 | 上游 R6a 已删除 fallback 分支（KRS:74-75 注释） | 当前多一条几何来源；C1/C2 忽略它不会改变已解析结果（资产同时有 route branch） |
| header component ids（frontend/backend/middleware） | BINH:34-37；BIN:262-266；NPT:52-54 | 上游 GLKv3 root 有 `backend`/`terminal`，没有 middleware id；`component_ids` 是当前扩展 | C1 解析 GLKv3 时需从 root/常量填 `component_ids`，否则 orchestrator 选择失败（MAIN:92-108） |
| GLK1 header magic/version=2 | BINH:29-30 | 上游 schema=3，root map | C1 必须并存两套解析（C3 前双轨只读） |
| `profile.conf` / `profile.bin` / `profile.release` 三参 | PARAMS:18-24,87-94 | 上游只传 document（+43284 的 secret frame）；conf 文本是当前证据需求 | C3 改传 document 时，conf 仅作 evidence；不要把它当第二权威 |
| GLK1 4 字节长度前缀（status_record 分支） | ENTRY:82-101；MAIN:74-78 | 上游 `ChannelBStdin` 也是 4 字节大端长度前缀（KCH/SessionSecretFrame:137-145） | 传输形态可对齐；C3 只需换 payload 解码分支 |

---

## 4. payload family（`bin`/`sha256`/`size`/`kind`）

### 4.1 上游规则（四键 + 三绑定键 + 两重校验）

- 键名唯一权威：`kPayloadKeys = {"bin","sha256","size","kind"}`（PK:23）；kind 取值 `{plan,script,module}`（PK:28）。
- 段名 = `backend.<id>.payload.<name>`（PK:131-133 的 `payload_segment`；DOC:255-266 使用它）。**name 不是自由字符串**：必须是“可选终止符 token + `_` + 1..16 个 `[a-z0-9_]`”（PK:34,77-106）。裸 token（如 `root_child`）、未知前缀（`free_name`）、非法后缀（`root_child_`）全部拒绝。
- 三绑定键（`sha256`/`size`/`kind`）与 `bin` 必须**同时存在**；任一缺失/类型不符 → `Missing`（DOC:267-304）。附加校验：sha256 必须 64 个十六进制字符且格式合法（DOC:279-292）；`size` 必须等于 `bin` 实际长度（DOC:305-317）；`bin` 非空（DOC:305-317）。kind 必须在 declared 集合内（DOC:293-304）。
- 读取入口 `Document::payload` 先校验名字规则，再查段（DOC:215-266）；因此非法 name 不会把文档路径暴露给任意字符串。
- plan 复用 payload family：name = `<terminal>_plan`（PD:56），读后再校验大小上限与**重算 SHA-256**；不匹配 → `PlanMissing`（PD:51-63）。
- 上游 native **没有**“payload family 之外禁止 bin”的全局规则；是否出现只由 consumer 决定。

### 4.2 当前处理

- GLK1 **只能承载 u64**（BINH:4-19；BIN:315 读 8 字节）。没有 str/bin/array/map，也没有 payload 段。`NativeProfileDocument.toBinary()` 只写 `section.key→u64`（NPT:38-72）。
- 当前生产参数只有 `profile.conf`（HOCON 源文本）、`profile.bin`（GLK1）、`profile.release`（PARAMS:18-24；CODEC:100；FLOW:50-60）。`profile.bin` 走 stdin（`FLOW` 注释：`stdin=profile.bin`；MAIN:74-78 的 `read_glk1_stdin`）。
- 当前没有 plan/script/module 的文档载体：payload 由宿主 `lib43499-Glued.so` 自身提供；KernelSU 由 `handoff_probe` 夜载（FE:68-78；`GHOSTLOCK_KSUD` 环境变量；RC:82-90）。`FlowEntry.payload`（`core/flow/FlowEntry.kt:27,46`）是通用 flow 字段，**43499 入口未使用**（FLOW:50 起只解码三键）。
- 当前 `HandoffPayload`（Kotlin）与 `payload_keys`（native）在参考树中存在，但**本仓库工作树没有对应实现**（`grep -r payload_keys app/src/main` 无命中；`NativeProfile.kt` 无 payload 字段）。

### 4.3 对照

| 上游规则 | 当前 | 差距类型 |
|---|---|---|
| 4 键 family + 段名规则 | 无 | 载体缺失（C2 需新增，C1 无目标） |
| name = selectable terminal + `_` + suffix（≤16） | 无（无 name 概念） | 缺失 |
| kind ∈ {plan,script,module} | 无 | 缺失 |
| sha256 64 hex + 重算 | 无 | 缺失 |
| size == bin 长度且 >0 | 无 | 缺失 |
| plan 名 `<terminal>_plan`、大小上限、摘要重算 | 无（plan 不随文档） | 缺失；C4 前不恢复 plan_gate/run_plan |

### 4.4 C1 含义

C1 计划搬 `payload_keys.{hpp,cpp}`（计划 §3），但**没有可绑定的目标字段**：`kernel_offsets` 没有 bin/sha/kind/size。C1 可做的最多是：
1. 原样搬 `payload_keys.*` + `glkv3_document::payload()` 读取接口，用 `FIX-PAY` 做 **读取器** golden（§7 G7）；绑定层不接收 payload。
2. 明确 C1 binder 对“文档里有 payload 段”的行为：忽略（不绑定）还是记录（例如日志）。建议：C1 读取器接受，binder 只记录存在性；**不得**把它当作 plan/script 执行（无 plan_gate，C1 明确不搬 RP/PD）。
3. 若 Lead 要求 C1 就完成 payload→交付物绑定，则 C1 范围溢出到 C3/C4，需要另立批次（U7）。

---

## 5. 差异与未决

### 5.1 仅上游有（清单）

1. `root.kernel_minor`（KC:164；上游 KNP:32/564；当前无字段）。
2. `root` 选择三键 `backend`/`terminal`/`route` 的 owner-qualified 形式（FIX-TSV:2-9；当前 header 固定 id + u8 route）。
3. `combination` root token（RP:70；当前无）。
4. `offset.vr_sys_exit_tp`（KC:135；上游 KNP:403；当前无；vr_guard 面已在当前移除）。
5. `abi.payload.*` 5 键（KC:61-65；当前编译期常量）。
6. `execution.heap.snitch_{threads,round_wait_us,wake_rounds}`（KC:111-113；当前无）。
7. `route.multicast_waiter.{attempts,arm_sequence,arm_hold}`（KC:138-140；当前无）。
8. `route.tcp_zerocopy.{chunk_bias,cred_copy_off,fake_task_off,payload_delta}`（KC:151-154；当前编译期常量）。
9. `experimental`（KC:128）、`queue`（KC:136）、`steps`（KC:156）。
10. payload family 4 键 + 名称规则 + plan 名/摘要（PK:23-34；PD:56）。
11. `available`/`backend.kind` 声明与投影（KNP:496-511；KAD:175-210；当前 PRT:12-16 的 KnownTopLevel 不接受 `available`/`backend`）。
12. 字符串/数组/二进制 wire 值（KENC 的 `Glkv3Value`；当前 GLK1 只有 u64）。

### 5.2 仅当前有（清单）

1. `meta.recommend_shizuku`（BIN:63；UI 用 CAT:42-44；native 无消费者）。
2. `meta.fallback_route`（BIN:64；ROUTEPOL:239-247 有行为）。
3. header component ids（BINH:34-37；BIN:262-266）。
4. `mcast.*` 平铺与 `fallback.route.*` 查找（PRT:56-70）。
5. `execution.selected_cpus`（PRT:42-47；PMT:50-55）——上游也有 `selected_cpus`（KRS:70-75），不算独有，但当前**运行时**用它覆盖推荐 CPU，而上游是 merger 注入。
6. `profile.conf`/`profile.bin`/`profile.release` 三参（PARAMS:18-24）。
7. GLK1 magic/version=2/header 长度/组件 id（BINH:6-19,29-37）。
8. `safe_mode` 的“恒 0 + 无调用 patch”实现（NPT:245,435）。
9. 当前 profile 资产是**平铺 schema**（`route {}`/`fallback {}`/`kernel_phys_load`/`kernelsnitch`），不含上游 `ghostlock{}`/`available`/`backend.*` 包装（ASSET(5.15.189...conf):1-69）。

### 5.3 语义不同（逐条）

| # | 键/主题 | 上游 | 当前 | 影响 |
|---|---|---|---|---|
| S1 | `safe_mode` | root bool，可缺省；编码端 `document.safeMode != 0` 总会发射（KAD:335） | `meta.safe_mode` u8；Kotlin `from` 恒 0（NPT:435）；`patchSafeMode` 无调用者 | safe mode 永不生效；C4 的 opt-in 语义无载体 |
| S2 | `compact_waiter` | bool wire（KC:129/145，width 1），上游资产写 `true`（`2933b838^:.../5.15.189-...conf:67`） | resolver 只认 Number（PRT:46-71 `getLongAt`），布尔被丢弃；当前资产写 `1` 掩盖了这一点 | C2 若直接搬上游资产/布尔语义，`compact_waiter` 静默丢失 → select/tcp W1 载荷形状变化（H6） |
| S3 | `kernel_phys_load/offset` | LFD:94-105 必填 all-or-nothing | optional + 编译期回退（MH:112-117；address_space.cpp:87-102） | 文档缺值时行为不同（当前继续跑，上游拒绝） |
| S4 | symbol offsets width | manifest width 8；LFD `offset_to_direct` 用 u64 并检查 0 | wire u64，但 `RSO:14-26` 截断到 u32 再消费 | >4 GiB 偏移静默截断；C1 需补宽度检查（H6） |
| S5 | `recommended_cpus` | profile 推荐值（merger 注入） | Kotlin 用 `selected_cpus` 覆盖（PRT:42-47）；native `apply_profile` 再校验 cpuset（RC:52-64） | 行为基本等价，但来源不同；C1 若只读文档 `recommended_cpus` 会丢掉会话 CPU 选择 |
| S6 | `execution.*` 缺省 | 上游 `PM:35-75` 表要求值非 0（0/缺→`prepare-value-missing:<name>`） | 当前缺→0，直接进入攻击（除 S7） | **H1**：W1/W2/W3/handoff 全部可能 0 次/0 等待 |
| S7 | `route_done_timeout_ms` | 上游缺→拒绝 | 0→native 回退 300000（MH:240-244） | 唯一有默认的 execution 字段；语义与上游不同 |
| S8 | `multicast attempts/arm_*` | 文档可调（REK:59-67；上游预设 128/16/20000） | 无字段；当前 multicast 一次性（MCAST:15-100） | 文档值被忽略；不是等价替代（H2） |
| S9 | tcp payload geometry | 文档/路由 expected keys（REK:44-50） | 常量 + 路由分支（UTIL:336-351） | 同上（H2） |
| S10 | payload | 4 键 family（PK:23） | 无 | 无法承载 plan/script/module；C4 前无计划事实 |
| S11 | 未知 key | native 不拒绝；Kotlin adapter 对未声明 path `error()`（KAD:290-320） | native 忽略（BIN:297-324）；Kotlin 只发射固定键 | C1 需**新建**严格策略（U2），不能声称继承 |
| S12 | `safe_mode`/root 选择 | `KAD` 输出 `combination`，`RP` 读 `combination`，而 golden/fixture 是 backend/route/terminal | 当前 header route + 固定 id | U1；静态矛盾（§1.4） |

### 5.4 高风险项（6 项；满足“≥3 且影响 W1/W2/W3 或 execution params”）

**H1（阻断级）：运行时路径没有合并 execution 默认值 → execution 参数（除 `route_done_timeout_ms` 的 native 300000 回退）为 0。**
- 证据链：`CODEC:80-101`（parse→convert→validate→from→toBinary，**无** merger）→ 全仓 `resolveMerged` 只在 `PEX:65` 调用（`grep` 证明）；`PMT:23` 是唯一实现；目标设备资产 `ASSET(5.15.189-android13-8-00016...conf)` 69 行无 `execution`；`NPT:409-434` 的 `vu()` 缺省 0；native `MH:18-35` 默认 0，仅 `MH:240-244` 一个回退。
- 受影响的消费点：`BACKEND43499:392-403`（W1 attempts/settle）、`:222-230`（W2 attempts）、`:293-318`（W3 attempts/settle）、`UTIL:704-709`（heap prepare）、`race/threads.cpp:28,39,71,122,149,180,195`、`FE:41,69-76`（handoff）。特别地 `FE:35-41` 注释记录“立即 dispatch 在 5.15 上导致 init 退出/panic”；`pre_dispatch_settle_ms=0` 正是立即 dispatch。
- C1/C2 要求：C2 必须把 `resolveMerged`（或等价 execution 补全）接进**运行时**路径，或在 C1 binder 对缺失 execution 严格拒绝；二选一，不能保持现状。
- 关联：计划 C2a 的“缺失 execution params”正是此项；当前实现是“缺→0”，不是“缺→默认”。

**H2（高）：9 个 payload/路由几何键无字段，文档值会被常量静默覆盖。**
- 键：`abi.payload.*`×5（KC:61-65）、`route.tcp_zerocopy.{chunk_bias,cred_copy_off,fake_task_off,payload_delta}`×4（KC:151-154）。
- 当前常量：`TH:123-139`（LOCK/W0/FAKE_TASK/CRED_COPY/FOPS/TCP_FAKE/TCP_CRED）、`UTIL:340-351`（payload_base、tcp 分支）、`TC:26-34`（数值）。
- 风险：C1 若“忽略未映射键”，文档携带的值不会生效；若“严格拒绝”，上游 `SELECTION_HEX`（含 `abi.kernel` 等但不含 abi.payload）可过，但任何携带 payload 几何的文档会被拒。两种都必须显式裁决（U3），不能静默。

**H3（高）：safe_mode + fallback_route 的当前语义无上游对应，丢失会改变 W2/W3 行为。**
- `safe_mode`：当前恒 0（NPT:435），但 shell 脚本在 `OPS:322` 用 `SAFE_MODE=1` 禁用 `/data/adb/modules/*`（`OPS:322-341`），`OPS:417` 传入该值。上游 root `safe_mode` 可配置；C1 若按上游 root 键绑定，必须决定是否真的把它接到脚本（当前 `meta.safe_mode` 可承接，但没有任何 Kotlin 路径会写 1）。
- `fallback_route`：`ROUTEPOL:239-247` 在首选路由失败时按 `meta.fallback_route` 重试；上游文档无此键。C1 若只绑定 root route，fallback 永远 Auto，W2/W3 的重试路径消失。
- 关联：`meta.recommend_shizuku` 无 native 消费者，可忽略；但 C2 仍要保留 UI 的 Shizuku 建议（CAT:42-44）。

**H4（高）：root 选择契约未定（route/backend/terminal/combination 四套说法）。**
- 上游 native：`RP:70` 只读 root `combination`；`KAD:340` 写 `combination`；但 `KAD` 的测试（`:53-58`）、FIX-TSV（`:2-9`）、`SELECTION_HEX` 都是 backend/route/terminal；`KDEC:49-61` 不认识 `combination`。四个事实两两冲突。
- 当前：route 来自 GLK1 header u8（`BIN:275-282`），backend/frontend 固定 id（`NPT:52-53`）。
- C1 binder 必须知道“route 从哪来”：root `route`（str→catalog）、root `combination`（需 chain catalog，C1 不搬 RP）、还是 `backend...route`（queue-level，当前无消费者）。未定则无法写 `kernel_offsets.route`/`fallback_route`。

**H5（中高）：重复键的优先级未定。**
- `kernel.kernelsnitch_collisions`（KC:130）与 `execution.heap.kernelsnitch_collisions`（KC:107）→ 同一个 `misc.kernelsnitch_collisions`；当前只读 `kernelsnitch.collisions`（NPT:407）→ GLK1 `kernel.kernelsnitch_collisions`（BIN:116）。
- `kernel.compact_waiter`（KC:129）与 `route.select_stack.compact_waiter`（KC:145）→ 同一个 `misc.compact_waiter`（BIN:115,179）。
- 两键同时出现时谁赢？当前 GLK1 是按 section 解析顺序覆盖（`kSections` 顺序 `kernel` 在 `route.*` 前，BIN:197-212），即 route 后写赢；C1 若按 manifest 顺序读，先后不同（谁赢取决于遍历顺序）。必须定成一条规则并写测试（G10）。

**H6（中高）：宽度/符号/布尔校验缺口。**
- `RSO:14-26` 把 u64 截成 u32（S4）；上游 manifest 对 98 个 43499 键有 width 列（3 个 str/array 为 `-`）且 `KAD:99-134` 把 width 当硬校验（`2^40` 进 u32 必须拒绝，不是 wrap）。
- `compact_waiter` 布尔（S2）：当前 `PRT:71` 的 `getLongAt` 丢布尔。
- `multicast attempts/arm_sequence/arm_hold` 上游 width 1/1/2（KC:140,139,138），当前无字段；若 C1 将来加字段，必须按宽度拒绝越界。
- `route.tcp_zerocopy.payload_delta` 上游 `int` 8 字节（KC:154）；当前常量是 `-0xe80`（`KCONST:14`），符号语义必须保留。

### 5.5 未决裁决（U1–U11）

| ID | 问题 | 需要谁/什么 | 证据 |
|---|---|---|---|
| U1 | root 选择契约：C1 从 `route`/`combination`/`backend...route` 哪个键取 route？是否要求 backend/terminal？ | Lead + 设计（ADR/wire 契约） | RP:70；KAD:340-348；KDEC:49-61；FIX-TSV:2-9；BIN:275-282 |
| U2 | “strict 未知”的粒度：未知 section？未知 key？未知 owner？`plugin.*`/43284 owner 是否拒绝？ | Lead（C1 验收判据） | 计划 §2/§4；BIN:297-324；KAD:175-210 |
| U3 | 15 个“只有常量”的键：接受并忽略 / 严格拒绝 / 新增字段？逐键或成组 | Lead + native owner | KC:61-65,111-113,138-140,151-154；TH:123-139；UTIL:340-351 |
| U4 | `safe_mode`：是否把 root bool 接到 `meta.safe_mode` 并由 C2 提供开关？ | 用户/Lead（安全语义） | NPT:435；MH:85；OPS:322,417 |
| U5 | `fallback_route`/`recommend_shizuku`：保留为当前扩展（从哪读）还是删除？ | Lead + Kotlin owner | BIN:63-64；ROUTEPOL:239-247；CAT:42-44 |
| U6 | 重复键优先级（collisions、compact_waiter） | native owner | KC:107,129,130,145；BIN:115-116,179 |
| U7 | C1 是否包含 payload 绑定（还是只搬读取器） | Lead（范围） | PK:23-34；DOC:215-300；计划 §3/§5-I1 |
| U8 | `kernel_minor`：加字段 / 忽略 / 拒绝 | Lead | KC:164；KNP:32,564；MH 无字段 |
| U9 | C1 host 测试入口：当前无 host 测试 target | 工程（Gradle/脚本） | `app/src/main/cpp/43499glued/CMakeLists.txt` 只有 `glued43499_exec`；仓库根无 Makefile |
| U10 | golden 权威序：`SELECTION_HEX`/FIX-TSV/FIX-DEV 三份，FIX-DEV 缺 abi.kernel/abi.payload | Lead + 设计 | §1.4；§7.1 |
| U11 | execution 默认值在 C1 还是 C2 补（H1） | Lead（批次边界） | CODEC:80-101；PMT:23；PEX:65；ASSET 无 execution |

---

## 6. 当前 Kotlin ProfileResolver/ProfileMerger vs 上游行为差异（C2a）

### 6.1 差异表

| # | 主题 | 当前 | 上游（`d2778a1a^`） | 证据 |
|---|---|---|---|---|
| K1 | 运行时是否 merger | **否**：CODEC 直接 parse→convert→validate→from→toBinary | 控制器/Exporter 都走 `resolveMerged`（KMR:40-70） | CODEC:80-101；PEX:65；PMT:23 |
| K2 | `schema_version` | 硬编码 **1** | `GHOSTLOCK_PROFILE_SCHEMA_VERSION = 3` | PMT:42；`ProfileSchemaVersion.kt:11-20`；KMR:63 |
| K3 | tuning preset 复用 | `put("execution", tuningExecution)` **共享实例**，`deepMergeValues` 会原地改写 preset | `tuningExecution.copyValue()`（修复跨 profile 污染） | PMT:33；KMR:54 |
| K4 | route 合并 | `mergeSource` 同时处理 `route` 与 `fallback.route` 分支（PMT:98-112） | 只处理 `route`（R6a 删除 fallback 分支） | PMT:98-112；KMR:119-126；KRS:74-75 |
| K5 | 默认 execution 来源 | 运行时无；Export 有 tuning + route preset | 两处都有（tuningExecution + routePresets） | CODEC；PEX:65-73；KMR:54-67 |
| K6 | 必填门控 | 43499 的 `route/task_struct/cred/offset` **无条件必填** | 仅当 `declaresBackend(profile,"cve_2026_43499")` 为真才要求（43284-only profile 不要求） | PRT:17-28,75-134；KRS:45,141-155,175-229 |
| K7 | 顶层 key 白名单 | 不含 `kernel_minor`/`backend`/`available`/`recommend_vr_guard`/`vr_guard` | 含上述（KRS:13-32） | PRT:12-16；KRS:13-32 |
| K8 | 布尔标量 | `getLongAt` 只认 `Number`（`compact_waiter=true` → null） | `getFlagLongAt` 接受 Boolean（true=1/false=0） | PRT:71；KRS:99-107；`ValueModel.kt:getLongAt` |
| K9 | fallback.route 查找 | `nativeValue` 先 route branch，再 fallback branch，再平铺 | route branch + 平铺，无 fallback branch | PRT:53-71；KRS:62-95 |
| K10 | 宽度校验 | 无 | multicast `attempts/arm_sequence ≤ 0xff`、`arm_hold ≤ 0xffff` | PRT:75-134；KRS:231-252 |
| K11 | 字符串/布尔访问器 | 无（只有 `nativeValue`） | `nativeText`/`nativeBool`（payload/queue/43284 策略需要） | PRT 全文；KRS:110-139 |
| K12 | `resolveDeclarations`/`available` 声明 | 无；`available`/`backend.kind` 被 validate 拒绝 | 有：`resolveDeclarations` + `declaresBackend` + `AvailablePriority` 投影 | PMT 全文；KMR:34-38；KRS:141-155 |
| K13 | 推荐 CPU | `applySelectedCpus` 强制 `execution.selected_cpus`（PMT:50-55），resolver 折进 `recommended_cpus` | 同（KMR:71-77；KRS:70-75） | PMT:50-55；KMR:71-77 |
| K14 | route preset 填充 | 缺 key 才填（PMT:58-72） | 同（KMR:79-98） | PMT:58-72；KMR:79-98 |
| K15 | execution 扁平化 | `executionFromMerged` 同 | 同 | PRT:137-152；KRS:257-272 |
| K16 | 未知顶层 key | 直接报错（PRT:81-85） | 同，但白名单更大 | PRT:81-85；KRS:165-173 |
| K17 | document 形状 | 只有 14 个固定 GLK1 section，值全 u64 | `v3Sections()` owner-qualified + text/bin/plugin/queue/payload 多通道 | NPT:74-157；KNP:91-283 |
| K18 | payload/plugins/queue | 无 | `HandoffPayload`/`PluginEmission`/`QueueElement` 均进 document | KNP:63-72,279-299；KAD:271-348 |

### 6.2 C2 必须搬/必须决策的上游语义

必须搬（否则文档与上游不一致）：
1. `schema_version = 3`（K2）——与 GLKv3 `schema=3` 同一数字，不能再用 1。
2. `tuningExecution.copyValue()`（K3）——否则 Exporter 多 profile 合并会串味。
3. `getFlagLongAt`/`nativeBool`/`nativeText`（K8/K11）——否则上游布尔 `compact_waiter`、字符串 selection、payload/queue 全部丢失。
4. 宽度硬校验（K10）——manifest width 是硬输入（`KAD:99-134`）。
5. `resolveMerged` 接入运行时路径（K1/K5/H1）——当前最大行为缺口。
6. `declaresBackend` 门控或明确“本批次只支持 43499，43284 profile 在输入边界拒绝”（K6）——二选一，不能保持“无条件 43499 必填”又声称兼容上游多 backend。
7. `nativeText`/`nativeBool` 以及 document 的 str/bin/array 通道（K11/K17/K18），若 C2 范围包含 selection/payload/queue。

需要 Lead 裁决（不是“搬”能解决的）：
- K4（fallback route 分支是否保留）、K7（`kernel_minor`/`available`/`backend` 是否接受）、K12（`available` 投影是否搬 ProfileLayout）、K18（payload/queue/plugin 是否进 C2），对应 U1/U4/U5/U7/U8。

---

## 7. C1 host golden 用例与 C2 交叉校验

### 7.1 上游 fixtures（实际存在，已核对）

| fixture | 内容/规模 | 用途 | 断言要点 |
|---|---|---|---|
| `profile-core/src/test/resources/glkv3-native-fixture.tsv`（FIX-TSV） | 90 行：`# root keys` + 8 root + `# sections` + 80 entry（= SELECTION_HEX 的 native dump） | C1 逐键 golden（**字段级**） | root 顺序 schema/release/terminal/backend/route/kernel_major/kernel_minor/safe_mode；15 个 section、80 个 entry 的 path/wire/value 全等；`waiter_off=-2`、`caps_value=1311768467463790320`、`arm_hold=20000`、`vr_sys_exit_tp=42` 等特征值 |
| `Glkv3Golden.SELECTION_HEX`（GOLD） | 3920 hex 字符；解码后 8 root + 15 section + 80 entry | C1 的字节输入（与 FIX-TSV 同一文档） | 与 FIX-TSV 逐项相等；`schema=3`；`safe_mode=true`；`kernel_minor=15`；`backend.cve_2026_43499` 段含 `experimental/queue/route` |
| `Glkv3Golden.NO_SELECTION_HEX` | 3766 hex 字符；8 root + 14 section（无 backend 段） | “未声明 selection 不增加字节”的零增量 pin | 与 SELECTION_HEX 的差 = backend 段 3 键；C1 binder 对两者输出相同字段 |
| `device-document-fixture.hex`（FIX-DEV） | 解码：**7 root + 14 section + 76 entry**；无 `kernel_minor`、无 `abi.kernel`、无 `abi.payload`；有 `backend.cve_2026_43499`(queue,route) + 7 个 mcast 键 + `offset` 仅 3 个 slide 键 | **真实设备文档**（A301SO 5.15.189）| root release=`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`、route=`multicast_waiter`、safe_mode=false；逐键比对；显式断言“缺 abi.kernel/abi.payload 时的策略” |
| `payload-document-fixture.hex`（FIX-PAY） | 7 root + 12 section；含 `backend.cve_2026_43499.payload.root_child_script` 4 键；无 `abi.kernel`、无 route section | payload family + 摘要 | native `real_document_test.cpp` 固定：doc SHA-256=`d8d7fb57…`、payload SHA-256=`527ed4ff…`、kind=`script`、name=`root_child_script`、size=23、bin=`echo ghostlock-fixture\n`；`root_child_module` → Missing |
| `core/src/tests/payload-manifest.pin.tsv` | payload key/kind 名单 | payload_keys 一致性 | 4 keys/3 kinds 与 PK:23,28 逐项相等 |

补充事实：**没有任何上游 fixture 覆盖 `abi.payload`**（3 份 hex + TSV 都没有该段）。因此 H2 的 5 个键必须用**合成文档**测，不能声称“fixture 已验证”。

### 7.2 C1 host golden 用例清单（建议）

| ID | 输入 | 断言 | 对应用途 |
|---|---|---|---|
| G1 | `document_reader_test` 语义（自建 mpack） | schema=3 通过；0/1/2/4/99 → Invalid；`nullptr/0` → Invalid；garbage → Invalid；未 parse 时任何读 → Invalid | 读取器拒绝面（DOC:56-76,215-300） |
| G2 | `document_pairs_test` 语义 | 段/键缺失 → `KeyUnavailable` 且 `missing_key` 命中；容量不足 → `TooManyKeys`；失败时 `filled=0`（不得半填） | 绑定层 all-or-nothing（DP:7-46） |
| G3 | `payload_keys_test` + `payload-manifest.pin.tsv` | `bin/sha256/size/kind` 与 `plan/script/module` 名单；name 规则：`root_child_script` 过，`free_name`/`root_child`/`shizuku_script`/`root_child_` 拒；suffix>16 拒 | 名字/键规则（PK:23-34,77-106） |
| G4 | FIX-PAY + `real_document_test` 常量 | doc/payload SHA-256 固定；payload 读回；size=23；kind；缺 `root_child_module` → Missing | 跨端字节 golden（`real_document_test.cpp`） |
| G5 | GOLD（SELECTION_HEX）→ C1 binder dump | dump 与 FIX-TSV **逐字段**相等（8 root + 80 entry）；未映射键（mcast attempts/arm_*/vr_sys_exit/queue/experimental）按 U2/U3 策略断言（忽略有日志/拒绝有名字） | 字段级绑定主 golden |
| G6 | FIX-DEV | 逐键断言 76 个 entry 中已映射的 71 个（76 = backend 段 queue/route 2 + mcast 7 + 其余 67；mcast 的 attempts/arm_sequence/arm_hold 3 个是常量键）；`abi.kernel`/`abi.payload` 缺失时断言既定策略；route=multicast_waiter 且 4 个几何齐全 | 生产资产 golden |
| G7 | FIX-PAY + `Document::payload` | 4 键齐全、摘要重算、size 匹配、kind=script；非法 name/kind/空 bin/尺寸不符 → Missing；plan 名 `root_child_plan` 不存在 → Missing | payload 读取器（C1 只读，不执行） |
| G8 | 负例：错误 schema、截断字节、非 map root、`sections` 非 map、段非 map、键类型为 str 而按 uint 读 | 全部 Invalid/Missing，且拒绝名可读；不得崩溃/越界 | 健壮性 |
| G9 | 负例：同段重复键（手写 mpack） | 定义并断言行为（建议：取首个或拒绝；上游 `document_pairs` 注释说“不可寻址”，见 DP:12-14） | 避免“后写覆盖”成为隐式规则 |
| G10 | 宽度/符号：`waiter_off=-2`（`0xfe` int）、`payload_delta=-0xe80`（int64）、`caps_value=2^64-1`、`2^40` 入 u32 键 | 有符号 two's complement 还原；u64 不丢位；u32 越界按 U3/U6 策略拒绝/记录，**不得静默 wrap** | S4/H6 |
| G11 | root 选择矩阵：仅 `route`；仅 `combination`；仅 `backend...route`；三者都有/冲突 | 按 U1 裁决断言（当前未定）；至少断言“缺 route → 错误名” | U1 |
| G12 | 重复键矩阵：`kernel.compact_waiter` + `route.select_stack.compact_waiter`；`kernel.kernelsnitch_collisions` + `execution.heap.kernelsnitch_collisions` | 按 U6 优先级断言最终值 | H5 |

### 7.3 C2 交叉校验方案

1. **字节对拍**：Kotlin `NativeProfileGlkv3Adapter.adapt(fixture)` → `Glkv3Encoder.encode` 必须等于 `SELECTION_HEX`；无 selection 版本等于 `NO_SELECTION_HEX`（沿用 `NativeProfileGlkv3AdapterTest.adapterEncodingMatchesNativeCanonicalGolden` 的思路）。当前快照的该测试与 adapter 代码静态矛盾（§1.4），C2 必须先修契约再谈 pin。
2. **字段对拍**：native C1 读取 `SELECTION_HEX` 字节，dump 成 `path<TAB>wire<TAB>value`，与 FIX-TSV 全等；Kotlin 从同一逻辑文档 dump 也必须全等（FIX-TSV 本来就是 native dump）。
3. **真实设备文档对拍**：Kotlin 用生产链（asset→merge→adapter→encode）生成 `FIX-DEV` 等值字节（原测试 `PayloadCarriageTest` 就是如此）；native C1 读取后，逐键与 §3 表比对。**这是 C3 前最有价值的一条**，因为它覆盖真实资产而不是合成 fixture。
4. **payload 对拍**：Kotlin `PayloadCarriageTest` 生成 `FIX-PAY`；native `real_document_test` 读取并重算摘要；两侧的 doc/payload SHA-256 固定值必须一致。
5. **负例对拍**：Kotlin 侧 `parseWidthColumn`（KAD:99-106）拒绝 `0/3/16/""/-1`；native 侧同一字段的越界必须同样拒绝（不能一边拒一边 wrap）。
6. **manifest 一致性**：Kotlin 从 `profile-manifest-v3.tsv` 读 path/wire/width；native 从 `key_catalog.hpp` 生成同一 TSV；逐行 diff（含 `required/default/wire/width` 四列）。C2 若只搬 43499 owner，必须在生成器里显式过滤并记录“裁剪清单”，不得手抄。
7. **选择契约回归**：C2/C1 对 `route`/`combination`/`backend/terminal` 的取舍必须用同一组字节 fixture 验证（G5/G11），不能一边按 root route、一边按 combination。

### 7.4 运行入口现状（影响 C1 门禁执行）

- 当前 `app/src/main/cpp/43499glued/CMakeLists.txt` 只定义 `glued43499_exec`（Android 可执行），**没有 host 测试 target**；仓库根无 `Makefile`；`app/src/main/cpp/43499glued/tests/*.cpp` 是“自包含 main()”风格（如 `profile_binary_test.cpp:303`），历史上由仓外 `make native-host-tests` 编译（见 `docs/archive/...`）。
- 结论：C1 的“host 编译零告警 + golden 逐键”需要**新增一个 host 构建入口**（Gradle task 或 `tools/tests` 脚本），这是 C1 的工程子任务（U9），不是设计阻断，但不能假设现成门禁存在。

---

## 8. 结论：C1 门槛

### 8.1 判定

- **C1-reader：具备条件，可立即写码。** 范围 = vendored mpack + `glkv3_document` + `document_pairs` + `payload_keys` + `key_catalog` + G1–G4/G7/G8。它不依赖 U1–U6，且上游 tests/fixtures 齐全。
- **C1-binder：不具备条件。** 按计划 §2“若映射存在无法裁决项 ⇒ C1 停下报 Lead（不猜、不复制旧 typed parse 绕过）”，当前有 **6 项阻断裁决**（U1–U6）+ 5 项 C1 任务输入（U7–U11）。binder 可以覆盖 **81/104 无争议键**，但其余 23 键（15 常量 + 5 无对应 + 1 别名 + 1 `safe_mode` + 1 选择 token）没有唯一正确动作；此时写码必然把某个猜测固化成 wire 契约。
- 因此：**完整 C1 不具备写码条件**；建议 Lead 裁决 U1–U6 后，按 C1a（reader）/C1b（binder）拆批。

### 8.2 还需要哪些 input（具体到 file:line）

| 需要 | 具体内容 | 位置 |
|---|---|---|
| U1 裁决 | root 选择到底读哪个键；是否保留 `backend/terminal` | `git show 2933b838^:core/src/profile/run_plan.cpp:66-90`；`d2778a1a^:.../NativeProfileGlkv3Adapter.kt:340-348`；`.../Glkv3Decoder.kt:49-61`；`FIX-TSV:2-9` |
| U2 判据 | strict 未知的范围/错误名/是否拒绝 `plugin.*`/43284 owner | 计划 §2/§4；当前忽略实现 `app/src/main/cpp/43499glued/profile/binary.cpp:288-324`；上游 Kotlin 拒斥 `NativeProfileGlkv3Adapter.kt:175-210,290-320` |
| U3 裁决 | 15 个常量键：忽略（带日志）/严格拒绝/加字段 | `key_catalog.hpp:61-65,111-113,138-140,151-154`；`kernel/target_constants.hpp:25-35`；`kernel/target.h:123-139`；`support/util.cpp:336-351` |
| U4 裁决 | `safe_mode` 是否接到 `meta.safe_mode`/root script | `NativeProfile.kt:435`；`profile/model.h:85`；`attack/ops.cpp:322,417` |
| U5 裁决 | `fallback_route`/`recommend_shizuku` 去留与来源 | `profile/binary.cpp:63-64`；`route/route_policy.hpp:239-247`；`data/BuiltinProfileCatalog.kt:42-44` |
| U6 裁决 | 重复键优先级 | `key_catalog.hpp:107,129,130,145`；`profile/binary.cpp:115-116,179` |
| U7 裁决 | C1 是否绑定 payload（还是只搬读取器） | `payload_keys.hpp:23-34`；`glkv3_document.cpp:215-300`；计划 §3/§5-I1 |
| U8 裁决 | `kernel_minor` 加字段/忽略/拒绝 | `key_catalog.hpp:164`；`NativeProfile.kt:32,564`；当前 `profile/model.h` 无字段 |
| U9 工程 | host 测试入口（C1 golden 运行方式、零告警门禁） | `app/src/main/cpp/43499glued/CMakeLists.txt`（只有 exec）；`app/src/main/cpp/43499glued/tests/profile_binary_test.cpp:303`（自包含 main 风格） |
| U10 裁决 | golden 权威序：SELECTION_HEX / FIX-TSV / FIX-DEV；FIX-DEV 缺 abi.kernel/abi.payload 时是否仍作 C1 主 golden | `profile-core/src/test/resources/*`；`Glkv3Golden.kt`；§1.4 |
| U11 裁决 | execution 默认值由 C1 拒还是 C2 补 | `Glued43499ProfileCodec.kt:80-101`；`ProfileMerger.kt:23`；`ProfileExporter.kt:65`；`ASSET(5.15.189...conf)`（无 execution）；`profile/model.h:18-35,240-244` |

### 8.3 建议批次切分（供 Lead 裁定）

- **C1a（可立即）**：mpack + `glkv3_document`/`document_pairs`/`payload_keys`/`key_catalog` + G1–G4/G7/G8；不接生产、不写 binder；host 构建入口（U9）。
- **C1b（待 U1–U6）**：`profile/document_bind.*`；先实现 81 个无争议键，再按裁决处理 23 个争议键；G5/G6/G9–G12；完成后再决定 U7/U11。
- 若 Lead 选择“最小参数化 GLK1 修复”备选（计划 §6 风险条）：本文件 §3 的“仅当前/语义不同”清单可直接作为修复清单，但需注意 H1 是当前实现的独立缺陷，不因放弃 GLKv3 而消失。

---

## 附录 A：104 键分类索引（便于 C1 验收勾选）

- **A 精确映射（82）**：
  - `abi.task_struct.*` 15；`abi.cred.*` 7；`abi.offset.*` 7；`abi.kernel.*` 2；`cred.*` 8；`kernel.*` 3；`offset.slide_*` 3；`execution.heap` 3（timeouts）；`execution.race` 4；`execution.stages` 8；`execution.handoff` 5；`execution.consumer` 2；`execution.recommended_cpus` 2；`route.multicast_waiter` 4（几何）；`route.select_stack` 4；`route.tcp_zerocopy` 3（tuning）；`root.kernel_major`；`root.safe_mode` ⚠。
- **B 别名（1）**：`execution.heap.kernelsnitch_collisions`。
- **C 常量（15）**：`abi.payload.*` 5；`execution.heap.snitch_{threads,round_wait_us,wake_rounds}` 3；`route.multicast_waiter.{attempts,arm_sequence,arm_hold}` 3；`route.tcp_zerocopy.{chunk_bias,cred_copy_off,fake_task_off,payload_delta}` 4。
- **D 无对应（5）**：`root.kernel_minor`；`offset.vr_sys_exit_tp`；`experimental`；`queue`；`steps`。
- **E 选择歧义（1）**：`backend.cve_2026_43499.route`。

## 附录 B：证据索引（本文件引用到的当前代码点）

- 当前字段表：`profile/binary.cpp:61-212`；结构体：`profile/model.h:18-147`；accessor：`kernel/runtime_struct_offsets.h:14-170`。
- 当前解析/序列化：`profile/binary.cpp:254-396`；入口：`profile/entry.cpp:20-116`；主入口：`main.cpp:35-120`。
- 当前 Kotlin 编码：`data/NativeProfile.kt:38-157,340-438,603-621`；`Profile.kt:26-100`。
- 当前 Kotlin 解析/合并：`ProfileResolver.kt:12-152`；`ProfileMerger.kt:23-118`；`ProfileExporter.kt:19-102`。
- 当前参数/流程：`Glued43499ProfileCodec.kt:39-101`；`Glued43499ExecParams.kt:16-94`；`Flow43499GluedEntry.kt:50-120`。
- 当前消费点：`session/backend/cve_2026_43499_backend.cpp:222-230,293-318,358,392-403,528`；`session/root_child_frontend.cpp:35-41,68-78`；`race/threads.cpp:28,39,71,122,149,180,195`；`support/util.cpp:193-221,336-351,563-566,704-709`；`route/route_policy.hpp:239-247`；`route/multicast_waiter_route.cpp:15-100`；`attack/ops.cpp:322,417`。
- 上游键/语义：见 §1.1 表。
