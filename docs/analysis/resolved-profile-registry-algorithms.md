# 详细设计附录：算法与代码逻辑走查（配套 resolved-profile-registry-plan）

> 分析类附录（≤8 KB）。补齐评审指出缺失的「算法 A1–A8 + 代码逻辑走查 T1–T8」半场。
> 与 [计划](resolved-profile-registry-plan.md) 同批；走查不通过 = 设计缺陷，必须在设计阶段修。

## A. 算法（前置 / 步骤 / 后置 / 不变量）

**A1 Kotlin：输入 → ResolvedProfile**（`ProfileLoader.load`）
- 前置：设备 uname；`activeBuiltinRelease() ?: deviceRelease`；用户档选择（prefs）与 imported-only 设备路径（无内置档时）。
- 步骤：① include 展开（**两个解析器**：资产文本级 / 存档目录级）② parse ③ wrapper 解包 ④ 别名归一（flat `backend.steps`：**legacy step id → 迁移**；**token 形态 → 拒**；ABI 拆分；`meta.*`→根标量（在 LPC）⑤ 校验：未知 canonical 键拒（4 类）/`available` **8 拒**/queue **7 拒**/回显逐值相等/queue_route 与几何 route 冲突 ⑥ 分层合并（**优先级：overrides > imported > builtin > tuning preset**）+ CPU 对 + route 预设 ⑦ 组装 tree（root + 选中 owner 段 + **declarations**）⑧ `safe_mode` 补丁（落 `Glkv3Decoder.patchSafeMode` 的等价位置）⑨ 逐字段对 manifest（类型/位宽/必填/来源）。
- 后置：`ResolvedProfile{tree, declarations, provenance, diagnostics}`；失败经 `LoadResult{profile?, diagnostics}` 返回（**不再 runCatching 吞掉**）。
- `available` 8 拒枚举：未知 backend / 列表形态 ×2 / 未知键 / route-not-applicable / 未知 route / experimental 非 bool / priority 非正整数。
- 不变量：段名 ∈ 声明 owner；键唯一；类型逐行符 manifest；queue 仅在声明键。

**A2 Kotlin：tree → wire**（`Glkv3Writer.encode(tree) → ByteArray`）——根 9 键 UTF-8 序 / sections+entries 排序 / 最短整数 / str ≤256B / 4B 大端前缀；**会话帧由 `ChannelBStdin.appCall(bytes, frame)` 另行拼装**。

**A3 Native：字节 → Document**（`frame_v3` + `Builder`）——① 根标量（`kernel_major/minor/safe_mode` → `kRootSection`）② **段名 ∈ {根段, 选中 owner}**，否则 `PathNotInSelection` ③ 键查声明表（未声明数组 → `UndeclaredArray`；Union/Bin/Map → `TypeMismatch`）④ 类型化入列，重复键 → `DuplicateKey` ⑤ `finish()`。
- 不变量：**(seg,key) 唯一 / 只含选择路径 / 取值只经 `find` / 不可复制**；**缺键 = 无 Entry（取消 `Value::present`）**。

**A4 Native：Document → 绑定**（`bind_view<B>(document, state, sink)`，定义在各 backend）——① `Registry::select<B>()` 编译期选行 ② `bind_all<B::Schema>`：缺键∧required → `MissingRequired`；位宽不符 → `WidthMismatch`；token 未命中 → `UnresolvedToken`；缺键∧有默认 → 采用 + `DefaultUsed` ③ 能力位 ④ 构造 `session::ResolvedProfile`（`document()/target() → const **profile::TargetProfile**&/cpus()/capabilities()/diagnostics()`）。

**A5 Registry**（编译期投影）——从 `kCombinationCatalog` 派生行 → `unique_paths(span<PathSpec>)`（constexpr 排序+相邻比较）→ `row_count == 110` → 每 available 组合有 target；三条 `static_assert`；**`pipeline/` 自我约束：只 include contract/backend/session/profile**（`pipeline` 不在 R1 8 条规则内）。

**A6 选行**——`if constexpr` 展开取代手写 switch；锁点 `pipeline.hpp:57-59`、`component_catalog.hpp:48-92`、`component_catalog_test.cpp:155-176`。

**A7 step 组装**——`StepSetRegistration<Steps...>::run(ctx)` 顺序展开：任一 `Failed` 停止；`Done` 立即返回；全 `Continue` 返回 `Continue`；`deps` 只引更低槽位。

**A8 CPU 单一来源**——消费点 4 处：`cve_2026_43499_backend.cpp:82`（pin_to_core）、`race/threads.cpp:173-174`、`bootstrap.cpp:27-28`、`core_session.cpp:6`（默认对，删除）。

## T. 代码逻辑走查（状态→操作→状态→不变量）

样例：release `6.1.138-android14-11-g0c3d559bcd85-ab14529422`、43499、`tcp_zerocopy`、queue `[w1,w2,w3]`、`w1_attempts=15`。

**T1 输入→tree**：磁盘文本 →（include 展开）单文本 → parse `ValueMap` → 解包 → 归一（无 token steps）→ 校验（8 拒/7 拒未触发）→ 合并+预设（`w1_attempts=15`）→ tree（根 + `""`/`backend.cve_2026_43499` 段 + declarations）→ 对 manifest 全命中。**结论：成立**。
**T2 tree→wire**：9 根键排序 → sections/entries 排序 → `UInt(15)` → `0x0F`（1B）→ 4B 前缀（43284 才加帧）。**成立**。
**T3 字节→Document**：长度校验 → decode（`schema==3`）→ 段名判定（仅根段/选中 owner；**非选中 owner ⇒ 拒绝**）→ 键查表 → `add_u64` → `finish`。**成立**（两处历史静默丢弃消失）。
**T4 Document→绑定**：`select<B>` → `static_assert` 双向 → `bind_view<B>`（缺 `w1_attempts` ⇒ `MissingRequired`）→ 冻结存储（520B/784B 不变）→ `ResolvedProfile`（中性类型）→ 诊断含 `DefaultUsed`。**成立**。
**T5 注册与查重**：收集路径（92+9+6 plugin+3 root = **110**）→ constexpr 排序 → 相邻比较（重复即编译失败）→ available 组合 7 行有 target → 导出 8 列 TSV ×2（命令不变）。**成立**。
**T6 step 执行**：`W1W3.run` → w1（用 15 次尝试）→ w2 → w3 → `Continue`；任一 `Failed` 停止；`Done` 早退。**成立**。
**T7 失败路径**：`MissingRequired` / `WidthMismatch` / `PathNotInSelection` / `UndeclaredArray` / `TypeMismatch` / `DuplicateKey` / 未支持能力（类型化错误）/ queue 缺失（Kotlin 拒，`reason=queue-required`）/ `DefaultUsed`（继续）。**无未定义分支**。
**T8 结论**：T1–T7 逐步成立；走查内已修 3 处缺陷（Document 改 class；两处静默过滤改 fail-closed；43499 缺默认 + 全 `required=false` 的静默 0 由 D-G 修）。

## 遗留（由门禁关闭，不在设计内假设）

构建/测试未实跑；A1–A8 正确性未独立审计；ADR-0001/0002 全文未读；`ResolvedProfile` 与槽位测试相容性未验；66 资产 required 可行性未逐一验证；B4.2 与 `kStepSetAliases` 绑定机制未展开；golden 未逐字节全核。
