# 退役 runner 复活判定（任务 B · 只读勘察）

> 基准＝成功那次：`ref` 6f7ded87（2026-10-04「43284 full chain PASS with self-built LKM」，见 `docs/analysis/chain-comparison.md:3`）。
> 本文为只读勘察：未跑构建、未改代码；唯一写入＝本文件。
> 设备实测（2026-10-16 20:45，`adb -s QV770MFGJ1`，内核 `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，与 PASS 同机）：`/proc/modules` 无 `kernelsu`（**当前无 root**）、`su` 不在 PATH、`/dev/dfm0|dfm1|df` 均不存在、Shizuku server 在跑、`com.ghostlock.app` versionName=1.3（现实现）。

## 一、成功那次的运行载体

| 项 | 事实 | 证据（`git show 6f7ded87:<path>`） |
|---|---|---|
| 入口 | `DevCve43284Activity`，属 **app 模块**（`app/src/main/kotlin/com/ghostlock/app/dev/`） | `app/src/main/kotlin/com/ghostlock/app/dev/DevCve43284Activity.kt:37` |
| 注册 | 清单 `exported=true`、`enabled=@bool/dev_cve_43284_enabled`；debug 源集 true、main false ⇒ **只有 debug APK 起得来** | `app/src/main/AndroidManifest.xml:33-38`；`app/src/debug/res/values/bools.xml:3`；`app/src/main/res/values/bools.xml:7` |
| 启动方式 | `adb shell am start`，extra＝`DEV_CONFIRM=true`＋`MODULE`＋`TARGET`＋`STAGE`（默认 write）＋可选 `DEV_ALLOW_TARGET` | `app/src/main/kotlin/com/ghostlock/app/dev/DevCve43284Activity.kt:22-26`、`:127-134` |
| 自锁 | 非 debug 拒、缺 `DEV_CONFIRM` 拒、`MODULE`/`TARGET` 空拒 | 同上 `:41-50`、`:55-59` |
| 依赖 | **Shizuku 必需、不是可选**：非 READY 直接 return；要求 `uid=2000` 且 UserService `Seccomp=0` | `DevCve43284Activity.kt:74-92`；`app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuExploitRunner.kt:36`；`app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt:45-51` |
| 载体链 | ① App 进程内建 IpSec SA → ② 84 B 会话帧 → ③ Shizuku UserService 起 native（`<nativeLibraryDir>/libghostlock.so`，**可执行 ELF、非 JNI**）→ ④ stdin 只写该帧 | `DevCve43284Activity.kt:93-97`、`:107-112`；`GhostlockUserService.kt:186`、`:213-218`；`AGENTS.md:15` |
| native argv | `--run-cve-2026-43284 <ko> <carrier> --stage=<stage>（plan / write / trigger / full）[--allow-dev-target]` | `app/src/main/kotlin/com/ghostlock/app/shizuku/Staged43284Args.kt:28-35`；分阶段入口 `src/core/backend/cve_2026_43284/stage_runner.cpp:825`、帧从 stdin 读 `:969` |
| 判据原文 | `written=769 verified=769 … hook=0` / `run.wait outcome=LkmLoaded terminus=1` / `uid=0`+`Enforcing`+`kernelsu=1` | `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:15-22`、`:29-31`、`:37-42` |

## 二、最小可运行集合：只编 runner 可行，能不能 adb 直跑取决于「谁造 SA」

- native 侧是**独立可执行**（`AGENTS.md:15`），单目标即可：`6f7ded87:src/Makefile:1`（API=35）、`:116`、`:136`（`BUILD_ROOT ?= ../build` `:86`、`NATIVE_BUILD_DIR := $(BUILD_ROOT)/native` `:87` ⇒ 产物 `build/native/ghostlock`）、`:142-149`；Gradle 同形任务与产物路径 `6f7ded87:build.gradle.kts:93-105`。本机两个 NDK（28.2/30.0）都自带 `aarch64-linux-android35-clang` ⇒ **不需要旧 NDK**。
- `.ko` **不必重编**：成功模块仍在两地且同哈希——本地 `build/app/lkm/android13-5.15/ghostlock.ko` 与设备 `/data/local/tmp/ghostlock.ko`，均 12304 B、sha256 `d557fe11aaa51afdbb43fceda164ed3fd4a52305383fdf22e6f02c85af49e678`（＝PASS 记录 `docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md:10`）；重建它才需要 podman/docker（`app/build.gradle.kts:496-502`）。
- 帧与 SA 两条路，**runner 本身既不需要 APK 也不需要 Shizuku**：
  - **A 路（adb-only，需 root）**：root 合成 XFRM SA＋`espencap` 持有 UDP 封装端口＋88 B 帧，随后直接跑 runner。做法与命令形状：`docs/analysis/43284-production-adb-harness.md:10-17`、`:44-63`。runner 侧 socket 由帧参数决定（bind `sender_port`、connect `encap_port`，均 loopback）——`6f7ded87:src/core/backend/cve_2026_43284/stage_runner.cpp:101-134`。
  - **B 路（无 root ⇒ 必须 APK+Shizuku）**：只有 App 能凭 `IpSecManager` 自己造 SA（`DevCve43284Activity.kt:93-97`）。

**A 路精确命令**：

```sh
git clone <repo> /tmp/glk-ref && git -C /tmp/glk-ref checkout 6f7ded87
(cd /tmp/glk-ref/src && ANDROID_NDK_HOME=$NDK make ghostlock)   # → /tmp/glk-ref/build/native/ghostlock
adb push /tmp/glk-ref/build/native/ghostlock /data/local/tmp/glk-ref
adb push build/app/lkm/android13-5.15/ghostlock.ko /data/local/tmp/ghostlock.ko   # 12304 B / d557fe11…
# SA + espencap + 帧：见 43284-production-adb-harness.md §5（root），跑前 rm -f /dev/df /dev/dfm0
adb shell "cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp ./glk-ref \
  --run-cve-2026-43284 /data/local/tmp/ghostlock.ko /vendor/lib64/libbinderdebug.so \
  --stage=write < /data/local/tmp/frame.bin"
```

**B 路精确命令（原样复跑）**：`./gradlew :app:assembleDebug`（退役树）→ `adb install -r`（debug APK）→ 启动 Shizuku（`adb shell sh …/start.sh` 或无线调试）并授权 → §一 的 `am start`。注意 applicationId 相同（`6f7ded87:app/build.gradle.kts:67`）⇒ 会**覆盖现实现 APK**，装回需反向再 install。

## 三、阻碍与替代

| # | 阻碍 | 证据 | 处置 |
|---|---|---|---|
| 1 | 设备**当前无 root** ⇒ A 路暂不可用（`ip xfrm` 要 CAP_NET_ADMIN） | 本次实测：`/proc/modules` 无 kernelsu、无 `su`；SA 合成方式 `43284-production-adb-harness.md:46-49` | 先用 B 路跑一次，让链自己加载 KernelSU（终态＝ksu 就绪），此后 A 路可反复 |
| 2 | SA 与 encap socket **不跨重启** | `43284-production-adb-harness.md:17`；`docs/analysis/device-gates/43284-production-20261005-pass.md:68` | 每跑前重建（设备上已有 `/data/local/tmp/mk_sa.sh`） |
| 3 | 退役链有**真实副作用**：769 块写 `/vendor/lib64/libbinderdebug.so`、patch#1 写 `/apex/com.android.runtime/bin/crash_dump64` | `…full-chain-lkm-pass.md:45-51`；AVB files-check 会留 `[FAIL]` 记录 `43284-production-20261005-pass.md:44-47` | 双跑排序（见 §四末）＋跑后复核；不要在「要求干净机」的门禁里混跑 |
| 4 | 与 2026-10-15 裁定冲突：43284 **必须从 app 自己 uid 起链、不许经 Shizuku** | `docs/analysis/43284-app-uid-design.md:3`（另 `:32`：没有一步需要 shell 特权） | 退役路径只能作**对比夹具**，不得回生产；按 AGENTS 留沿革/技术债登记 |
| 5 | 设备上疑似已有退役 runner 二进制：`glk-b59h1`（10-04 18:51）、`glk-plan2`、`glk-fix2`，三者都含 `run-cve-2026-43284` 与 `cve43284-hook-guard` 字样 | 本次设备 `grep -a` 实测 | 可省重建，但**构建修订未验**：先与 A 路重建物比对大小/sha256/关键字符串再采信 |
| 6 | 退役侧写路径**无逐块日志** ⇒ 逐行对齐缺一半数据 | `6f7ded87:src/core/backend/cve_2026_43284/steps/chain.cpp:243-267`（只有计数与错误枚举） | 见 §四：先做聚合对齐；要逐块则在**对比专用副本**里加打印（不改本仓库） |

**替代方案（不复活旧树）**：现树自带同形夹具 `core/src/tools/pagecache_load_lkm.cpp`——GL_PROBES 探针（`core/CMakeLists.txt:40`、`:268-272`），帧来自 `/data/local/tmp/ghostlock-sa.frame` 或 `--frame-stdin`（`:116`、`:122-126`），设备上已有现成二进制 `/data/local/tmp/pagecache_load_lkm`。它就是「把退役链 write/trigger/wait 段搬进探针」的现成版本；若要上 Kotlin 侧，现实现 JNI 已给 `write_image`/`hook`/`wait` 三个口（`core/src/jni/api_43284_device.cpp:1021-1042`、`:1128`、`:196-198`）。

## 四、双跑日志对比方案（逐行对齐字段）

| 字段 | 退役侧（6f7ded87） | 现实现 | 对齐做法 |
|---|---|---|---|
| 阶段名 | `run.cve_2026_43284 stage=` 与 `run.module/target/hook/chain/trigger/wait` 记录（`stage_runner.cpp:650-652`、`:724-786`） | 探针 `example: phase …`（`pagecache_load_lkm.cpp:277`、`:343`、`:372`、`:396`、`:438`）；JNI op 名 `api_43284_device.cpp:1021`、`:1128`、`:198` | 建映射表：plan↔phase1-2、write↔phase3a+5+3、trigger↔phase4a、wait↔phase4b（现探针编号与链序不同：patch#1=3a、hook=5、载体=3） |
| 块号/偏移/长度 | 仅聚合 `written=`/`verified=`（`stage_runner.cpp:732-735`），逐块无打印 | `block[%zu]: wrote=x/y offset=…`（`…/steps/pagecache_steps.cpp:246`）、`landed tries=…`（`:269`）、`verify failed: want/got`（`:277`）、`block_at=`（`:284`） | 逐块需在对比副本加打印；否则按「首个不符块」聚合对齐 |
| seq | **故意不打印**：记录只带路径/计数/枚举（`src/core/backend/cve_2026_43284/stage_runner.hpp:27-28`） | `initialize: adopt seq=%u present=%d`（`…/kernel_access.hpp:78`）、`initialize received: seq=… spi=… encap=… sender=…`（`api_43284_device.cpp:867`） | 共同基准改用 SA 反重放计数（root `ip -s xfrm state`），跑前/跑后各取一次 |
| ICV 长度 | 帧强校验 `icv_len=16`，不符 fail-closed（`…/session_frame.hpp:22`、`:35`） | `landing: bring_up … icv=%u`（`api_43284_device.cpp:747`） | 帧字段 ↔ 打印字段逐跑对齐 |
| 返回码 | 进程退出码 0/2/3/4/5/6（`stage_runner.cpp:790-823`） | JNI 返回码＋探针 `example: phase … refused` | 两张码表先映射，再逐阶段比 0/非 0 |
| 标记文件 | `/dev/dfm1`⇒Failed、`/dev/dfm0`⇒LkmLoaded、`/dev/df`⇒非终态、`/sys/module/dirtyfrag` present→gone⇒Failed（`6f7ded87:…/real_ops.cpp:405-424`） | 同表（常量 `pagecache_load_lkm.cpp:141-144`）＋`wait_for_module -> %s`（`api_43284_device.cpp:198`） | 每阶段前后 `ls -l` 三标记，两侧同口径 |

- 采集落盘：adb-only 路＝stdout 重定向；App 路＝`.ghostlock_native.log`＋logcat（`GhostlockUserService.kt:192`；现实现侧 `app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:37-48` 用 logcat 标签；归档口径 `AGENTS.md:86`）。
- 每跑固定动作（对齐前提）：跑前 `rm -f /dev/df /dev/dfm0 /dev/dfm1`，跑后 `getenforce`、`grep -c kernelsu /proc/modules`（复核口径 `…full-chain-lkm-pass.md:37-42`）；两侧之间建议重启一次以清页缓存/标记，再分别采日志。

## 五、结论

- **可复跑**：退役 native runner 是独立可执行，单目标可编（`6f7ded87:src/Makefile:142-149`），不需要 APK、不需要 Shizuku；`.ko` 与设备镜像同哈希，无需重编。
- **但「adb 直跑」卡在 SA**：帧/端口只是参数，SA 必须先在核内（root `ip xfrm` 或 App 的 `IpSecManager`）。当前设备无 root ⇒ 先跑一次退役链（B 路，需 debug APK＋Shizuku）让 KernelSU 就绪，之后的对比跑可全程 adb-only（A 路）。
- **代价与红线**：B 路会覆盖现实现 APK（同 applicationId）；链本身会永久改动 `/vendor` 载体与 `/apex` 的 crash_dump64；且与 2026-10-15「不许经 Shizuku」裁定冲突 ⇒ 退役路径只能作对比夹具，双跑记录按 AGENTS 归档并登记技术债。
