# 43499 三路攻击实现 vs legacy/legacy2/very_legacy 按行差异（归档）

- 日期：2026-10-27；快照 HEAD `a9af5482`(2.0.11)+工作树。
- 来源：三份只读按行审计（multicast / pselect / tcp），逐段表原文在会话记录；本件为阻断清单+修复依赖序汇总（供 R3 实现 writer 使用）。
- 总判：**三条 route 当前均不可真机命中**。物理 syscall 骨架大体忠实，但「攻击数据平面（Kotlin generator）+ 时序/常量对齐 + 页接受/安全收口 + 可证伪回执」缺失；multicast 另有 Flow/RunSpec 依赖方向倒置（必须先 placement facts 再生成 landing/primitive）。

## 0. 跨路线共性（R3 必须先解决）

1. **数据平面结构**：`Flow43499RunSpec` 在 placement 前冻结 landing/actor 输入；payload/stamp/frame 必须由 `candidatePageBase` 派生（legacy2 `support.cpp:417-451`、`route_multicast.cpp:76-91`）。必须改成 Flow 在 `placement.prepare` 后生成 landing/primitive。
2. **Kotlin 无 producer**：`FinishHandoffAdapter.kt:169-175,221-223` 两 factory 默认 null、显式拒绝非 multicast；`A301SoDebugConfig` 无 pselect/tcp 段 ⇒ 三路都不可发射。
3. **页接受判据消失**：`page.c:241-247,296-297` 只验长度；旧 `layout_matches` + `selinux_state.initialized` 偶数位拒绝（legacy2 `support.cpp:765-784`、`memory.cpp:216-225`）⇒ 坏页直接写（legacy 注释：会 panic）。
4. **时序/常量反向**：`A301SoDebugConfig.kt:80,88,120,126,127,129` — placement deadline 1s（旧 60s/240s；归档实测≈1.4–1.5s）、settle 200µs（旧 50ms）、poll 200µs（旧 1ms）、lock_pi_spin 16µs（旧 50ms）、consumer burst/max 8/256（旧 1/1）、nice 固定不轮转（旧 `(calls%19)+1`）。
5. **arm 语义**：multicast `arm_sequence` 被当“总 arm 次数”且首轮即 arm（旧 attempts=128、attempt≥16 才 arm）；tcp 无 arm_sequence 字段，第 1 次 ret==0 即 arm（旧 ≥16）。
6. **安全收口**：dirty 只 retained 不终止（旧 exit_group(70)/fail-stop）；retained fd/线程泄漏；`kill→close→reap` 顺序与旧 `close→kill` 相反（`placement.c:220-227`）。
7. **可证伪性**：回执缺 lock/park/setattr/select ret/errno、ghost disarm ret/errno、punch/zc errno、失败 step ⇒ 真机失败不可定位。
8. **多写/重试**：单次 execute，无 W1/W1b/W2/W3、无 fast-repair（legacy backend `cve_2026_43499_backend.cpp:469-545`、legacy2 `route.h:438-462`）。

## 1. multicast 阻断（B1–B8）

- B1 数据平面缺失+依赖倒置（`FinishHandoffAdapter.kt:169-175`、`Flow43499RunSpec.kt:27-34`）→ 先改 Flow/RunSpec。
- B2 arm 语义反转/次数塌缩：`carrier_multicast.c:128-131,141`（debug 4096/armSeq1）vs `legacy2 route_multicast.cpp:101-131`。
- B3 nice 固定 + waiter/owner 预置 BATCH：`actor.c:233-234,258,298,354` vs `threads.cpp:135-138`/`main.c:387-404`。
- B4 settle 200µs vs 50ms：`actor.c:1028` vs `threads.cpp:178-180`。
- B5 placement deadline 1s vs 60s/240s：`placement.c:526-551,844-857` vs `util.c:684-711`。
- B6 页接受判据消失：`page.c:241-247,296-297`。
- B7 ghost disarm 不执行/不可核验：`actor.c:424-439` 无 generator 传参、无回执字段；`carrier_multicast.c:71-77` 允许 flag=0。
- B8 单次 execute/无多写：`Flow43499Impl.kt:195-209`、`ActorOwner.kt:69-99`。
- 重要：dirty 不停（I1）、泄漏窗过宽（`placement.c:571-579` 上界 0xffffffc000000000 vs `direct_map_end 0xffffff9000000000`）、carrier 多一次 stamp（`carrier_multicast.c:169-189`）、socket 改 join 后 close（`actor.c:1269`）、arm hold 墙钟+窗口内取时（`carrier_multicast.c:142-147`）、三地址不交叉校验（`carrier_multicast.c:31-36,46`）、JNI 无 264B/family 校验（`jni43499.c:438-472`）。

## 2. pselect 阻断（P1–P12）

- P1 无 consumer 起跑延迟 + `per_attempt_delay_ns` 被当超时（`carrier_pselect.c:443-453,358-359`+`actor.c:238-283`）。
- P2 nice 不轮转（`actor.c:257-258`）。
- P3 fd_set 语义外移 + Kotlin 无 frame 生成器（`carrier_pselect.c:87-96,350`+`FinishHandoffAdapter.kt:173,222-234`）。
- P4 重试不重喷页/不重建 fd_set（`carrier_pselect.c:351-354,435-461`）。
- P5 fd 恢复时机错：drain 失败不恢复；stuck 反而 dup2 回写（`carrier_pselect.c:274-291,479-484`）。
- P6 页接受判据消失（`page.c:372-379`）。
- P7 缺 select(timeval) 分支（`carrier_pselect.c:451-453`）。
- P8/P9 成功判据与诊断码不足（`carrier_pselect.c:451,457-468`）。
- P10 备份 fd 起点 `nfds+FD_SETSIZE` 过高（低 RLIMIT_NOFILE -EIO）。
- P11 carrier prepare 保留态被 `free(actor)` 丢弃（`actor.c:865-874`）。
- P12 consumer 不 stop/自旋；dirty 不 fail-stop（`actor.c:193-202,1050-1099`）。

## 3. tcp 阻断（T1–T7）

- T1 fill_byte 交接晚于 prepare 使用（`carrier_tcp.c:290,324`+`actor.c:544`；memset 在 199）⇒ 实际发送/触页全 0（旧 0x33/0x55）。
- T2 zc 请求体只写 1 个字段（`carrier_tcp.c:386-398`）；旧三版写 0x18/0x20/0x28/0x30 四字段；`final_task_address/final_lock_address` 只校验不用。
- T3 偏移 0 vs 0x18：新注释/文档按 0 设计，旧三版统一 0x18（UAPI copybuf 字段位）；先固定 0x18 并白名单。
- T4 arm_sequence 缺失，第 1 次 ret==0 即 arm（`carrier_tcp.c:404-405`；旧 ≥16）。
- T5 zc_offset 上界用 receive_size 且可回绕 → 原生堆越界写（`carrier_tcp.c:257-258,397-398`）。
- T6 mapping_offset 未要求 ==page_size（punch 洞从 page_size 起；`carrier_tcp.c:235-245`）。
- T7 Kotlin 无 producer/启用链（`Native43499Jni.kt:130-146`、`FinishHandoffAdapter.kt:221-223`、`A301SoDebugConfig` 无 tcp 字段）。
- 重要：16MiB mapping 丢失、punch 线程窗口外常转+亲和继承、send 用 receive_size 且无 0x33/≤page 校验、等 inflight 顺序与旧相反、post_hold 单位语义、回执缺 attempts/punch/zc errno/last_step。

## 4. R3 实现依赖序（三路同批）

R3a Native 修复（三 carrier + actor/page/placement）：
1) Flow/RunSpec 数据流：placement facts → 生成 landing/primitive（Kotlin seam 扩展，也可先 Native 恢复构造再迁移）。
2) multicast：attempts=128/arm≥16 语义、nice 轮转（删 waiter/owner 自设 nice）、lock_pi_spin 50ms、burst/max 1/1、settle 50ms、placement deadline 60s、页接受、ghost 参数+回执、arm hold spin、地址窗 direct_map_end、dirty fail-stop、W1/W2/W3。
3) pselect：enter delay（拆字段）、attempt 重喷+重建 fd_set、select 分支、fd 恢复时机、stuck 不碰 fd、备份起点 nfds+64、页接受、诊断码、consumer stop。
4) tcp：fill_byte 入参前置、zc 四字段写入+白名单 0x18、arm_sequence、zc 越界修复、mapping_offset=page_size、16MiB、punch 相位等待、等 inflight 顺序、回执字段。
5) 通用：回执扩展（attempts/step/errno/ghost/select/zc）、dirty 安全收口、close 顺序。
R3b Kotlin：三 generator（multicast stamp+64KB landing+地址；pselect fdset 词表；tcp zc 0x40 帧+16MiB 参数）、Flow 数据流改造、RequestFactory 放开三路、debug 配置三段、单测（参数/向量/负例）。
R3c 验证：host 向量（与 legacy 常量逐字节对拍）→ android 零告警 → scanner gate → 三路各自冷机真机门禁（固定 CPU、单 route、KSU 未加载、pstore）→ device-gates 归档；未过门禁标 UNSUPPORTED，不 fallback。

## 5. R3 实施进度（R3a：writer routes-impl2；R3b/R3c：writer routes-impl3；2026-10-27；未 commit）

> R3a = Native + 数据流；R3b = 三路 Kotlin generator/配置/RequestFactory/单测；R3c = 门禁与矩阵回填。
> 每批过门禁：NDK cleanDebug+Debug EXIT=0（log warning/error grep=0）、
> compileDebugKotlin EXIT=0、过滤单测 XML 30/0/0（R3b 后）、`scan_native_literals.py --gate r2` EXIT=0
> （allowlist 已按 R3b 新行号重生成并重签 sha256=`07c8cad4…`；script sha256=`2beeb25e…`）。
> 真机门禁：A301SO 不在线 ⇒ **UNSUPPORTED / 待 P0c**，本文件不宣称任何 route 可攻击。

### 已完成

| 批次 | 内容 | 关键证据 |
| --- | --- | --- |
| R3a-1（B1/共性1） | `Cve43499PlacementFacts` + `Cve43499PostPlacementGenerator(facts)->landing+primitive` seam；Flow 第 3 步存 facts、第 4 步落页前生成 landing、第 5 步用生成 primitive；RunSpec 改为 `placement+actorSetup+frozenInputs?+handoff+runId+generator?+finishExecutor?`（frozen 兼容路径保留）；RequestFactory 三路 generator hook | Flow43499Impl/RunSpec/Lifecycle/FinishHandoffAdapter；evidence `landingSource=post-placement/frozen`、`candidatePageBase` |
| R3a-2a（B6/P6） | 页接受判据 ABI：`landing_request.page_accept_{required,offset,bit}`、`landing_receipt.page_accept_{value,checked,ok}`；page.c 在**提交点前**求值 `(base+offset)` 的 accept_bit 位，拒绝即 clean failure、Placement 不消费（legacy2 `memory.cpp:210-227`） | landing_request=104 / receipt=88 / result=120 |
| R3a-2b（T1–T6/重要） | TCP：fill_byte 在 prepare 使用前由 Actor 传入（映射页+发送缓冲，T1）；zc 四字段（address/length/task/lock）由 Native 按请求偏移写入、偏移在 prepare 校验对齐/界内/不重叠（T2/T3/T5）；`mapping_offset==page_size`（T6）、frame/receive ≤ page_size；arm_sequence 语义（T4）；punch 相位独立自旋上限；arm 后先清 go 再等 inflight；last_step/last_errno/punch 回执 | tcp_spec=160 / primitive_spec=176 / actor_execute_receipt=88 / result=120 |
| R3a-2c（P1/P4/P5/P7/P8/P10/P11） | pselect：consumer 起跑延迟（enter_delay_ns，consumer usleep）；per_attempt_delay_ns 改 attempt 间隔（不再当超时）；每 attempt 重载 fd_set；select(timeval)/pselect 双分支；select ret/errno 回执；fd 备份起点 `nfds+fd_reserve_stride`（旧 FD_SETSIZE 偏移）；stuck 时**不** dup2 回写/不 close（retained）；prepare 清理不干净上报 retained；carrier prepare 失败不再随 free(actor) 丢保留态 | pselect_spec=104；`--gate r2` 绿 |
| R3a-2d（B7/共性6） | multicast ghost disarm：disarm_required=1 时强制 futex_waiter_flag≠0；FUTEX_LOCK_PI ret/errno 进 execute 回执；placement child 清理顺序改回旧 close→kill→reap | `carrier_multicast_prepare` 校验 + `ghost_disarm_{result,errno}` |

### 未完成（下一 writer 入口，按序）

1. **R3b Kotlin**：三 generator（multicast stamp+64KB landing+三地址+页接受参数；pselect fd_set 词表+shift+320 nfds+地址+enterDelay/fdReserveStride/selectUseTimeval；tcp 0x40 帧+16MiB/64/20000/16 参数+四偏移 0x18/0x20/0x28/0x30+armSequence/punchPhaseSpin）→ 注入 `Cve43499RequestFactory.buildFromDebug(generator=…)` → 放开三路（当前无 generator 时仍只准 multicast）。
2. **R3b debug 配置三段**：`A301SoDebugConfig`/`DebugExecutionParameters` 增 pselect/tcp 段；tcp fill 值（旧 0x33 send/0x55 mapping）当前统一用 landing fill_byte，需在 generator 批确认是否拆两字段（A 决策下须进请求）。
3. **R3b 单测**：参数/字节向量/负例（seam 调用顺序、页接受参数、zc 偏移校验、arm 语义）。
4. **R3c**：host 向量与 legacy 常量逐字节对拍（`glk43499/host/` 仍为债）；`:app:assembleRelease` clean 重跑；三路冷机真机门禁（A301SO 在线时；固定 CPU、单 route、KSU 未加载、pstore）→ device-gates 归档；docs 矩阵/plan/approval-status 按 R3 实现重出（fields/abi/validation 尚未回填 R3 新增字段与 sizeof）。

### 仍 UNVERIFIED

- 三路均未真机；multicast/pselect/tcp 是否命中未知
- pselect 每 attempt 重喷页（P4 的页重建部分）未实现：新架构 landing 一次写页，重试仅重建 fd_set；需 generator/Flow 批决定是否支持 re-land
- `zc` 偏移白名单（0x18/0x20/0x28/0x30）与 tcp 双 fill 值属 generator 职责，Native 只校验域
- tcp mapping 16MiB / receive 64 / post_hold 20000 / arm 16 等具体值尚未由 debug 配置提供（R3b）


### R3b/R3c 已完成（writer routes-impl3，2026-10-27，未 commit）

| 批次 | 内容 | 关键证据 |
| --- | --- | --- |
| R3b-1（三 generator） | 新增 `backend/cve43499/generator/`：共享 `Cve43499PayloadBuilder`（64 KiB landing bytes，legacy2 `support.cpp:417-549` 移植）+ `Cve43499MulticastInputGenerator`（264B stamp：task@waiter+48=144、lock@waiter+56=152、family@8=AF_UNSPEC；三地址；页接受参数）+ `Cve43499PselectInputGenerator`（三组 fd_set 词表：compact 11 词，shift=-2，nfds=320 ⇒ 每 set 5 字，共 120B）+ `Cve43499TcpZerocopyInputGenerator`（0x40 zc 帧、16 MiB/64/20000/16、zc 0x18/0x20/0x28/0x30、双 fill） | `generator/*.kt`；`Cve43499GeneratorVectorTest` 逐字节对拍 legacy 常量 |
| R3b-2（debug 配置） | `debug-a301so.conf` 新增 `injection.primitive`（单 route 选择）+ `generator.{address_model,payload,multicast,pselect,tcp}` 段；`A301SoDebugConfig` 解析为 `DebugGeneratorParameters`（缺键 fail-closed，无默认值） | `A301SoDebugConfig.kt`；`A301SoDebugConfigTest`（含缺键/未知 route 负例） |
| R3b-3（RequestFactory） | 三路放开：`Cve43499RequestFactory.build` 缺省按 `injection.primitive` 注入真实 generator；`buildFromDebug` 删除 pselect/tcp 显式拒绝；无 generator 的冻结兼容路径仍要求 payload+primitive | `FinishHandoffAdapter.kt`；三路 Built 单测 |
| R3b-4（tcp 双 fill + ABI） | `tcp_zerocopy_spec` append-only 增 `send_fill_byte`(0x33)/`mapping_fill_byte`(0x55)，Native 不再借 landing.fill_byte；tcp_spec 160→**176**、primitive_spec 176→**192**（`_Static_assert` 同步） | `primitive.h`/`carrier_tcp.c`/`jni43499.c`/`Native43499Jni.kt`；ABI probe |
| R3b-5（单测） | 30 tests / 0 fail / 0 err：三 generator 全量字节向量（multicast stamp+64 KiB；pselect fd_set；tcp frame+tcp landing）、参数自洽、负例（base=0/reclaim mismatch/nfds 域/zc 重叠/Credential 拒绝）、三路 RequestFactory 路由 | `build/app/test-results/testDebugUnitTest/*.xml` |
| R3c（门禁，2026-10-27 实跑） | ① NDK `externalNativeBuildCleanDebug` EXIT=0 → `externalNativeBuildDebug`（全 C 源 touch 强制重编）EXIT=0，log warning/error grep **0/0**；② `compileDebugKotlin` EXIT=0（新增代码 0 warning）；③ 过滤单测 XML **30 tests / 0 fail / 0 err**；④ scanner `--gate r2` EXIT=0：`literals=1437 allowed=1437 unclassified=0 target=0 residue=0`，allowlist 重签 sha256=`07c8cad4…`/script=`2beeb25e…`；⑤ ABI probe host 实跑 20 sizeof 与 [abi §10](43499-r1-freeze-abi.md) 全等；⑥ `clean` + `assembleRelease` EXIT=0（APK 3,475,056B，sha256=`fedd0703…`；release 不打包 `debug-a301so.conf`，调试资产隔离成立） | 本记录；`git diff --check` EXIT=0 |

### R3 决策记录（本批新增；R3b 唯一权威）

| 编号 | 决策 | 依据 | 代价/偿还 |
| --- | --- | --- | --- |
| D-R3b-1 | **tcp 双 fill**：`send_fill_byte=0x33` / `mapping_fill_byte=0x55` 进 `tcp_zerocopy_spec`（append-only），不复用 landing.fill_byte | legacy2 `route_tcp_session.cpp.disabled:61-64`；A 决策“每个物理使用者一个请求字段” | ABI v4 内 append-only；landing.fill_byte 保留 ABI 槽但不参与 tcp 填充 |
| D-R3b-2 | **三路统一 W1 单写阶段**：target=`selinux_enforcing` data alias，mode=Zero，preserve_child=true（leaf=0）；页接受判据由 layout 派生 | very_legacy `run_w1_stage`；legacy2 `memory.cpp:210-227` | W1b/W2/W3+fast-repair 未实现 ⇒ **R3d 候选 + P0c 前置阻断**（见本节） |
| D-R3b-3 | **pselect 不实现每 attempt 重喷页**：单次 landing，仅 per-attempt 重建 fd_set（R3a-2c 已做）；页接受失败 ⇒ clean failure，不 fallback | R3a 新架构 landing 一次；legacy2 `route_select.cpp:408-497` 每 attempt 重喷 | **P0c 前置风险**：retry 可能因 stale page 无效；真机若显示 retry 无效 ⇒ 回补重喷或 Flow 重试再落页 |
| D-R3b-4 | **页接受参数由 generator 派生**：`layout.value=base+write_value_offset`，required/offset/bit 由请求携带，Native 提交点前求值 | legacy2 `memory.cpp:payload_write_layout/accepts_page` | 无静态 0x100/16 占位（frozen 兼容路径除外，已标注） |
| D-R3b-5 | **generator 只实现 compact waiter + W1**：Credential(W2)/非 compact(6.6) 布局具名拒绝（不猜） | A301SO 5.15 `compact_waiter=1`；W2 凭据镜像属 R3d | 具名 reason：`write-mode:credential-unsupported`/`payload-layout:compact-waiter-required` |
| D-R3b-6 | **generator 目标事实进 debug 配置**：address_model/payload 槽位/stamp 几何/pselect/tcp 参数全部来自 `generator` 段；HOCON 只接受十进制（高地址用有符号负数） | A 决策 + data 层 loader（I2）；typesafe-config 无 hex | 缺键 fail-closed；R2 遗留“debug 常量”债部分偿还 |

### R3 剩余范围与门禁前置阻断（不得只在 UNVERIFIED 一句带过）

1. **R3d 候选（P0c 真机门禁前置阻断）**：W1b（private scratch repair）、W2（credential 写+fast-repair）、W3（seccomp）多写与 `Credential` generator 布局、Fast repair prebuild/activate —— **任何 route 宣称可攻击/supported 之前必须完成**；当前 Flow 只有单 `execute`，单 route 只验证 W1 形状。
2. **P0c 前置风险（pselect）**：per-attempt 重喷页未实现；真机若 retry 无效，立即回补（或 Flow 重试 prepare+land），不做 fallback。
3. **host C harness 债**：`gl43499/host/` 仍未建（`validate.c`/`validation_host.c`/Gradle task）；本批用 Kotlin 纯函数字节向量覆盖 generator，Native 校验负例仍靠审读+NDK 编译。
4. **真机门禁**：A301SO 离线 ⇒ 三路均 **UNSUPPORTED / 待 P0c**；不虚报、不 fallback、不标 supported。

## 6. R3d 实施记录（writer r3d-impl，2026-10-27，未 commit；Native ABI diff = 0）

> 用户裁定（本轮）：**不要往 Native 加逻辑或状态机**；多写编排/重试/fast-repair/
> Credential 布局/pselect 重喷全部在 Kotlin（Flow + generator + RequestFactory）；
> Native 只允许 append-only 参数/回执机械字段。本轮 Native **零改动**（证据：所有
> `app/src/main/cpp/**/*.{c,h}` mtime 均早于本批开工时刻，`git diff` 无本批新增 Native 行；
> `scan --gate r2` 仍 exit 0，literals=1437/allowlist sha256 不变）。

### 6.1 已完成（Kotlin 数据平面 + Flow 编排）

| 批次 | 内容 | 关键证据 |
| --- | --- | --- |
| R3d-1（Credential generator） | `writeLayout` 实现 `Credential`（value=`init_cred` data alias、`needs_credential_copy=true`）；`Cve43499PayloadBuilder` 按 `generator.credential.copyOffset`(0x1080)/`tcpCopyOffset`(0x6800) 写 cred 镜像（usage/caps/ref 来自 debug `cred` 段；ref image 绝对地址经 `dataAliasImage`→direct-map 别名）；页接受判据只对 Zero+preserve_child(W1) 派生，Credential/W1b/W3 无判据；替换 `write-mode:credential-unsupported` 具名拒绝 | `Cve43499CredentialWriteVectorTest`：W2 landing 64 KiB 逐字节、cred 镜像、tcp 槽位；leaf 语义/缺 alias 负例 |
| R3d-2（每写生成 seam） | 三路 generator 实现 `Cve43499WriteGenerator.generateForWrite(facts, request)`（W1 兼容入口=`w1WriteRequest()`）；`Cve43499DebugWritePlanner` 按 legacy 目标式派生 W1b/W2/W2b/W3-0/W3 请求（0x798/0x108/init_cred+8/comm/seccomp/thread_info.flags=0）；`Cve43499WriteChainPolicy.fromDebug` 映射轮次/时序（multicast W1=1、tcp W3 exact、W1b/fast-repair 仅 multicast） | `Cve43499CredentialWriteVectorTest.planner derives legacy targets…`；`A301SoDebugConfigTest.write chain parameters…` |
| R3d-3（生命周期多链） | `Cve43499Lifecycle` 改链列表：每次 `preparePlacement` 新开链，`landPage/prepareActor/executeActor` 作用于最后一条链，新增 `closeChain()`；W1 链在 W1b 期间保持打开（legacy quarantine 语义），W1b 出口统一关闭；`Cve43499LifecycleApi` 接口供 fake 注入；dirty 链 close 报 retained 不伪释放 | `Cve43499LifecycleApi`；Flow 测试 `opensBeforeFirstClose>=2` 断言 W1/W1b 双链并存 |
| R3d-4（Flow 多写链） | 新增 10 步链式路径：`request.validate→write.w1→victim.start→write.w1b→write.w2→victim.probe→write.w3→finish.execute→lifecycle.close→acceptance`；每阶段每次写=新 placement→generator→land→prepareActor→execute 事务；W1 attempts（multicast=1）/W2 attempts+uid 判定/W3 leaf 探针+flags+mode+链轮次；attempt 计数与 Carrier 回执进 evidence（`w1Attempts/w1bWrites/w2Attempts/w2Repairs/w2Uid/w3Rounds/w3Attempts/w3LeafDirection/victimTaskAddress/retained`） | `Cve43499WriteChainFlowTest`（6 tests）：顺序/9 事务/重试/负例 |
| R3d-5（fast-repair） | Kotlin 语义：W2 路由成功后立即执行 `init_cred+8` 的 Zero leaf 修复事务（新事务，不是 Native 状态机）；fast 时序变体=actor_setup `setup_settle_us=5000`/`parked_timeout_ns=20ms`/`lock_pi_spin_ns=5ms`（既有 append-only 字段）；修复路由失败⇒W2 立即失败不重试（legacy `return false`） | Flow 测试 `fast repair route miss aborts w2 without retry`（placement 计数=4） |
| R3d-6（pselect 重喷页） | Flow 每次 attempt = 重新 prepare+land（新页 + 新 fd_set 帧，generator 从新 facts 重新生成）；不使用旧页、不在 Native 加重喷状态机；carrier 内 per-attempt 重建 fd_set（R3a-2c）保留 | Flow 测试 `w1 verify retry lands a new page for every attempt`（2 attempts→2 placements/2 landings）；happy 链 9 事务全为新 placement |

### 6.2 决策记录（D-R3d-*，本批唯一权威）

| 编号 | 决策 | 依据 | 代价/偿还 |
| --- | --- | --- | --- |
| D-R3d-1 | **多写链目标由 Kotlin seam 提供**：`Cve43499WriteTargetSource` 返回 victim task_struct 地址；未注入⇒`writeChain=null`（单 W1 兼容、evidence `writeMode=single-w1`）；注入但缺失⇒`cve43499:write.w2:write-targets-missing` 具名失败 | 用户硬约束“Native 零逻辑”；legacy 泄漏在 victim 内 `perf_find_task()`（新架构无此原语） | **P0c 前置阻断 A**：Native 泄漏原语（perf_event_open 机制）由 Lead 另报用户裁定；不得由本批实现 |
| D-R3d-2 | **W3 验收走 verifier seam**：`Cve43499WriteVerifier`（W1 SELinux/leafDirection/seccompProbe）；默认 UNSUPPORTED/无方向⇒W3 具名 fail-closed；生产默认 W1=`Cve43499AndroidWriteVerifier`（读 enforce），W3 保持 UNSUPPORTED | legacy W3 探针需 victim 协议 'M'/'F' 命令（victim/ 冻结） | **P0c 前置阻断 B**：victim 协议命令 + restarter；链轮次>1 需 `Cve43499VictimRestarter`（缺失=1 轮） |
| D-R3d-3 | **fast-repair = 新事务 + fast 时序变体**（不做页复用/prebuild stash）：prebuild 在新线性架构（每写=新 placement）不可表达 | 用户“多写编排在 Flow/generator”；legacy2 `race_session.cpp.disabled:147-148` 时序 | 缺失 prebuild 的“免 spray”收益；逻辑等价（修复写+fast 时序）保留 |
| D-R3d-4 | **Credential 页接受=无判据**：只有 Zero+preserve_child(W1) 需要 selinux initialized 判据；Credential/W1b/W3 派生 required=0 | legacy2 `memory.cpp:216-225` accepts_page 语义 | 无 |
| D-R3d-5 | **R3d 不新增任何 Native 字段**（ABI v4 不变）：多写链/receipt 全部走既有 append-only 字段与 Kotlin evidence | 用户“Native 只允许 append-only 参数/回执机械字段”；本轮未发现必须新增字段 | 无；ABI probe/静态断言不变 |
| D-R3d-6 | **单 W1 兼容路径保留**：`writeChain==null` 时 Flow 仍走 R3b 10 步（行为与字符串不变）；只有注入 target source 才启用多写链 | 兼容既有 UI 接线/测试；P0c 前默认仍单 W1 | 多写链在真机默认不可达（P0c 阻断 A） |

### 6.3 门禁（2026-10-27 实跑，literal+exit）

| 门禁 | 结果 |
| --- | --- |
| `:app:compileDebugKotlin` | EXIT=0；新增/触及文件强制重编译 warning/error=0 |
| 过滤单测 | `:app:testDebugUnitTest --tests "com.ghostlock.app.backend.cve43499.*" --tests "com.ghostlock.app.core.flow.*"` EXIT=0；XML 全量 **48 tests / 0 fail / 0 err**（R3b 基线 30 + R3d 18） |
| `:app:externalNativeBuildCleanDebug` + `:app:externalNativeBuildDebug` | 分两次执行 EXIT=0/EXIT=0；ghostlock43499 16 个 .o 全量重编；log warning/error=0（Native 未改） |
| `scan_native_literals.py --gate r2` | EXIT=0：literals=1437 allowed=1437 unclassified=0 target=0 residue=0；allowlist sha256=`07c8cad4…`（未变） |
| `git diff --check` | EXIT=0 |
| Native 改动审查 | **本批对 `app/src/main/cpp/` 零写入**：全部 `.c/.h` mtime ≤ 14:07（早于本批开工 14:12），`find -newermt` 无命中；`git diff` 中 Native 行均属 R1–R3 已验收改动，本批无新增分支/循环/状态机 |

### 6.4 仍 UNVERIFIED / P0c 前置阻断

1. **阻断 A（victim task_struct 地址）**：缺 Native perf 泄漏原语⇒W2/W3 物理目标不可得；R3d 只交付 seam 与 fail-closed 路径。默认 debug 请求 `writeChain=null`，仍只验证 W1 形状。**R3e 已实现（task-44，工作树未 commit；默认关闭）**：[主件](../plan/43499-perf-leak-primitive-plan.md) + [附件 A](../plan/43499-perf-leak-primitive-plan-annex.md) + [附件 B](../plan/43499-perf-leak-primitive-plan-annex-b.md)：Native 无状态 3 入口（机制-only）+ Kotlin parser/方案 B 投票/target source/owner/W2 precheck；门禁 compileDebugKotlin EXIT=0、XML 79/0/0、NDK 0 warning、scanner r2 EXIT=0（allowlist `841d6915…`）、ABI probe 128/32/48/80；`perf.enabled=0` 默认关闭，**G1 真机门禁未跑 ⇒ UNVERIFIED**。
2. **阻断 B（victim 协议）**：leaf 方向/comm 探针、seccomp finit_module 探针、rooted child park、W3 链轮次重生成（'M'/'F'/'P' 命令）均需 victim/ 改造，不在本批写范围；默认 verifier 对 W3 返回 UNSUPPORTED。
3. **fast-repair 非 prebuild 等价**：新事务需重新 placement（无 legacy 的免 spray 时序收益）；真机是否仍能修复未知。
4. 三路真机门禁仍缺席（A301SO 离线）⇒ 三路 **UNSUPPORTED / 待 P0c**；不宣称可攻击、不 fallback、不标 supported。
5. **victim 启动时机与 legacy 有差异**：legacy 在 `w2()` 内 spawn victim（W1b 之前不 spawn）；
   R3d 链式路径在 W1 成功后、W1b 之前 `victim.start`（Flow 共享 step 顺序所致）。W1b 目标
   只依赖 W1 页基址，与本差异无耦合；真机门禁需一并复核该顺序。
6. **多写链默认不可达**：未注入 `Cve43499WriteTargetSource` 时 `writeChain=null`，Flow 走 R3b
   单 W1 兼容路径（`writeMode=single-w1`）；只有显式注入 seam 才执行 W1b/W2/W3。
7. Credential ref image direct-map 别名与 legacy 逐值对拍已在单测覆盖，但**未在真机读回验证**。

## 7. R3f 实施记录（task-45，writer routes-impl3，2026-10-27，未 commit；Native 零改动）

> 用户指令：「**finish 脚本要交给 victim 执行，把 W1 的墙拆掉允许进入后续步骤**」。
> 本批纯 Kotlin：`victim/`、`flow/`、`lifecycle/`、`finish/` 适配与 `FinishHandoffAdapter`；
> `app/src/main/cpp/**` **零改动**（Native ABI diff = 0，scanner/NDK 不受影响）。

### 7.1 已完成

| 批次 | 内容 | 关键证据 |
| --- | --- | --- |
| R3f-A1（victim 行协议） | `ProcessVictim.DEFAULT_COMMAND` 改固定 shell 循环：`probe`⇒`id -u`；`finish <timeout_s> '<path>'`⇒victim 内 fork/exec `/system/bin/sh <path>` + watchdog（到点打 `GLK_FINISH_TIMEOUT:1` 并 kill 脚本，随后打 `GLK_FINISH_EXIT:<code>`）；`park`⇒`exec sleep` 驻留不杀；`bye`⇒退出。新增 `VictimProtocol` 纯函数（路径校验/单引号编码/marker 解析/输出 SHA-256）与 `VictimFinishResult` | `VictimProtocolTest`（6 tests） |
| R3f-A2（session） | `VictimSession`/`ProcessVictimSession` 增 `rooted`（uid==0 置位）、`finish(scriptPath, timeoutMs): VictimFinishResult`（有界 = timeout + 2s 排空；超时 fail-closed 并关闭会话）、`detachRooted()`（发 park 后不 close 流/不 destroy）；非 rooted `close()` 先 best-effort `bye` 再有界回收 | `ProcessVictimSessionTest`（8 tests） |
| R3f-A3（Flow finish） | `finishExecuteStep`：victimRooted ⇒ 先验磁盘脚本 SHA-256==handoff（不符 **Refused**）⇒ `lifecycle.victimFinish` ⇒ 构造 `FinishExecutionEvidence`（exit/outputSha/timedOut/marker）；victim 失败/超时**具名 fail-closed、不回退 su**；非 rooted ⇒ 保持既有 `FinishExecution` fallback，但其“成功”结果一律 `cve43499:finish.execute:su-fallback-refused`（不得静默 su 成功） | `Cve43499WriteChainFlowTest` R3f 4 tests |
| R3f-A4（rooted 不杀） | `Cve43499Lifecycle.closeAll`：rooted session 只 `detachRooted()`（park），termination=`victim=rooted-child-parked[(-unconfirmed)]`，`Cve43499LifecycleReceipt.parkedRootedChild=true`，`allReleased=true`/`retained=false`（不伪报 Native 泄漏）；非 rooted 正常 close。Flow evidence `retained=rooted-child-parked`/`rootedChildParked=true` | `Cve43499LifecycleRootedTest`（4 tests）+ flow `rooted child parked evidence` |
| R3f-B1（拆 W1 墙） | `writeW1Step` 的 `UNSUPPORTED` 由 Failed 改非阻断：`markExecuted` 保留 W1 链/facts，evidence `w1Verify=unsupported`+`w1VerifyReason`（verifier 提供：`enforce-unreadable`/`verifier-unsupported` 等），不 close W1 链，继续 W1b/W2/W3 | flow `w1 verify unsupported continues to w1b w2 w3` |
| R3f-B2（UNVERIFIED 耗尽） | 重试/backoff 语义保持；最后一次 UNVERIFIED 耗尽保留最后一条链与 facts（W1b quarantine 需要），`w1Verify=unverified`+reason 后继续；**RouteMiss/Failed 仍按原语义失败** | flow `unverified exhausted continues…` / `route miss still fails` |
| R3f-B3（后续不依赖 W1 verdict） | W1b 只消费保留的 `w1Facts`；W2/W3 路径不读 W1 verdict；acceptance/evidence 输出 `w1Verify`/`w1VerifyReason` | 同上 |
| 单测 | 过滤单测 XML 全量 **109 tests / 0 fail / 0 err**（R3f 新增 27：协议 6 + session 8 + lifecycle 4 + flow 9 + …） | `build/app/test-results/**/*.xml` |
| 门禁 | `:app:compileDebugKotlin` EXIT=0（新增代码无 warning 增量）；过滤单测 EXIT=0；`git diff --check` EXIT=0；Native 零改动（本批无 `app/src/main/cpp/**` 写入） | 任务报告 literal+exit |

### 7.2 决策记录（D-R3f-*，本批唯一权威）

| 编号 | 决策 | 依据 | 代价/偿还 |
| --- | --- | --- | --- |
| D-R3f-1 | **finish 执行权**：rooted victim 优先；无 rooted victim 时 App+su fallback 的“成功”结果一律拒绝 | 用户指令 + legacy 'G' 语义（rooted victim 内 fork/exec）；冷机无 su | 单 W1/无 W2 路径不再可能靠 su 伪成功；由具名拒绝表达 |
| D-R3f-2 | **rooted child 终结 = detach(park)**：发 park 后不 close 流/不 destroy；evidence `rooted-child-parked`；有意保留 root child 与两条管道到 App 进程退出 | 用户“rooted victim 不杀” | **技术债**：parked child/管道 fd 无回收/复检机制；P0c 观察驻留副作用（owner=Lead） |
| D-R3f-3 | **finish 有界双重保护**：victim 内 watchdog（ceil(timeoutMs/1000) 秒，到点 kill 脚本）+ session 侧 `timeoutMs+2s` 排空；超时 ⇒ 会话失效、Flow fail-closed（不回退） | 用户“有界 timeout”；避免挂死阻塞协议 | watchdog 只杀 direct child；孙进程不保证级联回收（见 7.3-3） |
| D-R3f-4 | **脚本对象身份**：交给 victim 前 Flow 用 `evidenceSource` 重算磁盘字节 SHA-256 与 handoff 对拍；不符 Refused（不执行） | F-D/FinishHandoff 既有契约（同一脚本对象） | 无 |
| D-R3f-5 | **W1 墙只拆“判据缺失/未成立”**：UNSUPPORTED/耗尽 UNVERIFIED 非阻断；RouteMiss/Failed 仍硬失败；W1 链保留供 W1b quarantine | 用户“把 W1 的墙拆掉允许进入后续步骤”；legacy quarantine 语义 | 后续步骤不再以“无法验证”为由停；真机命中率/时序仍需 P0c |

### 7.3 仍 UNVERIFIED / 风险

1. **真机门禁缺失**：A301SO 离线；victim 内 fork/exec finish、rooted parked child、W1 UNSUPPORTED 继续、watchdog kill 均**未真机验证**；三路仍 UNSUPPORTED / 待 P0c。
2. **parked root child 与两条管道 fd 有意保留到进程退出**（D-R3f-2）：无回收/复检；P0c 需观察 root child 驻留对后续 run/日志/UID 的副作用。
3. **watchdog 只 kill direct child**：脚本内孙进程不保证级联回收；hung 场景以 `GLK_FINISH_TIMEOUT:1`+exit 证据 fail-closed，但残留进程需 P0c 检查。
4. **victim finish 输出摘要**：SHA-256 覆盖脚本 stdout+stderr 逐行（含换行）；marker 仍由 Flow 用 evidenceSource 从 `/dev/dfm6`/`/dev/dfme2` 读回——root 脚本写 marker 后 app 域可读性未真机验证。
5. **R3e/perf 的 R3f 共享文档行**：已由 Lead 转交并并入 **§7.4**（task-46 文本，Lead 复核后转交）。

### 7.4 R3e/task-46 perf R3f 审计修复（2026-10-27，工作树未 commit；Native 仅机械回执/记账）

> 来源：r3e-impl（task-46）实现并提供的文本，Lead 复核后转交本 writer 并入；perf 设计三件见
> [perf plan](../plan/43499-perf-leak-primitive-plan.md) 及附件。task-46 落盘早于本 writer 冻结
> 共享文档范围；**B9 两处 Flow 改动已核对并保留**（`Flow43499Impl.kt` W2 失败 evidence 透传
> `perfCloseRetained`、`Cve43499WriteChain.kt` `Missing.closeRetained`），未回退/覆盖。

- **B1（必修）**：`PERF_SAMPLE_REGS_INTR` 由误写 `1<<14`（WEIGHT）改为 **`1<<18`**（单一常量 +
  注释禁写）；TargetSource expected 派生；debug asset `perf.attr.sample_type=262145`、
  `attr.flags=8`(CLOEXEC)、`enabled=0`；单测断言 262145 + 回归「仅 1<<14 ⇒ perf-config-invalid」；
  10-08 finding 撤回“寄存器不投递”结论（探针 WEIGHT 位错 ⇒ 24B/ABI=0；262145 的 G1 判据 =
  **280B/ABI=1/32 regs**）。
- **B5–B9（Native 机械边界，无业务逻辑）**：close DISABLE 失败 ⇒ SYSTEM_ERROR/DIRTY/retained=0；
  open 清理不净 ⇒ 不 free、SYSTEM_ERROR/DIRTY/QUARANTINED/retained=1；JNI open 回执写失败先
  close handle 再 -1；owner 仅非 NOT_ACCEPTED(-2) 清 token；`Failed/Missing` 携带 `closeRetained`
  并由 Flow W2 失败 evidence 透传（`perfCloseRetained`）。
- **建议项**：`ring_pages=2` 幂等、`tagShift=0 ⇒ nibble=0`、CLOEXEC 已入配置。
- **门禁（task-46，树绿 18:33:33）**：`compileDebugKotlin` EXIT=0；过滤单测 EXIT=0、XML
  **109/0/0**（task-46 直管 54/0/0）；NDK `CleanDebug`+`Debug` EXIT=0/0 warning；scanner r2
  EXIT=0（literals=1574、unclassified/target/residue=0、allowlist 重签 sha256 `7724a69b…`）；
  ABI probe 新增 128/32/48/80 且既有 20 项不变；`git diff --check` EXIT=0。
- **资产 sha**：debug-a301so.conf `38c5fc8b…`；10-08 finding `c362fb8b…`。
- **仍 UNVERIFIED**：G1/262145 寄存器投递、`perf_event_paranoid`；`perf.enabled=0`，未 commit/push。


### 8. R4：JNI victim 接入（批 B1/B2，2026-10-27，工作树未 commit；Native 仍纯机械）

> 来源：Lead 复核转交的批 B1（task-53）结果 + 本 writer（routes-impl3）批 B2（task-54）
> 实现记录。设计依据见 [app-domain plan](../plan/43499-app-domain-execution-model-plan.md)
> §8 与附件 §A–§D（U1–U4 已批准/确认）。**默认 `perf.enabled=0`、`injection.victim=process`：
> 生产/单 W1 行为不变；新路径只在 debug 配置显式开启且 perf 配置齐全时构造。**

#### 8.1 批 B1（task-53，Lead 复核转交）：JNI transport + ABI=2

- 新增 `core/victim/VictimNativeJni.kt`：把 `core/victim_protocol` 机械 API 1:1 映射为
  `VictimNative`（spawn/send/receive/readRaw/rawSize/waitExit/kill/detach/close）；**值域校验/内存
  编组/具名错误映射**，不解析帧内容、无状态；架构测试继续约束 `core/victim` 无 backend 依赖。
- Native 侧新增 9 个通用 JNI 符号（`VictimTransportJni`），帧语义仍由 Kotlin 权威；helper 仍
  posix_spawn（生产/JNI 零 fork）。
- 门禁（Lead 复核）：`compileDebugKotlin` EXIT=0；过滤单测 **XML 151/0/0**；scanner r2 EXIT=0；
  JNI 符号 9 个；ABI probe 全绿。**真机 helper exec/perf 仍 UNVERIFIED。**

#### 8.2 批 B2（task-54）：Flow 接入通用 victim

**新组件（backend 使用方，`core/victim` 保持 backend-agnostic）**
- `Cve43499JniVictimRuntime`：一次 run 的 helper 传输配置（nativeLibraryDir PIE 路径 + raw 容量）；
  按需创建会话；`currentSampler()` 只暴露已启动会话。
- `Cve43499VictimAdapter`：通用 victim → backend `VictimSession` 适配（start/uid/rooted/identity/
  finish/detachRooted/close）；`finish` 映射 helper `execScript`（`/system/bin/sh <script>`，当前
  uid，有界超时），输出 SHA-256 与 task-45 finish 语义一致；rooted detach=park 后只 detach、**不 kill**。
- `Cve43499JniVictimTargetSource`：helper self-perf（Kotlin 下发 attr/ring/迭代/raw 窗口）→ raw 经
  JNI memfd readRaw 回 Kotlin → 既有 R3e parser + 2-of-3 + 同意 pass 门槛；**ABI=2（280B/8B regs）
  接受**；helper 回传 `data_head`，Kotlin 以 `tail=max(0,head-ring)` 还原窗口；采样失败/截断/几何
  不符具名 fail-closed；**W1 permissive 未确认 ⇒ `perf-permissive-required`（不采样、不猜）**。
- 配置：`injection.victim = process | jni-spawn`（debug 资产，默认 `process`；未知/缺失 fail-closed）；
  `jni-spawn` 但 `perf.enabled!=1` ⇒ RequestFactory 具名 Refused（不静默回退旧路径）。
- Flow 接线：`Flow43499RunSpec.victimFactory`（null ⇒ 默认 `ProcessVictimSession`）；Flow 新增
  spec 级 lifecycle 工厂（默认路径按 runSpec 构造 `Cve43499Lifecycle(victimFactory)`），保留单一
  lambda 兼容构造供既有 fake 测试。链式 step 顺序保持 **W1 → victim.start → W1b → W2 → victim.probe
  → W3**（victim start 在 W1 成功之后）；W2 baseline/precheck 用新 victim identity；finish 由 rooted
  victim（execScript）执行；rooted detach/park 语义不变。
- **单 W1 兼容路径**：`writeChain=null` 时步骤表/行为不变（新增回归测试固化历史 10 步顺序）；
  未注入 victimFactory 时 lifecycle 仍为 `ProcessVictimSession`。

**决策**
| 编号 | 决策 | 依据 | 代价/偿还 |
| --- | --- | --- | --- |
| D-R4-1 | 新路径由 `injection.victim=jni-spawn` **显式开启**，默认 `process`；关=旧 ProcessVictim 行为不变 | 最小风险/可回退；U1 J 主路径为长期方向 | 双路径并存（技术债：E5 后删旧路径，owner=Lead） |
| D-R4-2 | `jni-spawn` 缺 perf 配置 ⇒ **Refused（不静默回退）** | fail-closed；避免“以为走了新路径其实旧路径” | debug 配置必须同时给 perf 完整参数 |
| D-R4-3 | self-perf 目标源**不混用** App 进程跨任务 perf（R3e 源）；只走 helper self-sample | 与 J 设计“helper 采样 pid=0”一致；避免两个机制做同一件事 | R3e 旧源保留（默认 perf 路径），E5 后决定去留 |
| D-R4-4 | permissive 观测以 `/sys/fs/selinux/enforce` 实际读数为准；读不到/非 0 ⇒ 拒绝 | W1 后 permissive 是 helper self-perf 生效前提；不基于旧证据 | 真机 SELinux 文件可读性/时序未验证（E4/E5） |
| D-R4-5 | finish 由 adapter 映射 `execScript`；task-45 finish/ProcessVictim 文件不改（本批无映射） | 减少闭口文件改动；语义（退出码/输出 SHA/超时）一致 | 输出 SHA 覆盖 stdout+stderr 合并字节（helper 管道），与 task-45 逐行语义需 E4 对拍 |

#### 8.3 门禁与 UNVERIFIED

- 门禁（B2 最终树，工作树未 commit）：`compileDebugKotlin` EXIT=0；`compileDebugUnitTestKotlin`
  EXIT=0；过滤单测（backend.cve43499 + core.flow + core.victim）**EXIT=0、XML 169/0/0**
  （B2 新增 18：adapter/target 13 + Flow 2 + config 3）；NDK `CleanDebug`+`Debug` EXIT=0/0、
  warning=0/error=0（本批 Native 零改动）；scanner r2 EXIT=0（literals=2113、all allowed、
  unclassified/target/residue=0，allowlist sha `02bddb0540…`）；ABI probe EXIT=0、24 sizeof
  与既有完全一致；`git diff --check` EXIT=0。
- 资产 sha：debug-a301so.conf `280c9128bd26…`（新增 `injection.victim` 行）。
- **UNVERIFIED**：① E4 真机 helper exec/uid/seccomp/fd 集合/spawn 性能；② E2/E5 helper self-perf
  实际寄存器投递/ABI=1/2 真机形态/2-of-3 命中；③ permissive 文件可读性与 W1→spawn 时序；
 ④ finish 输出摘要与 task-45 逐行语义对拍；⑤ RequestFactory 的 `jni-spawn` 端到端接线仅
  静态/单测覆盖（未跑真机/未跑 assemble）；⑥ perf 仍 `enabled=0`，未启用生产。

### 8.4 mondrian 5.10 配置接入 + placement floor（task-58，2026-10-28，工作树未 commit；Native 仅机械小改）

- **配置**：新增 `app/src/debug/assets/profile/43499/debug-mondrian.conf`（8,573 B，逐字段注释来源）：
  release/kernel_major/kernel_phys_load/`route.multicast_waiter`/`offset`/`task_struct`/`cred`/`kernelsnitch` 取
  设备 full.conf（`/sdcard/Download/k60-mondrian-20260531-full.conf`，host 副本当时 `/tmp/k60-mondrian-full.conf`）；
  `kernel_phys_offset=0x80000000` 为 full.conf 无键的 legacy 默认（UNVERIFIED，E1 回填）；route 几何与
  `docs/analysis/mcast-510-probe.md` 一致；pselect/tcp 段保留但 `injection.primitive=multicast`（TCP 已知 ENOPROTOOPT）。
- **loader**：`A301SoDebugConfig` 新增 `ASSET_PATH_MONDRIAN` + `DebugDeviceIdentity`/`detectDeviceIdentity`/
  `selectAssetPath`/`load`：A301SO→`debug-a301so.conf`（行为不变）、mondrian→`debug-mondrian.conf`；未知/歧义设备
  fail-closed（`debug-config-device-unsupported|ambiguous`）；`verifyKernelRelease` release 不匹配具名失败。
- **placement floor**：5.10 非整除允许，`objects_per_slab = allocation_size / mm_struct_size`（legacy2
  `support.cpp:564` 同式；legacy evidence）；派生式收敛到 `placement.h` 单一 inline 助手
  `glk43499_placement_objects_per_slab`（mm=0/mm>allocation/结果 0/>UINT32_MAX 拒绝）。A301SO 1024/32768⇒**32**
  数值不变；mondrian 960/32768⇒**34**（余 128；34*960=32640≤32768<35*960=33600）。
- **单测**：Native host `tools/tests/placement_floor_test.c`（`clang -std=c11 -Wall -Wextra -Werror -I
  app/src/main/cpp/gl43499/core ... && /tmp/placement_floor_test`）**EXIT=0**，覆盖 A301SO=32/mondrian=34/精确整除/
  非整除/边界与 u32 溢出；Kotlin `MondrianDebugConfigTest` 6 tests/0 fail（含 conf 字段、设备选择、release、
  floor 边界与 RequestFactory 请求向量）。
- **门禁（最终树实跑）**：compileDebugKotlin/UnitTestKotlin EXIT=0；过滤单测（backend.cve43499+core.flow+
  core.victim+data.cve43499）EXIT=0、XML **178/0/0**；NDK CleanDebug+Debug EXIT=0、warning/error=0；
  scanner r2 EXIT=0（literals=2115、allowed=2115、unclassified/target/residue=0、**allowlist 重签
  `f7f8d461…`**）；ABI probe EXIT=0（placement_request sizeof=216 与既有矩阵不变）；`git diff --check` EXIT=0；
  `assembleDebug` EXIT=0。并发 98260 测试首次 `testDebugUnitTest` 因共享 build 目录 EOFException 失败，
  **串行重试 EXIT=0**（非源码冲突；本批目标 43499 文件未被 98260 改动覆盖）。
- **UNVERIFIED**：设备全部离线（`adb devices -l` 空），E0/E4/E1 未执行；mondrian release/offset 与真实
  `uname/os.version` 对拍、E0 快照、E4 helper、E1 W1-only 冷机门禁均待设备恢复；`perf.enabled=0`，未启用生产。

### 8.5 零 fork ② 严格口径（task-60，2026-10-28，工作树未 commit；无需设备）

- **背景**：旧 CMake 对 Debug/Release 都传 `-Wl,--wrap=fork -Wl,--wrap=vfork`，Debug 产物按构造导入
  `fork`/`vfork` 并定义 `__wrap_*`/`__real_*`，使附件 §A 的严格 ②「无 fork/vfork 导入」在 Debug 上不可判。
- **实现**：CMake 改为 `$<CONFIG:Debug>` 条件：仅 Debug 传 wrap 并定义 `GLK_VICTIM_FORK_WRAP=1`；
  `victim_protocol.c` 的 `__wrap_*`/`__real_*` 与 `jni43499.c` 的 fork canary 受该宏保护；Release/
  RelWithDebInfo 无 wrap、无 `__wrap_*`/`__real_*` 依赖，`glk_victim_fork_count/vfork_count` 恒 0、
  reset no-op（调用方语义不变）。
- **Host 验证（可复跑）**：`python3 tools/verify_no_fork_imports.py`（自动定位产物与 NDK readelf/nm）→
  `PASS release libghostlock43499.so: fork=absent vfork=absent wrap=absent posix_spawn=UND nm_undefined_ok`；
  `PASS release libghostlock_victim_helper.so: ...`；`INFO debug ...: fork/vfork import + __wrap_* present
  (E4 ① measurement only)`；**EXIT=0**。构建系统计数：Release `build.ninja` 中 `--wrap=` **0**、宏 **0**；
  Debug `--wrap=` **2**、宏 11 处。
- **门禁**：`externalNativeBuildDebug` EXIT=0 / warning,error=0；`externalNativeBuildRelease` EXIT=0 /
  warning,error=0（首跑 stale `.cxx/RelWithDebInfo` ninja recompaction 错误，清生成缓存后重跑绿）；
  compileDebugKotlin/UnitTestKotlin EXIT=0；过滤单测 **178/0/0**；scanner r2 EXIT=0（literals=2117、
  allowed=2117、unclassified/target/residue=0、**allowlist 重签 `a7523f38…`**）；ABI probe EXIT=0
  （既有矩阵不变）；`git diff --check` EXIT=0。
- **UNVERIFIED**：仅 host 产物符号审计；E4 ① 的真机计数测量仍待设备（设备离线）；Release APK 未安装/未跑
  真机；`perf.enabled=0`，不 commit/push。

### 8.6 N01 修复：恢复 7 个 Placement/Page/Actor JNI 导出（task-62，2026-10-28，工作树未 commit；设备离线）

- **现象（task-61 矩阵 N01，S1 阻断）**：工作树 `jni43499.c` 仅 17 个 `Java_` 导出，缺
  `preparePlacement/cancelPlacement/closePlacement/landPage/closePage/prepareActor/executeActor`；
  Kotlin 仍声明 24 个 external（`Native43499Jni` 12 + `VictimTransportJni` 12）⇒ Flow W1/W2/W3 首次 JNI
  调用会 `UnsatisfiedLinkError`；NDK/单测不查符号故旧门禁未发现。
- **修复（机械）**：按 Lead 指令**只读** `git show HEAD:a9af5482:app/src/main/cpp/gl43499/jni43499.c`
  取回 7 个入口 + `glk43499_jni_placement_close_call` helper（HEAD 行 531–710），插入当前文件
  actor close helper 之前；**未用 checkout/restore/stash**。保留 victim 9 / perf 3 / fork guard 3
  导出（合计 24）、`GLK43499_WRITE_OR_FAIL` 顺序 writer、ABI v4、三态/INCOMPLETE 回执；恢复函数逐行
  等同 HEAD（`preparePlacement` 的 `result_ready→read→core→write`、`landPage` 的 bytes 深拷贝/清理、
  `INCOMPLETE` 返回等均保留）。
- **root cause（证据边界）**：`git diff -- jni43499.c` 显示 HEAD 的 `glk43499_jni_placement_close_call`
  + 7 导出 + actor close helper 区段（HEAD 559–711）被**单次块替换**为 actor close helper + perf3 +
  victim9 + fork guard3；不是逐行删除。工作树无 commit/per-edit 历史（task-52..60 均未 commit），
  无法唯一定位具体操作；时间边界：2026-10-28 00:xx A301SO E1 仍能进入 `preparePlacement`（JNI 符号在），
  task-61 快照（12:4x）已缺失，期间工作树由多 writer/98260 共享 ⇒ 判定为**追加 JNI 导出时覆盖同区段**
  的写操作事故。**技术债（owner=Lead）**：JNI 入口/导出只允许尾部 append，任何 `jni43499.c` 改动后
  必须跑 `tools/verify_jni_exports.py` + 保留文件 hash 对拍；本条随 task-62 关闭后偿还。
- **防回归 gate**：新增 `tools/verify_jni_exports.py`（Python host，无 sh）：解析
  `Native43499Jni.kt`/`VictimTransportJni` 的 `external fun`（含 JNI 名称转义）生成期望符号，
  NDK `llvm-nm -D --defined-only` 读取 Debug/Release `libghostlock43499.so`，断言期望 ⊆ 产物。
  实跑：Debug/Release 均 `expected=24 found_java=24 missing=0`、**EXIT=0**；负例（临时 Kotlin 加
  `missingProbe`）→ `FAIL missing: ...missingProbe`、**EXIT=1**（fixture 已删）。
- **门禁（最终树实跑）**：compileDebugKotlin/UnitTestKotlin EXIT=0；过滤单测 EXIT=0、XML **178/0/0**；
  NDK `externalNativeBuildCleanDebug+Debug` EXIT=0/0 warning；`externalNativeBuildCleanRelease+Release`
  首次 `metadataGenerationTimingFolder`/`NoSuchFileException`（AGP stale flake），Release-only 重试
  EXIT=0/0 warning；scanner r2 EXIT=0（literals=2128、allowed=2128、unclassified/target/residue=0、
  **allowlist 重签 `ed33ad49…`**）；`python3 tools/verify_no_fork_imports.py` EXIT=0（Release 仍无
  fork/vfork 导入）；ABI probe EXIT=0（既有矩阵不变）；`git diff --check` EXIT=0。
- **UNVERIFIED**：设备离线（`adb devices` 空），未做真机 `dlopen/dlsym`/Flow W1 JNI 调用验证；E1/E5
  仍待设备恢复；`perf.enabled=0`，不 commit/push。


### 8.7 P1a parity：G02/G06/G07/G09/N02/N03（task-63，2026-10-28，工作树未 commit；设备离线）

> 依据 task-61 三路 call matrix G 清单（`43499-w1-w3-three-route-call-matrix-20261028.md`）
> 与 legacy2/very_legacy 对照。约束：JNI 主路径不变、零 fork、逻辑 Kotlin（Native 仅机制）、
> `perf.enabled=0`、不碰 98260 文件。G01（dirty fail-stop）/G05（fast-repair 预建）需 Lead/用户
> 裁定，本批不做。

- **G02 pselect 每 attempt 超时（S1）**：`carrier_pselect.c` 旧逻辑每次 `select/pselect` 都吃
  剩余**总 deadline**（旧行 497–513）⇒ `operation_budget` 形同虚设。新增纯头
  `core/pselect_timeout_internal.h`：`per_attempt = timeout_ns / max(1, operation_budget)`
  （800ms/4=200ms，等价 legacy2 `route_select.cpp:457-470` 的 4×200ms）；
  `attempt_timeout = min(remaining, per_attempt)`；循环仍按 attempts/total deadline/成功退出。
  **不改 ABI**（不新增请求字段）。Host 单测 `tools/tests/pselect_timeout_test.c` EXIT=0
  （800/4、budget=1、budget=0⇒1、非整除 floor、timeout<budget⇒0、min 截断）。Kotlin config
  `operationBudget/timeoutNanos` KDoc 同步语义。
- **G06 RLIMIT（S2）**：新增 JNI 单步入口 `Native43499Jni.raiseProcessLimits(result)`
  （`jni43499.c:1171-1280`；getrlimit→setrlimit soft=hard→getrlimit 回读；NOFILE/NPROC 都做；
  返回 callStatus/per-limit errno/before/after；失败仍写回可证伪读数；不改 core ABI）。
  Kotlin `owned/ProcessLimits`：`Raised/Failed(process-limits:*)`，成功证据
  `processLimits/limitsNofileBefore/Hard/After/limitsNproc*`；Java/Kotlin DTO
  `Native43499LimitsResult`（@Keep）。Flow 在 `request.validate`（W1 前）调用
  `preflight()`；失败 ⇒ `cve43499:request.validate:process-limits:*` fail-closed；成功 evidence
  随 validate Done 合并。RequestFactory 注入生产 seam。单测 6（成功/not-accepted/incomplete/
  per-limit errno/未抬升回读硬校验/JNI 抛异常）。
- **G07 KSU 守卫（S2）**：`owned/KernelsuGuard` 三类进程外 positive signal：`/proc/modules`
  模块名 `kernelsu|ksu`、`/sys/module/ksu|kernelsu` 存在、`/data/adb/ksu|ksud` 存在；
  readers 全注入；任一命中 ⇒ `Loaded(signal)` ⇒ Flow W1 前拒绝
  `cve43499:request.validate:ksu-loaded:<signal>`，evidence `ksuGuard/ksuSignal/ksu.<signal>`；
  全 absent/unreadable ⇒ `Absent`（unreadable 不误判 loaded，记录读数）。单测 6（三类信号、
  组合顺序、clean、unreadable）。
- **G09 SIGPIPE（S2/N05）**：`page.c:358` shaping `sendmsg`、`page.c:403` reclaim `sendmsg`
  加 `MSG_NOSIGNAL`；`carrier_tcp.c:465` `send` 加 `MSG_NOSIGNAL`（legacy backend:431）；
  行为其余不变。架构回归测试断言三处。
- **N02 spawn file_actions（S2）**：旧顺序 `adddup2` 后 `addclose(control_pair[0/1]/raw_fd)`，
  未排除原 fd==3/4 ⇒ 可能关掉刚安装的 control/raw 通道。冻结：`addclose` 全部排在 `adddup2`
  **之前**，且只 close `fd>2 && fd!=3 && fd!=4`（`victim_protocol.h:
  glk_victim_should_close_fd`）；`CLOEXEC_DEFAULT` 与 helper `close_range` 为第二/三道防线。
  宿主规则单测 `tools/tests/victim_fd_rules_test.c` EXIT=0（保留 0..4、关闭 5/6/63/1023、
  负 fd 不关）；架构测试断言 addclose 先于 adddup2 且经规则过滤。child fd 集合
  `{0,1,2,3,4}` 的 instrumented fixture（E4）在 task-58 真机报告已 PASS，本批设备离线未重跑。
- **N03 helper perf raw 屏障（S2）**：`victim_helper.c` 由 `volatile` 读 `data_head` 改
  `__atomic_load_n(..., __ATOMIC_ACQUIRE)` + `__atomic_thread_fence(__ATOMIC_ACQUIRE)` 后再
  memcpy ring；`data_tail` 写改 `__atomic_store_n(..., __ATOMIC_RELEASE)`（与 `perf.c:330-333`
  同序）。架构回归测试断言三段。
- **门禁（最终树实跑）**：compileDebugKotlin/UnitTestKotlin EXIT=0；过滤单测 EXIT=0、XML
  **195/0/0**（P1a 新增 15 = ProcessLimits 6 + KSU 6 + Flow preflight 3，另 host 2 项 + 架构 3）；
  NDK `externalNativeBuildCleanDebug` EXIT=0、Debug EXIT=0/0 warning/0 error、Release EXIT=0
  （compiler warning=0；13 条 `CXX5106` 为 AGP “ndk.dir deprecated” 环境告警，非编译器告警）；
  scanner r2 EXIT=0（files=25、literals=2155、allowed=2155、unclassified/target/residue=0、
  **allowlist 重签 `3af0bd65…`**）；ABI probe EXIT=0、24 sizeof 不变（core 请求/回执结构未改）；
  `tools/verify_jni_exports.py` EXIT=0（Debug/Release **expected=25 found=25**，
  Native43499Jni 13 + VictimTransportJni 12）；`tools/verify_no_fork_imports.py` EXIT=0
  （Release 无 fork/vfork 导入、posix_spawn present）；`git diff --check` EXIT=0。
- **UNVERIFIED**：设备离线（`adb devices` 空），G06 真机 setrlimit/NPROC、G07 真机
  `/proc/modules` 可读性与 KSU 判定、G02 5.10 真机 select 每 attempt 时序、N02 真机 child fd
  集合、N03 真机 perf ring 一致性均**未真机验证**；`perf.enabled=0`，未 commit/push。

### 8.8 P1b-1：G01 dirty/retained fail-stop + G08 CPU pin 只 pin consumer（task-64，2026-10-28，工作树未 commit；设备离线）

> 依据 task-61 矩阵 G01/G08 与 legacy 同形证据（L1 backend `exit_group(70)`；L1
> `threads.cpp:135-138`）。约束：JNI 主路径不变、零 fork、逻辑 Kotlin（Native 仅机制）、
> `perf.enabled=0`、设备离线不跑真机、不碰 98260 文件。

- **G01 dirty/retained fail-stop（选 (a) Kotlin 收 receipt 后强制终止）**：
  - 新增 `backend/cve43499/owned/FailStopController.kt`：可注入 `terminator`
    （生产默认 `android.os.Process.killProcess(myPid())`，单测 fake 不真杀）/
    `evidenceSink`/`logger`；**顺序冻结 evidence→logger→terminate**；`enabled=false` 只记录；
    **只触发一次**；reason 冻结 `fail-stop:<stage>:<retainedReason>`；evidence 字段
    `failStopTriggered/failStopReason/failStopStage/failStopPrecededByRetained`。
  - Flow 集成（`Flow43499Impl.kt:1306-1350`）：`markRetained(stage,reason)` 在写链
    W1/W1b/W2/W2-fast-repair/W3 flags/mode/leaf-probe、单 W1 `actor.execute` dirty/retained、
    `lifecycle.close-chain` retained、`lifecycle.close`/`cleanup` 非 rooted-child-parked
    retained 处触发；`Cve43499WriteTransactionRunner` 把 Actor `retainedForProcessExit`
    归类为 `Failed(retained=true)`（进入同一 fail-stop 路径）；evidence 先合并进
    `run.writeEvidence`（validate/lifecycle.close Done 证据透传）。
  - debug 开关：两份 conf 新增 `failStop { enabled = 1 }`（legacy 同形；缺省=1）；
    `A301SoDebugConfig` 解析（非法取值 fail-closed）；release 不读 debug 配置。
  - RequestFactory 生产 sink：logcat + `filesDir/fail-stop.log` append/`flush`/`fd.sync()`
    （`FinishHandoffAdapter.kt:343/354`；IO 失败不阻止终止）。
  - 单测：`FailStopControllerTest`（顺序/只一次/disabled/字段）、Flow 3 例
    （dirty→先 evidence 后 terminate 且只一次、clean 不触发、retained close 触发并留
    evidence）、config 3 例（两份 conf/missing 缺省/非法 fail-closed）。
- **G08 CPU pin（legacy 同形：首次真机门禁只 pin consumer）**：
  - **不改 ABI**：复用既有 `owner_cpu/waiter_cpu/consumer_cpu` u32 字段，新增
    `core/cpu_pin_internal.h` 哨兵 **u32 全 1 = no-pin**（Kotlin `-1` 经 JNI `get_int`
    转 u32 同值）；`actor.c:28` include；owner pin（`:310`）与 waiter arm pin（`:366`）仅在
    `glk43499_internal_cpu_pin_required`=1 时执行；consumer 保持无条件 pin（`:232`）。
  - 校验（`actor.c:640-648`）：owner/waiter 允许哨兵或有效 CPU；consumer 必须有效 CPU
    （拒绝哨兵）。
  - debug conf 两份新增 `execution { owner_cpu=-1; waiter_cpu=-1; consumer_cpu=2 }`；
    `A301SoDebugConfig` 解析并对 `executionParameters(debug)` 覆盖
    （`ownerCpu/waiterCpu/consumerCpu`；缺省 legacy 0/1/2；非法/consumer 哨兵 fail-closed）。
  - 单测：host `tools/tests/cpu_pin_test.c` EXIT=0（哨兵/普通 CPU 判定）；
    `FailStopCpuConfigTest` 3 例（两份 conf、missing 缺省、非法 fail-closed）；架构回归断言
    owner/waiter 走 sentinel 跳过、consumer 无条件 pin、校验允许哨兵。
- **门禁（最终树实跑，见 send_message 表）**：compile EXIT=0；过滤单测 **205/0/0**
  （P1b-1 新增 10 UI 单测 + host 1）；NDK CleanDebug+Debug EXIT=0/0 warning、Release
  EXIT=0（compiler warning=0）；scanner r2 EXIT=0（files=26、literals=2160、
  **allowlist 重签 `f1489bfd…`**；哨兵用取反表达避免 address-space target 误判）；
  ABI probe EXIT=0、24 sizeof 不变（actor_setup 字段/布局未改，仅新语义值）；
  verify_jni_exports Debug/Release expected=25 found=25 EXIT=0；
  verify_no_fork_imports EXIT=0；`git diff --check` EXIT=0。
- **UNVERIFIED**：设备离线 —— G01 真机 killProcess/evidence 落盘时序、G08 真机
  owner/waiter 不 pin 与 consumer pin 的物理效果均未真机验证；「首次真机门禁只 pin
  consumer，后续单变量 A/B」仅登记计划；`perf.enabled=0`，未 commit/push。

### 8.9 A1：finish victim-only（禁 App/su 执行；task-68，2026-10-28，工作树未 commit；按用户令「先编码不编译」未跑编译/单测/NDK）

> 依据 task-66 设计 A1。约束：JNI 主路径不变、零 fork、不 commit、`perf.enabled=0`、
> 只动 43499 专属文件、不碰 98260。**用户指令「先不要编译，先编码」⇒ 本批只写
> 代码/测试/静态 gate，未跑 compile/单测/NDK；不得据此宣称门禁绿。**

- **Flow `finishExecuteStep`（A1 唯一执行点=rooted victim）**：
  - rooted victim：保持原流程（handoff 脚本 SHA-256 对拍 → `VictimFinish`（victim
    `execScript`）→ exit/timedOut/output SHA/markers）。
  - **无 rooted victim：删除 App/su 执行路径**（不再调用 `finishExecutor`/`FinishExecution`/
    `su -c`/ProcessBuilder），直接返回具名 Refused
    `cve43499:finish.execute:su-fallback-refused`（token 保持既有冻结字符串）。
  - 删除 `finishExecuteViaAppFallback`/`emptyExecutionEvidence`/`FINISH_SOURCE_APP_FALLBACK`；
    fail-stop `markRetained` 逻辑（task-64）不受影响。
- **`Flow43499RunSpec.finishExecutor`**：标 `@Deprecated(WARNING)`，KDoc 写明
  「finish 唯一执行点=rooted victim；字段保留源码兼容、不再被执行」，本批不删。
- **`FinishHandoffAdapter`**：删除 `execute()`/`executor()` 两个构造 `FinishExecution`
  的 helper；不再向 RunSpec 传 `finishExecutor`；`finishTimeoutMs` 透传保留。
  `buildFromDebug` 的 `finishExecutor` 形参保留但不使用（兼容旧调用；@Suppress
  UNUSED_PARAMETER）。
- **`FinishExecution`**：标 `@Deprecated(WARNING)` + KDoc「无生产调用，仅保留兼容/待删批」。
  生产源码中除该类定义与其 KDoc 外无 `FinishExecution(` 调用；`VictimFinish` 仅以注释
  引用其 DEFAULT 常量同值。
- **新增静态 gate `tools/verify_43499_finish_victim_only.py`**（host Python，不编译）：
  - 扫 `backend/cve43499/**` + `ui/.../backends/**` 生产 Kotlin（先剥注释）：禁
    `ProcessBuilder(`/`Runtime.exec(`/`Runtime.getRuntime().exec(`/`"su -c"`/`FinishExecution(`；
    豁免清单（可配、每条带理由）：`FinishExecution.kt`（Deprecated 类定义）、
    `FinishExecutionEvidence.kt` 的 ksud 证据命令 seam、`victim/ProcessVictim*.kt`
    （旧 Kotlin victim 路径，另批迁移）；
  - 要求断言：Flow 含 `finishExecuteStep`/`victimFinish(`/`su-fallback-refused` 且无
    `finishExecutor?.invoke`；RunSpec `finishExecutor` 附近有 `@Deprecated`；
    FinishExecution 有 `@Deprecated`；Adapter 无 `FinishExecution(`/`finishExecutor =`；
  - `--self-test` 负例自检：5 个违规字符串 + 临时违规文件必须命中，3 个干净样本不得误报，
    fixture 跑完删除。
  - 实跑（静态）：`python3 tools/verify_43499_finish_victim_only.py` ⇒ **EXIT=0**
    （files=55、exempt=3 均带理由、violations=0）；`--self-test` ⇒ **EXIT=0**
    （违规样本全部命中、干净样本无误报）。
- **测试（先写、按令暂不跑）**：`Cve43499WriteChainFlowTest` 更新 5 处旧 fallback 期望并新增/
  改写：
  1. 无 rooted victim ⇒ finish step Refused token，步骤止于 `finish.execute`（不再
     lifecycle.close/acceptance）；deprecated executor 注入后**调用计数=0**；
  2. 单 W1 路径同样 Refused token（步骤 8 步，finish 前调用计数=0）；
  3. rooted victim ⇒ 仍走 `VictimFinish`（既有测试断言 `finishCalls=1`、`finishSource=victim`、
     `executionOutputSha256`、deprecated executor 计数=0）；
  4. 无 rooted victim + 注入“静默成功/失败”executor ⇒ 均 Refused 且无 `finishSource`/
     `executionExitCode` App 证据；
  5. adapter 不再构造 executor（静态 gate 断言）。
- **UNVERIFIED**：按用户令 **未运行 compile/单测/NDK**，上述代码/测试/重构的编译正确性
  与测试结果未验证；G01/G08 真机行为未验证（设备离线）；G05 v2 等待安排。

### 8.10 G05 fast-repair 预建（task-70 + Lead delta，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

- 状态机：新增 `owned/Cve43499PrebuiltRepair.kt`（EMPTY→PLACEMENT_READY→PAGE_READY→ACTIVATED→CONSUMED；dirty→RETAINED；clean discard→DISCARDED；最多 1 条；不入 active chains；`discard()` 返回 clean/retained/termination）。
- Lifecycle/LifecycleApi：`prebuildRepairPlacement`/`landPrebuildRepair`/`activatePrebuiltRepair`（Activated/Failed）/`discardPrebuiltRepair`/`prebuiltRepairAvailable`；`closeAll` 固定顺序 **active(newest→oldest)→prebuilt→victim(rooted detach)**；默认实现 unsupported（fake 兼容）。
- Runner：`prebuildRepair`（placement→generate→prebuilt landing，保存 primitive）/`executePrebuiltRepair`（activation→prepareActor→executeActor→clean close；不再 placement/landing）；clean-failure 后 close 回执判定 retained；`Cve43499RepairTiming`（activation/execute/close/total 子项，`elapsedRealtimeNanos`）。
- Flow：W1 成功出口预建（Lead delta 要求 W1 dirty 不进 W1b/W2；与 v2 Q1=B 字面差异已在此记录，若 v3 要求 B 仅移动调用点）；开关 `fastRepairPrebuildEnabled=false` 默认零行为变化；`allowFullFallback=false`；**B1 统一合同 `requireCleanCloseAfterCred`**：W2 cred 成功后 7 类 close（W2 链/prebuilt repair actor/fallback 链/prebuild discard/activation/clean-failure close/closeAll 聚合）任一 retained/dirty ⇒ W2 `Failed("cve43499:write.w2:fast-repair-close-retained")`+G01 fail-stop，禁 W3/finish/acceptance；`failStop.enabled=0` 仍 Failed；W3/finish 前 `requireCleanPostCredPreflight`；closeAll 晚发现 retained 使 lifecycle.close step Failed（acceptance 不可达）。
- 配置：两份 debug conf `writeChain { fast_repair_prebuild_enabled=0; allow_full_fallback=0 }`；`A301SoDebugConfig` fail-closed 解析 + `writeChainParameters(debug)` 透传；RunSpec `elapsedRealtimeNanos` seam。
- evidence：`w2RepairMode=prebuilt/full-fallback/none/prebuild-clean-failed`、`prebuildUs`、`w2RepairUs`、`w2RepairActivationUs/ExecuteUs/CloseUs`、`prebuiltState`。
- **按用户令「先编码不编译」：未运行 compile/单测/NDK；代码/测试编译正确性与结果 UNVERIFIED；不宣称门禁绿。**

### 8.11 A1 delta + A2 gate 收口（task-72，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline**：HEAD `a9af5482…`、worktree_status `15225492…`、r1a `e0969163…`、
> r1e `3ce0b807…`、98260 plan `bac0bfc0…`（task-69 §4）。98260 R1a/R1e 用户批准并冻结后需
> 重采 A0 并重跑 A1/A2；共享文件变化即停报 Lead。本批**不宣称稳定交付**。

- **A1 delta/I2（finish 超时不得杀 victim）**：`VictimFinishResult.Failed` 增 `retained: Boolean=false`；
  `ProcessVictimSession.finish` 超时路径由 `closeLocked()` 改为 **detach/park（不 close/kill/destroy）**，
  返回 `Failed("victim-finish:timeout")` + `retained=true`（park 未确认带 `:park-unconfirmed` 仍不杀）；
  只有显式 `closeAll`（非 rooted）才允许终止。Flow `finishExecuteViaVictim` 对 retained 失败写
  evidence `victimRetained=true` 并随 Failed step 返回。新增测试（先写不跑）：fake finish 阻塞 ⇒
  Failed(retained)/park 已请求/close 从未调用；rooted finish timeout ⇒ victimRetained evidence。
- **A2 gate/I1（粒度+负例）**：`tools/verify_43499_finish_victim_only.py` 更新：
  - `FinishExecution` 按**整个类声明/实现整类豁免**（@Deprecated 死代码含 execute 内 ProcessBuilder/su），
    PASS 判据改为「无生产调用点」（除类文件外 `FinishExecution(`/`finishExecutor?.invoke` 为 0）；
  - `ProcessVictimSession.finish` 按调用点白名单：允许 finish 存在，但新 `scan_finish_block` 断言其函数块
    内禁 `closeLocked(`/`kill(`/`killVictim(`/`destroy(`/`destroyForcibly(`，并要求 timeout+park 标记；
  - 新增 `close-contract-seven` 断言：`requireCleanCloseAfterCred` 存在且 7 类 stage token 齐全
    （w2-chain/prebuilt-repair/w2-fallback/prebuild-discard/activation/clean-failure/closeAll；task-70 已合入）；
  - 负例/自检新增：finish 路径 ProcessBuilder、timeout kill victim（块级）、生产 FinishExecution 引用；
    干净样本含 detachForFinishTimeoutLocked 不误报；self-test 检测失败即 EXIT≠0。
- **门禁声明**：按用户令「先不要编译，先编码」**未运行 compile/单测/NDK/真机**；仅运行静态检查
  （py_compile/gate/self-test/git diff --check）。代码/测试编译正确性、真机行为 UNVERIFIED。

### 8.12 G05 Q1=B 修正：预建调用点移至 W1b 成功出口（task-73，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / worktree_status `15225492…` / r1a `e0969163…` /
> r1e `3ce0b807…` / 98260 plan `bac0bfc0…`**（task-69 §4）；98260 R1a/R1e 冻结后重采/重跑；不宣称稳定交付。

- **Q1=B 修正**：`prebuildRepair` 调用点从 **W1 成功出口** 移至 **W1b 成功出口、W2 之前**
  （skip 与 repaired 两个成功出口都预建）。W1 出口恢复不预建；单 W1 路径不涉及。
- 保持语义：W1 dirty/retained 由既有失败路径 + G01 fail-stop（与预建无关）；**W1b dirty/retained ⇒
  先走既有失败/fail-stop，预建不执行、不进 W2**；预建 clean failure ⇒ discard 成功、W1b 继续，
  `allowFullFallback=false`（默认）时 W2 具名失败 `fast-repair-prebuilt-required`，true 才 full fallback。
- evidence 不变（`w2RepairMode/prebuildUs/w2RepairUs/子项/prebuiltState`）；失败 token 前缀改为
  `cve43499:write.w1b:fast-repair-prebuild-retained:*`，markRetained stage=`write.w1b.prebuild`。
- **测试（先写未运行）**：`g05 prebuild happens after w1b and before w2`（events 顺序：W1b close-chain →
  prebuild → W2 placement → activation）；`g05 w1b dirty skips prebuild and w2`（W1b RouteMiss ⇒
  prebuildCalls=0、无 W2）；dirty-prebuild 用例改期望 W1b fail-stop；其余 G05 用例保持。
- **门禁声明**：按用户令未跑 compile/单测/NDK/真机；仅静态检查（py_compile/gate self-test/`git diff --check`）。
  代码/测试编译正确性与真机行为 UNVERIFIED。

### 8.13 P2 JNI 边界修复 L1–L4（task-74，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

- **L1 primitiveKind 显式化**：`Native43499PrimitiveSpec` 增加 `val primitiveKind: Int` + companion 常量
  `KIND_MULTICAST_SENDMSG=1/KIND_PSELECT=2/KIND_TCP_ZEROCOPY_RECEIVE=3`；三个 `@Keep` 实现显式携带字段
  （generator 3 处 + Flow 测试 2 处构造已同步）。`jni43499.c` 删除 `glk43499_is_instance`/primitive 类名
  `IsInstanceOf` 判类，改为 `glk43499_get_int(..., "primitiveKind")`；读取失败=翻译失败(NOT_ACCEPTED 无回执)，
  未知/越界=INVALID_ARGUMENT（JNI 侧具名回执）。result DTO 的 `IsInstanceOf` 类型校验保留（非 primitive 判类）。
- **L2 翻译失败不触 core/不写回执**：`preparePlacement/landPage/prepareActor` 原有翻译失败已 NOT_ACCEPTED；
  `openPerfSampler` 读 request 失败、victim `spawnVictim` 字符串/数组解析失败改为 clear+NOT_ACCEPTED，**不写**
  `INVALID_ARGUMENT`；raw_size 参数域失败仍走 INVALID_ARGUMENT（非翻译失败）；**仅 core 调用后写结果**。
- **L3 数组语义**：`glk43499_copy_bytes` 空数组(length=0)=OK/data=NULL/size=0（不再 INVALID）；malloc
  失败=NO_MEMORY（landing/prepareActor 写 SYSTEM_ERROR/ENOMEM，core 不调用）；null/负长度=INVALID_ARGUMENT；
  GetArrayLength/GetByteArrayRegion 异常=TRANSLATION_FAILED（NOT_ACCEPTED 无回执）。`write_byte_array` 增
  capacity 校验，越界拒绝。
- **L4 perf collect**：先取 `result.receipt.bytes_copied`，校验 `0≤bytes_copied≤capacity` 后仅
  `SetByteArrayRegion(bytes_copied)`；JNI 不再按 `data_size` 全量拷贝；capacity<data_size 保持 core 具名失败；
  JNI malloc 失败写 SYSTEM_ERROR/ENOMEM；`capacity=0` 仍调用 core 取得具名失败。
- **测试（先写未运行）**：新增 `app/src/test/.../backend/cve43499/JniBoundaryContractTest.kt`（host/JVM）：
  L1 DTO 反射+字段/常量+JNI 无 primitive 判类；L2 翻译失败分支无 reject/core 调用（host-parseable 代替 fake
  core count）；L3 空数组/OOM/翻译分类；L4 bytes_copied/capacity 截断与无 data_size 全量拷贝。**本轮按令
  未运行 compile/单测**；测试真值待编译后执行。
- **静态检查**：`git diff --check` **EXIT=0**；源码期望 JNI 符号 **25**（task-63 G06 已增 1；本批不新增）与
  `jni43499.c` `Java_` 定义 **25/25** 一致（无 missing/extra）。`verify_jni_exports.py` 因共享工作树
  `build/app/intermediates/cxx` 产物已被清理而 **EXIT=2（产物缺失，未运行）**；未用旧产物冒充当前树。
- **UNVERIFIED**：**未跑 compile/单测/NDK**（用户令「先编码不编译」）⇒ 代码/测试编译正确性、L1–L4 真机
  JNI 行为、三态/回执语义均未验证；`perf.enabled=0`；不 commit/push。

### 8.14 P3 Native/JNI 机械卫生 B11–B17 + JNI L5–L8（task-77，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

- **B11 pthread_create 错误码**：`actor.c` 三处、`carrier_tcp.c` 一处改为保存 `pthread_create` **返回值**到
  `create_status`，不再读 errno（bionic 不保证同步写 errno）；失败路径错误码可证伪。
- **B12 multicast 窗口内墙钟**：`carrier_multicast_trigger` 删除 loop 内 `glk43499_internal_deadline`/
  `expired`；总时长由 `operation_budget` + 自旋次数界定，不再依赖 vDSO；注释更新。
- **B13 inflight 有界**：inflight 等待改用 `disarm_spin_limit` 有界 spin；超限置
  `outcome.failed=EBUSY`、`last_step=GLK43499_CARRIER_STEP_INFLIGHT`、`last_errno=EBUSY`，并禁止被
  legacy 成功回退覆盖。
- **B15 域收窄**：placement `scan_round_limit > UINT32_MAX` 拒绝；三 carrier `operation_budget >
  INT32_MAX` 拒绝（attempt 最终收窄 int32，溢出安全）。
- **B16 pselect retained 记账**：backup 失败分支只有 `clean==0`（资源实际未清）才 `retained=1`；
  `clean!=0` 已全清则显式 `retained=0`，不留 seam 残留。
- **B17 page abort fd 身份**：reclaim socketpair 建立后立刻写回 `page->reclaim_read/write`；abort 签名改为
  `(page, shaping[2], reclaim[2], ...)`，close_pair 后把未关成功的 reclaim fd 同步回 Page ⇒ retained Page
  仍可由 closePage/Page owner 关闭；shaping 仍为调用期临时资源。
- **JNI L5–L8**：L5 四个 close/cancel wrapper 增加 `handle/actor == 0` 前置拒绝（不先触碰 result 类）；
  L6 close/cancel 统一用 `jboolean close_operation` + 唯一 `write_close_result` 归一 0/1 bool（stop/
  workers/ownedChain/retained）；L7 C 宏加 `_Static_assert` 四哨兵，Kotlin `RESULT_*` 对拍；L8 priority
  改 `glk43499_get_int32` 显式有符号读取，进入 core 前校验 `PRIO_MIN..PRIO_MAX-1`，越界写
  INVALID_ARGUMENT。
- **测试（先写未运行）**：新增 `app/src/test/kotlin/com/ghostlock/app/backend/cve43499/NativeHygieneContractTest.kt`
  （host-parseable：B11–B17、L5–L8 逐项源码契约 + L7 Kotlin 常量对拍）。**按用户令未运行 compile/
  单测/NDK**；编译/运行时真值待 build 批。
- **静态检查**：`git diff --check` **EXIT=0**；JNI 源码期望/定义 **25/25**（无新增/删除导出，任务-63 的
  `raiseProcessLimits` 仍在）。`verify_jni_exports.py` 因 build/cxx 产物缺失返回 **EXIT=2（未运行）**；
  未用旧产物冒充。
- **UNVERIFIED**：**未跑 compile/单测/NDK**（用户令「先编码不编译」）⇒ 编译正确性、B11–B17 真机行为、
  JNI L5–L8 三态/回执/priority 域真值均未验证；`perf.enabled=0`；不 commit/push。


### 8.13 G04 W3 硬门实现（task-78，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / worktree_status `15225492…` / r1a `e0969163…` /
> r1e `3ce0b807…` / 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

- **Kotlin 硬门**：新增 `backend/cve43499/w3/Cve43499W3HardGate.kt`（P1–P8 判定 + 双源
  `parseStatus`/`PreInput`/`PreVerdict`/`EvaluatedFinal` + `LeafDecision` 0/8/15/其他；失败 reason
  稳定 `gate-*`；`coldRebootRequired` 判定由 Flow 写 evidence）。P3/P4 为**同源双路径**（父
  `/proc/<pid>/status` + victim self `PROBE_STATUS`），mismatch/missing 一律 FAIL；`Seccomp:0`
  充分性标 UNVERIFIED（无 TIF_SECCOMP 直读，可选自探针另批）。
- **task-45 append（不改既有语义）**：`VictimCapability` append **bits 7/8/9**
  （PROBE_STATUS=1<<7、PROBE_COMM_SET=1<<8、PROBE_COMM=1<<9；不重排 0–6；当前树无
  `EXEC_SCRIPT_NO_KILL` 占用 bit7，已登记去向）；`VictimFinish.kt` 的 `VictimProtocol` append
  opcode/行协议（status/comm-set/comm、GLK_STATUS/GLK_COMM/GLK_COMM_SET、4 KiB 上限、len0..15
  hex 小写无分隔）；`ProcessVictim.DEFAULT_COMMAND` append 三个 shell case（固定 `od -An -tx1`）；
  `ProcessVictimSession`/`VictimSession` append `status/commSet/comm`（默认不支持）；
  `Cve43499LifecycleApi/Lifecycle` append `victimStatus/victimCommSet/victimComm`。
- **Flow**：W3 step 在 B1 post-cred preflight 后调用 `w3GatePreflight`（P1/P2/P3/P4/P7/P8；
  fail ⇒ `cve43499:write.w3:gate-*` + G01 fail-stop + `w3ColdRebootRequired=true` evidence，
  terminate 前由既有 evidence sink fsync）；finish step 前置 gate（multi-chain 且 W2 cred 已写但
  `w3GatePassed=false` ⇒ `cve43499:finish.execute:gate-finish-prereq`）；evidence
  `w3Gate/w3FailReason/w3ColdRebootRequired`。
- **DECLARED DEFERRED（冲突）**：helper Native append（`victim_protocol.h/c` opcode 6/7/8 +
  capability bits、`victim_helper.c` 三命令）**本批未做**：in-progress **task-56（owner r3e-impl）**
  写范围覆盖同样文件（另有 task-77 写 jni/卫生）；待其收尾后补，报告已说明。J/helper 路径
  self-probe 因此不可用 ⇒ gate 按 FAIL（不 fallback），ProcessVictim 路径可用。
- **测试（先写未运行）**：`Cve43499W3HardGateTest`（capability bits；status/comm 行协议
  4 KiB/hex/len0/8/15/非法；P2 uid 双源一致/mismatch/missing/not-root；P3 CapEff mismatch；
  P4 seccomp before=2/after=0/双源/mismatch/missing；P5 leaf 0/8/15/歧义；P6 close-retained；
  P7 finish 前置；P8 env；final PASS/FAIL evidence）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；仅静态检查。代码/测试编译正确性、真机门禁、
  helper Native append、J 路径 self-probe、E7 真机矩阵均 UNVERIFIED；不宣称门禁绿。

### 8.15 G19 pselect fd install 窗口收窄到 execute（task-79，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

- **prepare**：`carrier_pselect_prepare` 仍完成 frame 解码、fd_set 装载、目标收集、`F_DUPFD_CLOEXEC`
  只读备份与 pipe/timerfd 准备；**不再 dup2 安装**（原审计 Native-B5 的跨 JNI 返回窗口被消除）。
  `selected_fds_installed=0`（prepare 不安装）。
- **trigger/execute**：进入 attempt/consumer 启动前，在同一 native 调用内 `glk43499_pselect_install_targets`
  完成 dup2；成功后 `selected_fds_installed=1`；所有 attempt（成功/超时/失败）结束后立即
  `glk43499_pselect_restore_backups` 并置 `selected_fds_installed=0`，restore 后才返回。
- **失败记账**：install 失败 → `failure_errno`（首个 fcntl/dup2 errno）+ `outcome.last_step=
  GLK43499_CARRIER_STEP_IO`、`last_errno` 具名返回；已安装项在 install 内 rollback，rollback 失败
  `retained=1`。restore 失败 → `retained=1`、`result=0`、outcome errno=restore errno（不静默）。
- **dispose 兜底**：`selected_fds_installed!=0` 才 restore；未安装路径只 `close_backups`（不 dup2 回写），
  restore 失败/retained 路径提前返回（不重复 restore）；释放失败保留身份。
- **不改**：ABI/请求/回执字段、G02 超时、target 选择；只改 `carrier_pselect.c`。
- **测试（先写未运行）**：新增 `app/src/test/kotlin/com/ghostlock/app/backend/cve43499/PselectWindowContractTest.kt`：
  prepare 无 install/dup2、trigger 顺序 install→attempt→restore、install 失败 rollback/errno、dispose
  不重复 restore（host-parseable 源码契约）。**按用户令未运行 compile/单测**。
- **静态检查**：`git diff --check` **EXIT=0**；源码契约自检 15 项全 PASS（见测试/本批报告）；未跑
  compile/单测/NDK；真机（A301SO/mondrian pselect 门禁）UNVERIFIED；`perf.enabled=0`；不 commit。


### 8.14 G04-Native append：helper PROBE_STATUS/COMM_SET/COMM + capability 7/8/9（task-80，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / status `15225492…` / r1a `e0969163…` / r1e `3ce0b807…` /
> 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

- **冲突纠正**：task-56 已 completed（早间零 fork/E1 harness）；本批 Native writer 仅 task-79
  （`carrier_pselect.c`）无交集；`victim_protocol.c` 未改动（hash 不变），`victim_protocol.h` 与
  `victim_helper.c` 仅本 writer 追加（前后 hash 记录于日志）。
- **`victim_protocol.h` append**：opcode 6/7/8（PROBE_STATUS/COMM_SET/COMM，不重排 1–5）；
  新增 `enum glk_victim_capability` 0–9（与 Kotlin `VictimCapability` 镜像；7/8/9 append；
  bit7 去向=当前树无 `EXEC_SCRIPT_NO_KILL` 占用）；常量 `PROBE_STATUS_MAX_BYTES=4096`、
  `PROBE_STATUS_MAX_REPLY_BYTES`（=frame body − overhead − 1B status = 4087，单帧上限；
  raw 在 4088..4096 时 IO err 不截断）、`PROBE_COMM_MAX_BYTES=15`；frame v1/长度/CRC 不变。
- **`victim_helper.c` append**：`glk_helper_probe_status`（`/proc/self/status` raw，≤4 KiB，
  超出单帧回复能力/超限均 IO err 不截断）；`glk_helper_probe_comm_set`（`/proc/self/comm`
  写入 15B `[A-Za-z0-9._-]` marker，非法/超限 BAD_ARGS）；`glk_helper_probe_comm`（读 self comm，
  去尾 `\n`/`\0`，回复 `[len][c0][bytes]`，len0/8/15 可判定，超限 IO err）；dispatch 追加三 case；
  **机制-only**：无 fork/system/execve/posix_spawn、不解析/不写 flags/mode；既有 1–5 命令不变。
- **Kotlin J/helper 路径使能**：`core/victim/VictimProtocol` append opcode 6/7/8、`encodeProbe*`、
  `decodeProbeStatusReply`（4 KiB）、`decodeProbeCommReply`/`CommProbe`（len0..15）；`VictimSession`
  append `probeStatus/probeCommSet/probeComm`（capability 检查 + 既有 roundTrip/帧校验）；
  `Cve43499JniVictim` 已用 `VictimCapability.entries` ⇒ bits 7/8/9 自动启用。
- **静态契约 gate（新增，非编译）**：`tools/verify_43499_probe_protocol.py`：C/Kotlin opcode
  6/7/8、capability 0–9 bits 对拍、helper 三 handler 存在且无 fork/system/execve/posix_spawn、
  4 KiB/frame 常量存在、既有 dispatch 保留；`--self-test` 负例（opcode drift/capability drift/
  Kotlin drift/helper fork）检测。实跑 gate **EXIT=0**、self-test **EXIT=0**。
- **测试（先写未运行）**：`VictimProbeAppendTest`（opcode 线帧解码、marker 校验、capability 7/8/9、
  status 4 KiB、comm len0/8/15）；既有 finish/park/bye/detach 语义未改（append-only，回归断言在
  task-45/task-72 既有测试保持）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；编译正确性、helper 真机命令、4 KiB 边界、J 路径
  round-trip、provisional-baseline 重采均 UNVERIFIED；不宣称门禁绿。

### 8.15 Kotlin B4/B6（task-82，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / status `15225492…` / r1a `e0969163…` / r1e `3ce0b807…` /
> 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

- **B4 fail-closed readiness/request**：`BackendCve202643499.readiness()` 增稳定 detail
  `unsupported-single-w1:default-debug`（默认 debug 单 W1/`perf.enabled=0`/无 rooted victim 可能 ⇒
  明确 unsupported，不得标 supported；单 W1 路径行为不变）；`FinishHandoffAdapter` 增
  `unsupportedDebugRunReason(debug)`：`perf.enabled!=1` ⇒ `unsupported-single-w1:perf-disabled`，
  `writeChain==null` ⇒ `unsupported-single-w1:write-chain-missing`。
- **B6 finish settle/poll**：`Flow43499RunSpec` append `finishPreDispatchSettleMs`（默认 2000ms）、
  `finishSleeper`/`finishEnforceReader`/`finishModuleStateReader`（可注入 clock/sleeper）；
  Flow `finishPreDispatchSettle` 在 rooted victim 派发 finish 前：settle（0..10000 越界
  `settle-invalid`、中断 `settle-interrupted`）+ module/enforce 有界轮询（≤20×50ms）；evidence
  `finishPreDispatchSettleMs/finishEnforceBefore/finishEnforceAfter/finishModuleState/finishEnforcePollAttempts`。
- **B6 acceptance 语义修正**：`FinishAcceptanceInput` append `enforceBefore/enforceAfter`；
  acceptance 由「必须 Enforcing==1」改为「恢复**运行前记录值**」：before 缺失 ⇒ `finish-selinux-missing`，
  after≠before ⇒ `finish-selinux-not-restored:<before>:<after>`（避免脚本默认不恢复导致 Accepted 永远不可达；
  before=0/after=0 视为恢复但不宣称 Enforcing）。
- **配置**：`A301SoDebugConfig` append `finish { pre_dispatch_settle_ms }`（缺省 2000、0..10000 fail-closed）；
  两份 debug conf 追加 `finish` 段；adapter 透传 `finishPreDispatchSettleMs`。
- **测试（先写未运行）**：`FinishEnforceRestoreTest`（before=1/after=1、0/0 不拒绝；mismatch/missing 具名拒绝）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；编译正确性、settle 真机时序、module/enforce 轮询、
  acceptance 真机、B4 单 W1 回归、provisional-baseline 重采均 UNVERIFIED；不宣称门禁绿。

### 8.16 B7 KernelSnitch scan bounds fail-closed 一致性（task-81，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

- **单一权威**：detector 扫描范围由共享 header-only `glkgeneral/kernelsnitch/kernelsnitch.h` 的编译期常量决定
  （ARM39：`IDENTITY_START=0xffffff8000000000`、`IDENTITY_END=start+64GiB`、`COARSE_SZ=1GiB`、`PAGE_SIZE=4096`）；
  Kotlin 请求不驱动扫描，只做事后校验。
- **新头**：43499 专属 `app/src/main/cpp/gl43499/core/kernelsnitch_bounds_internal.h` 从 detector 头提取
  `IDENTITY_START/END`、`SCAN_LOWER/UPPER`（=identity 范围）、`SCAN_STEP=PAGE_SIZE`、`COARSE_STEP`
  与具名 token `scan-bounds-mismatch`；**不改共享 glkgeneral 头**，避免与 98260 冲突。
- **placement validate**：在既有 scan 窗口/step 基本校验之后、identity 包含校验之前，对 request 的
  `identity_start/end`、`scan_lower/upper_bound`、`scan_step` 与头常量做**严格相等**检查；不一致返回
  `GLK43499_PLACEMENT_VALIDATE_SCAN_BOUNDS_MISMATCH`，`preparePlacement` 落
  `GLK43499_CALL_INVALID_ARGUMENT` + `system_error=EINVAL`（具名 token 见头；无 ABI/回执字段变更）。
- **A301SO/mondrian**：`A301SoDebugConfig.executionParameters()` 的 scan/identity 值为
  `0xffffff8000000000..0xffffff9000000000`、step `0x1000`，与 detector 常量一致（两 conf 走同一执行参数派生）。
- **测试（先写未运行）**：新增 `app/src/test/kotlin/com/ghostlock/app/backend/cve43499/KernelsnitchBoundsContractTest.kt`：
  detector/bounds 常量对拍、placement 五字段严格比较/具名 token、A301SO/mondrian executionParameters
  一致、窄/宽/step 不符的 B7 判据镜像拒绝。**按用户令未运行 compile/单测**。
- **静态检查**：`git diff --check` **EXIT=0**；B7 源码契约自检 16 项全 PASS；未跑 compile/单测/NDK；
  ABI/请求/回执结构未改；真机 KernelSnitch/placement 门禁 UNVERIFIED；`perf.enabled=0`；不 commit。


### 8.16 Kotlin 卫生清理（task-84，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / status `15225492…` / r1a `e0969163…` / r1e `3ce0b807…` /
> 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

逐项（file:line 证据）：
1) **`w1SettleUs` 死字段**：`Cve43499WriteChain.kt:428/458/499` 原仅校验、Flow 未使用 ⇒ 本批**接线**：
   `Flow43499Impl` W1 事务 Executed 后 `settleUs(chain.policy.w1SettleUs)`（首个 W1 分支），
   负值继续由 `validate()` `write-chain:settle` fail-closed。
2) **session/debugSession 错位映射**：`FinishHandoffAdapter.buildFromDebug:555-569` 的
   `debugSession(debug)` 原调用无参 `A301SoDebugConfig.executionParameters()`（忽略 debug/G08 CPU
   覆盖）⇒ 改为 `executionParameters(debug)`；`Flow43499Request.session` 映射保持
   `session ?: debugSession(debug)`（测试：单 W1 路径不变）。
3) **`expectedUid` 硬编码**：`FinishAcceptance.kt:53` 默认 `ROOT_UID` 仍为安全默认；新增
   `Flow43499RunSpec.expectedUid`（debug-only，默认 0），Flow acceptance 传入；`writeChain` 校验
   负值 ⇒ `cve43499:write.chain:expected-uid-invalid`；debug conf `finish.expected_uid` 可选 fail-closed。
4) **`resourcesReleased` 证据过强**：`Flow43499Impl` cleanup 原直接 `= receipt.allReleased`，
   clean 覆盖先前 false ⇒ 改**只降不升**（`priorReleased` 保持 false / null 语义），lifecycleClose
   step 仍以本次 closeAll 回执为准；未尝试保持 null ⇒ acceptance fail-closed。
5) **`victimSnapshot` 副作用**：`ProcessVictimSession.identity()` 原调用 `uid()`（发 probe 协议命令，
   有副作用）⇒ 新增 `statusUidReader` 只读 `/proc/<pid>/status` Uid；identity 只读/幂等（测试：
   fake uid 调用计数=0，两次 identity 相等）。
6) **`startVictim` 非幂等**：`Cve43499Lifecycle.startVictim` 增 `if (victim != null) return null`，
   重复 start no-op、不双重 spawn（fake 工厂计数单测）。
7) **`ProcessVictim.close/uid` 无互斥**：`uid` 原已 `@Synchronized`；本批给 `close` 加 `@Synchronized`
   （同一对象锁，最小 Kotlin 互斥，不引新线程模型）；反射单测断言。
8) **安全死代码**：外部调用方 grep：`FinishExecution` 无生产调用（仅 deprecated 类/测试），
   `finishExecutor` @Deprecated 保留；按「禁用而非删除用最可逆形式」**不删**，登记待删批；
   `Flow43499RunSpec.finishExecutor`/`FinishExecution` 维持 task-68 结论。
- 新增静态核验 `tools/verify_43499_kotlin_hygiene.py`（8 项 + self-test 负例）：实跑 **EXIT=0**、
  self-test **EXIT=0**；测试先写未运行：`KotlinHygieneTest`（startVictim 幂等/identity 只读/
  close+uid synchronized/expectedUid 可配）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；编译正确性、settle 时序、多线程互斥真机、
  expectedUid debug 配置、provisional-baseline 重采均 UNVERIFIED；不宣称门禁绿。

### 8.17 A1/A2a provisional：公共 victim opt-in（task-86，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / status `15225492…` / r1a `e0969163…` / r1e `3ce0b807…` /
> 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

- **`injection.victim = common`（opt-in）**：`DebugVictimTransport` append `COMMON`；parse 接受
  `"common"`，未知 token fail-closed；默认两份 debug conf 仍 `process`（行为不变）；
  `Flow43499RunSpec.victimTransport` 透传（process|jni-spawn|common，默认 process）；
  process/jni-spawn 路径**不删/不禁用**。
- **`Cve43499VictimLaunchSpecFactory`（NEW，backend）**：产出公共 `VictimSessionConfig`：
  helperPath=`nativeLibraryDir/libghostlock_victim_helper.so`；argv/env **空**；capabilities 用
  **符号**（PROBE_UID/SPAWN_HELPER/PERF_SELF_SAMPLE/EXEC_SCRIPT/PARK/BYE/DETACH_ROOTED/
  PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM）；rawSize 由 perf 派生（common 无 perf 时用
  `VictimProtocol.RAW_HEADER_BYTES`）；timeouts 用既有 `Cve43499JniVictimLimits`；不写死数字、
  core 不引入 backend 常量。
- **`Cve43499CommonVictimRuntime`（NEW）**：用公共 core `VictimSession` + `VictimNativeJni` 创建
  会话（经既有 `Cve43499VictimAdapter` config seam；jni-spawn 内联 config 保持 null 默认行为不变）；
  `common` 选中时作为 `victimFactory`；perf sampler 供 target source。
- **Q5 事实（边界）**：**43499 不使用** 98260 的 script-detach payload 载体（`VictimScriptLauncher`
  为 98260 script-carrier，start→execScript→onScriptDone→close）；43499 helper 是**长驻 victim**，
  走公共 `VictimSession`/config；静态 gate 断言 43499 backend/UI 源码无该类引用。
- **hoist plan 注记**：D0 部分解决（script launcher 由 98260 冻结；A1/A2a 已编码；Q2/Q4 剩余
  owner）——hoist plan 文档由 task-83（routes-impl2）在写，按 Lead「不要动」**本批不编辑**，
  需回填请 Lead 转达/另批。
- **测试（先写未运行）**：`Cve43499VictimLaunchSpecFactoryTest`（config 符号 capability/空 argv-env/
  limits/rawSize；common runtime 可创建 adapter 会话）。
- **静态检查**：新增 `tools/verify_43499_victim_common_gate.py`（enum/parse common、factory 符号
  capabilities、runtime Native/Session、adapter common wiring、RunSpec 透传、legacy 路径保留、
  43499 无 script-launcher 引用、可失败 self-test）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；编译正确性、common helper 真机（spawn/uid/
  seccomp/finish）、perf 派生 rawSize、jni-spawn/process 回归、hoist plan 回填、provisional-baseline
  重采均 UNVERIFIED；不宣称门禁绿。

### 8.18 A1 killPolicy 接入（task-91，2026-10-28，工作树未 commit；按用户令未跑编译/单测/NDK）

> **provisional-baseline = HEAD `a9af5482…` / status `15225492…` / r1a `e0969163…` / r1e `3ce0b807…` /
> 98260 plan `bac0bfc0…`**（task-69 §4）；冻结后重采/重跑；不宣称稳定交付。

- **`Cve43499VictimKillPolicy`（NEW，backend）**：`killOnTimeout=false`（finish 超时只 kill 脚本
  child、不 kill/destroy victim）、`killOnClose=false`（rooted close ⇒ detach/park，不 kill/reap）、
  `detachOnTimeout=true`；`shouldCloseKill(rooted)`：rooted ⇒ killOnClose，非 rooted ⇒ 可回收；
  `shouldKeepVictimOnTimeout(rooted)`。**D0 映射**：公共 `VictimSessionConfig` 暂无 killPolicy 字段 ⇒
  由 backend runtime/adapter 携带；D0 冻结后字段上浮到公共 spec，本类降为 43499 默认值来源。
- **接线**：`Cve43499VictimLaunchSpecFactory.create(policy=...)` + `policyOf()` 暴露；
  `Cve43499CommonVictimRuntime(killPolicy=...)` 透传 adapter + `killPolicy()` 证据口；
  `Cve43499VictimAdapter` 新增 `killPolicy` 字段：`close()` 在 rooted 且 `shouldCloseKill=false` 时
  只 `detachRooted()`/park、不 close/kill（非 rooted 正常回收）；`finish()` 超时按
  `killOnTimeout=false` 保留 victim（helper 既有语义只 kill 脚本 child；A1 delta/§8.11 一致）。
- **不删/不禁用旧路径**：process/jni-spawn、既有 `detachRooted/close/finish` 语义保留；默认
  `killPolicy=DEFAULT`，common opt-in 之外行为不变。
- **测试（先写未运行）**：`Cve43499VictimKillPolicyTest`（默认语义/rooted 分流/显式覆盖/
  factory+runtime 携带 policy）。
- **静态检查**：`tools/verify_43499_victim_common_gate.py` 扩展 killPolicy 断言
  （fields/close wiring/runtime passthrough/factory exposure；self-test 增加 policy 变异负例）。
- **声明**：按用户令未跑 compile/单测/NDK/真机；编译正确性、rooted 真机 detach/park、
  finish 超时真机、D0 公共 spec 映射、provisional-baseline 重采均 UNVERIFIED；不宣称门禁绿。


### 8.19 E1 W1 landing clean-failure 诊断 + harness 有界重试（task-93/94，2026-10-28；按用户令本阶段可编译/未跑真机/未 commit）

- **root cause（E1 第二轮回填）**：`landing:clean-failure:call=invalid-argument:errno=0` = core `page.c`
  页接受判据（`page_accept_required=1` 且 `(candidate_page_base+offset)>>16 &1 ==0`）拒绝本次页；
  属 legacy 语义下**预期可重试 clean failure**（token 恢复、未写、无 panic；E1 后 Enforcing、已 reboot）。
  先前 E1 harness 单次 `runner.execute(W1)` 无重试 ⇒ ~50% 冷机首轮 FAIL；非参数/写失败。
- **诊断增强（task-93）**：Lifecycle/prebuilt landing `CleanFailure` reason 追加
  `call=<Native43499CallStatus>:errno=<systemError>`（Kotlin-only，token/重试/close 语义不变）。
- **harness 修复（task-94）**：`W1OnlyProbeTest.runW1Only` 对 `landing:clean-failure`
  RouteMiss 有界重试（上限 debug `w1Attempts`=15；每 attempt closeChain、记录 attempt reason/primitive/
  page_base；非 clean-failure 立即 FAIL；成功仍 fail-stop `REQUIRED_COLD_REBOOT`）。
- **payload 对拍（task-93）**：mondrian `generator.payload`（3712/4480/4736/22528）与 legacy2
  `target.h` 5.10 常量逐项一致；full.conf legacy1 k5 geometry（4608/12800/128/12/8）当前 loader
  不消费 ⇒ **无证据化 conf 修改**；`kernel_phys_offset=0x80000000`/pageAccept 判据真机有效性仍 UNVERIFIED。
- `assembleDebug` + `assembleDebugAndroidTest` EXIT=0；未安装/未跑真机/未跑单测；不 commit、perf=0。


### 8.20 E1 重试轮加固 + placement errno 透出（task-95，2026-10-28；可编译/未跑真机/未 commit）

- E1 重试轮：attempt1/2 `landing:clean-failure:call=invalid-argument:errno=0`（page-accept 判据，
  预期可重试）；attempt3 未到 landing：`preparePlacement:system-error/not-applied`（总 124s，
  无残留进程/内存正常）。
- 修复：① Lifecycle/prebuilt `PlacementStep.Failed` reason 追加 `:call=<Native43499CallStatus>:errno=<systemError>`
  （区分 ENOMEM/EAGAIN，未触 Native 时无 detail）；② E1 harness 每 attempt 全新 lifecycle+runner、
  attempt 间 250ms settle、上限 8、仅 landing clean-failure 重试、逐 attempt 记录
  phase/reason/page_base/close 回执与 `/proc/self/status` Threads/VmSize/fd 数。
- 构建：`:app:clean :app:assembleDebug` EXIT=0；`:app:assembleDebugAndroidTest` EXIT=0（首轮
  `mergeProjectDexDebug` stale `BuildInfo` 重复类经 `:app:clean` 消除）；`git diff --check` EXIT=0。
- UNVERIFIED：attempt3 placement 退化是否被"新 lifecycle/attempt"消除、真机 errno 分布、E1 下轮。


### 8.21 G08 NO_PIN 哨兵误拒修复（task-96，2026-10-28；可编译/未跑真机/未 commit）

- E1 加固轮：attempt2 landing 成功但 `prepare-actor` 失败 `actor-cpu`（105s）。root cause =
  `PageOwner.precheckPreparation` 把 G08 `NO_PIN_CPU=-1`（owner/waiter 不 pin 哨兵）当非法负值拒绝；
  consumer=2 合法。A301SO E4 未触 actor prepare 故未暴露。
- 修复：抽出 `precheckActorCpuPinning(owner,waiter,consumer)`：owner/waiter 允许 -1（透传 Native
  0xFFFFFFFF）、consumer 必须 ≥0、其它负值 fail-closed，reason 细分 `actor-cpu:owner|waiter|consumer`；
  precheckPreparation 调用；不改 JNI；其它 local precheck（PlacementOwner.placementCpu）语义不同，
  无 NO_PIN 通道，保持原样。
- 测试先写：`ActorCpuPinningTest`（(-1,-1,2)/(0,1,2) 通过；consumer=-1/owner=-2/waiter=-2 拒绝）。
- `assembleDebug`/`assembleDebugAndroidTest` EXIT=0；`git diff --check` EXIT=0；真机 UNVERIFIED。


### 8.22 actor-execute clean-failure 诊断透出 + harness 重试扩展（task-97，2026-10-28；可编译/未跑真机/未 commit）

- E1 第五轮：landing/prepare-actor PASS；attempt1 `actor-execute:clean-failure`（28s，未写/Enforcing/已 reboot）。
- 修复：Lifecycle executeActor CleanFailure/Dirty reason 追加 ActorExecute 回执实字段（call/errno/
  consumer/carrier/opened/ops/step/errno/retained），prepareActor CleanFailure 追加 prepare 回执字段；
  新增生产纯函数 `isRetryableCleanFailure`（landing/page.land/actor-execute/prepare-actor clean failure）供
  E1 harness 有界重试（上限 8、250ms、fresh lifecycle/runner、phase 记录）；dirty/非 clean 立即 FAIL。
- 测试先写：`Cve43499CleanFailureReasonTest`（clean 四种 true；dirty/incomplete/其他 false；detail 后缀不影响）。
- `assembleDebug`/`assembleDebugAndroidTest` EXIT=0；`git diff --check` EXIT=0；真机 UNVERIFIED。


### 8.23 EACCES placement 诊断 append（task-100，2026-10-28；可编译/未跑真机/未 commit）

- E1 第三轮：attempt3 `preparePlacement:system-error/not-applied:call=system-error:errno=13`（EACCES，174s）。
- Native 诊断 append：`glk43499_placement_result` 尾部 `diag_step/diag_syscall/diag_errno`（数值枚举，
  无控制逻辑；sizeof 96→112，static assert + `43499-r1-freeze-abi.md` 同步）；placement 失败点标注
  ALLOC/PIN_CPU/CLONE_CHILD/OPEN_MEMFD/ACQUIRE；JNI 写回 + Kotlin `Native43499PlacementResult` +
  `PlacementStep.Failed` reason 透出 `call/errno/diag_step/diag_syscall/diag_errno`。
- EACCES 只读审计：最可疑 `open("/proc/<pid>/mem")`（SELinux proc_mem/dumpable 竞态）与 pin CPU，
  其次 clone 资源类；无 pagemap 访问。修复选项 A 重试/B child-ready/C child 自读/D 参数/E 设备 audit
  取证（各自代价/风险/是否改 wire 见门禁文档），**待下一轮 E1 diag 证据定案，不先改行为**。
- `:app:clean :app:assembleDebug` EXIT=0；`git diff --check` EXIT=0；真机 UNVERIFIED；ABI probe/freeze
  需随 append 重采（96→112，待 Lead 确认 frozen ABI 变更）。
- **日志接入实现（task-101，2026-10-28）**：`Flow43499Request.progress` append-only + `Cve43499RunLog`（双 sink 分别 runCatching/ASCII≤512B/脱敏）+ W1/W1b/W2/W3/victim/finish/close/fail-stop 阶段行 + JNI 入口 2 行 gl_log；静态 gate `tools/verify_43499_log_sites.py`/`verify_no_log_in_pi.py` --self-test 变异 FAIL→恢复 PASS；`assembleDebug`/`assembleDebugAndroidTest` EXIT=0；单测新增但 test source 受在途文件阻塞未跑；真机 UNVERIFIED；不 commit。


### 8.24 actor timeout 阶段诊断透出（task-102 收尾，2026-10-28）

- `jni43499.c` actor execute 回执写回 `diagStage/diagRequeueRet/diagRequeueErrno`；
  Kotlin `Native43499ActorExecuteResult` append 3 字段；Lifecycle `executeActor`/`prepareActor`
  clean-failure/dirty reason 追加 `:diag_stage=…:requeue_ret=…:requeue_errno=…`（保留 clean-failure 子串）。
- ABI 临时诊断扩展（Lead 批准）：placement_result 112、actor_execute_result 136；probe 重采、freeze 同步。
- E1 harness residual：`threads/fds/children` 在 clean-failure 重试前记录（task-102 owner）。
- `assembleDebug`/`assembleDebugAndroidTest` EXIT=0、`git diff --check` EXIT=0；未安装/未跑真机；真机 UNVERIFIED。
- **日志合规修正（task-103，2026-10-28）**：`Cve43499RunLog` UI 仅 Info+（Debug/Verbose 只落 logcat/FileSink）；Info 去参数细节、细节降 Debug；W1/W1b/W2/W3 循环 Debug 改首次进入 1 条；Warn/Error 改 `log.warn/error(msg, reason)`；测试同步；双构建/gate 见 approval。


### 8.25 诊断字段生效修复（task-104，2026-10-28）

- 根因：prepare_placement 未把 impl diag 复制到 result；actor_execute 未写 diag（struct/JNI 已有）⇒ 真机全 0。
- 修复：placement 统一 `publish_diag`（所有 return 前、free 前 copy；SYSTEM_ERROR/TIMEOUT 兜底 ACQUIRE/MISC；
  VALIDATE=9/既有 STAGING=5）；actor execute 阶段写 diag_stage + requeue ret/errno（PI 窗口内仅回执字段）。
- 构建 clean assembleDebug/assembleDebugAndroidTest EXIT=0、diff-check EXIT=0；未安装/未跑真机；真机 UNVERIFIED。
- **task-103 单测解锁/结果（2026-10-28）**：Lead 授权最小修 `BackendParameterSchemaTest:14` 函数名反引号；routes-impl3 修 LifecycleRootedTest named factory/W3HardGateTest LeafDecision；`:app:compileDebugUnitTestKotlin` EXIT=0；`--tests "*Cve43499RunLogTest*"` **6 tests / 0 failed / EXIT=0**；全量 `:app:testDebugUnitTest` **389 tests / 39 failed / 0 errors / 13 XML 文件失败**（集中在 Cve43499WriteChainFlowTest 20、ScriptRender/PayloadRequests/其他在途项；非日志单测）；双构建/gate 绿；真机 UNVERIFIED；不 commit。


### 8.26 诊断 v3：fail_stage 冻结 + placement 全 goto fail 标注（task-105，2026-10-28）

- E1 diag-v2：actor timeout 但 cleanup 的 DISARM 覆写真实阶段；placement OPEN_MEMFD/errno13 可能 stale。
- actor append `diag_fail_stage/diag_publish_count/diag_requeue_calls`（sizeof 136→144，probe/freeze 同步）；
  首次失败阶段冻结（timeout→WAIT_WINDOW、drain→DRAIN），publish/requeue 计数+ret/errno 记录；PI 窗口内仅回执字段。
- placement acquire 的所有 `goto fail` 补 ACQUIRE/MISC diag（pin measurement_cpu/leak_child/leak_memfd 等）。
- JNI/Kotlin reason 透出 fail_stage/publish_count/requeue_calls；actor 顺序与 legacy2 race.cpp:185/380 同形审计
  记录；构建 assembleDebug/assembleDebugAndroidTest EXIT=0、diff-check EXIT=0；真机 UNVERIFIED。


### 8.27 诊断 v4：marker/版本核验 + waiter 就绪快照 + placement 独立枚举（task-107，2026-10-28）

- E1 v3 观测矛盾（stage=7/fail_stage=0 且 timeout 本应立即 return）⇒ 优先怀疑设备 so/结构布局与源码不一致；
  `diag_version=0x2026102804` marker 用于下轮判定。
- `requeue_ret=0` 与 legacy2 同源分析：CMP_REQUEUE_PI 返回 0 可能表示**无 waiter 在队列**；legacy2 在 requeue 前
  显式等待 `waiter_waiting && owner_started`（race.cpp:371-374），当前 actor execute 未等待 ⇒ 最强根因假设。
- actor append 版本+waiter/owner 快照（sizeof 144→160，probe/freeze 同步）；timeout fail_stage 无条件写；
  placement 独立枚举 MEASUREMENT_PIN/LEAK_CLONE/LEAK_OPEN/STOP/DRAIN 标注 goto fail；JNI/Kotlin 透出。
- 双构建 EXIT=0、diff-check EXIT=0；真机 UNVERIFIED。
- **task-106 全量单测清零（2026-10-28）**：39 failures → **0（389 tests/61 XML/0 errors/0 skipped）**。分类：①测试期望过期（Cve43499WriteChainFlowTest 22：W3 gate 测试 seam/预建路径/close 期望；victim timeout retained；Jni 切片 anchor；A301 资产行匹配；KotlinHygiene stat 偏移/@Synchronized 反射；NativeHygiene anchor；BackendCve UID；Shizuku 相对路径；ScriptRender/Payload 排序）→ 最小改测试；②真实生产 bug（W2 prebuilt fast-repair 成功后未复查 uid 导致误报 prebuilt-unavailable；launch spec helperArgv 空违反 core 契约；ProcessVictimSession.identity 缺 pid seam）→ 最小修生产；③规则过期（A16 实测不再 addclose；native header 43499 文档引用）→ 改测试/注释并在 approval 标规则待 Lead 确认。新增测试 seam：`Flow43499Impl(w3GateFactory)`/`Cve43499W3HardGate open`、`VictimProcess.pid()`。双构建 EXIT=0；真机 UNVERIFIED；不 commit。
- **task-106 Lead 复核（2026-10-28）**：**生产修复通过（provisional；真机 UNVERIFIED）**——①W2 prebuilt 成功后复查 uid 再 Done；②`helperArgv=listOf(helperPath)`；③`VictimProcess.pid()` seam + identity 扩展。**测试 seam 通过**：`Flow43499Impl(w3GateFactory)` 仅测试注入、默认生产 gate 不变；`Cve43499W3HardGate open` 可见性放宽已记录理由。**规则更新通过**：`VictimArchitectureTest` addclose→adddup2+close_range 属 A16 实测修正；`victim_protocol.h` 去 43499 工具名文档引用。全量 389/0/0、双构建/静态 gate EXIT=0；后续不扩测试 seam（除已有失败）；待 routes-impl3 actor v5 后真机 E1 + 日志 UI/logcat/FileSink 验证。


### 8.28 actor timeout 证据 v5：waiter futex/owner 时序（task-108，2026-10-28）

- Lead 定位：timeout 来自 execute 尾部 `window_opened==0`（DRAIN/DISARM 已走，fail_stage=0）；window_done=1 但
  window_opened 未置 1 ⇒ waiter 未 dispatch。legacy2:185 waiter 返回后无条件 dispatch；:371-374 requeue 前等
  waiter/owner ready；:380 val3=0。
- 诊断 v5：pi_shared+result append waiter ret/errno/in_futex/dispatched/target_value + 4 timestamps
  （sizeof 160→216，probe/freeze 同步）；execute settle 后有界预等 waiter/owner ready（诊断性、失败语义不变）；
  marker=0x2026102805；JNI/Kotlin 透出。
- 选项 A 预等正式化/B dispatch 无条件/C futex 早返回重试/D val3 匹配；待下轮证据。构建双 EXIT=0；真机 UNVERIFIED。


### 8.29 placement leak mm 引用时序 + 独立枚举启用（task-109，2026-10-28）

- E1 v5：placement 仍 EACCES 且 step=ACQUIRE/MISC（独立枚举只加在头文件、goto fail 未用）。
- 修复：真正启用独立枚举（MEASUREMENT_PIN/LEAK_CLONE/LEAK_OPEN/STOP/DRAIN）；leak clone 后**立即**
  open leak memfd（对齐 legacy2 support.cpp:620-628；A16 扫描 child 可能很快退出，晚 open 会 EACCES）；
  open 失败 append child 证据（WNOHANG reaped/status、/proc status 存在/Uid、open errno、pid；
  placement_result 112→136，probe/freeze 同步）；JNI/Kotlin 透出；v5 actor 诊断不动。
- 双构建 EXIT=0、diff-check EXIT=0；真机 UNVERIFIED。


### 8.30 futex 实参诊断 v6（task-110，2026-10-28）

- E1 v5：waiter FUTEX_WAIT_REQUEUE_PI 进入后 29µs 即 ETIMEDOUT（配置 1s），execute 51ms 后才 requeue（无 waiter）。
- append 实际 futex 参数/owner tid/时间戳（actor_execute_result 216→288，probe/freeze 同步）；JNI/Kotlin 透出。
- 下轮判定：实测 parked_timeout_ns 是否=1e9（排除映射）→ 转 futex 语义/val3（owner tid vs 0）证据；不盲改。
- 双构建 EXIT=0、diff-check EXIT=0；真机 UNVERIFIED。


### 8.31 FUTEX_WAIT_REQUEUE_PI 立即 ETIMEDOUT：legacy 对拍 + 最小自测（task-111，2026-10-28）

- E1 v7：配置/字段正确（parked=1e9、op/val/val3/uaddr2 正确），但 waiter 32µs 即 ETIMEDOUT。
- legacy2 逐行对拍（file:line）：race.cpp:171-189 **绝对 CLOCK_MONOTONIC deadline**（本实现为相对 {1,0} ⇒
  最可能根因）；:190-192 wait op/val/uaddr2/val3 同形；:371-374/376-380 主线程预等+settle+CMP_REQUEUE_PI
  (val=1/timeout=(void*)1/val3=0) 同形；support.cpp:279-283 裸 SYS_futex 同形；race.cpp:193-195 waiter 返回后
  **无条件 dispatch**（本实现条件 dispatch，差异待根因修复后复测）。
- 修复：actor waiter timeout 改 legacy 绝对 CLOCK_MONOTONIC deadline（预算不变）；新增 debug-only 自测
  （actor.c+JNI+androidTest）只复刻 legacy 同形一组，logcat `GLK-FUTEX`；不发明变体、不写内核。
- 双构建 EXIT=0；自测/JNI exports 结果见报告；真机 UNVERIFIED。


### 8.32 waiter dispatch 无条件化（task-112，2026-10-28）

- 自测 legacy 同形 PASS（绝对 CLOCK_MONOTONIC deadline 正确）；E1 v9 CMP_REQUEUE_PI EDEADLK=35，
  waiter 未 dispatch（旧条件 execute_requested && errno!=ETIMEDOUT）。
- legacy2 证据：race.cpp:183-193 waiter 忽略 wait_ret 后无条件 route_dispatch；:371-380 主线程不中止。
- 修复：waiter_arm dispatch 恒 1（保留 wait_ret/errno 记录）；caller window_opened/carrier trigger/finish
  顺序不变；requeue/预等/settle/超时/retained 不变；无 stop/cancel 例外（race.cpp:183-230 无门控）。
- 双构建 EXIT=0、diff-check EXIT=0、ABI 不变；真机 UNVERIFIED。

### 8.33 carrier legacy 对齐 semantic change（task-113 Step B，Lead 批准，2026-10-28）

- **Lead 批准 semantic change（依据 legacy file:line；含 hang 风险，E1 后复核）**：
  1) inflight 等待改回**无界**（legacy2/core/route_multicast.cpp:126-127、disarm:149-155）；
  2) window_opened 仅在 carrier `observed_write!=0`（=legacy ROUTE_OK：stamp_result==0 或 consumer_success>0，
     route_multicast.cpp:133-139）后置 1；失败/未发送不进写窗口尾段；wait_ret 仍忽略（race.cpp:183-193）。
- Step A 只读证据：payload 写入序列 Kotlin file:line 未定位（blocker）；arm_hold=自旋次数（A301SoDebugConfig.kt:350，
  旧 20000；conf arm_sequence=16）；full.conf attempts=128/arm=16 原件缺（blocker）。
- Step C：carrier 原子快照写 + E1 harness 100ms monitor→`e1-carrier-trace.txt`（fsync）；双构建 EXIT=0、diff-check EXIT=0。
- UNVERIFIED：无界 inflight 真机 hang 风险、window_opened 新语义、trace 落盘、E1 下轮。
- **task-116 A1 debug-only 单发 multicast probe（2026-10-28）**：page-prep-only seam + Debug-only JNI `mcast510Probe` + androidTest `Mcast510ProbeTest`（default SKIP，opt-in `mcast510/MCAST510_ACK`）；legacy 依据 `very_legacy/temp_mcast_510.c:105-130`、`fops.c:136-150`、`util.c:395-410`、`main.c:387-442`；target=0x1000 非安全、单条 stamp、consumer go/inflight/calls/success、不写 flags/mode/不 spawn victim；结果 filesDir `mcast510-probe.txt`+`GLK-MCAST510`；双构建 EXIT=0；真机 UNVERIFIED；不 commit。
- **task-118 A1 target 参数化（2026-10-28）**：`Mcast510ProbeTest` 新增 `-e mcast510_target`（默认 0x1000；`0x...` 绝对；`page+<offset>`，裸数字十进制/0x 十六进制）；`preparePageOnly { pageBase -> request }` 做 page-relative；JNI target 最小 append jlong；结果/日志记录 target_arg+实际 target/page_base；双构建/diff-check EXIT=0；三组真机对照（0x1000/page+512/selinux alias）UNVERIFIED，待 Lead 跑。
- **task-119 W1 target debug override（2026-10-28）**：`debug-mondrian.conf` 可选 `generator.write_target_override`（hex/unset，非法 fail-closed）；仅 `w1WriteRequest()` 受影响，release 不读；E1 report 新增 `E1|target|w1/override`；build+androidTest EXIT=0、focused A301 config 20/0；两组 E1（0x1000 vs alias）UNVERIFIED，待 Lead 跑；未 commit。
- **task-120 A1 preserve_child 参数（2026-10-28）**：`Mcast510ProbeTest` 增 `-e mcast510_preserve_child true|false`（默认 false；true=W1 同形 request target/mode ZERO/preserveChild=true）；begin/page/result 记录实际值；只改 debug probe；双构建/diff-check EXIT=0；两组真机 UNVERIFIED，待 Lead 跑。
- **task-121 A1 pi_waiter 模式（2026-10-28）**：`Mcast510ProbeTest` 增 `-e mcast510_pi_waiter true|false`（默认 false）；true 时 native 创建 legacy waiter（LOCK_PI chain→WAIT_REQUEUE_PI 1s），consumer 用真实 waiter tid 做 PI walk；记录 waiter_wait_ret/errno/tid/getenforce 前后；双构建/diff-check EXIT=0；真机 UNVERIFIED。
- **task-122 A1 owner/requeue 模式（2026-10-28）**：`Mcast510ProbeTest` 增 `-e mcast510_owner`/`-e mcast510_requeue`（默认 false）；owner 按 race.cpp:206-230 LOCK_PI(target)→waiter_ready→LOCK_PI(chain)；requeue 按 race.cpp:371-380 等 waiter_waiting+owner_started→50ms→CMP_REQUEUE_PI(1,1,target,0)→stamp；记录 owner/chain/requeue ret/errno；双构建/diff-check EXIT=0；A/B/C 三组真机 UNVERIFIED。
- **task-123 actual page base + paranoid oracle（2026-10-28）**：`Landed.actualPageBase` append+`Prepared` 透出；`Mcast510ProbeTest` 记录 candidate/actual/delta/mismatch，`mcast510_page_base actual|candidate`（默认 actual）切换 fake 地址派生；前后读 `/proc/sys/kernel/perf_event_paranoid` 记录 write_landed；双构建/diff-check EXIT=0；真机 UNVERIFIED。
- **task-124 probe/legacy 逐行对拍（2026-10-28）**：发现并修正最大差异——consumer `go=1` 原在 stamp 前，legacy 在 setsockopt 后（`fops.c:196-215`）；calls/success/stop/delay 清零移到 setsockopt 前；route_last_step PASS→0/FAIL→61；consumer nice/futex fallback/pin/计数一致；双构建/diff-check EXIT=0；真机 oracle UNVERIFIED。
- **task-125 very_legacy standalone 真机（2026-10-28）**：NDK C 编译（归档 glk_log + main.c/fixed_515 + glgeneral kernelsnitch）在 b4541958 root 跑 baseline/A(perf alias)/B(selinux alias)：均 RC=0、paranoid -1→-1、Enforcing，无 panic ⇒ **legacy 原码同样无真实写入**；app-domain run-as exec 被 SELinux 拒绝；构建/二进制 sha/依赖缺口见 device-gates；未改仓库/未 commit。
- **task-126 legacy2 完整 W1 standalone（2026-10-28）**：build-tree 恢复 `.disabled` + 最小 shell，NDK build exit=0；root 3x discover step72 EAGAIN（mm leak failed）；app 域 static 二进制 discover/land 成功、`CMP_REQUEUE_PI ret=-1 errno=35` 后设备消失/重启，paranoid 前后 -1、Enforcing ⇒ legacy2 完整 W1 同样 EDEADLK 后 hang；但 profile 5.15 vs 设备 5.10 不匹配为关键限制。sha/命令/缺口见 device-gates；未改仓库/未 commit。
- **task-127 very_legacy 5.10 resident W1 app 域（2026-10-28）**：/tmp/glk127 build-tree `main_510_full.c`（fixed_510 + resident_5x=true + w1_only + KERNEL5 route），static binary sha f116fd…；app run-as 成功进入 resident writer，15×`resident write ret=0` 后 Write1 failed；enforce 仍 1/Enforcing、paranoid -1；无 panic；⇒ resident ret=0 仍不代表写入落地，需查 value/target/5.10 offsets。未改仓库/未 commit。
- **task-128 direct resident A/B/C（2026-10-28）**：/tmp/glk128 入口 sha 3f6413f；app/root 均 resident ready/start=1/ret=0/write_ret=1，A perf alias、B selinux+value0、C selinux+empty_zero_page 全部 oracle 不变（paranoid -1、enforce 1）；无 panic；与 archive BUG-3 同形；`mr_stamp` target/value@+0/+8 与 legacy2 compact waiter parent@+0x18/target@+0x28 不一致为首要嫌疑。未改仓库/未 commit。
- **task-129 oneshot EDEADLK/hang 证据分析（2026-10-28，只读）**：app actor vs legacy2 race/route_multicast 差异表；EDEADLK 为 owner→chain / waiter→target 锁环的正常 legacy 形态（Linux 5.10 futex_requeue→rt_mutex_start_proxy_lock），不应判 requeue 成功；hang 首嫌疑 S1 stamp/stale waiter 栈重叠（actor 调用栈深 vs A1/legacy2），次 S2 EDEADLK 后 1s timeout/dispatch、S3 legacy1 vs legacy2 geometry、S4 consumer、S5 timeout 误分类；下一步建议复用 task-122 A1 矩阵 + 栈深度/geometry debug-only 对照，不实现；未改仓库/未跑设备。
- **task-130 E1 trace 重解码/参数/序列 diff（2026-10-28，只读）**：`e1u-carrier-trace` 尾段 `1/1/0/...` 字段解码为 state=EXECUTING/waiter_waiting=1/**carrier_started=0/frames_sent=0**（此前误读为 1/1）；hang 在 actor pre-carrier requeue/window，不在首 stamp。参数：op_budget=128/arm_seq=16/arm_hold=20000(spin)/timeout=1s/settle=50ms/exec timeout=1s；legacy dev：settle=100ms/route_done=300000ms/nice base1 span19/enter_delay=50ms，另有 mcast attempts 来源冲突。序列 diff 首位：app settle 在 ready 前（`actor.c:1155-1168`）vs legacy ready 后 settle（`race.cpp:374-378`）⇒ **F1 可实施候选**；F2 timeout 1s→300000ms/分离字段；F3 nice；F4 enter delay；F5 参数先不改。未改仓库/未跑设备。
- **task-131 F1 实施（2026-10-28）**：`actor.c` execute 把 `usleep(setup_settle_us)` 移到 `waiter_waiting∩owner_started` ready 等待之后（对齐 `race.cpp:374-378`）；旧顺序 TODO 暂留；新增 NativeHygiene F1 源码契约测试（B13 仍既有失败）；clean 双构建 EXIT=0、全量 392/1（仅 B13 非本批）、diff-check EXIT=0；真机 E1 UNVERIFIED，待 Lead。
- **task-132 S1 PR403（2026-10-29）**：pselect fd 表改 16B `{target,backup,flags,pad}`，`F_GETFD` 快照 + `F_SETFD` 恢复 CLOEXEC，forced slot/回滚/fail-closed，dirty retain 不变；victim `ProcessVictim` 通过 `/proc` 匹配 pipe inode 后 `F_DUPFD_CLOEXEC` 高移到 384（320+64）并重建流，失败 IOException⇒spawn fail-closed；新增 Pselect/victim 宿主逻辑测试；clean build EXIT=0、全量 399/1（仅 B13 非本批）、diff-check EXIT=0；真机/实际系统调用 UNVERIFIED。
- **task-133 S2（2026-10-29）**：mondrian conf 改 pselect/5.10 cred(40/168/usage0/5)/pselect 50ms/stride64/waiter_shift-2/compact1；A301 parser 对 compact≠1、stride<64 fail-closed；MondrianDebugConfigTest 更新+2 非法；clean 双构建 EXIT=0、全量 401/1（仅 B13 既有）、diff-check=0；未跑设备。
- **task-134 S3（2026-10-29）**：本机 boot sha `9448f9…`；anchor=`dump_skip.zeroes` image +0x27dc7c8(bytes 4096/stride 32，stride 依据 diting 5.10 手测、本机未独立推导)；镜像全零、下一符号 +0x1000、字面量 0、bss_xref 仅 2 bulk read；配置 `route[*].select_stack` + parser fail-closed；pselect waiter lock word 9 写 dataAlias(anchor)；Mondrian 单测 anchor/alias/partial；clean 双构建 EXIT=0、full 403/1（仅 B13 既有）、diff-check=0；未跑设备。
- **task-135 S4-timing（2026-10-29）**：`execution.race`（park/route_done/settle）解析+fail-closed，executionParameters 仅覆盖 actor 三项；mondrian 30ms/300000ms/10ms + pselect enter 5ms，burst/max 1/1；Mondrian 单测值/派生/非法；clean 双构建 EXIT=0、full 405/1（仅 B13 既有）、diff-check=0；未跑设备，S4 E1 UNVERIFIED。
