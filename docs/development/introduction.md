# 从代码可辨识的事实（introduction）

<a id="run-introduction"></a>

> **代号与快照范围**：当前条目引用[主计划语义代号](../plan/backend-root-execution-unification-plan.md#plan-semantic-identifiers)，不是统一 RUN 前缀下的旧 R 序号；下文原快照、历史批准与门禁保持原义，旧来源子号按映射定位。

> 当前快照：Backend root-execution R4B source complete + R5-S1 common product host source complete，2026-11-05。
> 唯一在办架构计划是 [P-RUN-X-001](../plan/backend-root-execution-unification-plan.md#p-run-x-001)；逐文件处置权威是[主计划 ledger](../plan/backend-root-execution-unification-ledger.tsv)；结构图唯一权威是 [UML 当前执行视图](full-process-uml.md#run-current-views)。
> 旧计划只从 docs/archive/ 读取，不再授权实现。
> 按用户明确指令，本轮没有运行 Gradle、unit、architecture verifier、AVD、Shizuku runtime 或设备门；R2-R5-S1 的相关 gate 均保持 NOT_RUN。

## 零、当前产品边界

GhostLock 的产品成功不是 raw uid 0，而是 concrete backend 到达自己的 root execution point 后，派发公共层发布的同一份 root script，并由 typed result 证明用户 Payload 达成。Finish 是 Payload 之后的附加动作，backend cleanup 是独立层；三者不能互相冒充。

当前 registry 刻意没有 selectable production-root backend：

- CUSTOM 声明 PRODUCT_ROOT capability，但 selection policy 为 DISABLED，operation catalog 为空，readiness 为 NotReady。
- DEBUG-PROBE 只在 debug build 注册，capability 为 DIAGNOSTIC_ONLY；即使 probe 完成，产品成功投影仍为 false。
- R5-S1曾写入neutral common product host spine，但后续用户已取消R5-S2/S3并要求断开全部真攻击backend。43499-Glued、43284-Glued、98260只保留source-only candidate；它们不在registry或APK native target graph，源码存在不等于selectable、supported或device proof。

## 一、Android/Kotlin 分层

| 层 | 当前 owner 与职责 | 禁止反向依赖 |
| --- | --- | --- |
| domain/backend | neutral BackendSpec、presentation、configuration schema、parameter requirements、availability facts | Android、Compose、concrete backend、persistence |
| domain/run | FrozenRunInput、RunCapability、六层 RunReport、terminal 与 product-success projector | host implementation、日志推断 |
| domain/payload 与 domain/finish | 两份独立 version-3 document、校验和用户语义 | backend root primitive |
| data | Android persistence、profile/config projection、workspace、PFD staging、diagnostic report | concrete backend membership |
| backend/declarations | internal BackendPlugin：spec、resource-free readiness、fresh one-shot Flow factory | UI、profile、AIDL 暴露 |
| backend/registry | immutable same-APK membership 与 exact key lookup | runtime register、digest、reflection |
| backend/transport | typed RunHost clients、host state machine、event/snapshot validation、process carrier | Bundle 与动态 method lookup |
| core/run | 一次冻结输入与 application-lifetime RunCoordinator | renderer/ViewModel lifetime ownership |
| core/flow | Flow.run 接受 BackendFlowInput 并返回 BackendRunFacts | product script generator |
| UI | immutable UI state 与 action contract | data/backend/core implementation import |

BackendSpec 不持有 Flow、Context、资源、动态 readiness cache 或 persistence callback。BackendPlugin 仅在 backend 内部可见；每次 accepted run 必须创建新的 Flow。

## 二、Payload、Finish 与单一 root script

Payload 与 Finish 分别由自己的 repository 保存 version-3 typed document。RunInputFreezer 在一次 run 开始前同时冻结 backend key、profile-v3 snapshot、Payload 与 Finish，之后设置变化不影响 in-flight run。

RootScriptRenderer 是唯一整脚本 writer。发布过程固定为 host workspace 内写入、关闭、权限收口、同一 opened object 身份确认，再向 Flow 提供 published identity。backend 不生成 KernelSU、Payload 或 Finish 产品脚本，只负责 root primitive、exact artifact dispatch 与 attack-private cleanup facts。

结果由 typed root-result codec 与 RunReport 合成。产品成功只允许来自 PRODUCT_ROOT capability 与成功 terminal 的合法组合；raw uid 0、stdout、exit 0、callback event 或全局日志只能作为诊断事实。

## 三、typed RunHost 执行链

合同定义：[P-RUN-IPC-001：RunHost wire 与状态机](../plan/backend-root-execution-runhost-wire.md#p-run-ipc-001)；下列为本页原快照说明，不复制合同 authority。

1. UI 只提交 selected stable backend key。
2. BackendRunSessionAdapter 重新解析 captured registry 与动态 availability。
3. RunInputFreezer 一次冻结所有输入并创建 application-owned read-only source artifact。
4. AppRunHostClient 或 ShizukuRunHostClient 发送相同 typed request 与只读 PFD；request 不携带 capability。
5. RunHost 校验实际 Parcel 大小、结构、run identity、generation、semantic fingerprint、execution UID 与 SELinux domain。
6. RunHost 从 exact plugin 投影 capability，dup PFD，stage 到 host-owned workspace，并创建 fresh Flow。
7. Flow 返回 backend primitive、dispatch、cleanup 与 diagnostic facts；PRODUCT_ROOT carrier 还必须让公共脚本产生 Payload/Finish item facts。
8. host 发布 immutable terminal snapshot；callback 只作低延迟通知，client 同时轮询 getSnapshot 读取 durable authority。
9. client 先请求 requestClose；host 接受并进入 DESTROYED 后，App 才 unbind，Shizuku 才 remove UserService。

running cancel 只预留取消请求并保持 RUNNING。worker 必须先停止最后访问、完成 backend cleanup，再发布 CANCELLED terminal；这段窗口内 requestClose 固定拒绝。late callback 必须同时匹配 runId、generation、strict sequence 与 non-terminal state，否则丢弃。

App host workspace 位于 app-private no-backup root；Shizuku shell host 位于 shell-owned /data/local/tmp 固定 namespace。两者都拒绝 symlink、错误 owner 与未发布 executable identity。

## 四、native current tree

~~~text
app/src/main/cpp/
  CMakeLists.txt
  common/
  backends/CMakeLists.txt
  backends/43499glued/   frozen future executable carrier candidate
  backends/43284glued/   frozen future executable/bootstrap candidate
  backends/98260/        frozen future candidate, default build disabled
tools/tests/
tools/avbcheck/
~~~

original 43499/43284 手写 JNI closure、glgeneral、generic access/native-handle/public victim 与旧 native log bridge 已退出 current graph。`backends/CMakeLists.txt` 现在是空的断开边界，不声明backend option或include；Gradle也不再转发backend/M0开关、注册M0 task或打包攻击ELF。current CMake只构建`gl_common` OBJECT target，future JNA尚未引入。

保留 candidate 内的攻击事务不得修改。当前没有首个真实carrier排期；重新接线必须由新的L级计划明确取代2026-11-05断开裁定。

## 五、日志与诊断

日志是 diagnostic sink，不是成功 authority。2026-11-05 用户纠正 automatic export 的删除处置，随后批准R4B简化重写；current source保留historical flag/destination/picker与双皮肤controls，旧根级caller-zero object继续ABSENT。`domain/run`只保留neutral factory/run-scoped sink与唯一GLLOG1 formatter；current RunCoordinator在canonical lock内纯format并nonblocking enqueue，`data/run`的single MediaStore worker独占URI/output stream，App/Shizuku仍不各建exporter。

用户主动触发的 diagnostic report 仍由 AndroidDiagnosticReportBuilder 生成，并包含 typed run report。manual share 与 P-LOG-IO-001/批次02 automatic export 复用同一纯 typed formatter；三者与 root-script result、产品成功判据彼此独立，export failure 不改变 RunReport 或 terminal。

## 六、source guard

tools/kotlin_architecture_d0.py 维护以下 source 规则：

- UI/domain/core 到 concrete backend 的禁止边；
- runtime reflection 禁止；
- backend 生成产品 root script 禁止；
- 旧 IFlowHost 与 IGhostlock UserService AIDL transaction 禁止；
- frozen candidate 之外的 manual JNI object marshalling 禁止；
- P-RUN-X-001/批次01/P-RUN-CORE-001/批次01 删除路径必须在 file-action manifest 中保持 ABSENT。

本轮只更新规则与 manifest，没有执行 verifier；因此这些规则是已写入的门禁实现，不是本轮 PASS 证据。

## 七、规范命令与本轮状态

~~~sh
./gradlew nativeTestHost --console=plain
./gradlew nativeBuildAndroid --console=plain
python3 tools/kotlin_architecture_d0.py verify --repo . --fixtures app/src/test/resources/architecture --section architecture
~~~

以上是当前规范入口。本轮按用户要求未执行门禁，不能从 source change 推导 compile、runtime、AVD、Shizuku 或设备结果。P-BACKEND-X-001/批次01 的历史证据保留在计划记录中；P-LOG-IO-001/批次02 source已接线但仍为`NOT_RUN`，P-RUN-IO-001/批次01 至 P-LOG-IO-001/批次02（原批次范围） evidence欠账必须在release或启用真实carrier前偿还。

## 八、后续边界

- P-BACKEND-X-001/批次02：已按用户后续裁定关闭为real-backend disconnection，不再实施43499 carrier。
- P-DIAG-NATIVE-001/批次01 / P-BACKEND-NATIVE-001/批次01 / P-BACKEND-NATIVE-002/批次01：不自动排队；只有用户重新批准具名consumer/backend后，才可另立L级计划引入JNA或carrier delta。
- extractor/KMP、profile v3 schema 与 LegacyProfileConverter 不由本计划顺手改动。
- current Class、Sequence、State 与 result projection 只维护在 docs/development/full-process-uml.md。
