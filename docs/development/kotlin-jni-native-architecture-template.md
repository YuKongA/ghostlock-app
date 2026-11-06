# Kotlin / JNI / Native 架构计划附录模板

<a id="cross-platform-appendix"></a>

> **代号范围**：`E-DOC-RULE-001/跨端附录`；旧 A–N、D.1、E.1 等仅是此附录的问题组。跨文档须带“跨端附录”限定；问题表不成为第二套批准或验收规则。

> 本文件是 [`documentation-standards.md`](documentation-standards.md)“L 级计划”模板的 JNI 架构专用附录，不是第二套通用计划规范。
> 新计划仍放在 `docs/analysis/<主题>-plan.md`，必须先完整保留通用模板八节，再按需加入本文问题。
> 本模板不预选层数、库形态、ABI、handle、Flow 或资源模型；没有证据需要的章节直接删除。

## 使用方式

1. 先复制 `documentation-standards.md` 的 L 级计划模板。
2. 在“目标与约束、数据流/控制流差异、改动清单、验证矩阵”中选用本文问题。
3. 先回答当前必要性；CBN 的适用条件与唯一表格见 [E-REVIEW-RULE-001/§2：成本权衡](design-review-principles.md#review-cbn)，不因套用模板重复填表。
4. 正文记录 branch、commit 和版本；输入识别按 [E-REVIEW-RULE-001/G4](design-review-principles.md#review-g4)，必要身份信息进入已有审核记录。design-critic 对应 gate 的阻断为 0 且用户批准后才实现。

## 问题组 A：现状与基线

将本组片段嵌入通用计划的“现状与基线”。

| 项 | 内容 |
|---|---|
| branch / commit | `<BRANCH>` / `<FULL_COMMIT>` |
| 当前入口 | `<KOTLIN/JNI/NATIVE_ENTRY>` |
| 当前行为 | `<OBSERVED_BEHAVIOR>` |
| 当前 owner | `<RESOURCE_OWNER>` |
| 已知问题 | `<FACT_WITH_FILE_LINE_OR_LOG>` |
| 已验证范围 | `<HOST/BUILD/DEVICE_EVIDENCE>` |
| 未验证范围 | `<UNVERIFIED>` |

附当前依赖图和调用图。图只描述事实，不把目标结构写成现状。

## 问题组 B：目标、约束与明确保留

将本组问题嵌入“目标与约束”；保留项表嵌入“明确保留”。

### B.1 问题

- 哪些决策现在放错层？
- 哪个数据或状态存在多个权威？
- 哪条读取、校验或清理路径重复？
- 哪个物理操作不能安全拆分？证据是什么？

### B.2 非目标

- `<OUT_OF_SCOPE>`
- 不借架构重构改变既有参数、业务含义或支持状态。
- 不创建临时旁路、第二执行入口或兼容壳。

### B.3 明确保留片段

| 保留项 | 原因 | 保护判据 |
|---|---|---|
| `<FILE/SYMBOL/BEHAVIOR>` | `<WHY>` | `<TEST_OR_HASH>` |

## 问题组 C：权威与层级

适用时嵌入“数据流/控制流差异”。

先填数据权威，再决定是否需要分层：

| 数据/状态 | 唯一生产者 | owner | 消费者 | 允许校验 | 禁止的第二权威 |
|---|---|---|---|---|---|
| `<ITEM>` | `<PRODUCER>` | `<OWNER>` | `<CONSUMER>` | `<CHECKS>` | `<FORBIDDEN>` |

逐项回答：

1. 该逻辑是业务决策、跨平台数据处理，还是必须靠 Native 完成的物理机制？
2. 它是否是独立服务？若是，结果能否复制为普通 DTO 后由上层再分配？
3. 它是否被两个以上 Native target 真实复用？若没有，为什么需要公共库？
4. 哪些依赖边必须禁止？如何用 include/link firewall 证明？
5. 新增同类 backend/service 时需要改几处？哪些层不得变化？

画目标依赖图，并列允许边与禁止边。不要预设必须存在 common、general 或多个 backend。

## 问题组 D：接口与 ABI

适用时嵌入“数据流/控制流差异”。

### D.1 接口清单

| entry | 输入 | 输出 | 前置条件 | effect | owner/borrow | 失败语义 |
|---|---|---|---|---|---|---|
| `<ENTRY>` | `<INPUT>` | `<OUTPUT>` | `<PRECONDITION>` | `<EFFECT>` | `<OWNERSHIP>` | `<STATUS>` |

### D.2 ABI 决策

- 为什么需要 JNI，而不是纯 Kotlin 或进程接口？
- 需要具名 typed request，还是简单标量足够？
- 哪些字段固定宽度？字节序、长度、上限和 0/1/MAX 行为是什么？
- 哪份定义是唯一权威？如何生成或逐字段对拍 Kotlin/Native 两侧？
- errno 与业务状态如何分离？unknown、dirty、partial effect 如何表达？
- Native 可以检查哪些字段关系？未登记的关系必须禁止重算。

禁止直接跨 JNI 传 C/C++ pointer、容器、view 或生命周期不明确的引用。

## 问题组 E：跨调用资源

存在跨调用资源时嵌入“数据流/控制流差异”；否则不使用本组。

仅当对象同时满足“跨调用存活、不可复制或需要显式释放、有状态机”时填写；否则使用 DTO 并删除本节。

### E.1 方案 CBN

先判断一次调用内完成、调用方持有资源、Native opaque handle 中哪些确实适用于当前需求；成本权衡按[问题组 L](#cross-platform-cbn)所指唯一规则，不要求凑齐三个方案或虚构数值。跨调用存活、owner 与终结证据仍须回答。

### E.2 owner 表

| 资源 | 创建者 | owner | 借用者 | 访问区间 | 终结点 | 释放者 |
|---|---|---|---|---|---|---|
| `<RESOURCE>` | `<...>` | `<...>` | `<...>` | `<...>` | `<...>` | `<...>` |

### E.3 状态与并发

- 列每个状态允许的 operate/cancel/transfer/close。
- 若有并发，定义 pin、closing 线性化点和 close 等待规则。
- 若有长期借用，定义 borrow 的建立、解除和 dirty 传播。
- dirty 必须包含完整依赖闭包；不得释放仍可能被访问的关联资源。
- 说明 fork、进程退出、库卸载、容量和 generation 回绕。
- Cleaner 只能回收已证明 clean 的资源。

若使用 scope/capability identity：Kotlin 可见性不是安全边界。identity 必须由权威层签发并绑定 scope id/epoch/owner/state，调用者不能自行构造；随机 cookie 只能是组成部分，不能假设同进程保密。raw handle 或 holder 的反射调用必须 fail-closed。

## 问题组 F：不可拆 transaction

有物证证明需要时嵌入“数据流/控制流差异”；否则不使用本组。

只有物证表明跨调用拆分会破坏时序、所有权或一致性时才保留本节。

```text
prepare outside critical section
-> enter
-> perform direct operation
-> publish effect
-> leave
-> stop/synchronize/release borrow
-> return
```

填写窗口起止、直接调用符号、允许调用和禁项。说明如何以反汇编、调用图或设备证据证明没有意外分派。不得用业务流程名代替物理 transaction 边界。

## 问题组 G：编排、重试与 fallback

适用时嵌入“目标与约束”和“数据流/控制流差异”。

上层只有在本计划逐状态明确授权时才能重试或 fallback：

| status/effect | 可重试 | 可换实现 | 必须清理 | 下一步 |
|---|---|---|---|---|
| clean failure | `<YES/NO>` | `<YES/NO>` | `<CLEANUP>` | `<ACTION>` |
| dirty | **否** | **否** | quarantine/fail-stop | stop |
| indeterminate/unknown | **否** | **否** | freeze/fail-stop | stop |

未填写即禁止。任何重试前必须证明上一轮没有访问者、借用和部分 effect。

若项目存在 Flow、Script 或 Access，再回答：

- 谁是 run 入口，谁只是默认策略？
- Script 能调用哪些受支持 API？资源如何绑定本次 run？
- Access 是 owner 还是 borrowed view？谁负责恰一次 close？
- 正常、异常、取消和 run 结束后的 stale 调用如何处理？

## 问题组 H：外部效果与身份

消费外部对象时嵌入“数据流/控制流差异”；否则不使用本组。

若设计消费文件、脚本、fd 或其它外部对象，必须画 sequence 并回答：

1. 谁生成、保存并拥有对象？
2. 谁计算身份/hash？
3. 如何保证校验和消费的是同一对象，避免路径替换竞态？
4. 谁验证最终 marker、日志或状态？
5. 正常、失败和取消如何清理？

Native 可以消费上层已决定的数据，但不得自行引入第二套业务策略。

## 问题组 I：改动清单与迁移

把本组表格嵌入通用计划的“改动清单”。

| source file:symbol | target file:symbol | 处置 | 行为差异 | owner 变化 | 验证 |
|---|---|---|---|---|---|
| `<SOURCE>` | `<TARGET>` | 保留/拆分/迁移/取消 | `<DIFF>` | `<OWNER_DIFF>` | `<GATE>` |

- 未列入表的文件冻结。
- 旧实现作为参考时不得形成第二生产权威。
- 所有生产 symbol 映射完成前不得删除旧入口。

## 问题组 J：兼容性与失败恢复

只在当前合同确有旧调用者、数据迁移或部分部署要求时嵌入通用计划同名节；源码恢复与冗余产物禁令统一见 [E-REVIEW-RULE-004/§6](../../AGENTS.md#agents-collaboration)，不准备回滚文件或演练。

| 场景 | 适用兼容要求 | 运行时失败处置与资源终态 | 验证方法与预期 |
|---|---|---|---|
| `<SCENARIO>` | `<APPLICABLE_CONTRACT>` | `<FAILURE_AND_RESOURCE_STATE>` | `<METHOD_AND_EXPECTATION>` |

这里只记录产品运行时合同，不因旧实现存在就增加未经要求的兼容层；不预填未执行的结果。

## 问题组 K：验证矩阵

把适用行嵌入通用计划的“验证矩阵”。

按 [E-REVIEW-RULE-001/§3：阶段与证据](design-review-principles.md#review-execution) 选择本批适用项，覆盖规则只引用 [E-REVIEW-RULE-001/D1](design-review-principles.md#review-rule-d1)，具体构建/真机门禁只引用 [E-REVIEW-RULE-004/§8](../../AGENTS.md#agents-gates)。设计阶段填写方法与预期，实现/验证阶段记录实际命令、输入、结果与退出码；保留适用 XML 和产物身份，不重抄命令表。

本领域补充问题仅在触及时回答：Kotlin/Native ABI 与 symbol/link map 一致性；跨调用资源的 stale/wrong-scope/wrong-DSO、并发 close/cancel；问题组 F 的 transaction 边界证据。攻击关键路径的真机门与归档不变，模板不额外要求全文 hash 或未进入实现阶段的运行 PASS。

<a id="cross-platform-cbn"></a>

## 问题组 L：CBN

仅链接 [E-REVIEW-RULE-001/§2：CBN 适用条件与表格](design-review-principles.md#review-cbn)，需要时在取舍发生处填写一次；已有论证直接引用。本附录不再维护“每结构一表、至少两个方案、必须给数字”的第二套要求。

> **沿革（2026-11-05）**：K/L 的全文 hash、旧 core make 命令与无条件 CBN 重复定义收拢到已生效规则；资源、ABI、攻击关键路径和真机验收责任保留。此整理不改既有计划的批准或门禁结果。

## 问题组 M：风险、技术债与扩展点

适用时嵌入“目标与约束”或“兼容性与失败恢复”。

| 风险/债务 | 触发条件 | fail-closed 处置 | owner | 偿还时机 |
|---|---|---|---|---|
| `<RISK>` | `<CONDITION>` | `<ACTION>` | `<OWNER>` | `<GATE>` |

适用的当前变化轴写清改哪里、不改哪里；是否建立抽象统一按 [E-REVIEW-RULE-001/C2：当前变化轴](design-review-principles.md#review-c2)，登记后续需求不构成提前实现授权。

## 问题组 N：进度补充项

把适用项合并进通用计划的“进度”，不要建立第二份进度表。

- [ ] Explore：代码、历史、活跃计划和调用图完成。
- [ ] Design：数据权威、owner、CBN、UML 完成。
- [ ] Review：design-critic 阻断为 0。
- [ ] Approval：已有记录可定位用户批准与对应输入；输入标识沿用 G4，不另索全文 hash。
- [ ] Implement：按单变量批次完成。
- [ ] Verify：离线、构建、真机适用门禁完成。
- [ ] Cleanup：临时物、旧入口和技术债按批准计划处理。

批准前只做设计；批准后逐批实现，上批全绿再进入下一批。
