# 待重细化的Diagnose压力需求与候选

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:tools/engineering/.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-cdy"></a>

## P-DIAG-NATIVE-001：重新细化压力批真实设置消费者

<!-- migrated-source: 043fdf6a Beads gl-cdy description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
producer/churn/>10MiB/drain-handoff需求保留；重做typed DataStore/run snapshot/cross-process slice与exact ledger，再自检、critic、用户批准；旧GLK/JCS及51行scope失效，AS-I1/V1–V4 NOT_RUN。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-diag-native-001-batch-02

<!-- migrated-source: design gl-cdy -->
当前为待重细化候选：原37/51行scope撤销；旧PROFILE、同APK binding、BackendDefinition名称须按typed DataStore及最终plugin owner重设计。下文producer/ABI/lifecycle和矩阵是保留需求/候选，不是production批准。

<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L2120-L2254 -->
<a id="p-diag-native-001-batch-02"></a>

##### P-DIAG-NATIVE-001/批次02 Diagnose 并发 source churn 与 >10 MiB 全链路压力（producer/lifecycle 需求保留；持久化切片已重开）

###### 取代与保留

旧候选把 stress 冻结成单个同步 `gl_diagnose_log_stress`、4×8192 records 与一次调用范围 reader；它已不足以覆盖用户要求的动态 connector 与“运行中停止旧 producer、同时新增 native/Kotlin producer”。旧 AS-D1 critic PASS 仅保留沿革，37-row source ledger已经撤销，不授权该ABI。本节v4依据已经落地的`LogConnector`、`BackendDefinition/BackendUi`与当前JNA binding冻结了producer/lifecycle候选。

**2026-11-08 持久化纠正**：v4 中过早冻结的通用 `profile-v3/schema.json`、`ProfileDocumentPort`、`FileProfileDocumentRepository`、`DiagnoseProfileCodec`、JCS `PROFILE` freeze 及相应51行exact ledger不再授权实施。用户已修订AGENTS §4：只有需要系统内核偏移信息的backend配置以GLK profile为权威，Diagnose不属于该集合；因此`stress_enabled`以Diagnose typed DataStore data class为唯一持久authority。native ABI、A/B producer churn、connector drain、handoff与>10 MiB判据仍作为候选保留；snapshot跨进程承载及逐文件scope须在DS-D1后重新细化并复审，不能拿旧critic PASS直接进source。

2026-11-08同岗critic对v2给出`仍需修订`、阻断5、重要1：独立`backend-config`形成第二配置authority；ABI域、数值status和result sentinel未冻结；取消时collector可能先停而让pipe写端与join死锁；A/B交错只证明B开始、未证明A两侧在B后仍被中枢接受；ledger含无consumer的component-api行且重复安排direct-JNA device stress；10 MiB又混用了body、canonical record与GLLOG2 file三种口径。本v3逐项改为同一PROFILE authority、完整ABI矩阵、collector-drains-first终结、四个accepted happens-before事实、单一runtime stress入口及三种字节计数分离；production仍保持`NOT_RUN`。

2026-11-08同岗critic对v3关闭`DIAG-CONFIG-01`、`DIAG-LEDGER-01`与`DIAG-EVID-01`，但保留三个精确终结缺口：partial thread create之后若清理join失败，`out=null`会遗失live owner；可恢复join持续失败会让write fd与collector互等；source validator只能证明validated/emitted，不能把返回`Unit`的`LogEmitter.emit`表述为LogHub committed。本v4让partial-start返回cleanup-only handle、把不可能的production `pthread_join`错误收敛到RunHost进程固定fatal exit并由既有host-death路径终结，同时把source事实与最终commit-order gate分开；不扩展`LogEmitter`，不增加ack、timeout、detached thread或第二状态机。

本批继续保持：Diagnose为 `DIAGNOSTIC_ONLY`、`DiagnoseFlow : SequentialFlow`、binding构造器constant-name加载、同版发布无version/size/reserved兼容层、正常 `gl_diagnose_run` 在native内完成fixed-home handoff、Payload/Finish跳过且产品成功恒false。stress关闭时，Flow返回的steps中完全不包含stress step，不执行no-op step。

###### 已冻结的场景需求

一次正常 Diagnose Flow 在stress选项开启时至少包含以下阶段，累计有效日志必须大于10 MiB，reader/connector/主流/sink不设累计上限：

1. **阶段A**：attach native FD source A；native启动多个producer线程；Kotlin同时启动多个协程producer，全部进入各自 connector并并发输出。
2. **阶段B**：A仍有输出时attach native FD source B，并新增native线程与Kotlin协程组B；同时请求停止部分A线程/协程，join已停止producer，再drain并detach source A。新增与中止必须真实交错，不能先全部停完再串行启动B。
3. **阶段C**：B继续输出；验证单个producer失败/取消不会停止其它source；随后停止并join全部producer、drain/detach全部connector。
4. **handoff**：connector drain完成后仍通过现有 `gl_diagnose_run` 在native侧执行fixed-home handoff，Flow组合完整typed facts；日志不是成功authority。

###### 设置 authority 与条件 step（SETTINGS-REQ-CONFIG-01 已关闭）

- Diagnose serializable settings data class 是 `stress_enabled` 的唯一持久 authority；具名 `DiagnoseBackendUi` 自动保存它。Flow启动前等待durable read，由P-DIAG批制作一次immutable backend-specific snapshot；GLK profile、SharedPreferences、环境变量和native global均不得镜像该值，Flow也不得回读live DataStore。
- `stress_enabled=false` 时，`DiagnoseFlow.steps(input)`不包含 stress step；true 时才加入一个真实 `DIAGNOSTIC_CHECK` physical step，原 primitive/handoff chain不变。缺失值使用最终获批authority的default false；已提交 snapshot 损坏或不一致则在调用stress ABI前fail closed。
- 本批不实现 kernel profile repository、profile引用、tree editor、custom package WebUI映射、export-conf或其它 backend codec。只有需要系统内核偏移信息的backend攻击参数由其具体`.conf`/GLK profile设计负责；Diagnose、43284等其余backend使用各自具名typed authority。

###### Native/JNA最小ABI与显式FD writer

current `gl_log`每个DSO只有一个process-global export fd，不能同时代表source A与B。把它在producer线程间反复attach会产生fd owner竞争，给每个source再建logger/queue又会重复LogConnector。因此本批只给common C logger增加一个无状态direct helper：

```c
int gl_log_write_fd_record(
    int fd,
    gl_log_level level,
    uint64_t source_elapsed_ms,
    const char *message
);
```

helper复用既有ASCII规整和`[级/毫秒] body\n`编码，要求NUL在第385字节前出现且规整后body为`1..384` ASCII bytes；越界或非法level在write前返回0。合法记录每条最多一次`write(2)`，只有完整写入才返回1，EINTR/partial/error均返回0且不补写第二片；不保存fd、不增加queue/thread/global或累计容量。现有`gl_log_write`行为不变，Diagnose producer group各自借用自己的duplicated pipe write fd调用该helper。stress单条canonical record固定小于448 bytes，reader/connector/run/sink累计不设记录数或字节cap。

不建立native session、registry或进程全局handle表。每个active native source只对应一个opaque `gl_diagnose_stress_group *`，由Kotlin binding的nested owner持有；same-release ABI固定为三个新增ordinary-C symbol：

```c
typedef struct gl_diagnose_stress_group gl_diagnose_stress_group;

typedef struct gl_diagnose_stress_request {
    int32_t log_fd;
    uint32_t group_id;
    uint32_t thread_count;
    uint32_t record_limit;
    uint32_t message_bytes;
    uint32_t delay_us;
    int32_t failure_thread_index;
    uint32_t failure_after_records;
} gl_diagnose_stress_request;

typedef struct gl_diagnose_stress_result {
    uint32_t threads_started;
    uint32_t threads_completed;
    uint32_t threads_failed;
    uint64_t records_written;
    uint64_t bytes_written;
} gl_diagnose_stress_result;

int32_t gl_diagnose_stress_start(
    const gl_diagnose_stress_request *request,
    gl_diagnose_stress_group **group
);
int32_t gl_diagnose_stress_request_stop(gl_diagnose_stress_group *group);
int32_t gl_diagnose_stress_join(
    gl_diagnose_stress_group *group,
    gl_diagnose_stress_result *result
);
```

没有version、size、reserved、runtime layout检查、旧symbol或兼容分支。字段合法域冻结为：`log_fd>=0`；`group_id=1..UINT32_MAX`；`thread_count=1..8`；`record_limit=0`或`1..1,000,000`；`message_bytes=64..384`；`delay_us=0..1,000,000`；`failure_thread_index=-1`表示无故障且必须搭配`failure_after_records=0`，否则index必须位于`0..thread_count-1`、failure-after必须大于0且在finite group中小于record-limit。所有counter加法与`thread_count×record_limit×message_bytes`使用checked `uint64_t`，任一溢出在创建fd/thread前以`INVALID_FIELD`拒绝；完整canonical record长度固定小于448 bytes，低于Android/POSIX pipe的512-byte最小原子写边界和FD parser单行预算。

三个symbol使用独立的`GL_DIAGNOSE_STRESS_STATUS_*`数值集合：`OK=0`、`NULL_REQUEST=1`、`NULL_OUT=2`、`OUT_NOT_NULL=3`、`INVALID_FIELD=4`、`DUP_FD=5`、`ALLOCATE=6`、`THREAD_CREATE=7`、`NULL_GROUP=8`、`NULL_RESULT=9`、`RESULT_SENTINEL=10`、`INTERNAL=11`。`start`要求非null request/out且输入`*out==NULL`。validation/dup/allocation以及首个`pthread_create`失败时没有live thread，关闭已取得资源并保持out为null；若第N个create失败且已有`1..N-1`个thread，native只原子置stop，把持有这些joinable threads、duplicate fd和thread args的cleanup-only group写入out并返回`THREAD_CREATE`，不在start内部join或释放。binding必须无可抛出间隙地接管这个non-null handle并执行同一stop→join→EOF路径，不能只按status丢弃out。全部创建成功才以`OK+non-null group`交付active owner。

`request_stop(NULL)=NULL_GROUP`，对仍由caller拥有的live group重复调用均为OK，只原子置stop，不join、不close。`join`要求live group、non-null result，且caller预置`threads_started/threads_completed/threads_failed=UINT32_MAX`与`records_written/bytes_written=UINT64_MAX`；sentinel不符返回`RESULT_SENTINEL`且不改变group/result。在single owner、joinable、non-self、每thread恰好join一次的不变量下，production `pthread_join`必须成功；任一非0返回表示所有权合同已破坏，native立即`_exit(86)`终止当前RunHost进程，依靠OS一次关闭fd/线程/地址空间，由既有App/Shizuku host-death路径形成terminal，不返回悬空handle、伪EOF或cleanup fact。host CTest在fork child中以test-only join seam制造该分支并断言exit 86；production不带seam。正常join逐个回收thread、关闭最后write fd、一次提交全部result并消费handle，旧pointer随即失效，JNA wrapper置null并拒绝重复调用。线程不detach，opaque pointer不进wire、日志、文件或全局表。

正常join结果冻结为：`threads_started`等于成功create的thread数；`threads_completed`只计finite自然结束或观察到stop后正常结束的thread；`threads_failed`只计注入fault或完整record write失败的thread；恒有`threads_completed + threads_failed == threads_started`。`records_written/bytes_written`包含所有thread在正常、stop或fault终结前已经完整写出的record及其canonical bytes，expected fault之前的完整prefix不得丢弃；producer fault不改变join的`OK`，只由result计数表达。

JNA以`Int`承载`uint32_t` raw bits、以`Long`承载`uint64_t` raw bits，以`PointerByReference`接收opaque group；Kotlin只通过`toUInt()/toULong()`解释raw bits并在转回有符号API前做范围检查，不增加size/layout/header检查。`record_limit=0`只用于A组“直到显式stop”；B组使用有限记录数保证终结。fault字段用于Diagnose真实隔离验证，不是兼容字段或用户可编辑数值；production UI只暴露一个stress开关。`records_written`只统计native完整canonical records，`bytes_written`只统计这些native canonical record的完整字节；Kotlin streaming validator另行累计native+Kotlin message body bytes，二者与最终GLLOG2文件byte count分别报告、不得相互替代。

###### Kotlin binding、Flow编排与可观察不变量

`DiagnoseNativeBinding`继续由构造器constant-name load并成为全部native生命周期owner。它新增private `NativeStressGroup`：创建Java pipe、把read端包装成`FdLogConnector`、把write fd借给start、成功后立刻关闭Java write端，只保留native duplicate。JNA start返回后先检查out：无论status为`OK`还是partial `THREAD_CREATE`，non-null pointer都先被局部owner接管并登记到active set，之后才解释status或执行其它可抛出操作；partial owner立即进入stop/join/drain并让diagnostic step失败。binding维护active group集合，正常join移除，`close()`对残留group执行request-stop→join并只在EOF后关闭reader。production join要么成功消费handle，要么native `_exit(86)`令RunHost host-death；不存在“返回后仍持有write fd、反复重试且让collector等待”的第三态。JNA mapping仍只是direct function/struct/pointer mapping，不增加loader、callback、executor或对象转换层。

`DiagnoseStressScenario`是backend-private具体编排，不是通用stress framework。它按以下固定preset执行：A为2个native thread+2个Kotlin coroutine，stop-controlled并带小延迟；各侧恰有一个producer在达到256条后按预期提前失败/取消，另一个继续。A connector与Kotlin producer都ready后attach B；B为3个native thread+3个Kotlin coroutine，每个6144条、384-byte body。先等B至少一条native和一条Kotlin record已被validator验证并调用现有`LogEmitter.emit`，再继续等待仍存活的A native和A Kotlin producer各自至少一条post-B record也完成同样的validated/emitted事实；只有这四个source-side事实齐全后才request-stop/cancel A。它们只决定producer编排，不冒充LogHub commit acknowledgement；同一run的最终commit order由下述sink gate独立证明。A join/drain期间B继续，随后等待B自然完成并join/drain。B planned body=`6×6144×384=14,155,776` bytes，独立大于10 MiB。

每条stress body包含`source/group/producer/ordinal`，但不携带全局序号。每个FD connector的streaming validator只保存“每producer下一ordinal、record/body-byte计数、ready/failure/post-B事实”，边收边转发到现有`LogEmitter`，不保存完整日志；Kotlin producer使用相同body grammar直接写RunHost Kotlin connector。validation必须观测：A/B各producer ready、B首条先于对应A post-B、A预期单producer failure/cancel、同组其它producer在其后继续、B finite count完整、每producer ordinal连续、所有FD connector=`Drained`、有效stress message body bytes>10 MiB。任一不成立使`DIAGNOSTIC_CHECK`失败；native canonical-record bytes、body bytes和GLLOG2 file bytes分别记录，日志本身仍不升级产品成功。

正常顺序固定为：创建pipe/read connector→启动structured collector并通过ready signal确认其已进入`withContext(NonCancellable + Dispatchers.IO)`的read循环→native start duplicate write fd→关闭Java write端→producer输出→request-stop（如需）→join native或cancel/join Kotlin→native最后write fd关闭→FD reader EOF→connector drain result→关闭read端。collector仍是scenario scope的child，不使用detached/global scope；只是它的collect临界区不可被父取消提前截断。取消或exception的`finally`也在`NonCancellable`清理区内：先cancel/join Kotlin，再request-stop所有native group，在collector仍持续读取时join native，最后await EOF并close reader。不得在EOF前以close模拟正常detach，也不得增加detached scope、第二timeout状态机或reader累计cap。pipe接近满载时取消、partial start及active-set登记窗口进入focused tests；unexpected production join error由forked host fatal-exit test和既有RunHost host-death class覆盖，不构造可恢复retry状态。stress step全部drain后才执行现有`gl_diagnose_run`，所以fixed-home handoff、short Native/Shell log和typed primitive/dispatch仍由原chain交付。

###### 故障、取消与结果矩阵

| 位置 | 必须行为 | 禁止结果 |
|---|---|---|
| settings snapshot false | stress step absent；现有chain不变 | no-op stress step、live UI/DataStore读取 |
| committed snapshot invalid/inconsistent | plan失败、binding cleanup、无stress/handoff调用 | 第二配置fallback、猜默认true、把GLK profile当设置 |
| pipe/JNA start/partial pthread create失败 | 0 thread时out=null且关闭资源；已有thread时返回cleanup-only handle，binding先登记owner再stop/join、drain已验证prefix、diagnostic check失败 | out=null同时遗失live group、live fd/thread、继续chain |
| 预期A producer fault | 只结束指定producer，其它A/B producer继续并留下after-fault记录 | supervisor取消全组、把预期fault当产品成功 |
| unexpected write/count/ordinal/EOF错误 | drain可证明prefix，connector/validator返回失败，chain不运行 | drop后仍PASS、timestamp重排 |
| Flow coroutine取消/exception | non-cancellable finally中collector继续drain；先cancel/join Kotlin，再request-stop/join native，再等EOF/close reader | collector先取消、reader先close、pipe写阻塞后join死锁、handle丢失 |
| unexpected production `pthread_join` error | `_exit(86)`终止仅承载当前Flow的RunHost进程；OS回收fd/thread/address-space，parent走既有host-death terminal | 返回live handle后永等EOF、detach、伪Drained、继续同host运行 |
| normal stress complete | diagnostic check SUCCESS，之后原chain primitive/dispatch/cleanup SUCCESS | stress代替handoff或新建RunReport authority |
| RunHost transport gap/host death | 既有RunHostLogConnector保留prefix并Incomplete；typed product success仍false | sequence当global sort、日志错误升级root |

###### Exact source ledger与守卫

旧51行ledger已因持久化owner纠正撤销；当前没有可实施的exact source ledger。重新细化时只允许一个concrete stress scenario和Diagnose typed-setting snapshot的必要闭包；不加入session manager、stress framework、第二hub、sink factory、generic settings schema或UI element DSL。architecture guard必须拒绝：Flow读取live DataStore、把Diagnose设置塞入GLK profile、JNA callback、native global handle表、detached producer、version/size/reserved/compat、第二日志queue、reader累计cap、timestamp排序及stress关闭仍返回step。

###### 准出顺序

1. v4 producer/lifecycle部分保留既有critic意见，但旧配置owner候选与逐文件ledger不再授权；SETTINGS-REQ-CONFIG-01已由用户规则修订关闭，下一步完成P-SETTINGS-IO-001/DS-D1，再重新细化Diagnose typed authority的run snapshot和exact scope并复审，之后才可申请production source批准。
2. source完成后先跑debug/release unit XML、architecture、`nativeTestHost`与只构建双variant的`nativeBuildAndroid`；不得恢复已删除的JNA distribution或native artifact verifier。
3. 4 KiB/16 KiB App/direct-JNA/Shizuku短链先证明现有handoff不回归；stress ABI边界由host CTest覆盖，不另做重复direct-JNA device stress。6.6 AVD分别以App和Shizuku走production Flow运行stress，证明真实A/B交错、expected producer fault隔离、有效stress body>10 MiB、零transport drop、typed report完整与product=false。
4. stress storage exact gate读取同一run的UI committed entries、private GLLOG2和MediaStore bytes：明确断言`B-native first commit < A-native post-B commit`及`B-Kotlin first commit < A-Kotlin post-B commit`，private/MediaStore逐字节一致、正文commit id连续、每producer ordinal连续、final End存在且文件>10 MiB；测试删除自己创建的MediaStore row。source-side validated/emitted只控制停启时机，这两条same-run commit关系才是LogHub接受顺序证据。
5. 最终树执行`RunHostLifecycleTest`、`RunHostCancellationTest`、`RunHostHostDeathTest` exact AVD欠账。automatic-export真实设备门与最终产品验收保持独立；真实carrier设备门继续`NOT_RUN`。

- 原已完成步骤、批准与运行证据：见 Beads `gl-cdy` notes 的迁移前记录；本设计不维护进度checkbox。
- AS-D1R：producer/lifecycle/ABI/故障矩阵候选保留；获批setting authority的run snapshot与逐文件ledger等待DS-D1后重写并复审，旧通用GLK/JCS配置切片不得实施。
- AS-I1：按获批exact设计实现并发producer churn与Flow条件step。
- AS-V1：普通门、4/16 KiB短链与6.6 AVD App/Shizuku >10 MiB场景通过。
- AS-V2：最终树三个RunHost exact AVD class通过。
- AS-V3：真实设备automatic-export与最终产品验收通过。
- AS-V4：真实carrier继续 `NOT_RUN`。



<a id="acceptance-gl-cdy"></a>

### 验收定义与原边界：gl-cdy

<!-- migrated-source: acceptance_criteria gl-cdy -->
producer/churn/>10MiB/drain-handoff需求保留；重做typed DataStore/run snapshot/cross-process slice与exact ledger，再自检、critic、用户批准；旧GLK/JCS及51行scope失效，AS-I1/V1–V4 NOT_RUN。

批准/审核/实际结果：Beads `gl-cdy` notes；本段不维护实时勾选或新PASS。
