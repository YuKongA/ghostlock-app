# GhostLock 载荷 / 收尾单向链设计（v8）

- 日期：2026-10-19
- 状态：设计 v8 定稿候选；**在 v7 上取消日志上限并改为正常保存**
- 裁定来源：用户 2026-10-19 系列裁定，最后一条「log 大小无上限，log 正常保存，存在 app 文件夹里」
- 级别：L（攻击关键路径 + 跨 Native/Kotlin 收尾协议 + LKM 载荷数据契约）
- 关联：`docs/plan/43284-production-engine-plan.md`（P3/P4/P5）、`docs/plan/payload-finish-chain-plan.md`（批次）、`docs/development/full-process-uml.md`

## 0. 修订沿革

| 版本 | 日期 | 脚本 | 日志 | 落盘方 |
|---|---|---|---|---|
| v1 | 2026-10-19 | runId 目录文件 | runId 目录文件 | App + root |
| v2 | 2026-10-19 | memfd | memfd | 无 |
| v3 | 2026-10-19 | argv | argv | 无 |
| v4 | 2026-10-19 | 进 knob | 无 | 无 |
| v5 | 2026-10-19 | run.sh | run.log | root 直写 |
| v6 | 2026-10-19 | run.sh | memfd → App | 仅 App |
| v7 | 2026-10-19 | run.sh | memfd → App，1 MiB 上限 | 仅 App，panic 转存 |
| v8 | 2026-10-19 | run.sh | memfd → App，**无上限，正常保存** | 仅 App，日志常驻 |

v1–v7 的实现只作为 git 历史，不作为基线。

## 1. 裁决

1. **落盘白名单**：攻击流程只允许 App 写 `run.sh` 与 `run-<时间戳>.log`。
2. **root 零落盘**：子进程（UMH / 脚本）零文件写入；日志只写 memfd。
3. **并行落盘**：App 在攻击进行中用独立线程持续 drain + `fdatasync`，panic 时已同步的日志留在盘上。
4. **日志无上限、正常保存**：不设 1 MiB 上限、不做截断；每次 run 一个独立日志文件，完成后保留。
5. **脚本是临时物**：`run.sh` 在完成后删除；panic 残留的 `run.sh` 在下次启动删除。
6. **无信号文件**：exit、marker、ready/connected、pid、request、status、socket 文件全部取消。
7. **终态**：内核视图＋效果探针；日志只做事后证据。
8. 攻击原语、hook 6 槽位、carrier 选择、`insmod` 路径保持不变；内核侧只加 `knob` 数据块。
9. KernelSU 仍是可选载荷；其效果单独记录。
10. 旧实现整体作废，不保留双路径。

## 2. 落盘白名单

**攻击流程**：从 `:attack` 收到 START 到终态判定与清理结束。

允许写入（仅 App，且只在 app 私有目录）：

```text
files/finish/run.sh                App 写入的脚本正文（完成后删除）
files/finish/run-<epochMillis>.log 本次 run 的日志（无上限，保留）
files/boot-attempt               开机重试计数（流程外状态，不是攻击信号）
```

- 日志文件名按开始时间生成，互不覆盖；panic 后原样保留。
- 子进程写入目标只有 log memfd 与内核日志（`pr_info`）。
- 不得写信号文件（exit / ready / connected / marker / pid / request / status / *.sock）、prefs、外部目录、`/data/local/tmp`；不得 staging 复制或解压 asset；不得扫描目录判断状态。

允许读取：

```text
· APK assets、nativeLibraryDir、/data/adb 模块、用户显式选择的输入文件
· 内核视图：/proc/modules、/sys/module/*、/proc/<pid>/cmdline（本进程）、dmesg（门禁侧）
· 页缓存改写（primitive 本身；重启复原）
```

## 3. 现有落盘点与处置

| # | 落点 | 生产者 | 处置 |
|---|---|---|---|
| 1 | `files/finish/runs/<runId>/pending.sh` | `FinishScriptStore` | 改为固定 `run.sh`；完成后删除 |
| 2 | `.../pending-<stamp>.sh` | 同上 | 取消 |
| 3 | `.../finish.params` | `FinishParams` | 取消；取值内联进脚本 |
| 4 | `.../exit` | `root_cmd.sh` | 取消；终态改效果探针 |
| 5 | `.../finish.log` | supervisor 直写 | 改为 log memfd；App 转写 `run-<ts>.log` 并保留 |
| 6 | `files/finish/pending.sh` | `FinishScriptStore` | 取消 |
| 7 | 外部 `finish/finish.params` | `FinishParams` | 取消 |
| 8 | `finish/root/<binary>`、`script.sh`、`custom.ko`、`folkpatch.ko` | staging | 取消；直取原路径 |
| 9 | `/data/local/tmp/.ghostlock_lkm_cmd.sh` | `deployUmhPayload` | 取消；路径进 `.ko` knob |
| 10 | `.ghostlock_finish_supervisor.sh`、`.ghostlock_lkm.log`、`.ghostlock_root.pid` | `root_cmd.sh` | 取消（脚本整体删除） |
| 11 | `ghostlock-lkm-ready` / `-connected`、`root-channel.sock` | relay / RootChannel | 取消 |
| 12 | `attack.pid`、`ghostlock-attempts` | `AttackService` / `BootSettings` | 取消；内存态替代 |

## 4. 两条通道

```text
脚本通道（App→root，文件）：
  App：FinishScript.render → 写 files/finish/run.sh（0644）
       knob.script_path = "<绝对路径>"
  root：argv = { "/system/bin/sh", "<路径>", NULL }

日志通道（root→App，memfd）：
  App：log_fd = memfd_create("glk-log")
       render 时把 LOG=/proc/<pid>/fd/<log_fd> 写进脚本
       drainer 线程：每 50 ms 读新增 → 追加 run-<epochMillis>.log → fdatasync
  root：exec 3>>"$LOG"；printf ... >&3
```

- 脚本正文只存在于 `run.sh` 与内存；`.ko` 只带路径。
- 日志 memfd 不落盘；App 是唯一磁盘写入方。
- 路径长度 ≤255，绝对路径，NUL 结尾；模块校验首字节 `/`。
- 单 run 由 App 内存锁保证；`run.sh` 在触发前覆盖写。
- 脚本可读性：app 私有目录 root 可读；SELinux 已由模块置 permissive。

## 5. LKM knob（唯一内核侧改动）

```c
struct glk_knob {
    char magic[8];          /* "GLKKNOB1" */
    u32  size;              /* sizeof(struct glk_knob) */
    u32  permissive;
    u32  defex;
    u32  restore_enforce;
    u32  path_len;
    char script_path[256];  /* 默认 "/dev/null" */
};
static struct glk_knob knob = {
    "GLKKNOB1", sizeof(struct glk_knob), 1, 0, 0, 9, "/dev/null"
};
```

- App 在 `LkmSelector.read()` 返回的镜像里定位唯一 `GLKKNOB1`，校验 `size`，写入路径与 flags。
- 模块校验：magic、size、`path_len < 256`、`script_path[path_len]==0`、首字节 `/`。
- 默认 `/dev/null`：开机模块装载路径执行空脚本，立即退出，inert。
- 镜像体积基本不变（多 256 字节路径槽）。

## 6. 执行与终态

```text
App：渲染脚本 → 写 run.sh → 建 memfd → 启动 drainer → 补丁 → 触发
root：sh /data/user/0/com.ghostlock.app/files/finish/run.sh
      载荷 → 收尾 → 退出；日志只进 memfd
完成信号：/proc/modules 或 /sys/module/ghostlock 由在到无
终态：效果探针 → verified / failed / unknown
清理：最后一次 drain + fdatasync → 停止 drainer → 删除 run.sh
      日志文件保留
```

判据：

```text
整体成功 = 用户启用的关键载荷全部 verified，且没有 failed
部分未知 = 无 failed，但存在 unknown ⇒ 整体记 unknown，不标成功
失败     = 任一关键载荷 failed，或完成信号超时
```

- 日志不参与完成判定：drainer 只负责把 memfd 搬到盘上。
- 脚本可在日志里写 `exit=<code>`；该行是证据，不作为信号。
- 数值 exit 同时在 `dmesg`：`ghostlock: umh exec returned N`；门禁侧 `adb` 取证。
- 不读 exit 文件、不 tail 日志判状态、不扫目录、不轮询 marker。

## 7. 载荷输入

| 载荷 | 输入 | 脚本里的形态 |
|---|---|---|
| root（KernelSU） | `nativeLibraryDir/libksud.so` 或 `/data/adb/ksu/bin/ksud` | 直接 exec 的字面量 |
| app | 包名 | `monkey` / `am start` 的参数 |
| cmd | 用户命令原文 | 整行插入 |
| lkm | 绝对路径 | `insmod` 的字面量 |
| FolkPatch | APK asset 内的 ko | 无路径可用时用 memfd 只读输入（输入例外） |
| relay | `/dev/glk` | App 先监听 loopback；脚本内有界等待；无 marker |

SAF 选择：只接受可解析为 `/data/media/0/...` 的 URI，直接作为脚本里的路径；其余 provider 以具名原因结束，不复制文件。

## 8. 失败与清理

| 阶段 | 失败 | 终态 | 清理 |
|---|---|---|---|
| 渲染/写脚本 | 写失败 | `prep-failed` | 删除 run.sh；日志文件为空或不存在 |
| memfd | 创建失败 | `prep-failed` | 删除 run.sh |
| knob 定位 | 缺失/重复/超长/路径非法 | `prep-failed` | 删除 run.sh |
| 触发 | 模块未进入 init | `trigger-failed` | 删除 run.sh |
| 完成信号 | 超时 | `timeout` | 最后 drain → 删除 run.sh；日志保留 |
| 载荷 | 探针与期望相反 | 该载荷 `failed` | 同上 |
| 载荷 | 不可观测 | 该载荷 `unknown`（不标成功） | 同上 |
| **panic** | 进程随内核消失 | 以已 fsync 的日志为准 | `run.sh` 与日志都留在盘上；下次启动删 `run.sh`，日志保留 |
| 软重启 | 进程随 zygote 消失 | 以重启前记录为准 | 同上 |
| 中止 | 用户中止 | `aborted` | 停止 drainer → 删除 run.sh；日志保留 |

原则：不重试、不伪造、不遗留脚本；一次 run 一个日志文件。

## 9. 日志

- **子进程零文件写入**：脚本只有 `exec 3>>"$LOG"` 与 `printf ... >&3`，`LOG` 是 `/proc/<pid>/fd/<log_fd>`。
- **并行落盘**：drainer 在触发前启动，每 50 ms 读 memfd 新增字节，整行追加日志文件，随后 `fdatasync`。
- **无上限**：memfd 与日志文件都不截断；写多少留多少，占用由设备内存与存储承载。
- **panic 语义**：内核 panic 时 memfd 随内核内存消失；已 `fdatasync` 的行留在盘上，最多丢最后一个 50 ms 批次。
- **正常完成**：最后 drain + `fdatasync` → 停止 drainer → 删除 `run.sh`；日志保留。
- **下次启动**：删除残留 `run.sh`；历史日志按文件名保留，不清理、不覆盖。
- 持久化导出不需要额外动作：日志本身就是文件；界面读同一文件。
- 模块仍以 `pr_info` 写 `dmesg`，与 memfd 日志互相独立。

## 10. 中止

- 删除 `attack.pid`；`:attack` 在自己的进程内收到 `ACTION_ABORT`，kill 自身与子进程，停止 drainer，删除 `run.sh`，日志保留。
- 中止不依赖文件，不跨进程读 pid。

## 11. 影响文件

```text
tools/lkm/ghostlock/ghostlock.c            knob + 路径执行 + inert 默认
tools/lkm/ghostlock/ghostlock.glk.c        同上
tools/lkm/ghostlock/root_cmd.sh            删除
app/build.gradle.kts                       删除 copyPayloadScriptIntoAssets
app/.../engine/pagecache/LkmSelector.kt    返回可变副本
app/.../engine/pagecache/LkmImagePatch.kt  新增：magic 定位、路径写入、校验
app/.../engine/pagecache/TypedBackendProbe.kt  删除 deployUmhPayload / MODULE_PATH 生产路径
app/.../backends/FinishScript.kt           整脚本渲染（载荷 + 收尾 + LOG 路径）
app/.../backends/FinishScriptStore.kt      写 run.sh、删除；日志文件名生成
app/.../backends/LogMemfd.kt               新增：memfd、drainer、fdatasync、无上限
app/.../backends/FinishParams.kt           删除；取值并入 FinishScript
app/.../backends/FinishExecution.kt        重写：脚本、memfd、补丁、触发、drain、清理
app/.../backends/PayloadEffects.kt         新增：效果探针（模块/su/app/relay）
app/.../backends/PayloadRequests.kt        直取路径；去掉 LOG 与 marker
app/.../backends/FinishUi.kt               保存按钮改预览
app/.../core/access/RelayAccessService.kt  删除 ready/connected
app/.../core/rootchannel/RootChannelClient.kt  移出攻击链
app/.../log/LogFile.kt / FileSink.kt / LogExport.kt  攻击期内存环；导出仅流程外
app/.../attack/AttackService.kt            删除 pid 文件与自动导出
app/.../attack/BootSettings.kt             attempts 内存化或移出流程
docs/development/full-process-uml.md       收尾序列图与状态机（B1 同批）
```

## 12. 验证

**静态守卫**（必须能失败）：

```text
只允许两个写点：files/finish/run.sh、files/finish/run-<epochMillis>.log（均在 App 侧）
生成脚本内不得出现文件重定向，唯一例外是 LOG=/proc/<pid>/fd/<n>
攻击包内不得出现：writeText/writeBytes/outputStream/copyTo/mkdirs/deleteRecursively/
                  ParcelFileDescriptor.open/FileOutputStream/LogExport.open
                  （两个白名单写点除外）
禁用字符串：ghostlock_lkm_ok|ghostlock_lkm_fail|dfm0|dfm1|ghostlock-lkm-ready|
            ghostlock-lkm-connected|root-channel.sock|attack.pid|finish/runs|
            pending.sh|finish.params|finish.log
```

**单测**：

```text
ScriptRenderTest     四载荷顺序、收尾语句、路径转义、LOG 指向 memfd
ScriptFileTest       写 run.sh、覆盖旧文件、完成后删除、失败路径删除
LogMemfdTest         增量读取、部分行、无上限、fdatasync 调用、文件名互不覆盖
KnobPatchTest        fixture 镜像：唯一 magic、路径写入、len 校验、重复 magic 拒绝
EffectProbeTest      verified/failed/unknown 三态映射
```

**真机门禁**：

```text
冷机 hook=1
运行中：files/finish 下只有 run.sh 与本次 run 的日志
完成后：run.sh 删除；日志保留且内容完整
panic 演练：强杀进程后重启，本次日志保留、run.sh 被清理
/proc/modules 或 /sys/module/ghostlock 的出现在到无
adb shell dmesg | grep 'ghostlock: umh exec returned'  → 0
/data/local/tmp 攻击前后差集为空
```

## 13. 待决问题

1. `/proc/modules` 与 `/sys/module/*` 在 untrusted_app 域的可读性；读不到时完成信号退化为效果探针 + 超时。模拟器实测：untrusted_app 打开 `/proc/self/fd/<n>` 被 EACCES（O_CREAT 与 O_APPEND 都拒），而 `adb root`（su 域、Enforcing）`cat /proc/<app_pid>/fd/<n>` 成功，且 root 执行 `run.sh` 后日志 memfd 收到 `finish start` / `finish done`；UMH 域仍需真机证据。
2. `dmesg` 在非 root 上不可读：exit 证据只在门禁的 `adb` 侧成立；App 侧不承诺 exit。
3. 效果探针覆盖：KSU 与 lkm 可观测；`app` 与 `cmd` 多数情况下只能记 `unknown`，是否接受。
4. 8 个 KMI 镜像的 `knob` 偏移与镜像签名：当前镜像无 `~Module signature appended~`，仍需逐镜像核对。模拟器 5.15.119 与 DDK 5.15.202 的 vermagic 不一致，运行时 retag 容量 80B < 所需 101B；用改 `UTS_RELEASE` 重建的镜像可手工 insmod（UMH 已验证），但那是门禁夹具，不是生产镜像。
5. FolkPatch asset 的 memfd 输入是输入例外；是否改为安装期既有文件。
6. 日志无上限：长期运行会持续占用存储，清理策略由用户决定（界面可提供手动清理）。
7. 日志文件名用 `epochMillis` 或 `yyyyMMdd-HHmmss`，二选一固定。
8. `FinishUi`「保存脚本」按钮是否保留为预览功能。
9. 模拟器完整链：vendor 载体写入 `verify-read-failed`、`hook=0`；hook landing 0xFE628 超过模拟器 libc++ 的 671496 B，固定 plan 不适用。DFRoot 的 `libdfroot.so` 用 `vmsplice`/`pipe`/`splice` + 动态 code cave + 自带 LKM，走的是另一条原语；手工/KSU insmod 已验证 UMH 与脚本/日志，但 `hook=1` 仍需真机或移植该原语。
