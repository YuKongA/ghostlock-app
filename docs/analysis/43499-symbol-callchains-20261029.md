# 43499 Kotlin / C++ 符号与调用链（工具生成 + 人工校核）

> 生成时间：2026-10-29（host America/Toronto）。
> 工具：**NDK clang AST JSON**（`-Xclang -ast-dump=json -fsyntax-only`，编译参数来自 `app/.cxx/.../compile_commands.json`）+ **Python Kotlin 源码解析**（声明/调用点/花括号匹配/同名解析）。生成脚本已随文档保存于 [`43499-callchains-20261029/tools/`](43499-callchains-20261029/tools/)。
> 范围：Kotlin 全部 `app/src/main/kotlin`（348 文件）；C/C++ 全部 Android 编译单元（`gl43499/core/*.c`、`jni43499.c`、`mcast510_probe.c`、`victim_helper.c`、`common/gl_log.c`）。`legacy/legacy2/very_legacy` 为 parked 参考树、未编译，不在本图内。
> 精度：C/C++ 边来自 clang AST（宏已展开；函数指针/vtable 分派不产生 CallExpr 名称，需结合线程入口表）；Kotlin 为源码级近似（重载、扩展函数接收者、lambda/高阶可能有漏边；owner 统一取文件首个顶层类型以降低嵌套噪声）。file:line 为声明或调用起始行。
> 本文件是**索引与关键链路**；全量机器可读数据见同目录 TSV，自动生成的展开链（含噪声）见 [`43499-callchains-20261029/README.md`](43499-callchains-20261029/README.md)。

## 0. 产物清单

| 文件 | 内容 | 规模 |
|---|---|---|
| [`kotlin-symbols.tsv`](43499-callchains-20261029/kotlin-symbols.tsv) | 全部 Kotlin class/object/interface/fun（file/line/owner/签名/external） | 2,671 行 |
| [`kotlin-callgraph.tsv`](43499-callchains-20261029/kotlin-callgraph.tsv) | Kotlin 调用边（caller→callee，含 resolved 标记） | 20,418 行 |
| [`native-symbols.tsv`](43499-callchains-20261029/native-symbols.tsv) | app C/C++ 函数符号（file/line/参数/static/TU） | 418 行 |
| [`native-callgraph.tsv`](43499-callchains-20261029/native-callgraph.tsv) | C/C++ 调用边（caller→callee/file:line） | 1,462 行 |
| [`jni-bridge.tsv`](43499-callchains-20261029/jni-bridge.tsv) | Kotlin `external fun` ↔ C `Java_com_ghostlock_app_jni_*` | 43 对 |
| [`tools/`](43499-callchains-20261029/tools) | `native_ast.py`（clang AST 抽取）、`kotlin_parse.py`（Kotlin 解析）、`gen_report.py`（汇总） | — |

复现：

```sh
python3 docs/analysis/43499-callchains-20261029/tools/native_ast.py   # clang AST -> native_graph.json
python3 docs/analysis/43499-callchains-20261029/tools/kotlin_parse.py  # Kotlin -> kotlin_graph.json
python3 docs/analysis/43499-callchains-20261029/tools/gen_report.py    # -> tsv + 自动链附录
```

## 1. 总量

- Kotlin：文件 **348**；class/object/interface **745**；fun **1,926**（external **43**）；调用边 **20,418**（resolved **6,919**）。
- C/C++：app 源码函数 **418**；调用边 **1,462**（resolved **668**）；JNI 导出 **34**；pthread 线程入口 **10**。
- 43499 focus 集（`backend/cve43499` + `data/cve43499` + `jni` + `FinishHandoffAdapter`/`BackendFlowRunner`）：**47 文件 / 489 fun**。

## 2. 关键调用链（人工校核，file:line）

### C1. UI → Flow 编排 → W1 事务 → JNI（Kotlin 主链）

```
BackendFlowRunner.kt:61                     Flow43499Impl().run(build.request)
└─ Flow43499Impl.kt:184                     class Flow43499Impl
   ├─ :220                                   override fun steps(request)
   ├─ :262/266/284/286/288                  step(REQUEST_VALIDATE / PLACEMENT_PREPARE / PAGE_LAND / ACTOR_PREPARE / ACTOR_EXECUTE)
   ├─ :292                                  step(FINISH_EXECUTE)
   ├─ :456                                  Cve43499WriteTransactionRunner(...)
   └─ :561                                  runner.execute(Cve43499WriteStageId.W1, request)
      └─ Cve43499WriteTransactionRunner.kt:197  fun execute(stage, request)
         ├─ :202                            lifecycle.preparePlacement(placement)
         ├─ :226                            lifecycle.landPage(generated.landing)
         ├─ :252                            lifecycle.prepareActor(...)
         └─ :277                            lifecycle.executeActor()
            └─ Cve43499Lifecycle.kt:279/317/359/396/439  preparePlacement / landPage / prepareActor / executeActor / closeChain
```

### C2. Placement（page 供给）

```
Cve43499Lifecycle.kt:279            preparePlacement(config)
└─ :282                             PlacementOwner.prepare(config)
   └─ PlacementOwner.kt:323         fun prepare(...)
      └─ :327                       Native43499Jni.preparePlacement(...)
         └─ Native43499Jni.kt:444   external fun preparePlacement(...)
            └─ jni43499.c:1006      Java_..._preparePlacement
               └─ :1021             glk43499_prepare_placement(...)
                  └─ placement.c:921  glk43499_prepare_placement
```

### C3. Landing（内核页落地）

```
Cve43499Lifecycle.kt:317                  landPage(config)
└─ :595                                  holder.land(config)
   └─ PlacementOwner.kt:180              fun landPage(...)
      └─ :204                            Native43499Jni.landPage(...)
         └─ Native43499Jni.kt:512        external fun landPage(...)
            └─ jni43499.c:1074           Java_..._landPage
               └─ :1127                  glk43499_land_page(...)
                  └─ page.c:222          glk43499_land_page
```

### C4. prepareActor / executeActor（三线程 PI race + carrier）

```
Cve43499Lifecycle.kt:359            prepareActor(...)
└─ PageOwner.kt:107                 PageOwner.prepareActor(...)
   └─ :117                          Native43499Jni.prepareActor(...)
      └─ Native43499Jni.kt:524      external fun prepareActor(...)
         └─ jni43499.c:1158         Java_..._prepareActor
            └─ :1224                glk43499_prepare_actor(...)
               └─ actor.c:852       glk43499_prepare_actor

Cve43499Lifecycle.kt:396            executeActor()
└─ :401                             owner.execute()
   └─ ActorOwner.kt:69              fun execute()
      └─ :78                        Native43499Jni.executeActor(...)
         └─ Native43499Jni.kt:532   external fun executeActor(...)
            └─ jni43499.c:1235      Java_..._executeActor
               └─ :1246             glk43499_execute_actor(...)
                  └─ actor.c:1120   glk43499_execute_actor
                     ├─ actor.c:1018/1024/1030  pthread_create(consumer / owner / waiter)
                     ├─ actor.c:1150-1200       ready 等待 → F1 post-ready settle → CMP_REQUEUE_PI / window wait
                     └─ actor.c:993             waiter 入口选择 = glk43499_actor_waiter_multicast
```

### C5. multicast carrier（oneshot stamp → setsockopt）

```
actor.c:472                       glk43499_actor_waiter_multicast（线程入口）
├─ :376                           glk43499_actor_waiter_arm
├─ :481                           glk43499_carrier_multicast_trigger(...)
│  └─ carrier_multicast.c:119     trigger
│     ├─ :149                     setsockopt(MCAST_BLOCK_SOURCE, frame, 264)   ← 内核侧 stamp
│     └─ :132                     consumer_go / route_delay=0（PI 窗口内无 IO）
├─ :489-506                       ghost disarm（FUTEX_LOCK_PI dummy，D3）
└─ :451                           glk43499_actor_waiter_finish → window_done
actor.c:241 / :319                consumer / owner 线程入口
```

### C6. pselect / tcp carrier（备选 route）

```
actor.c:519 → glk43499_carrier_pselect_trigger (carrier_pselect.c:506)
              ├─ carrier_pselect.c:454   syscall(SYS_timerfd_create)
              └─ carrier_pselect.c:590   pselect(nfds=320, ...)（fd_set 落内核栈）
actor.c:542 → glk43499_carrier_tcp_trigger (carrier_tcp.c:443)
              ├─ carrier_tcp.c:46        glk43499_carrier_tcp_punch_run（线程入口 :422）
              ├─ carrier_tcp.c:82/105    socket(AF_INET, SOCK_STREAM)
              └─ carrier_tcp.c:524       getsockopt(TCP_ZEROCOPY_RECEIVE)
```

### C7. victim / finish（payload 执行与收尾）

```
Flow43499Impl.kt:264   step(VICTIM_START) → victimStartStep
Flow43499Impl.kt:290   step(VICTIM_PROBE) → victimProbeStep
Flow43499Impl.kt:292   step(FINISH_EXECUTE) → :1559 finishExecuteStep → :1625 finishExecuteViaVictim
Native43499Jni.kt:626  external fun spawnVictim → jni43499.c:1556
Native43499Jni.kt:683/699/708  waitExitVictim / detachVictim / closeVictim
```

## 3. JNI 边界（`Native43499Jni` / `VictimTransportJni`，17+8 对；全 43 对见 TSV）

| Kotlin external | C 导出 |
|---|---|
| `Native43499Jni.preparePlacement` (Native43499Jni.kt:444) | jni43499.c:1006 |
| `Native43499Jni.cancelPlacement` (:450) | jni43499.c:1056 |
| `Native43499Jni.closePlacement` (:453) | jni43499.c:1065 |
| `Native43499Jni.raiseProcessLimits` (:462) | jni43499.c:1472 |
| `Native43499Jni.mcast510Probe` (:493) | mcast510_probe.c:236 |
| `Native43499Jni.actorFutexSelfTest` (:508) | jni43499.c:1875 |
| `Native43499Jni.actorDebugSnapshot` (:509) | jni43499.c:1889 |
| `Native43499Jni.landPage` (:512) | jni43499.c:1074 |
| `Native43499Jni.closePage` (:521) | jni43499.c:1138 |
| `Native43499Jni.prepareActor` (:524) | jni43499.c:1158 |
| `Native43499Jni.executeActor` (:532) | jni43499.c:1235 |
| `Native43499Jni.cancelActor` (:535) | jni43499.c:1278 |
| `Native43499Jni.closeActor` (:538) | jni43499.c:1287 |
| `Native43499Jni.openPerfSampler` (:547) | jni43499.c:1296 |
| `Native43499Jni.collectPerfSamples` (:560) | jni43499.c:1328 |
| `Native43499Jni.closePerfSampler` (:573) | jni43499.c:1395 |
| `VictimTransportJni.spawnVictim` (:626) | jni43499.c:1556 |
| `VictimTransportJni.sendVictim` (:642) | jni43499.c:1639 |
| `VictimTransportJni.receiveVictim` (:652) | jni43499.c:1671 |
| `VictimTransportJni.waitExitVictim` (:683) | jni43499.c:1758 |
| `VictimTransportJni.detachVictim` (:699) | jni43499.c:1784 |
| `VictimTransportJni.closeVictim` (:708) | jni43499.c:1793 |
| `NativeKernelSnitch.*`（6 个） | glgeneral/general_jni.c:98-186 |
| `Native98260Jni.*` / `NativeLogBridge.*` | 在 98260 的 DSO（非 `libghostlock43499.so`），TSV 中标 MISSING |

## 4. pthread 线程入口（clang AST 提取，10 个）

| 入口 | file:line |
|---|---|
| `glk43499_actor_consumer_run` | actor.c:241 |
| `glk43499_actor_owner_run` | actor.c:319 |
| `glk43499_actor_waiter_multicast` | actor.c:472 |
| `glk43499_actor_waiter_pselect` | actor.c:519 |
| `glk43499_actor_waiter_tcp` | actor.c:542 |
| `glk43499_carrier_tcp_punch_run` | carrier_tcp.c:46 |
| `glk43499_futex_selftest_owner` / `_waiter` | actor.c:1541 / :1560+ |
| `mcast510_owner` / `mcast510_waiter` / `mcast510_consumer` | mcast510_probe.c |
| `__mm_leak` / `__do_increase`（KernelSnitch） | glgeneral/kernelsnitch/*.h |

## 5. syscall 叶子（C/C++ 调用点计数）

| syscall | 调用点数 | syscall | 调用点数 |
|---|---|---|---|
| `close` | 31 | `open` | 24 |
| `syscall`（原始号） | 17 | `read` | 12 |
| `write` | 10 | `ioctl` | 8 |
| `pthread_create` / `pthread_join` | 6 / 6 | `mmap` / `munmap` | 4 / 5 |
| `socket` | 3 | `setsockopt` | 4 |
| `getsockopt` | 1 | `select` / `pselect` | 1 / 1 |
| `prctl` | 3 | `dup2` / `pipe` / `timerfd_create` | 2 / 1 / 1 |

> PI 窗口内物理 syscall 重点：`carrier_multicast.c:149`（setsockopt）、`carrier_pselect.c:590`（pselect）、`carrier_tcp.c:524`（getsockopt）、`actor.c:1150-1200`（CMP_REQUEUE_PI）。

## 6. 符号索引（按文件）

Kotlin（43499 focus，classes/funs）：

| file | classes | funs |
|---|---|---|
| `flow/Flow43499Impl.kt` | 4 | 60 |
| `flow/Cve43499WriteTransactionRunner.kt` | 2 | 20 |
| `flow/Cve43499WriteChain.kt` | 7 | 18 |
| `lifecycle/Cve43499Lifecycle.kt` | 4 | 22 |
| `owned/PlacementOwner.kt` | 3 | 12 |
| `owned/PageOwner.kt` | 3 | 10 |
| `owned/ActorOwner.kt` | 2 | 8 |
| `generator/Cve43499PayloadBuilder.kt` | 1 | 8 |
| `generator/Cve43499MulticastInputGenerator.kt` | 1 | 6 |
| `victim/*.kt` | 12 | 45 |
| `finish/*.kt` | 4 | 14 |

C/C++（app 源码，函数数）：

| file | functions | file | functions |
|---|---|---|---|
| `gl43499/core/actor.c` | 74 | `gl43499/core/carrier_multicast.c` | 8 |
| `gl43499/core/placement.c` | 32 | `gl43499/core/carrier_pselect.c` | 9 |
| `gl43499/core/page.c` | 18 | `gl43499/core/carrier_tcp.c` | 12 |
| `gl43499/core/victim_protocol.c` | 28 | `gl43499/jni43499.c` | 40 |
| `gl43499/core/perf.c` | 8 | `glgeneral/general_jni.c` | 12 |

（全量逐符号 file:line/参数见 `native-symbols.tsv` 与 `kotlin-symbols.tsv`；自动展开链见 `43499-callchains-20261029/README.md`。）

## 7. 已知缺口

1. **函数指针分派**：`actor.c` 的 carrier trigger 为直接调用（:481/:525/:548），已覆盖；JNI 注册表/回调（victim finish 等）不在 CallExpr 名称中，需结合线程入口与 JNI TSV。
2. **Kotlin 重载/扩展/高阶**：同名解析可能归并重载；`resolved=0` 边保留在 `kotlin-callgraph.tsv` 供人工追溯。
3. **宏**：C 侧以宏展开位置记录调用边，跨宏函数名可能与源码字面不同。
4. **legacy/legacy2/very_legacy**：未编译、不在图内；需要时增加临时编译单元后复用 `native_ast.py`。
5. **动态/反射/协程**：`Class.forName`、反射、SequentialFlow 协程调度不产生静态边；主链以显式 `step()`/lifecycle 调用为准。
