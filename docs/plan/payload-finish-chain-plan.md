# 载荷 / 收尾脚本与日志实施计划（v8）

- 日期：2026-10-19
- 基线：`2933b838`（设计基线；工作树含 v1 文件版未提交改动，v8 **不以其为基线**）
- 级别：L（攻击关键路径 + 跨 Native/Kotlin 收尾协议 + LKM 载荷数据契约）
- 设计依据：[payload-finish-chain-design.md](../analysis/payload-finish-chain-design.md)（v8 日志无上限、常驻）
- 计划关系：本件是 [43284-production-engine-plan.md](43284-production-engine-plan.md) 的 **P3/P4/P5 子计划**；不新增并行计划
- 取代：v1（runId 文件）、v2（memfd 三通道）、v3（argv 参数）、v4（整脚本进 knob）、v5（root 直写日志）、v6（完成后转写）、v7（1 MiB 上限 + crash 转存）均作废

## 1. 目标

```text
App 渲染整段脚本 → 写 files/finish/run.sh
→ 建 log memfd，脚本里写 LOG=/proc/<pid>/fd/<n>
→ 启动 drainer：每 50 ms 读新增 → run-<epochMillis>.log → fdatasync
→ 补丁 .ko 的 script_path → 页缓存写 → 触发
→ UMH: sh <脚本路径>；脚本只写 memfd
→ 内核视图确认完成 → 最后 drain → 删除 run.sh
→ 日志文件保留，无大小上限

panic 时：已 fsync 的行留在盘上；日志文件原样保留。
```

## 2. 约束

1. **落盘白名单**：攻击流程只允许 App 写 `run.sh` 与 `run-<epochMillis>.log`。
2. **root 零落盘**：UMH 与脚本零文件写入；日志只写 memfd。
3. **日志只由 App 落盘**：App 建 memfd、并行 drain、`fdatasync`、转写日志文件。
4. **无上限、正常保存**：不设 1 MiB 上限、不截断；每次 run 一个独立日志文件，完成后保留。
5. **脚本是临时物**：`run.sh` 在完成、失败、超时、中止后删除；panic 残留的在下次启动删除。
6. **无信号文件**：exit、marker、ready/connected、pid、request、status、socket 文件全部取消。
7. **路径传参**：`knob.script_path` 是唯一 App→root 参数；脚本正文不进镜像、不进 argv。
8. **零返回通道**：终态由内核视图与效果探针判定；日志只做事后证据。
9. 攻击原语、hook 6 槽位、carrier 选择、`insmod` 路径保持不变；内核侧只加 `knob` 数据块。
10. 收尾（`setenforce` / `ctl.restart`）由脚本末段执行；LKM `restore_enforce=0`。
11. 未过离线验证不跑真机；每批一类事，上批绿再进。
12. 旧实现不保留运行期 fallback；回滚 = 整批 `git revert`。
13. 实施按 AGENTS §3：注释套旧码 → `//TODO(reason)` → 新码 → 判据全绿后删旧码。

## 3. 计划表

| 批次 | 目标 | 主要文件 | 验收 | 依赖 | 状态 |
|---|---|---|---|---|---|
| B0 | 设计 v8：日志无上限、常驻 | 设计文档、本计划 | 文档定稿；用户认可 | 无 | **已完成** |
| B1 | 脚本文件 + 日志并行落盘 | `FinishScript.kt`、`FinishScriptStore.kt`、`LogMemfd.kt`、`FinishExecution.kt`、`full-process-uml.md` | `ScriptRenderTest`/`LineAssemblerTest`；单测全绿 | B0 | **已实现（工作树）** |
| B2 | 删除 root_cmd 与部署 | `root_cmd.sh`、`TypedBackendProbe.kt`、`app/build.gradle.kts` | 文件删除；asset 任务删除；`assembleDebug` | B1 | **已实现（工作树）** |
| B3 | 删除全部 marker 判据 | `WaitPolicy43284.kt`、`TypedBackendProbe.kt`、`RelayAccessService.kt`、`PayloadRequests.kt` | 禁用串 `rg` 为 0；单测绿 | B2 | **已实现（工作树）** |
| B4 | 取消 runId 与 exit 轮询 | `FinishExecution.kt`、`FinishConfig.kt` | 无 `runs/`/`exit`/`pending`；完成信号走内核视图 | B1 | **已实现（工作树）** |
| B5 | 收尾进脚本 | `FinishScript.kt`、`FinishSettingsUi.kt` | 脚本含 `setenforce`/`ctl.restart` | B1 | **已实现（工作树）** |
| B6 | LKM knob 路径 + 8 镜像 | `ghostlock.c`、`ghostlock.glk.c`、`LkmSelector.kt`、`LkmImagePatch.kt` | `LkmImagePatchTest`；8 镜像唯一 magic；真机 UMH 待门禁 | B5 | **已实现（工作树，8 镜像已校验）** |
| B7 | 载荷直取现有输入 | `PayloadRequests.kt`、`FinishConfig.kt` | 无 staging；原路径或 asset fd | B2 | **已实现（工作树）**：`file://` 直取；`content://` 与 FolkPatch asset 进 memfd |
| B8 | 日志常驻、中止、清理 | `LogMemfd.kt`、`FinishScriptStore.kt`、`LogFile.kt`、`FileSink.kt`、`LogExport.kt`、`AttackService.kt`、`BootSettings.kt`、`RootChannelClient.kt` | 日志保留；`run.sh` 删除 | B4 | **已实现（工作树）**：pid 文件、自动导出、RootChannel staging 已删；诊断日志保留；开机重试计数属流程外状态 |
| B9 | 效果探针 + 真机审计 | `PayloadEffects.kt`、测试、`docs/analysis/device-gates/` | 三态映射；日志完整保留 | B3/B6/B7/B8 | **部分**：探针、静态守卫、模拟器 UMH/finish 门禁完成；完整链 hook=1 在模拟器不可达，真机门禁待设备 |

### 3.1 当前进度与证据（2026-10-19）

```text
:app:testDebugUnitTest                     全绿（LkmImagePatch / ScriptRender / LineAssembler / PayloadEffects / FinishCompletion / AttackFlowGuard）
nativeBuildAndroid                        BUILD SUCCESSFUL（native_memfd.cpp，零告警）
:app:assembleDebug -PlkmCacheOnly=true     BUILD SUCCESSFUL
buildLkmImages（8 个 KMI，podman）          BUILD SUCCESSFUL
8 镜像校验                                  magic 唯一、size=284、flags=(1,0,0)、默认 /dev/null
verifyLkmKnob（工程门禁）                   PASS：8 镜像缺 magic/size 不符时构建失败
APK assets 抽查                             android14-6.1 / android12-5.10 均含 magic
marker 串扫描                              ghostlock_lkm_ok|...|ready|connected → 0
静态守卫                                    AttackFlowGuardTest（含自证可失败）
ghostlock.glk.c 容器编译                    PASS（android14-6.1）
connectedDebugAndroidTest（emulator-5554）  PASS：memfd 往返 / run.sh 生命周期 / 日志 drainer+fdatasync / prepare+镜像补丁
memfdRootProbe（adb root）                  PASS：root 读 /proc/<app_pid>/fd/<n> 得到 root-probe
scriptRootExecutionProbe（adb root）        PASS：root 执行 run.sh；日志含 finish start / finish done
UmhGate（emulator，手工补丁模块）           PASS：insmod → UMH → run.sh；dmesg `umh exec returned 0`；日志含 finish start / done
AttackSmoke（emulator，完整链）              FAIL：模块写 verify-read-failed；hook landing 0xFE628 > libc++ 大小 671496；hook=0
DFRoot APK 对照                              libdfroot.so 用 vmsplice/pipe/splice + 动态 code cave + 自带 LKM；其路径不走我们的 ESP 固定 plan
```

未覆盖：真机 UMH 执行、`/proc` 可读性、panic 演练、冷机门禁归档；当前仅连接 emulator-5554，未做真机验证。

## 4. 批次细化

### B1 脚本文件 + 并行日志

```text
FinishScript.render(state, logPath) → 整段脚本（LOG=/proc/<pid>/fd/<n>）
FinishScriptStore.write(context, script) → files/finish/run.sh（覆盖写）
LogMemfd：
  create() → fd
  startDrain(context, fd, logFile)：独立线程，每 50 ms 读新增 → 日志文件 → fdatasync
  stopDrain()：最后一次 drain + fdatasync 后停止
FinishExecution：
  写脚本 → 建 memfd → 启动 drainer → 补丁 → 触发 → 完成 → 停止 drainer
  → 删除 run.sh；日志保留
```

验收：

```text
./gradlew :app:testDebugUnitTest --tests '*ScriptFileTest' --tests '*LogMemfdTest'
./gradlew :app:compileDebugKotlin
```

### B2 删除 root_cmd

```text
删除：
  tools/lkm/ghostlock/root_cmd.sh
  TypedBackendProbe.deployUmhPayload
  app/build.gradle.kts: copyPayloadScriptIntoAssets
  MODULE_PATH 生产读取（改用 LkmSelector asset 字节）

保留：
  LkmSelector.read（返回可变副本）
```

验收：

```text
rg 'root_cmd\.sh|deployUmhPayload|MODULE_PATH' app/src/main/kotlin tools/lkm → 0
./gradlew :app:assembleDebug
```

### B3 marker 判据

```text
删除：
  WaitPolicy43284 的 SUCCESS/FAILURE/ALT/IN_FLIGHT marker
  TypedBackendProbe 的 freshMarker/lkmOk/lkmFail/markers
  RelayAccessService 的 READY_FILE/CONNECTED_FILE
  PayloadRequests 的 ACCESS_READY/ACCESS_CONNECTED

新判据：
  内核视图：/proc/modules 或 /sys/module/ghostlock 由在到无
  效果探针：verified / failed / unknown
```

验收：

```text
rg 'ghostlock_lkm_ok|ghostlock_lkm_fail|dfm0|dfm1|ghostlock-lkm-ready|ghostlock-lkm-connected' app/src/main/kotlin tools/lkm → 0
:app:testDebugUnitTest 全绿
```

### B4 取消 runId

```text
删除：
  files/finish/runs/<runId>/ 全部
  clearRuns / pendingScript / 时间戳副本
  exit 轮询与 finish.log tail

替代：
  files/finish/run.sh + run-<epochMillis>.log
  完成信号：/proc/modules（读不到时效果探针 + 上限超时）
  完成后最后 drain → 删除 run.sh；日志保留
```

验收：

```text
rg 'finish/runs|clearRuns|pendingScript|pending\.sh|finish\.log' app/src/main/kotlin → 0
单测：完成信号超时 ⇒ failed；清理只删 run.sh
```

### B5 收尾进脚本

```text
脚本末段：
  restore_selinux=1 → setenforce 1
  soft_reboot=1     → setprop ctl.restart zygote

LKM knob：
  permissive=1
  restore_enforce=0（恢复由脚本负责）
```

验收：

```text
ScriptRenderTest：两种取值各生成/省略对应语句
脚本内唯一文件重定向是 LOG=/proc/<pid>/fd/<n>
dmesg 记录 UMH 返回码
```

### B6 LKM knob

```text
struct glk_knob（magic "GLKKNOB1" + size + flags + path_len + script_path[256]）
默认 script_path = "/dev/null"，path_len=9（inert）

模块：
  校验 magic/size/path_len/script_path[path_len]==0/首字节 '/'
  argv = {sh, script_path, NULL}
  UMH_WAIT_PROC

App：
  LkmSelector.read() → 定位唯一 magic → 写路径/flags → 页缓存写
  0 个/多个 magic、路径超长或非绝对 ⇒ 具名失败，不触发
```

验收：

```text
./gradlew :app:testDebugUnitTest --tests '*KnobPatchTest'
8 个 build/lkm/<label>/ghostlock.ko 均断言：唯一 magic、路径容量、默认 inert
真机：UMH 执行 run.sh，uid=0；dmesg 出现 umh exec returned
```

### B7 载荷输入

```text
root：nativeLibraryDir/libksud.so 或 /data/adb/ksu/bin/ksud，直接 exec
app ：monkey / am start
cmd ：用户命令原文整行
lkm ：原路径 insmod；SAF 仅接受 /data/media/0 可解析路径
FolkPatch：asset 无路径时用 memfd 只读 fd（输入例外）
relay：App 先监听 loopback；脚本内有界等待；无 marker
```

验收：

```text
无 stage()/copyTo()/extract() 调用
每种载荷在无 KSU su 时仍执行
```

### B8 日志常驻与清理

```text
日志文件：run-<epochMillis>.log，每次 run 一个，保留
drainer：整行追加 + fdatasync；无截断、无上限
完成/超时/中止：停止 drainer，删除 run.sh；日志保留
panic：日志保留；下次启动只删除残留 run.sh
启动：删除残留 run.sh；历史日志不清理、不覆盖

LogFile/FileSink：攻击期零 ghostlock-run.log 写入
LogExport：只从内存或历史日志显式导出（流程外）
AttackService：删除 attack.pid；中止停 drainer 并删 run.sh
BootSettings：attempts 内存化或移出流程
RootChannelClient：移出攻击链
```

验收：

```text
rg 'attack\.pid|ghostlock-run\.log|LogExport\.open' app/src/main/kotlin/com/ghostlock/app/attack → 0
单测：两次 run 的日志文件名不同；完成后日志存在、run.sh 不存在
```

### B9 探针与门禁

```text
效果探针：
  manager：/system/bin/su 存在、/sys/module/kernelsu
  lkm    ：/proc/modules 或 /sys/module/<name>
  app    ：能观测则 verified，否则 unknown
  cmd    ：能观测则 verified，否则 unknown

静态守卫（必须能失败）：
  只允许 App 侧 run.sh 与 run-<epochMillis>.log 两个写点
  生成脚本内除 LOG 外出现文件重定向 ⇒ 测试红
  其余写文件 API 或信号文件 ⇒ 测试红

真机审计：
  冷机 hook=1
  运行中：files/finish 只有 run.sh 与本次日志
  完成后：run.sh 不存在；日志保留且非空
  panic 演练：强杀进程后重启，日志保留、run.sh 被清理
  adb shell dmesg | grep 'ghostlock: umh exec returned' → 0
  /data/local/tmp 攻击前后差集为空
```

## 5. 数据/控制流差异

```text
v1：App → pending.sh/params → root 扫目录 → 写 exit/log
v2：App → memfd → 补丁 → root 读 fd
v3：App → 脚本 + argv → 补丁
v4：App → 整脚本进 knob → 补丁
v5：App → run.sh + 路径 → root 执行 → root 写 run.log
v6：App → run.sh + 路径 + log memfd → root 只写 memfd → App 完成后转写
v7：App → drainer 并行写 run.log + fdatasync → 1 MiB 截断 → crash 转存
v8：App → drainer 并行写 run-<ts>.log + fdatasync → 无上限、常驻
```

兼容性：

```text
· 攻击原语、sequence、PageCacheAccess 数据面不变
· 收尾选项不变（restore_selinux / soft_reboot）
· 旧协议只作为删除对象，不保留 fallback
```

## 6. 回滚

```text
· 任一批失败：git revert 该批，不跨批
· B2/B3 成对回滚（root 侧与 Kotlin 判据）
· B5/B6 成对回滚（收尾语句与 LKM knob）
· v1–v7 实现不恢复：失败时回到设计评审
```

## 7. 明确保留（不做）

```text
· 不重写 43284 primitive / ESP / splice
· 不重写 PageCacheAccess / AssembledPageCacheAccess
· 不新增 C++ access session
· profile wire / KMI 选择规则保持不变
· 不恢复 Lua / CLI / 旧 tempui
· 不把 FolkPatch 和 KernelSU 混装
· hook 6 槽位与 carrier 选择保持不变
```

## 8. 验证矩阵

| 批次 | 离线 | 构建 | 真机 |
|---|---|---|---|
| B1 | ScriptFile/LogMemfd 单测 | compile | 不需要 |
| B2 | `rg` 0 | assemble | 不需要 |
| B3 | 禁用串 0 + 单测 | compile | 冷机 hook |
| B4 | 完成信号/清理单测 | compile | 内核视图 |
| B5 | 收尾语句断言 | compile | dmesg exit |
| B6 | KnobPatchTest + 8 镜像 | buildLkmImages | UMH run.sh + uid=0 |
| B7 | 无 staging 断言 | assemble | 四载荷执行 |
| B8 | 日志命名/常驻单测 | compile | 日志保留 |
| B9 | 探针三态单测 | — | 运行中两文件、完成后日志在 |

## 9. DoD

- [ ] B1–B8 离线全绿；
- [ ] 源码与脚本中固定 marker 引用为 0；
- [ ] Attack 流程只有 App 写 `run.sh` 与 `run-<epochMillis>.log`；子进程零文件写入；
- [ ] 日志经 memfd 到 App，drainer 并行写盘并 `fdatasync`；
- [ ] 日志无上限、无截断；每次 run 一个独立文件；
- [ ] 正常完成、失败、超时、中止都删除 `run.sh`，日志保留；
- [ ] panic/强杀后重启日志保留、残留 `run.sh` 被清理；
- [ ] 四种载荷只通过 `PayloadRequest` 列表渲染进脚本；
- [ ] 脚本正文只存在于 `run.sh`；`.ko` 只携带路径；
- [ ] 无任何信号文件（exit/marker/ready/pid/request/status/socket）；
- [ ] 收尾语句在脚本内，LKM `restore_enforce=0`；
- [ ] 终态只由内核视图与效果探针给出，三态都有具名路径；
- [ ] LKM knob 默认 inert，8 个镜像校验通过；
- [ ] 冷机 hook=1，`dmesg` 出现 `umh exec returned 0`；
- [ ] 设备效果与终态一致；未知不标成功；
- [ ] KernelSU 作为可选载荷，其效果单独记录；
- [ ] 运行中只有脚本与本次日志；完成后日志保留；
- [ ] 设备日志归档。
