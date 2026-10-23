# JNI 重写附件：状态机 / JNI 不变量 / 43284 映射（2026-10-22）

> 从属 [43499-43284-jni-rewrite-plan.md](../plan/43499-43284-jni-rewrite-plan.md)。本件只放可执行细则，不放新裁定。

## A. 无状态函数契约（用户 2026-10-22 约束）

- **每个 native 函数独立**：输入 POD（caller 分配）+ 输出缓冲；**不引用全局/静态/外部变量**；跨调用值由 caller 放进下一次调用的 POD。
- 无 create/destroy/abort、无 handle、无状态机；fd/mmap/页/线程由 caller 创建与关闭，native 只按参数操作。
- 取消：caller 传 `atomic stop` 字；native 每轮检查；不建 native 状态。
- actor batch：一次调用内创建/join 三线程，共享字/stop 由 caller 传入；不跨调用保存；待批 T-PI2。
- 错误码：`0 ok / -1 not-ready / -2 bad-param / -3 unsupported / -4 failed / -10 cancelled`；无 handle 相关码。
- 单测：同一函数用不同 POD 两次调用结果一致；无跨调用污染；NULL/越界 fail-closed；stop 置位后下一轮返回 `-10`。
- **无兜底**：未实现原语不导出；缺参数 `-2`；无 ENOSYS 占位、无默认值、无旧实现回退。

## B. JNI 四不变量（N1）

| # | 不变量 | 判据 |
|---|---|---|
| J1 | ByteArray 入参只用 `GetByteArrayRegion` 拷贝到固定缓冲；不用 `GetPrimitiveArrayCritical`；不缓存 `jarray` | 单测：正常/越界/空数组；代码扫描无 Critical |
| J2 | 直接 ByteBuffer：`GetDirectBufferAddress` 为 NULL 或 capacity < 期望 ⇒ `-2`；不 retain | 单测：NULL/短容量/正常 |
| J3 | 每次 JNI 调用后 `ExceptionCheck`；异常挂起时不再调 JNI；native 线程不 Attach | 单测：注入异常后无二次调用 |
| J4 | 返回 ByteArray 用 `NewByteArray`+`SetByteArrayRegion`；局部引用 `PushLocalFrame(16)/PopLocalFrame`；不跨调用保存 jobject | 单测：1000 次调用引用计数稳定 |

## C. 43284 历史 13 文件逐文件映射（C21；2.0.1 = 5479d2c6）

| 历史文件 | 处置 | 目标 |
|---|---|---|
| Native43284.kt（374 行，36 external） | 参考形状，不照搬；收敛为计划 §4.4 原语 | jni/cve43284/Native43284.kt |
| Assembled43284.kt | 复用设计（页/载荷装配） | backend/cve43284/Assembled43284.kt |
| AssembledPageCacheAccess.kt | 复用设计 | backend/cve43284/PageCacheAccess.kt |
| CarrierCandidates.kt | 复用设计 | backend/cve43284/CarrierCandidates.kt |
| ModulePrecheck43284.kt | 复用设计 | backend/cve43284/ModulePrecheck.kt |
| ModuleVermagic43284.kt | 复用设计 | backend/cve43284/ModuleVermagic.kt |
| NativePayloadProbe.kt | 不采用（诊断，J7 删） | — |
| NativeRawEsp.kt | 参考形状，新写 | jni/cve43284/NativeRawEsp.kt |
| PlanClosure.kt | 复用设计 | backend/cve43284/PlanClosure.kt |
| SaFrame.kt | 复用设计 | backend/cve43284/SaFrame.kt |
| SequenceContinuation.kt | 复用设计 | backend/cve43284/SequenceContinuation.kt |
| TriggerSpec43284.kt | 复用设计 | backend/cve43284/TriggerSpec.kt |
| WaitPolicy43284.kt | 复用设计 | backend/cve43284/WaitPolicy.kt |

`engine/pagecache` 14 文件按组处理：不采用 Stage 形态，映射为 PipelineEngine 节点（J2c）。native `core/src/backend/cve_2026_43284` 34 文件只作行为参考。

## D. 原语三类（N3）

| 类 | 定义 | 原语 |
|---|---|---|
| 逐 syscall（第 1 层） | 单 syscall + 原始返回 | create/destroy/abort/readKernel/writeKernel/schedAffinity/log |
| 复合原语 | 多 syscall、无决策、无重试；必须列 syscall 集合与边界 | preparePage（memfd/mmap/sendmsg/spray）、discover（扫描循环+防 spin）、perfTask（perf_event_open/mmap/采样/close）、espOpen/writeBlock、pagecacheOpen/write/read/adopt、hookParams/trigger/wait/restore |
| actor batch（第 2 层，待批） | 三线程 PI 固定序列 | actorBatch |

复合原语判据：列出 syscall 集合、步数/字节/超时上界、失败 errno；不得内部决策/重试/回退（否则归 Kotlin）。

## E. copycheck 规则（C10）

- 工具 `tools/copycheck.py`：owner=build，J0b2 写项；输入新 C 文件 vs legacy/very_legacy 同主题文件。
- 规则：token 8-gram Jaccard > 0.6 ⇒ 必须书面归因并由 reviewer 签名；≤0.6 记入对照表。
- 阈值依据：J0b2 用 3 组已知复制样本 + 3 组独立实现校准，校准记录归档；未校准前 0.6 只作 warning，不阻断。
- 替代判据：计划 §7 对照表逐行签名 + 抽查记录（≥max(5 文件,10%)）归档。

## F. 设备日志判据（N2）

```text
设备 Download/ghostlock-debug-log/<时间>/*.log.txt
同目录 profile.conf / profile.bin
判据：route 命中、写验证、child uid=0、无 panic
归档：docs/analysis/device-gates/*.md（≤8 KB）+ 原始日志路径
```

## G. verifyJniLibs 处置（N4）

- J0b2 先核实当前是否红（kt-step 记为既有红/43284 缺库）。
- 红 ⇒ 修复，或登记 T-VJL（owner=build，J4 偿还）并豁免；G1 在 J0b2 前不挂 `verifyJniLibs`。
## H. 行为对照表（C3/C18）

| 原语 | legacy 参考（只读） | 新实现 | 对照项 |
|---|---|---|---|
| preparePage | legacy/memory/payload_builder.cpp、session/exploit_session.cpp | glk43499_jni.c | 入参/分配顺序/页布局/返回 |
| actorBatch | legacy/race/pi_race.cpp、threads.cpp | glk43499_route_*.c | 线程数/futex 顺序/join/errno |
| discover/perf | legacy/kernelsnitch/* | glk43499_jni.c | 扫描边界/候选顺序/防 spin |
| 43284 ESP/door | 2.0.1 backend/*（只读） | glk43284_*.c | 页对齐/块大小/序号/返回 |

每行必填：原语/参考 file:line/新实现 file:line/入参/返回/errno/差异/审查人/抽查 n/N；任一空=FAIL。