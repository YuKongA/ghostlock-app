# 归档说明

- 原路径：`docs/plan/kotlin-clean-architecture-ui-plan-annex-b.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：completed/current-fact plan superseded by root execution authority；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Kotlin Clean Architecture 计划附件 B：验证与失败恢复数据

> 从属于 `kotlin-clean-architecture-ui-plan.md` v5；只定义命令、输入、结果和阈值。

## 0. D0 可重复采集命令

D0 按 §4 在最终树直接留证；tool 独占 `Pixel_5/default_boot`/`emulator-5580`，`DEVICE_SERIAL`=A301SO；资源缺失/冲突、skip、数量错均 fail且不换路。

1. `python3 tools/kotlin_architecture_d0.py inventory --repo "$PWD" --out app/src/test/resources/architecture`；预期 `D0_INVENTORY=PASS COMPONENT_FILES=71 COMPONENT_API_ROWS=<positive> API_KEY_COLLISIONS=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 UNCLASSIFIED=0`。分类后 `python3 tools/kotlin_architecture_d0.py verify --repo "$PWD" --fixtures app/src/test/resources/architecture`，预期 `D0_VERIFY=PASS ... COMPONENT_UNCLASSIFIED=0 API_MAPPING_INVALID=0 ... D0_CAPTURE=68 DEFERRED_D5=20`；均 exit0。
2. `./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --console=plain`；预期 `BUILD SUCCESSFUL`/exit 0；记录 absolute `DEBUG_APK/TEST_APK` 及SHA，禁手装。
3. capture 唯一入口：`python3 tools/kotlin_architecture_d0.py capture --avd Pixel_5 --snapshot default_boot --port 5580 --debug-apk <absolute-debug-apk> --test-apk <absolute-test-apk> --matrix app/src/test/resources/architecture/ui-surface-matrix-v1.tsv --class com.ghostlock.app.architecture.UiBaselineCaptureTest --out build/reports/kotlin-architecture-d0`。tool 从 `local.properties sdk.dir` 解析绝对 emulator/adb；启动参数必须逐字为 `-avd Pixel_5 -snapshot default_boot -force-snapshot-load -no-snapshot-save -no-snapshot-update-time -read-only -no-window -no-audio -no-boot-anim -gpu swiftshader -port 5580`。
4. 启动前校验：config SHA=`df8ee6515740419d19827e3875887ed9251a1bb36e0b9470bbf034f8802f5d53`；snapshot regular files按 filename排序，每行 `name<TAB>bytes<TAB>sha256<LF>`且尾随LF，必须 `FILES=5 BYTES=3063024640 MANIFEST_SHA=9a474d743d26fee0a1d5c9846cb887c5980d1a27f3302131268ecdaf94e26c02`；build.prop SHA=`5b6526736e4db77700c5b21ff602858d4b42ac355ca0b5101f6695467a4a168a`。boot后 AVD name=`Pixel_5`、fingerprint=`google/sdk_gphone64_arm64/emu64a:13/TE1A.240213.009/12342917:userdebug/dev-keys`；任一失配在 install前输出 `D0_CAPTURE=FAIL IDENTITY_MATCH=0 FIELD=<name> EXPECTED=<v> ACTUAL=<v>` 并非0退出。
5. runtime transaction：先把 adbd uid、locale、`wm size/density`（含有无 override）、font、三个 animation、`accelerometer_rotation/user_rotation` 写入 `avd-before.json`。执行 `adb root`；再执行 `setprop persist.sys.locale zh-CN`、`stop`、`start`；每次 root/stop/start/reconnect均 `wait-for-device` 且等 `sys.boot_completed=1`。narrow依次 `wm size 1080x2400`、`wm density 420`、`settings put system font_scale 1.0`、`settings put global window_animation_scale 0`、`settings put global transition_animation_scale 0`、`settings put global animator_duration_scale 0`、`settings put system accelerometer_rotation 0`、`settings put system user_rotation 0`；复读后安装并捕获52行。wide只改 `wm size 1600x2560`、`wm density 320`，复读后捕获M3 16行。
6. active gate除 settings/property复读，还必须由 instrumentation 回报 resource locale=`zh-CN`、display rotation=0、narrow bounds=1080x2400、wide=1600x2560；再只调 production composable/组件，M5a 20行保持 `deferred-D5`。预期 `D0_CAPTURE=PASS SCREENSHOTS=68 SEMANTICS_FILES=68 SEMANTICS_RECORDS=132 DEFERRED_D5=20 SKIPPED=0 IDENTITY_MATCH=1 RESTORE_MATCH=1`，exit 0；禁止复制 renderer或 dispatch。
7. 副作用前原子(temp+rename+fsync)更新 `avd-before.json`：`status=incomplete,avd,snapshot,serial,ports,pid,pid_start,exe,argv,stage,before,hashes`。SIGINT/TERM/HUP只置abort；`finally` 不短路：按stage恢复 locale→stop/start→wait/boot→settings/wm（null则delete、无override则reset）→必要时unroot→wait/boot→复读；错误累计，仍先 `adb emu kill`；adb断链仅对 PID/start/exe/argv 全匹配者 TERM→10s→KILL→5s，确认PID/serial/5580/5581消失，再验原locale/rotation/uid及snapshot/config/build.prop。任一错=`D0_AVD_RESTORE=FAIL FIELD=<name>...`/非0/禁pack；禁写 `config.ini/default_boot`。SIGKILL后运行 `python3 tools/kotlin_architecture_d0.py recover --state build/reports/kotlin-architecture-d0/avd-before.json`；只认owned identity，预期末行 `D0_AVD_RECOVERY=PASS RESTORE_MATCH=1 PROCESS_GONE=1`/exit0。原capture恒FAIL，partial移入 `failed-sigkill-<pid>`，恢复后仍须整步重跑才可pack；identity失配fail-closed。
8. pack：`python3 tools/kotlin_architecture_d0.py pack --input build/reports/kotlin-architecture-d0 --matrix app/src/test/resources/architecture/ui-surface-matrix-v1.tsv --output app/src/test/resources/architecture/ui-goldens-v1.tar`；预期 `D0_PACK=PASS SCREENSHOTS=68 DEFERRED_D5=20 MISSING=0 EXTRA=0 NORMALIZED_MTIME=0 SHA256=<64hex>`，exit 0。
9. M5 walk：先以 `adb -s "$DEVICE_SERIAL" install -r "$DEBUG_APK"`/`install -r "$TEST_APK"` 各得 `Success`；再运行 `adb -s "$DEVICE_SERIAL" shell am instrument -w -e class com.ghostlock.app.architecture.UiBaselineCaptureTest -e suite m5-device-walk com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`；不触发Run，预期 `M5_UI_WALK=PASS MENU=6 CONFIG_CARDS=3`、`OK (1 test)`、exit0。
10. final：`python3 tools/kotlin_architecture_d0.py final --repo "$PWD" --fixtures app/src/test/resources/architecture --goldens app/src/test/resources/architecture/ui-goldens-v1.tar`；预期唯一末行 `D0_FINAL=PASS MANIFEST_SHA_MATCH=5 GOLDEN_SHA_MATCH=1 OUT_OF_SCOPE=0`，exit 0；命令、输入、literal、exit 与 source SHA 直接写入 D0 记录。

## 1. 每子批离线命令与 literal 判据

所有命令在仓根串行运行；任一 exit 非 0 即停，不用后续绿掩盖。

1. Kotlin compile：`./gradlew :app:compileDebugKotlin --console=plain`；输入=最终工作树；预期包含 `BUILD SUCCESSFUL`，exit 0，新增 Kotlin warning=0。
2. unit：`./gradlew :app:testDebugUnitTest --console=plain`；预期 `BUILD SUCCESSFUL`，exit 0。`app/build.gradle.kts` 把 app module 的 `buildDirectory` 固定为根目录下 `build/app`，因此 XML 必须从 `build/app/test-results` 全量读取；`app/build/test-results` 不存在且 0 XML 必须 FAIL。随后运行：

~~~sh
python3 - <<'PY'
from pathlib import Path
import xml.etree.ElementTree as ET
T=F=E=S=0
for p in Path('build/app/test-results').rglob('*.xml'):
    r=ET.parse(p).getroot()
    T+=int(r.attrib.get('tests',0)); F+=int(r.attrib.get('failures',0))
    E+=int(r.attrib.get('errors',0)); S+=int(r.attrib.get('skipped',0))
P=T-F-E-S
print(f'TESTS={T} PASSED={P} FAILED={F+E} FAILURES={F} ERRORS={E} SKIPPED={S}')
raise SystemExit(0 if T>0 and P>=0 and F==0 and E==0 else 1)
PY
~~~

预期 literal：`TESTS=<positive> PASSED=<positive> FAILED=0 FAILURES=0 ERRORS=0 SKIPPED=<recorded>`，exit 0；报告通过=`P=T-F-E-S`、失败=`F+E`、跳过=`S`。

3. architecture/ledger：`./gradlew :app:testDebugUnitTest --tests 'com.ghostlock.app.architecture.*' --console=plain`；输入=五份批准 TSV；预期 `BUILD SUCCESSFUL`、`FORBIDDEN_EDGES=0 REMOVE_RESIDUE=0 COMPONENT_FILE_MISSING=0 COMPONENT_API_MISSING=0 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 KEEP_DRIFT=0`（由测试报告输出），exit 0。
4. builds：依次 `./gradlew :app:assembleDebug --console=plain`、`./gradlew :app:assembleDebugAndroidTest --console=plain`、`./gradlew :app:assembleRelease --console=plain`；每条 `BUILD SUCCESSFUL`/exit 0。记录三 APK absolute path/bytes/mtime/SHA-256；仓内 build 必须是真目录、无 symlink。
5. diff：`git diff --check -- <本子批 exact paths>`；无输出，exit 0。只读 `git status --short` 核对未越 manifest。

极端输入只测可达值：empty string/list、0/1/MAX、oversize text/log、unknown enum/key、duplicate backend key、stale prefs/schema、missing Context/package/file、unauthorized Shizuku、binder/callback death、late callback after handoff。非 nullable API 不造 null。所有失败返回批准的具名 reason，禁止 fallback/换路/推进下一批。

## 2. UI surface/state 唯一矩阵

固定环境仅由 §0 的 `Pixel_5/default_boot` read-only transaction 到达：narrow=1080x2400/420，wide=1600x2560/320；两者 font 1.0、zh-CN、animations 0、portrait。每次记录 AVD/snapshot/fingerprint/APK SHA、逐字段 before/active/restored identity，等待 idle；动态 uptime/log/timestamp 只用批准 mask。

| 组 | surface/state | mode/theme/layout | 证据数 |
|---|---|---|---|
| M1 | Home、Backend、Run Idle、Settings | Material+Miuix x light+dark x narrow | 16 screenshot + semantics |
| M2 | About、ColorPalette、Finish、FinishSettings | Material+Miuix x light+dark x narrow | 16 screenshot + semantics |
| M3 | wide shell: Home、Backend、Run Idle、Settings | Material+Miuix x light+dark x wide rail | 16 screenshot + semantics |
| M4 | ConfigCard: 43499-Glued、43284Glued、DebugProbe | Material+Miuix x light+dark | 12 screenshot + semantics |
| M5a | Run Running、Blocked、Handoff、Done、long-log | Material+Miuix x light+dark；D0=`deferred-D5`，D5d production renderer seam 后捕获 | 20 screenshot + semantics |
| M5b | confirm dialog | Material+Miuix x light+dark | 4 screenshot + semantics |
| M5c | component gallery（同屏三个 panel：disabled、empty、oversize） | Material+Miuix x light+dark | 4 screenshot + 12 panel semantics |

D0 当前生产基线=68 screenshot；surface TSV 仍必须逐 member 展开最终 88 行，不得保留 M1-M5 汇总行，其中 M5a 20 行 action=`deferred-D5` 且无 D0 文件。D5d 形成 `RunUiState + Actions -> production renderer` 后，测试只向同一 renderer 注入状态并补齐总基线=88，禁止复制 renderer 或 dispatch。每个 semantics JSON 固定为 `{member_id,records[]}`：M1/M2 恰有 `normal,rotation,back` 三个 record，其余 D0 active file 恰有一个 `normal`；M5c 的每个 normal 内恰含 disabled/empty/oversize 三个具名 panel assertion，不增加 record；D5 M5a 每 file 恰一个 injected-state record。因此 D0=68 files/132 records/M5C_PANELS=12，D5=88/152。`UiSurfaceManifestTest` 分项断言，禁止仅核总数。D4 按页重跑 active 行，D5d 补 M5a，D6 全量 88。

D5d 补齐命令沿用 `capture`/`pack`，但只在 G1 批准且 production renderer seam 门绿后把 M5a 20 行从 `deferred-D5` 定点改为 `capture`；预期 `D5_CAPTURE=PASS ADDED=20 SCREENSHOTS_TOTAL=88 SEMANTICS_FILES_TOTAL=88 SEMANTICS_RECORDS_TOTAL=152 DISPATCH_CALLS=0`，随后 deterministic pack 预期 `SCREENSHOTS=88 DEFERRED_D5=0`。该 TSV action 变更属于已登记 D5d row，不回写 D0 历史报告。

像素规则：KSU 删除内容在 `ui-surface-matrix-v1.tsv` 写 `x,y,w,h,reason` mask。mask 外先按每通道差<=2 容忍抗锯齿，再要求差异像素 <=0.05%；超过即失败。布局 bounds、圆角区域、navigation bounds 与 semantic order 必须逐值相等，不受像素容忍豁免。mask 内新增 GhostLock 内容须有新 golden，不得整页 mask。

## 3. Android/设备门

### 3.1 安全机制门（每次 D5 子批）

安装记录身份一致的 debug/test APK 后执行（`SERIAL` 为 A301SO）：

- `adb -s SERIAL shell am instrument -w -e class com.ghostlock.app.backend.core.DebugProbeHostSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`
- `adb -s SERIAL shell am instrument -w -e class com.ghostlock.app.backend.core.FlowHostProcessSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`
- `adb -s SERIAL shell am instrument -w -e class com.ghostlock.app.backend.core.ShizukuFlowHostSmokeTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`
- no-op fixture APK 下分别运行 `Flow43499GluedHostSmokeTest`、`Flow43284GluedHostSmokeTest`。

每条预期 `OK (<recorded> tests)`、exit 0；skip 不是 PASS。检查新 pid、旧 host 消失、unauthorized/missing symbol/oversize/cancel/timeout/handoff 具名状态、无意外 fallback、非 keepalive 无 host/payload 残留。

### 3.2 真实攻击门（D5 完成后无条件）

前提：用户另行 opt-in；每 backend 独立冷机、固定 CPU 对、单 boot 单 route、SELinux Enforcing、KernelSU module 未加载、记录 boot_id/uptime/profile/APK/ELF SHA。

- 43499-Glued：`adb -s "$DEVICE_SERIAL" shell am instrument -w -e ghostlock_real_43499 1 -e class com.ghostlock.app.backend.core.Real43499GateTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`；只 dispatch 一次；预期 `OK (1 test)`/adb exit 0 且无 skip，真实 PASS 另以设备终态判。
- 43284Glued：G1 批准 D5-pre 后新增同构 `Real43284GateTest`；命令=`adb -s "$DEVICE_SERIAL" shell am instrument -w -e ghostlock_real_43284 1 -e ghostlock_real_43284_ack REAL43284_ACK -e class com.ghostlock.app.backend.core.Real43284GateTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner`；预期 `OK (1 test)`/adb exit 0 且无 skip。未有该入口不得手点 UI 代替可重复门。

终态必须按各 backend 既有合同记录 route 命中、write/root/KSU handoff、profile.conf/profile.bin 与完整设备 log。PASS 要求目标终态、无 panic、无非合同残留；panic/FAIL 归档并停，连续两次失败回 debug。缺 opt-in 或门未跑=`UNVERIFIED`，D6/goal 不完成。

### 3.3 运行生命周期判据

instrumented white-box + device 覆盖：rotation Running 不 cancel；VM clear 只 detach；explicit cancel 仅在 abortable/不可逆前传 host；Handoff 后 late callback 丢弃且不写状态；UI death/host death/binder death用既有 reason；start/cancel/close 重复幂等；log fd 先停访问再解绑再 close。每项同时断言 child/host/payload 是否应存活。

## 4. D0 与每子批最终树证据 / 失败恢复

1. 禁止创建 `ROLLBACK.sh`、`MODIFIED_FILE`、`DIFF_FILE`、`VERIFICATION.txt`、baseline/rollback fixture 或隔离回滚演练；不得把已有历史 transaction 当作新批模板。
2. 对所有整语句/整块语义锚点先计数，任一不是预期值则整体不写；用定点 patch，不分片读后全文覆盖。
3. 每批在最终树运行 §0/§1 对应门，直接把 branch/HEAD、source SHA、exact command、input、literal output、exit、XML 三桶与 AVD/真机 identity 写入 `kotlin-clean-architecture-d0.md` 和 review log。`OUT_OF_SCOPE` 由冻结的 file-action manifest 与最终 `git status --porcelain=v1 -z` 逐项核对；既存脏项原样不算越界，新增/变化的非批准 path 失败。
4. 源码恢复只引用 Git 历史与 Time Machine，且仅由用户/明确 owner 执行；这不授权智能体运行 `checkout/restore/reset/stash/clean` 等工作树写回。运行时资源仍按各 owner 的 cleanup/fail-closed 合同收尾。

prefs/schema：本计划不改保留 key/schema；旧版本必须能读回原值。KSU-only key 始终只忽略、不删除 SharedPreferences 文件，故 downgrade 不发生破坏性写回。

## 5. 报告与停止

每次报告命令、输入、完整关键输出、exit、XML 三桶、source/APK identity、已验证/未验证。设备证据完整写入 `docs/analysis/device-gates/*.md`，不设字节或篇幅门。同子批连续失败两次立即停；没有 G1、真实门或视觉矩阵时只能报进度，不能标 supported/complete。
