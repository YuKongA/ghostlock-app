# 43499 Kotlin / C++ 符号与调用链（工具生成）

> 生成时间：2026-10-29（host America/Toronto）。工具：clang AST JSON（NDK clang `-Xclang -ast-dump=json`，来源 `app/.cxx/.../compile_commands.json`）+ `ctags` 辅助 + Python Kotlin source 解析（声明/调用点/解析）。
> 范围：Kotlin 全部 `app/src/main/kotlin`（348 文件）；C/C++ 全部 Android 编译单元（`gl43499/core/*.c`、`jni43499.c`、`mcast510_probe.c`、`victim_helper.c`、`common/gl_log.c`）。legacy/legacy2/very_legacy 为参考树，未编译，不在本图内。
> 精度：C/C++ 调用边来自 clang AST（宏展开/函数指针有缺口）；Kotlin 调用边为源码解析+同名解析（重载/高阶函数可能有遗漏）；file:line 为声明/调用起始行。

## 0. 产物清单

| 文件 | 内容 |
|---|---|
| `kotlin-symbols.tsv` | 全部 Kotlin class/object/interface/fun（含 owner/签名/external），2671 行 |
| `kotlin-callgraph.tsv` | Kotlin 调用边（含 resolved 标记），20418 行 |
| `native-symbols.tsv` | app C/C++ 函数符号（file/line/参数/static/TU），418 行 |
| `native-callgraph.tsv` | C/C++ 调用边（caller→callee/file:line），1462 行 |
| `jni-bridge.tsv` | Kotlin `external fun` ↔ C `Java_com_ghostlock_app_jni_*` 导出，71 对 |

复现命令：

```sh
# 1) 原生：clang AST（脚本 /tmp/glk_analysis/native_ast.py）
python3 /tmp/glk_analysis/native_ast.py   # -> native_graph.json
# 2) Kotlin：源码解析（脚本 /tmp/glk_analysis/kotlin_parse.py）
python3 /tmp/glk_analysis/kotlin_parse.py  # -> kotlin_graph.json
# 3) 汇总（脚本 /tmp/glk_analysis/gen_report.py）
python3 /tmp/glk_analysis/gen_report.py    # -> 本目录 tsv/md
```

## 1. 总量

- Kotlin：文件 287，class/object/interface **745**，fun **1926**（其中 external **43**），调用边 **20418**（resolved 6919）
- C/C++：函数 **418**（app 源码），调用边 **1462**（resolved 668），JNI 导出 **34**，pthread 线程入口 **10**

## 2. JNI 边界（Kotlin external ↔ C 导出）

| # | method | Kotlin | C 导出 |
|---|---|---|---|
| 1 | `Native43499Jni.actorDebugSnapshot` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1889` |
| 2 | `Native43499Jni.actorFutexSelfTest` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1875` |
| 3 | `Native43499Jni.cancelActor` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1278` |
| 4 | `Native43499Jni.cancelPlacement` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1056` |
| 5 | `Native43499Jni.closeActor` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1287` |
| 6 | `Native43499Jni.closePage` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1138` |
| 7 | `Native43499Jni.closePerfSampler` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1395` |
| 8 | `Native43499Jni.closePlacement` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1065` |
| 9 | `Native43499Jni.collectPerfSamples` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1328` |
| 10 | `Native43499Jni.executeActor` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1235` |
| 11 | `Native43499Jni.landPage` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1074` |
| 12 | `Native43499Jni.mcast510Probe` | `MISSING` | `app/src/main/cpp/gl43499/mcast510_probe.c:236` |
| 13 | `Native43499Jni.openPerfSampler` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1296` |
| 14 | `Native43499Jni.prepareActor` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1158` |
| 15 | `Native43499Jni.preparePlacement` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1006` |
| 16 | `Native43499Jni.raiseProcessLimits` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1472` |
| 17 | `Native43499PlacementRequest.actorDebugSnapshot` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:509` | `MISSING` |
| 18 | `Native43499PlacementRequest.actorFutexSelfTest` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:508` | `MISSING` |
| 19 | `Native43499PlacementRequest.cancelActor` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:535` | `MISSING` |
| 20 | `Native43499PlacementRequest.cancelPlacement` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450` | `MISSING` |
| 21 | `Native43499PlacementRequest.closeActor` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:538` | `MISSING` |
| 22 | `Native43499PlacementRequest.closePage` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:521` | `MISSING` |
| 23 | `Native43499PlacementRequest.closePerfSampler` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:573` | `MISSING` |
| 24 | `Native43499PlacementRequest.closePlacement` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:453` | `MISSING` |
| 25 | `Native43499PlacementRequest.closeVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:708` | `MISSING` |
| 26 | `Native43499PlacementRequest.collectPerfSamples` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:560` | `MISSING` |
| 27 | `Native43499PlacementRequest.detachVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:699` | `MISSING` |
| 28 | `Native43499PlacementRequest.executeActor` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:532` | `MISSING` |
| 29 | `Native43499PlacementRequest.forkGuardCanary` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:731` | `MISSING` |
| 30 | `Native43499PlacementRequest.forkGuardReset` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:723` | `MISSING` |
| 31 | `Native43499PlacementRequest.forkGuardSnapshot` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:716` | `MISSING` |
| 32 | `Native43499PlacementRequest.killVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:691` | `MISSING` |
| 33 | `Native43499PlacementRequest.landPage` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:512` | `MISSING` |
| 34 | `Native43499PlacementRequest.mcast510Probe` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:493` | `MISSING` |
| 35 | `Native43499PlacementRequest.openPerfSampler` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:547` | `MISSING` |
| 36 | `Native43499PlacementRequest.prepareActor` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:524` | `MISSING` |
| 37 | `Native43499PlacementRequest.preparePlacement` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444` | `MISSING` |
| 38 | `Native43499PlacementRequest.raiseProcessLimits` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:462` | `MISSING` |
| 39 | `Native43499PlacementRequest.rawSizeVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:674` | `MISSING` |
| 40 | `Native43499PlacementRequest.readRawVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:663` | `MISSING` |
| 41 | `Native43499PlacementRequest.receiveVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:652` | `MISSING` |
| 42 | `Native43499PlacementRequest.sendVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:642` | `MISSING` |
| 43 | `Native43499PlacementRequest.spawnVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:626` | `MISSING` |
| 44 | `Native43499PlacementRequest.waitExitVictim` | `app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:683` | `MISSING` |
| 45 | `Native98260Jni.attachLogFd` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:47` | `MISSING` |
| 46 | `Native98260Jni.detachLogFd` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:50` | `MISSING` |
| 47 | `Native98260Jni.logLine` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:58` | `MISSING` |
| 48 | `Native98260Jni.nativeApiVersion` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:29` | `MISSING` |
| 49 | `Native98260Jni.spawnStatic` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:39` | `MISSING` |
| 50 | `Native98260Jni.waitExit` | `app/src/main/kotlin/com/ghostlock/app/jni/Native98260Jni.kt:67` | `MISSING` |
| 51 | `NativeKernelSnitch.bruteforce` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:57` | `app/src/main/cpp/glgeneral/general_jni.c:134` |
| 52 | `NativeKernelSnitch.create` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:34` | `app/src/main/cpp/glgeneral/general_jni.c:98` |
| 53 | `NativeKernelSnitch.destroy` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:75` | `app/src/main/cpp/glgeneral/general_jni.c:169` |
| 54 | `NativeKernelSnitch.findCollisions` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:49` | `app/src/main/cpp/glgeneral/general_jni.c:116` |
| 55 | `NativeKernelSnitch.run` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:84` | `app/src/main/cpp/glgeneral/general_jni.c:186` |
| 56 | `NativeKernelSnitch.state` | `app/src/main/kotlin/com/ghostlock/app/jni/NativeKernelSnitch.kt:66` | `app/src/main/cpp/glgeneral/general_jni.c:153` |
| 57 | `NativeLogBridge.attachFd43284` | `app/src/main/kotlin/com/ghostlock/app/log/NativeLogBridge.kt:20` | `MISSING` |
| 58 | `NativeLogBridge.attachFd43499` | `app/src/main/kotlin/com/ghostlock/app/log/NativeLogBridge.kt:17` | `MISSING` |
| 59 | `NativeLogBridge.attachFdLegacy` | `app/src/main/kotlin/com/ghostlock/app/log/NativeLogBridge.kt:14` | `MISSING` |
| 60 | `VictimTransportJni.closeVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1793` |
| 61 | `VictimTransportJni.detachVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1784` |
| 62 | `VictimTransportJni.forkGuardCanary` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1848` |
| 63 | `VictimTransportJni.forkGuardReset` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1839` |
| 64 | `VictimTransportJni.forkGuardSnapshot` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1829` |
| 65 | `VictimTransportJni.killVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1772` |
| 66 | `VictimTransportJni.rawSizeVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1745` |
| 67 | `VictimTransportJni.readRawVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1713` |
| 68 | `VictimTransportJni.receiveVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1671` |
| 69 | `VictimTransportJni.sendVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1639` |
| 70 | `VictimTransportJni.spawnVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1556` |
| 71 | `VictimTransportJni.waitExitVictim` | `MISSING` | `app/src/main/cpp/gl43499/jni43499.c:1758` |

## 3. Kotlin 主干调用链（自动生成，聚焦 43499/UI 入口）

### K1

`BackendFlowRunner.run` (app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendFlowRunner.kt:36)

### K2

`BackendFlowRunner.run` (app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendFlowRunner.kt:50)

### K3

`BackendFlowRunner.run` (app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendFlowRunner.kt:85)

## 3.5 Kotlin → JNI 调用者链（自动，最多 40 条）

### J1

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444)

### J2

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450)

### J3

`PlacementConfig.closeDetailed` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:280) → `Native43499PlacementRequest.closePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:453)

### J4

`PlacementConfig.prepare` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:323) → `Native43499PlacementRequest.closePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:453)

### J5

`ProcessLimitsOutcome.raise` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/ProcessLimits.kt:72) → `Native43499PlacementRequest.raiseProcessLimits` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:462)

### J6

`Native43499PlacementRequest.mcast510Probe` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:493)

### J7

`Native43499PlacementRequest.actorFutexSelfTest` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:508)

### J8

`Native43499PlacementRequest.actorDebugSnapshot` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:509)

### J9

`Native43499PlacementRequest.landPage` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:512)

### J10

`ActorPreparation.closeDetailed` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PageOwner.kt:181) → `Native43499PlacementRequest.closePage` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:521)

### J11

`PlacementConfig.landPage` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:180) → `PlacementConfig.landPage` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:180) → `PlacementConfig.landPage` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:180) → `PlacementConfig.landPage` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:180) → `Native43499PlacementRequest.closePage` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:521)

### J12

`Native43499PlacementRequest.prepareActor` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:524)

### J13

`Native43499PlacementRequest.executeActor` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:532)

### J14

`ActorExecuteOutcome.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/ActorOwner.kt:107) → `Native43499PlacementRequest.cancelActor` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:535)

### J15

`ActorExecuteOutcome.closeDetailed` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/ActorOwner.kt:130) → `Native43499PlacementRequest.closeActor` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:538)

### J16

`ActorPreparation.prepareActor` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PageOwner.kt:107) → `ActorPreparation.prepareActor` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PageOwner.kt:107) → `ActorPreparation.prepareActor` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PageOwner.kt:107) → `ActorPreparation.prepareActor` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PageOwner.kt:107) → `Native43499PlacementRequest.closeActor` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:538)

### J17

`Cve43499PerfNative.open` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:56) → `Native43499PlacementRequest.openPerfSampler` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:547)

### J18

`Cve43499PerfNative.collect` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:61) → `Native43499PlacementRequest.openPerfSampler` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:547)

### J19

`Cve43499PerfNative.open` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:56) → `Native43499PlacementRequest.collectPerfSamples` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:560)

### J20

`Cve43499PerfNative.collect` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:61) → `Native43499PlacementRequest.collectPerfSamples` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:560)

### J21

`Cve43499PerfNative.open` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:56) → `Native43499PlacementRequest.closePerfSampler` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:573)

### J22

`Cve43499PerfNative.collect` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt:61) → `Native43499PlacementRequest.closePerfSampler` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:573)

### J23

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.spawnVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:626)

### J24

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.spawnVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:626)

### J25

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.sendVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:642)

### J26

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.sendVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:642)

### J27

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.receiveVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:652)

### J28

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.receiveVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:652)

### J29

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.readRawVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:663)

### J30

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.readRawVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:663)

### J31

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.rawSizeVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:674)

### J32

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.rawSizeVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:674)

### J33

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.waitExitVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:683)

### J34

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.waitExitVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:683)

### J35

`Native43499PlacementRequest.preparePlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:444) → `Native43499PlacementRequest.killVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:691)

### J36

`PlacementConfig.cancel` (app/src/main/kotlin/com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt:265) → `Native43499PlacementRequest.cancelPlacement` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:450) → `Native43499PlacementRequest.killVictim` (app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt:691)

## 4. Native 调用链（JNI 导出 → 下游；自动生成）

### N1

`Java_com_ghostlock_app_jni_NativeKernelSnitch_create` (app/src/main/cpp/glgeneral/general_jni.c:98) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `glk_ks_args_valid` (app/src/main/cpp/glgeneral/general_jni.c:45)

### N2

`Java_com_ghostlock_app_jni_NativeKernelSnitch_create` (app/src/main/cpp/glgeneral/general_jni.c:98) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `kernelsnitch_setup` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:332) → `kernelsnitch_possible_cpu_count` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:304)

### N3

`Java_com_ghostlock_app_jni_NativeKernelSnitch_create` (app/src/main/cpp/glgeneral/general_jni.c:98) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `kernelsnitch_setup` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:332) → `futex_hash_table_size_for` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:209)

### N4

`Java_com_ghostlock_app_jni_NativeKernelSnitch_findCollisions` (app/src/main/cpp/glgeneral/general_jni.c:116) → `glk_ks_state` (app/src/main/cpp/glgeneral/general_jni.c:61)

### N5

`Java_com_ghostlock_app_jni_NativeKernelSnitch_findCollisions` (app/src/main/cpp/glgeneral/general_jni.c:116) → `kernelsnitch_find_collisions` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:556) → `__collision_pass` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:472) → `__measure` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:171) → `rdtsc_begin` (app/src/main/cpp/glgeneral/kernelsnitch/timeutils.h:20)

### N6

`Java_com_ghostlock_app_jni_NativeKernelSnitch_findCollisions` (app/src/main/cpp/glgeneral/general_jni.c:116) → `kernelsnitch_find_collisions` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:556) → `__collision_pass` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:472) → `__measure` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:171) → `__futex` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:104)

### N7

`Java_com_ghostlock_app_jni_NativeKernelSnitch_bruteforce` (app/src/main/cpp/glgeneral/general_jni.c:134) → `glk_ks_state` (app/src/main/cpp/glgeneral/general_jni.c:61)

### N8

`Java_com_ghostlock_app_jni_NativeKernelSnitch_bruteforce` (app/src/main/cpp/glgeneral/general_jni.c:134) → `kernelsnitch_bruteforce` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:586) → `reset_cpu_pin` (app/src/main/cpp/glgeneral/kernelsnitch/utils.h:147)

### N9

`Java_com_ghostlock_app_jni_NativeKernelSnitch_bruteforce` (app/src/main/cpp/glgeneral/general_jni.c:134) → `kernelsnitch_bruteforce` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:586) → `__run_mm_leak_pass` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:268)

### N10

`Java_com_ghostlock_app_jni_NativeKernelSnitch_state` (app/src/main/cpp/glgeneral/general_jni.c:153) → `glk_ks_state` (app/src/main/cpp/glgeneral/general_jni.c:61)

### N11

`Java_com_ghostlock_app_jni_NativeKernelSnitch_destroy` (app/src/main/cpp/glgeneral/general_jni.c:169) → `glk_ks_state` (app/src/main/cpp/glgeneral/general_jni.c:61)

### N12

`Java_com_ghostlock_app_jni_NativeKernelSnitch_destroy` (app/src/main/cpp/glgeneral/general_jni.c:169) → `kernelsnitch_cleanup` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:603)

### N13

`Java_com_ghostlock_app_jni_NativeKernelSnitch_run` (app/src/main/cpp/glgeneral/general_jni.c:186) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `glk_ks_args_valid` (app/src/main/cpp/glgeneral/general_jni.c:45)

### N14

`Java_com_ghostlock_app_jni_NativeKernelSnitch_run` (app/src/main/cpp/glgeneral/general_jni.c:186) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `kernelsnitch_setup` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:332) → `kernelsnitch_possible_cpu_count` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:304)

### N15

`Java_com_ghostlock_app_jni_NativeKernelSnitch_run` (app/src/main/cpp/glgeneral/general_jni.c:186) → `glk_ks_create` (app/src/main/cpp/glgeneral/general_jni.c:71) → `kernelsnitch_setup` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:332) → `futex_hash_table_size_for` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:209)

### N16

`Java_com_ghostlock_app_jni_Native43499Jni_preparePlacement` (app/src/main/cpp/gl43499/jni43499.c:1006) → `glk43499_jni_result_ready` (app/src/main/cpp/gl43499/jni43499.c:88)

### N17

`Java_com_ghostlock_app_jni_Native43499Jni_preparePlacement` (app/src/main/cpp/gl43499/jni43499.c:1006) → `glk43499_init_call_result` (app/src/main/cpp/gl43499/jni43499.c:233)

### N18

`Java_com_ghostlock_app_jni_Native43499Jni_preparePlacement` (app/src/main/cpp/gl43499/jni43499.c:1006) → `glk43499_read_placement_request` (app/src/main/cpp/gl43499/jni43499.c:537) → `glk43499_get_long` (app/src/main/cpp/gl43499/jni43499.c:151) → `glk43499_field` (app/src/main/cpp/gl43499/jni43499.c:140)

### N19

`Java_com_ghostlock_app_jni_Native43499Jni_cancelPlacement` (app/src/main/cpp/gl43499/jni43499.c:1056) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_jni_result_ready` (app/src/main/cpp/gl43499/jni43499.c:88)

### N20

`Java_com_ghostlock_app_jni_Native43499Jni_cancelPlacement` (app/src/main/cpp/gl43499/jni43499.c:1056) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_init_call_result` (app/src/main/cpp/gl43499/jni43499.c:233)

### N21

`Java_com_ghostlock_app_jni_Native43499Jni_cancelPlacement` (app/src/main/cpp/gl43499/jni43499.c:1056) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_close_placement` (app/src/main/cpp/gl43499/core/placement.h:182)

### N22

`Java_com_ghostlock_app_jni_Native43499Jni_closePlacement` (app/src/main/cpp/gl43499/jni43499.c:1065) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_jni_result_ready` (app/src/main/cpp/gl43499/jni43499.c:88)

### N23

`Java_com_ghostlock_app_jni_Native43499Jni_closePlacement` (app/src/main/cpp/gl43499/jni43499.c:1065) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_init_call_result` (app/src/main/cpp/gl43499/jni43499.c:233)

### N24

`Java_com_ghostlock_app_jni_Native43499Jni_closePlacement` (app/src/main/cpp/gl43499/jni43499.c:1065) → `glk43499_jni_placement_close_call` (app/src/main/cpp/gl43499/jni43499.c:1033) → `glk43499_close_placement` (app/src/main/cpp/gl43499/core/placement.h:182)

## 5. actor 线程入口调用链（pthread_create 目标）

线程入口：`__do_increase` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:118), `__mm_leak` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:227), `glk43499_actor_consumer_run` (app/src/main/cpp/gl43499/core/actor.c:241), `glk43499_actor_owner_run` (app/src/main/cpp/gl43499/core/actor.c:319), `glk43499_carrier_tcp_punch_run` (app/src/main/cpp/gl43499/core/carrier_tcp.c:46), `glk43499_futex_selftest_owner` (app/src/main/cpp/gl43499/core/actor.c:1519), `glk43499_futex_selftest_waiter` (app/src/main/cpp/gl43499/core/actor.c:1535), `mcast510_consumer` (app/src/main/cpp/gl43499/mcast510_probe.c:199), `mcast510_owner` (app/src/main/cpp/gl43499/mcast510_probe.c:124), `mcast510_waiter` (app/src/main/cpp/gl43499/mcast510_probe.c:157)

### T1

`__do_increase` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:118) → `__futex` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:104)

### T2

`__mm_leak` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:227) → `scan_limit` (app/src/main/cpp/glgeneral/kernelsnitch/scan_bounds.h:7)

### T3

`__mm_leak` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:227) → `__mm_candidate_matches` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:209) → `futex_hash` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:199) → `__futex_hash` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:192) → `futex_hash_no_trunc` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:184) → `jhash2` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:100) → `rol32` (app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h:17)

### T4

`__mm_leak` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:227) → `__mm_mark_found` (app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:219)

### T5

`glk43499_actor_consumer_run` (app/src/main/cpp/gl43499/core/actor.c:241) → `glk43499_internal_pin_cpu` (app/src/main/cpp/gl43499/core/core_internal.h:212)

### T6

`glk43499_actor_consumer_run` (app/src/main/cpp/gl43499/core/actor.c:241) → `glk43499_actor_set_nice` (app/src/main/cpp/gl43499/core/actor.c:159)

### T7

`glk43499_actor_consumer_run` (app/src/main/cpp/gl43499/core/actor.c:241) → `glk43499_internal_timespec` (app/src/main/cpp/gl43499/core/core_internal.h:181)

### T8

`glk43499_actor_owner_run` (app/src/main/cpp/gl43499/core/actor.c:319) → `glk43499_internal_cpu_pin_required` (app/src/main/cpp/gl43499/core/cpu_pin_internal.h:42)

### T9

`glk43499_actor_owner_run` (app/src/main/cpp/gl43499/core/actor.c:319) → `glk43499_internal_pin_cpu` (app/src/main/cpp/gl43499/core/core_internal.h:212)

### T10

`glk43499_actor_owner_run` (app/src/main/cpp/gl43499/core/actor.c:319) → `glk43499_actor_futex` (app/src/main/cpp/gl43499/core/actor.c:130)

### T11

`glk43499_carrier_tcp_punch_run` (app/src/main/cpp/gl43499/core/carrier_tcp.c:46)

### T12

`glk43499_futex_selftest_owner` (app/src/main/cpp/gl43499/core/actor.c:1519) → `glk43499_actor_futex` (app/src/main/cpp/gl43499/core/actor.c:130)

### T13

`glk43499_futex_selftest_waiter` (app/src/main/cpp/gl43499/core/actor.c:1535) → `glk43499_actor_futex` (app/src/main/cpp/gl43499/core/actor.c:130)

### T14

`mcast510_consumer` (app/src/main/cpp/gl43499/mcast510_probe.c:199) → `mcast510_apply_nice` (app/src/main/cpp/gl43499/mcast510_probe.c:102) → `mcast510_futex` (app/src/main/cpp/gl43499/mcast510_probe.c:95)

### T15

`mcast510_owner` (app/src/main/cpp/gl43499/mcast510_probe.c:124) → `mcast510_futex` (app/src/main/cpp/gl43499/mcast510_probe.c:95)

### T16

`mcast510_waiter` (app/src/main/cpp/gl43499/mcast510_probe.c:157) → `mcast510_futex` (app/src/main/cpp/gl43499/mcast510_probe.c:95)

## 6. route 触发链（actor → carrier trigger，BFS 最短路）


## 7. syscall 叶子（按调用者）

| syscall | 调用点数 | 调用者（前 6） |
|---|---|---|
| `close` | 31 | `app/src/main/cpp/common/gl_log.c:26`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/gl43499/mcast510_probe.c:236`; `app/src/main/cpp/gl43499/core/placement.c:158`; `app/src/main/cpp/gl43499/core/placement.c:177`; `app/src/main/cpp/gl43499/core/placement.c:196` |
| `dup2` | 2 | `app/src/main/cpp/gl43499/core/carrier_pselect.c:181`; `app/src/main/cpp/gl43499/core/carrier_pselect.c:273` |
| `getsockopt` | 1 | `app/src/main/cpp/gl43499/core/carrier_tcp.c:443` |
| `ioctl` | 8 | `app/src/main/cpp/gl43499/core/perf.c:153`; `app/src/main/cpp/gl43499/core/perf.c:153`; `app/src/main/cpp/gl43499/core/perf.c:290`; `app/src/main/cpp/gl43499/core/perf.c:290`; `app/src/main/cpp/gl43499/core/perf.c:354`; `app/src/main/cpp/gl43499/core/perf.c:354` |
| `mmap` | 4 | `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:332`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:257`; `app/src/main/cpp/gl43499/core/perf.c:153`; `app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c:453` |
| `munmap` | 5 | `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:603`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:142`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:578`; `app/src/main/cpp/gl43499/core/perf.c:68`; `app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c:453` |
| `open` | 24 | `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/gl43499/core/placement.c:125`; `app/src/main/cpp/gl43499/core/placement.c:125` |
| `pipe` | 1 | `app/src/main/cpp/gl43499/core/carrier_pselect.c:359` |
| `prctl` | 3 | `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:172`; `app/src/main/cpp/gl43499/core/placement.c:90`; `app/src/main/cpp/gl43499/core/placement.c:107` |
| `pselect` | 1 | `app/src/main/cpp/gl43499/core/carrier_pselect.c:506` |
| `pthread_create` | 6 | `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:134`; `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:268`; `app/src/main/cpp/gl43499/mcast510_probe.c:236`; `app/src/main/cpp/gl43499/core/actor.c:852`; `app/src/main/cpp/gl43499/core/actor.c:1564`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:257` |
| `pthread_join` | 6 | `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:268`; `app/src/main/cpp/gl43499/mcast510_probe.c:236`; `app/src/main/cpp/gl43499/core/actor.c:182`; `app/src/main/cpp/gl43499/core/actor.c:1564`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:142`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:578` |
| `read` | 12 | `app/src/main/cpp/gl43499/core/placement.c:367`; `app/src/main/cpp/gl43499/core/placement.c:367`; `app/src/main/cpp/gl43499/core/victim_protocol.c:122`; `app/src/main/cpp/gl43499/core/victim_protocol.c:122`; `app/src/main/cpp/gl43499/core/victim_protocol.c:148`; `app/src/main/cpp/gl43499/core/victim_protocol.c:148` |
| `select` | 1 | `app/src/main/cpp/gl43499/core/carrier_pselect.c:506` |
| `setsockopt` | 4 | `app/src/main/cpp/gl43499/mcast510_probe.c:236`; `app/src/main/cpp/gl43499/core/page.c:222`; `app/src/main/cpp/gl43499/core/carrier_multicast.c:119`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:80` |
| `socket` | 3 | `app/src/main/cpp/gl43499/mcast510_probe.c:236`; `app/src/main/cpp/gl43499/core/carrier_multicast.c:38`; `app/src/main/cpp/gl43499/core/carrier_tcp.c:80` |
| `syscall` | 17 | `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h:104`; `app/src/main/cpp/gl43499/mcast510_probe.c:95`; `app/src/main/cpp/gl43499/mcast510_probe.c:102`; `app/src/main/cpp/gl43499/mcast510_probe.c:157`; `app/src/main/cpp/gl43499/core/core_internal.h:212`; `app/src/main/cpp/gl43499/core/placement.c:90` |
| `write` | 10 | `app/src/main/cpp/common/gl_log.c:107`; `app/src/main/cpp/common/gl_log.c:107`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/glgeneral/kernelsnitch/utils.h:184`; `app/src/main/cpp/gl43499/core/victim_protocol.c:171`; `app/src/main/cpp/gl43499/core/victim_protocol.c:171` |

## 8. 符号索引（文件级）

Kotlin 文件（focus 43499/UI）符号数：

| file | classes | funs |
|---|---|---|
| `com/ghostlock/app/backend/cve43499/finish/FinishAcceptance.kt` | 7 | 7 |
| `com/ghostlock/app/backend/cve43499/finish/FinishExecutionEvidence.kt` | 5 | 4 |
| `com/ghostlock/app/backend/cve43499/finish/FinishHandoff.kt` | 1 | 4 |
| `com/ghostlock/app/backend/cve43499/flow/Cve43499CleanFailureReason.kt` | 0 | 1 |
| `com/ghostlock/app/backend/cve43499/flow/Cve43499RunLog.kt` | 1 | 10 |
| `com/ghostlock/app/backend/cve43499/flow/Cve43499WriteChain.kt` | 19 | 28 |
| `com/ghostlock/app/backend/cve43499/flow/Cve43499WriteTransactionRunner.kt` | 11 | 9 |
| `com/ghostlock/app/backend/cve43499/flow/Flow43499Impl.kt` | 3 | 58 |
| `com/ghostlock/app/backend/cve43499/flow/Flow43499RunSpec.kt` | 2 | 2 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499DebugWritePlanner.kt` | 1 | 9 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499GeneratorSupport.kt` | 8 | 26 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499MulticastInputGenerator.kt` | 1 | 3 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499PayloadBuilder.kt` | 1 | 4 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499PselectInputGenerator.kt` | 2 | 3 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499RouteGeneratorFactory.kt` | 1 | 3 |
| `com/ghostlock/app/backend/cve43499/generator/Cve43499TcpZerocopyInputGenerator.kt` | 2 | 4 |
| `com/ghostlock/app/backend/cve43499/lifecycle/Cve43499Lifecycle.kt` | 26 | 27 |
| `com/ghostlock/app/backend/cve43499/lifecycle/Cve43499LifecycleApi.kt` | 4 | 20 |
| `com/ghostlock/app/backend/cve43499/owned/ActorOwner.kt` | 8 | 5 |
| `com/ghostlock/app/backend/cve43499/owned/Cve43499PrebuiltRepair.kt` | 8 | 10 |
| `com/ghostlock/app/backend/cve43499/owned/FailStopController.kt` | 1 | 3 |
| `com/ghostlock/app/backend/cve43499/owned/KernelsuGuard.kt` | 4 | 2 |
| `com/ghostlock/app/backend/cve43499/owned/PageOwner.kt` | 9 | 5 |
| `com/ghostlock/app/backend/cve43499/owned/PlacementOwner.kt` | 13 | 6 |
| `com/ghostlock/app/backend/cve43499/owned/ProcessLimits.kt` | 5 | 5 |
| `com/ghostlock/app/backend/cve43499/owned/TokenOwner.kt` | 17 | 17 |
| `com/ghostlock/app/backend/cve43499/perf/Cve43499PerfNative.kt` | 2 | 6 |
| `com/ghostlock/app/backend/cve43499/perf/Cve43499PerfOwner.kt` | 1 | 5 |
| `com/ghostlock/app/backend/cve43499/perf/Cve43499PerfSampleParser.kt` | 6 | 8 |
| `com/ghostlock/app/backend/cve43499/perf/Cve43499PerfTargetSource.kt` | 4 | 8 |
| `com/ghostlock/app/backend/cve43499/port/Cve43499BackendPort.kt` | 5 | 11 |
| `com/ghostlock/app/backend/cve43499/victim/Cve43499CommonVictimRuntime.kt` | 1 | 4 |
| `com/ghostlock/app/backend/cve43499/victim/Cve43499JniVictim.kt` | 5 | 22 |
| `com/ghostlock/app/backend/cve43499/victim/Cve43499VictimKillPolicy.kt` | 1 | 2 |
| `com/ghostlock/app/backend/cve43499/victim/Cve43499VictimLaunchSpecFactory.kt` | 1 | 2 |
| `com/ghostlock/app/backend/cve43499/victim/ProcessVictim.kt` | 4 | 20 |
| `com/ghostlock/app/backend/cve43499/victim/ProcessVictimSession.kt` | 5 | 18 |
| `com/ghostlock/app/backend/cve43499/victim/VictimFinish.kt` | 4 | 13 |
| `com/ghostlock/app/backend/cve43499/victim/VictimProbe.kt` | 5 | 2 |
| `com/ghostlock/app/backend/cve43499/victim/VictimProtocolAppend.kt` | 1 | 6 |
| `com/ghostlock/app/backend/cve43499/w3/Cve43499W3HardGate.kt` | 9 | 4 |
| `com/ghostlock/app/data/cve43499/A301SoDebugConfig.kt` | 26 | 32 |
| `com/ghostlock/app/jni/Native43499Jni.kt` | 21 | 28 |
| `com/ghostlock/app/jni/Native98260Jni.kt` | 1 | 6 |
| `com/ghostlock/app/jni/NativeKernelSnitch.kt` | 1 | 6 |
| `com/ghostlock/app/ui/backend/BackendFlowRunner.kt` | 3 | 3 |
| `com/ghostlock/app/ui/screen/module/backends/FinishHandoffAdapter.kt` | 5 | 8 |

C/C++ 文件符号数：

| file | functions |
|---|---|
| `app/src/main/cpp/common/gl_log.c` | 14 |
| `app/src/main/cpp/common/gl_log.h` | 8 |
| `app/src/main/cpp/gl43499/../common/gl_log.h` | 8 |
| `app/src/main/cpp/gl43499/core/actor.c` | 31 |
| `app/src/main/cpp/gl43499/core/actor.h` | 6 |
| `app/src/main/cpp/gl43499/core/carrier_internal.h` | 12 |
| `app/src/main/cpp/gl43499/core/carrier_multicast.c` | 6 |
| `app/src/main/cpp/gl43499/core/carrier_pselect.c` | 19 |
| `app/src/main/cpp/gl43499/core/carrier_tcp.c` | 11 |
| `app/src/main/cpp/gl43499/core/core_internal.h` | 19 |
| `app/src/main/cpp/gl43499/core/cpu_pin_internal.h` | 2 |
| `app/src/main/cpp/gl43499/core/page.c` | 9 |
| `app/src/main/cpp/gl43499/core/page.h` | 2 |
| `app/src/main/cpp/gl43499/core/page_internal.h` | 4 |
| `app/src/main/cpp/gl43499/core/perf.c` | 10 |
| `app/src/main/cpp/gl43499/core/perf.h` | 3 |
| `app/src/main/cpp/gl43499/core/placement.c` | 23 |
| `app/src/main/cpp/gl43499/core/placement.h` | 4 |
| `app/src/main/cpp/gl43499/core/placement_internal.h` | 10 |
| `app/src/main/cpp/gl43499/core/pselect_timeout_internal.h` | 2 |
| `app/src/main/cpp/gl43499/core/victim_helper/victim_helper.c` | 32 |
| `app/src/main/cpp/gl43499/core/victim_protocol.c` | 36 |
| `app/src/main/cpp/gl43499/core/victim_protocol.h` | 17 |
| `app/src/main/cpp/gl43499/jni43499.c` | 65 |
| `app/src/main/cpp/gl43499/mcast510_probe.c` | 8 |
| `app/src/main/cpp/glgeneral/general_jni.c` | 9 |
| `app/src/main/cpp/glgeneral/kernelsnitch/futex_hash.h` | 10 |
| `app/src/main/cpp/glgeneral/kernelsnitch/kernelsnitch.h` | 24 |
| `app/src/main/cpp/glgeneral/kernelsnitch/scan_bounds.h` | 1 |
| `app/src/main/cpp/glgeneral/kernelsnitch/timeutils.h` | 2 |
| `app/src/main/cpp/glgeneral/kernelsnitch/utils.h` | 11 |

## 9. 已知缺口/注意

- Kotlin 解析为**同名解析**：重载、扩展函数接收者、高阶/lambda、反射调用可能漏边；resolved=0 的边在 `kotlin-callgraph.tsv` 保留可查。
- C/C++ clang AST 已展开宏但**函数指针调用**（如 vtable 风格的 carrier->trigger、JNI 注册表）不会出现在 CallExpr 名称中；线程入口单独给出。
- actor 内部通过 `pthread_create` 进入的 10 个入口见 §5；动态线程（victim helper 子进程）不在同进程调用图。
- legacy/legacy2/very_legacy 未编译（parked），其符号/调用链需要单独分析批次（可复用同一 AST 脚本，但需临时编译单元）。
