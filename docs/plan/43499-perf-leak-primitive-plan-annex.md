# 43499 perf 泄漏原语 L 设计 · 附件 A（ABI / 解析与向量 / 设备证据 / 生命周期 / 影响文件）

- 主体：[43499-perf-leak-primitive-plan.md](43499-perf-leak-primitive-plan.md)（唯一设计权威；本附件只承载明细，不另立结论）。CBN/风险/裁定记录见 [附件 B](43499-perf-leak-primitive-plan-annex-b.md)。
- 日期 2026-10-27（修订 3，H1/H2/H3=ABA 已并入）；作者 writer r3e-impl（task-43）；快照 HEAD `a9af5482`+R1–R3d 未提交工作树。
- 配额例外：Lead 2026-10-27 批准「主件 ≤16 KiB；附件各自 ≤16 KiB」。

## A. ABI 明细

### A1 新结构（`core/perf.h` NEW；公共头不得 include Linux 头，保证 host `abi_layout_probe` 可编译）

- `struct glk43499_perf_open_request`：abi 头 + **15×u64**：`pid`（0=self；否则 victim TID，域 0..INT32_MAX）、`cpu`（-1；per-task 要求 -1）、`attr_type`、`attr_config`、`attr_sample_period`、`attr_sample_type`、`attr_sample_regs_intr`、`attr_flags`、`attr_disabled`、`exclude_user`、`exclude_hv`、`exclude_idle`、`page_size`、`ring_pages`、`auto_enable`。
- `struct glk43499_perf_open_result`：`struct glk43499_call_result call`。
- `struct glk43499_perf_collect_receipt`：`data_offset`、`data_size`、`data_head`、`data_tail_before`、`bytes_copied`、`tail_moved`（6×u64，**绝对位置语义**见 B2）。
- `struct glk43499_perf_collect_result`：`call` + `receipt`；close 复用 `struct glk43499_close_result`（48）。

### A2 sizeof（`_Static_assert`+host probe；**4 项全纳入**）

| 结构 | sizeof |
|---|---|
| perf_open_request | **128**（8+15×8） |
| perf_open_result | **32** |
| perf_collect_receipt | **48**（6×8） |
| perf_collect_result | **80**（32+48） |

既有 20 个结构 sizeof 全部不变（清单见 `43499-r1-freeze-abi.md`）。

### A3 C 原型与调用语义（**无状态句柄 H3-a**）

```c
glk43499_handle glk43499_perf_open(const struct glk43499_perf_open_request *request,
                                   struct glk43499_perf_open_result *result);
int32_t glk43499_perf_collect(glk43499_handle perf, uint8_t *out,
                              uint64_t out_capacity,
                              struct glk43499_perf_collect_result *result);
int32_t glk43499_perf_close(glk43499_handle perf,
                            struct glk43499_close_result *result);
```

- **open**：先校验输出 ABI（`call.abi.struct_size==sizeof(perf_open_result)`），再输入 ABI/域（A4）；`perf_event_open`→`mmap`→（`auto_enable=1`）`ioctl(ENABLE)`。成功返回新 handle；失败返回 **0** 且 `result.call` 有效（token 未创建、失败路径已释放 fd/mmap、`retained=0`）。
- **collect**：先校验输出 ABI；`out` 由调用方提供、`out_capacity` 为字节容量；`out_capacity<data_size`⇒`INVALID_ARGUMENT`（token 不消费）。成功=disable→acquire fence→整块 copy `data_size` 字节到 `out`→`data_tail=data_head`；receipt 写绝对位置（B2）；返回 call_status；token 不消费。**无状态（H3-a）**：Native 不记录/不校验 OPEN/COLLECTED，重复 collect=机械重读 head/拷贝/tail（无 BUSY/无状态错）；Kotlin owner 保证 collect 恰一次、close 唯一。
- **close**：先校验输出 ABI；输出 ABI 通过且 token≠0 即**接受**并**消费 token**（无论 call_status/retained）；执行 best-effort disable→`munmap`→`close(fd)`；接受后 token 永不复用（重复 close 属不支持调用）。
- JNI 三态（`jni43499.c:20-36`；`Native43499Jni.kt:326-339`）：NOT_ACCEPTED=未调用 core/未消费 token；INCOMPLETE=fail-stop；正常返回按 call_status 结算。

### A4 校验矩阵（Native 只做机制/ABI/域校验；**不校验状态**）

| 校验 | 入口 | 失败 |
|---|---|---|
| 输出 ABI 前缀 | open/collect/close | 入口未接受（不消费 token、不写回执） |
| 输入 ABI 前缀 | open | `BAD_ABI` |
| `page_size==sysconf(_SC_PAGESIZE)`；`ring_pages≠0`；`page_size*(ring_pages+1)` 不溢出 | open | `INVALID_ARGUMENT` |
| `pid`/`cpu`/`auto_enable` 域；`pid>0 && cpu!=-1` | open | `INVALID_ARGUMENT` |
| `out!=NULL && out_capacity≥data_size` | collect | `INVALID_ARGUMENT`（token 不消费） |
| **不校验 handle 状态/kind**（H3-a；伪造/复制/复用/重复 close 属不支持调用，Kotlin owner 负责） | collect/close | 按契约 |

### A5 attr 字段策略

请求字段（Kotlin 显式给值）：`attr_type/config/sample_period/sample_type/sample_regs_intr/flags/disabled`、`exclude_user/hv/idle`、`pid/cpu/page_size/ring_pages/auto_enable`。**未列 attr 字段=机制零值**：Native 以 `struct perf_event_attr attr = {0}` 初始化后只填请求字段；`exclude_kernel`、`precise_ip`、`wakeup_events/watermark`、`sample_max_stack`、`inherit`、`mmap/mmap2/comm/task`、`namespaces/cgroup/aux_watermark`、`sample_id_all`、`context_switch`、`freq`、`use_clockid/clockid` 等保持 0（零=机制默认）。Kotlin 交叉校验 `sample_type` 位、`sample_regs_intr` popcount 与 `register_count` 一致；G1 记录实际 `attr.size`。**说明（非用户例外）**：未列字段=机制零值，非目标/容量/协议参数；若与「全部参数化」字面冲突由 Lead 再报用户。

### A6 close 失败语义

| 情况 | call_status | effect | retained | token |
|---|---|---|---|---|
| disable/munmap/close(fd) 全部成功 | `OK` | `CLEAN_SUCCESS` | 0 | consumed |
| disable ioctl 失败（仍继续释放） | `SYSTEM_ERROR`+errno | `DIRTY` | 0 | consumed |
| `munmap` 或 `close(fd)` 失败 | `SYSTEM_ERROR`+errno | `DIRTY` | **1**（不 free，进程退出兜底） | consumed |
| 输出 ABI 无效 / token=0 | 入口未接受 | — | — | **未消费** |

close 失败不改变已取得地址有效性（地址已独立校验）；Kotlin 记 `perfCloseRetained=1`。

### A7 JNI / Kotlin DTO

- `Native43499Jni.kt` 新增 `@Keep`：`Native43499PerfOpenRequest`（15 字段全必填）、`Native43499PerfOpenResult`/`Native43499PerfCollectResult`（后者+6 字段），复用 `Native43499CloseResult`；新 extern `openPerfSampler/collectPerfSamples/closePerfSampler`。
- output ByteArray 只在 core 返回后复制（`jni43499.c:5-6` 不变）：core 写 JNI 临时缓冲→`SetByteArrayRegion`→free；open 请求栈上组装（`GLK43499_INIT_ABI`）。

### A8 参数化/零硬编码

Native 不新增目标/容量/协议数值宏；`PERF_EVENT_IOC_*`/`SYS_perf_event_open` 取自系统头；`page_size` 请求传入并与 `sysconf` 交叉校验；ring/寄存器/候选/窗口/tag/对齐/票数门槛全部 debug 配置→Kotlin→请求/解析器（Native 无 `4096/256/32/0xf<<56` 类 token）。`scan_native_literals.py --gate r2` 须重签 allowlist（新 token 逐条具理由、sha256 记录）；`BANNED_MAGIC_TOKENS` 不得命中。

## B. Kotlin 解析与单测向量

### B1 记录布局与期望长度（**280B**，由配置推导）

| 偏移 | 字段 | 说明 |
|---|---|---|
| +0 | `type` u32 | `PERF_RECORD_SAMPLE=9` 才是样本 |
| +4 | `misc` u16 | 忽略 |
| +6 | `size` u16 | **窄字段**（退役曾把 size 读成第二个 32 位字，d5a46686 消息） |
| +8 | IP u64 | 仅当 `sample_type` 含 `PERF_SAMPLE_IP` |
| +16 | ABI u64 | 1=64 位寄存器组；0=无寄存器载荷；2=32 位 compat |
| +24 | regs[register_count] u64 | x0..x30,sp（`sp_register_index` 配置）；仅 ABI=1 |

**期望长度推导**（Kotlin 纯函数，输入=`sample_type` 位/`register_count`/`ip_bytes`/`abi_bytes`）：`expected = header_bytes + (has_ip?8:0) + (has_regs_intr?8:0) + (abi==1? register_count*8 : 0)`；本配置=**280**。ABI=0 合法记录=**24**（header 8+IP 8+ABI 8，无 regs；10-08 修正：探针 bit 错）计为 no-registers；ABI=1 size≠expected（含 272/279）⇒`perf-record-invalid`；ABI=2⇒`perf-sample-abi32-unsupported`；`size==0` 结束走查。**禁止写死 280**：由配置推导，测试断言推导值=280。

### B2 环语义（单调绝对位置 + 未消费字节判 overflow）

`data_head/data_tail` 是自事件创建起累计写入/读取的**绝对字节数**（单调，非环内下标）；环内定位=`pos % data_size`。判据：`unconsumed = data_head - data_tail_before`；`unconsumed<=data_size`⇒可用（**允许 `head>data_size` 正常回绕**）；`unconsumed>data_size`⇒记录被覆盖⇒`perf-ring-overflow`。走查：pos 从 `tail_before` 到 `data_head` 逐记录前进，字节经 `%` 取模组装（跨环首尾拼接）；`pos+size>head`⇒`perf-record-invalid`；结束 `tail_moved=head`。receipt：`data_offset`/`data_size`/`data_head`/`data_tail_before`（绝对）/`bytes_copied=data_size`/`tail_moved=1`。

### B3 地址推导/校验（legacy 语义 + 新增假设 + **H1-a 唯一策略**）

1. `restored = v | (tag_nibble << tag_shift)`（A301 `0xf/56`=`execution.leakTagNibble/Shift`；0/0=不还原）——legacy `ops.cpp:488-491`。
2. 仅严格开区间 `identityStart<restored<identityEnd` 入候选；`candidate_capacity=256` 满停——legacy `ops.cpp:473-496`。
3. 投票：频次最大；平票取先出现（严格 `>`）——legacy `ops.cpp:499-509`。
4. **H1-a 方案 B（用户裁定，唯一方案）**：a) 一致性=**≤3 pass、任意两个 pass 地址相等（2-of-3）才采信**（取代 legacy 相邻一致）；无任一候选出现在 ≥2 pass⇒`perf-agreement-failed`。b) **门槛只作用于 2-of-3 选中的地址，按同意 pass 的票数聚合**：`agree_votes=Σ votes(p) (winner_p==A)`、`agree_candidates=Σ candidates(p) (winner_p==A)`、`threshold=max(min_votes_floor=64, ceil(min_votes_ratio=0.25 × agree_candidates))`；判定顺序=①infra/parser 失败（open/collect/pump/parser）立即 abort；②无 2-of-3 地址⇒`perf-agreement-failed`；③`agree_candidates<min_votes_floor`⇒`perf-insufficient-candidates`；④`agree_votes<threshold`⇒`perf-votes-below-threshold`；⑤（可选）对齐/SP；⑥Acquired。**低票异地址 pass 不 abort、不参与门槛**。c) **对齐/SP 默认关闭**：`require_alignment=0`/`require_sp_evidence=0`（仅记 `alignmentOk`/`spEvidenceOk`）；G1 后按证据收紧为 1。**声明：0x200 对齐/SP reg[31] 是新增假设、非 legacy；反例=10-06 非对齐候选、mondrian `ffffff80ffffffd0`。**
5. `Acquired(address, evidence)`/`Missing(reason)`；reason 并入错误串，evidence 含 `perfPasses/perfWinnerVotes/perfCandidates/perfSpVotes/alignmentOk/spEvidenceOk`。

### B4 单测向量（“能失败”用例显式；JVM `app/src/test/.../perf/`）

- 合法：280B ABI=1（期望 winner=restored reg）；24B ABI=0（计 no-registers，全 0⇒`perf-registers-not-delivered`）。
- **能失败**：272/279/281B ABI=1⇒`perf-record-invalid`；`size<24`⇒invalid；ABI=2⇒`perf-sample-abi32-unsupported`。
- **能失败**：`unconsumed>data_size`（head=9000,tail=0,data=4096）⇒`perf-ring-overflow`；**通过**：`head>data_size` 但 unconsumed≤data_size（head=5000,tail=1000,data=4096，含跨环记录）。
- **H1-a/B 门槛（同意 pass 聚合）**：2 pass 各 63 票⇒`agree_votes=126<128` 拒；各 64 票⇒128≥128 过；`agree_candidates<64`⇒`perf-insufficient-candidates`（先于票数判定）；比例 0.24/0.25 边界。
- **2-of-3 与顺序**：p1≡p2 采信（P1/P2 都计入）；p1≡p3 采信；p2≡p3 采信；全不同⇒`perf-agreement-failed`；**低票异地址 pass 不 abort**（10-06：p1=A(149)/p2=B(34,非对齐)/p3=A(162)⇒A 选中，agree_votes=311、agree_candidates=512、threshold=128⇒Acquired）；infra/parser 失败⇒立即 abort（`perf-no-samples` 等）。
- **对齐/SP 开关**：默认关——`alignmentOk=0` 仅 evidence、Acquired；`require_*=1` 时同输入⇒`perf-address-untrusted:*`。
- tag 还原 `0xf0ff800000100000→0xffff800000100000`；平票取先；开区间两端拒绝；10-03 真值 `0xffffff896fe31200` 作期望 winner；mondrian/`…5500` 负例。
- fake Native 顺序/重复 collect 机械返回（H3-a）/finally close/close 失败不丢地址；Flow：seam 新签名、acquire 一次、pump 时 openChains==0、W3 复用缓存、**w2-precheck 四分支**。

## C. 设备证据（四列 + 明确 App 跨任务零证据）

| # | 证据 | 执行上下文 | pid 目标 | 跨任务? | 候选绑定? | 事实/推出 |
|---|---|---|---|---|---|---|
| ① | A2-1 2026-10-03 A301SO | shell（uid 2000，`u:r:shell:s0`） | pid=0（victim 自采样） | 否 | **绑定**：winner `0xffffff896fe31200`（134/173 of 256），W2 后 `child uid=0`（A2-1:96-97,137-138） | 该设备 **shell 自采样**可行 |
| ② | 20261006-015621 | shell | pid=0（victim 自采样） | 否 | **部分**：p1 `0xffffff896d2c1200`(149)/p2 `0xffffff8935335500`(34,非 0x200 对齐)/p3 `0xffffff896d2c1200`(162)；legacy 相邻一致失败换轮，下轮 178/160 绑定 `child uid=0`（:99-108,146-147） | 候选可**非 0x200/低票**；**方案 B 下低票异地址 pass 不 abort**，A 由 2-of-3 选中、门槛按同意 pass 聚合 |
| ③ | 2026-10-08 finding | shell | pid=0（自采样） | 否 | **未绑定**：探针 bit 错致 `abi=0`（finding 已修正） | 非内核不投递；262145 待真机 |
| ④ | mondrian 2026-10-20 | app（`setenforce 0` 后） | pid=0（app 自采样） | 否 | **未绑定**：候选 `ffffff80ffffffd0` 非 0x200 对齐；select 写后 uid/comm 不变（:32-34） | app 可开 perf 但候选不可信/写不落 |
| ⑤ | **本设计生产形态** | App（untrusted_app/app uid） | **victim pid** | **是（跨任务）** | **G1 零证据** | **未验证**（G1 首证点） |

**结论**：现有全部证据都是 **shell/app 自采样（pid=0）**；**「App 进程跨任务采样」在 A301SO 零证据**，设计不得当成已验证事实；G1 为生产启用硬门（H2-b）。

## D. 生命周期明细（**无状态句柄 H3-a**）

| 对象 | 创建 | 唯一 owner | 借用 | 终结 |
|---|---|---|---|---|
| perf handle（不透明 token） | `openPerfSampler` | Kotlin 目标源（每 pass 一个） | collect | `closePerfSampler` 消费（finally，唯一一次） |
| fd / ring mmap | open（Native impl） | Native impl | ioctl/collect | `munmap`→`close` |
| JNI 临时缓冲 | 每次 collect（malloc） | `jni43499.c` | core 写 | 返回前 free |
| raw ByteArray / 地址值 | 每 pass / 解析后 | 目标源→`run.victimTaskAddress` | parser / W2·W3 planner | GC / 不跨 run 复用 |

**无状态**：Native impl 不保存/不校验 OPEN/COLLECTED/CLOSED（重复 collect 机械重读，同 A3）；close 唯一性由 Kotlin owner 契约保证（重复 close/close 后使用=不支持调用；owner 合同测试见附件 B §H3）。**顺序**：collect=disable→fence→copy→tail；close（含 open 后未 collect）=best-effort disable→munmap→close fd。**dirty/retained**：munmap/close 失败⇒DIRTY/`retained=1`/不 free/token 已消费；不影响已得地址。**失败/取消**：pump/解析/不可信/取消⇒`finally close`；无 Cleaner/无全局注册。**PI**：三入口只在 W2 step 入口前后调用；Flow 断言「pump 时 openChains==0 且无 executeActor 进行中」；Native 不在 actor 线程/`executeActor` 内调用；不把 perf syscall 放进 PI 内。**并发**：单线性 owner、同 handle 不并发。

## E. 影响文件清单（实现批 write scope；本批不动）

- Native：`app/src/main/cpp/gl43499/core/perf.h`(NEW)、`core/perf.c`(NEW)、`core/api.h`、`jni43499.c`。
- Kotlin：`jni/Native43499Jni.kt`、`backend/cve43499/perf/{Cve43499PerfNative,Cve43499PerfSampleParser,Cve43499PerfTargetSource}.kt`(NEW)、`flow/{Cve43499WriteChain,Flow43499Impl}.kt`、`data/cve43499/A301SoDebugConfig.kt`、`ui/screen/module/backends/FinishHandoffAdapter.kt`。
- 资产：`app/src/debug/assets/profile/43499/debug-a301so.conf`（+`perf` 段）。
- 测试：新增 `app/src/test/kotlin/com/ghostlock/app/backend/cve43499/perf/{Cve43499PerfSampleParserTest,Cve43499PerfTargetSourceTest}.kt`、`app/src/androidTest/kotlin/com/ghostlock/app/cve43499/PerfMechanismProbeTest.kt`；改 `Cve43499WriteChainFlowTest.kt`、`A301SoDebugConfigTest.kt`。
- 工具：`tools/scan_native_literals.py`（allowlist+签名，机械改动）、`tools/abi_layout_probe.c`（+4 sizeof）。
- **其余文件冻结**（C1）；本批（设计批）零实现写入。
