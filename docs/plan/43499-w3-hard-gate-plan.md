# 43499 G04 W3 硬门/leaf 探针 L 设计 v2（B1–B3/I1–I3 关闭）

- 状态：**L 设计 v2（2026-10-28，task-76），只写文档+approval append、零实现、不 commit**；v1 被 critic 判不可（B1–B3+I1–I3），本件按 Lead 裁定修订，待二轮复核。
- 范围：W3（W2 cred 成功→W3 flags/mode→finish/acceptance 前）；不改 W1/W2/G05；逻辑全 Kotlin、Native 仅机制、**JNI ABI 不变**（helper 协议 append-only）。
- 依据：Flow W3 `:1161-1277`、leaf `:1279-1330`、verifier `Cve43499WriteChain.kt:331-399`、planner `Cve43499DebugWritePlanner.kt:32-68`；协议 `victim_protocol.h:39,103-125`、`VictimProtocol.kt:83-110`、helper `:795-835`、ProcessVictim `:90-140,191-220`；Lead 裁定见 §7。

## 0. 结论摘要

1. **B1 已解（Lead 裁定 a）**：G04 实现批获准**增量修改 task-45 文件** `ProcessVictim.kt`/`ProcessVictimSession.kt`/`VictimProtocol.kt`；只允许 **append-only 新命令**，**不改既有 finish/park/bye 语义**；approval 已登记授权+回归要求（§7）。
2. **B2 已解**：新增 victim self-probe——J helper append `PROBE_STATUS`（返回 raw `/proc/self/status`，Kotlin 解析，Native 不解析业务）；ProcessVictim 新增 `status` line-protocol（同样返回 raw 文本）；**P2/P3/P4 双源**=父侧 `/proc/<pid>/status` 与 victim 自读一致；不一致/任一不可读⇒FAIL（`gate-uid-source-mismatch`/`gate-creds-source-mismatch`/`gate-seccomp-probe-missing`），不得静默 PASS。
3. **B3 已解**：成对 append `PROBE_COMM_SET`（写 self comm marker）+`PROBE_COMM`（读 len/c0/bytes）；J 与 ProcessVictim 路径均实现；JNI ABI 不变、capability append、协议 frame/错误码见 §3.2；若某路径命令不可用⇒该路径 **FAIL**（不宣称 fallback）。
4. **I1**：P3/P4 观测强度/失败 reason 写死（§2/§6）；E7 失败矩阵含 `source-mismatch`/`probe-missing`。
5. **I2**：门禁编号裁定 **E7**（不占 G14/G15/G16）；approval/门禁索引已 append；98260 gate 表旁注记由 Lead/98260 owner 同步（我方不编辑 98260 文件）。
6. **I3**：W3 失败⇒G01 fail-stop（app 终止）后设备仍 permissive/root ⇒ **外部冷重启**；`coldRebootRequired=true` 在 terminate **前**落盘/fsync；E5/⑥ 排除 W3 FAIL 后未冷启的 run；reason 分类见 §6。
7. **保留**：P1/P2/P5–P8、§3.3 防假成功、G05→W3→finish/acceptance 顺序、单 W1/非 root N/A+finish 拒绝、CBN A/B、风险/TD、UNVERIFIED。

## 1. 现状盘点

| 项 | 位置 | 事实 |
|---|---|---|
| W3 step | `Flow43499Impl.kt:1161-1277` | preflight:1163；leaf `:1179`；flags/mode 两次 W3；`verifyW3` 三分支；失败 `leaf-direction-unsupported:1321`/`seccomp-probe-unsupported:1243`/`unverified` |
| leaf/verifier | `Flow:1279-1330`；`Cve43499WriteChain.kt:331-399` | `w3ExactTarget`(tcp) 直接 EXACT；否则 `w3CommProbe`→`leafDirection()`（当前 null/UNSUPPORTED） |
| planner | `Cve43499DebugWritePlanner.kt:32-68` | `w3CommProbe`=task+`taskStruct.comm` Zero leaf；flags/mode 经 `leafAdjusted`（+8⇒目标-8） |
| victim comm/uid | `ProcessVictim.kt:191-220`（sh 2B）；J helper 无 PR_SET_NAME⇒basename 截断 15B；`ProcessVictim.kt:97-104` uid probe；`VictimIdentityReader` 读父侧 `/proc/<pid>/stat` | 当前无 comm/status self-probe；W3 无判据 |
| finish/acceptance | `Flow:1424-1439`；`Flow:1547-1565`；`FinishAcceptance.kt:91,102,105,108,176-188` | finish 仅 rooted victim；acceptance 需 uid=0/script sha/markers/enforce/binding/resources；W3 无 gate 前置 |
| G05/G07 | `Flow:1128,1163`；`Flow:459-467` | post-cred preflight/close 合同；KSU 守卫 W1 前；W3 不绕过 |
| legacy | VL `main.c:945,1001-1014,1215-1245,1410-1450`；L1 `victim_process.cpp:33,87-104,258-275`+`backend:240-350`；L2 disabled 无完整 W3 探针（缺失） | comm marker；leaf dir 4 attempts/set 50ms；len8⇒+8/len0⇒EXACT/len15⇒未命中 FAIL/其他 ambiguous FAIL；flags/mode 按方向调整；seccomp 'F' 自探针 |

## 2. W3 硬门语义（PASS=P1..P8 全成立）

| # | 判据（强度） | 证据源（双源） | 失败 reason |
|---|---|---|---|
| P1 | victim 存活且 pid/starttime 不变 | lifecycle snapshot before/after | `...:gate-victim-changed` |
| P2 | uid=0（real/effective/saved/fs） | 父侧 `/proc/<pid>/status` Uid + victim `PROBE_STATUS`/`status` Uid **一致** | `...:gate-uid-not-root` / `...:gate-uid-source-mismatch` / `...:gate-uid-probe-missing` |
| P3 | cred 生效（uid=0 且 CapEff 一致、root 语义记录） | 父侧 status 与 victim self status 的 Uid+CapEff **一致**；不额外猜 | `...:gate-cred-not-effective` / `...:gate-creds-source-mismatch` |
| P4 | seccomp 清除（`Seccomp:0`） | 父侧 status + victim self status `Seccomp:` 一致；before(2)/after(0) | `...:gate-seccomp-not-cleared` / `...:gate-seccomp-source-mismatch` / `...:gate-seccomp-probe-missing` |
| P5 | leaf 方向经实际写观测（非 tcp exact） | `PROBE_COMM` pre/post：len0=EXACT、len8=+8、len15/其他=FAIL | `...:gate-leaf-direction` |
| P6 | W3 链 close clean/无 retained | `requireCleanPostCredPreflight`+close 回执 | `...:gate-close-retained` |
| P7 | finish 前置（rooted victim/script handoff 冻结） | Flow 既有 | `...:gate-finish-prereq` |
| P8 | 同 boot/无 panic/KSU 未加载（W1 前置已证） | evidence/ksuGuard | `...:gate-env` |

- **P4 充分性**：`Seccomp:0` 来自父/victim 两个独立读取，足以证明 seccomp mode/filter 已清除；`TIF_SECCOMP` 无直接用户态读口，**不要求**直接观测（如产品要求，追加 victim `finit_module` 自探针作为补充，真机 UNVERIFIED 可标）；不得以单一 `Seccomp:0` 直接 PASS。
- **失败行为**：W3 `StepResult.Failed`（具名）；**禁止 finish/acceptance**（finish/acceptance 预检 `w3Gate==PASS`，否则 Failed）；W2 cred 成功后失败⇒G01 fail-stop（`fail-stop:write.w3:<reason>`）+ **冷重启**（§5/I3）；App 侧不尝试 setenforce/二次写恢复 enforcing；成功时 enforce 恢复由 finish/acceptance 既有判据。
- **顺序**：W1(permissive)→W2(cred root)→G05 post-cred preflight/close→**W3 硬门**→finish(victim-only)→acceptance；G01 dirty 先 fail-stop；W3 失败后不回 W2、不盲试；单 W1/非 root W3 N/A、finish 拒绝（`su-fallback-refused`）。

## 3. leaf 探针设计

### 3.1 类型/执行者

| 类型 | 执行者/机制 | 用途 |
|---|---|---|
| comm-set | victim 自身：J `PROBE_COMM_SET`（prctl/self comm）；ProcessVictim `comm-set`（shell 写 self comm） | 15B marker（解决 comm="sh"） |
| comm-read | victim 自身：J `PROBE_COMM`（读 self comm raw len/bytes）；ProcessVictim `comm` line | leaf 方向 len0/8/15 |
| status-read | victim 自身：J `PROBE_STATUS`（raw `/proc/self/status`）；ProcessVictim `status` line | P2/P3/P4 双源之一 |
| uid-read | Kotlin 父侧 `/proc/<pid>/status`（既有跨进程读能力） | 双源之另一 |
| leaf-write | Kotlin 决策+既有 W3 JNI 写事务（`w3CommProbe`） | 实际观测写方向 |

- **Kotlin 决定一切**（marker 校验、len/c0→EXACT/+8/FAIL、双源一致性、证据）；Native 只执行 `prctl`/读 self comm/status/写既有事务，不解析业务、不猜方向。
- **J 路径**：helper 每 attempt 可 `PROBE_COMM_SET` 重设 marker；`PROBE_COMM` 返回 len/c0；`PROBE_STATUS` 返回 raw status（≤4 KiB frame；超限 IO err）。
- **ProcessVictim**：`comm-set <15B>`→`GLK_COMM_SET:OK|ERR:<code>`；`comm`→`GLK_COMM:<len>:<hex>`；`status`→`GLK_STATUS:<hex>`（shell `od -An -tx1` 编码）；既有 finish/park/bye 行不变（B1 append-only）。
- **协议 append**（§3.2）：J `PROBE_STATUS=6`/`PROBE_COMM_SET=7`/`PROBE_COMM=8`（`core/victim_protocol.h:103-107` 尾部 append；Kotlin `VictimProtocol.Opcode:83-98` 镜像）；frame 复用 v1（4B 长度前缀 + body header 4B + payload + CRC32 4B，`:64-81`）；错误码复用 `GLK_VICTIM_ERROR_{UNSUPPORTED=2,IO=3,BAD_ARGS=7}`；不新增错误枚举。

### 3.2 协议/能力 append（B3，JNI ABI 不变）

| 项 | 追加 |
|---|---|
| Native opcode | `GLK_VICTIM_OPCODE_PROBE_STATUS=6`、`_PROBE_COMM_SET=7`、`_PROBE_COMM=8`（`victim_protocol.h:103-107` 尾部） |
| Kotlin opcode | `PROBE_STATUS(6)`/`PROBE_COMM_SET(7)`/`PROBE_COMM(8)`（`VictimProtocol.kt:83-98`） |
| capability | append `PROBE_STATUS(1<<7)`/`PROBE_COMM_SET(1<<8)`/`PROBE_COMM(1<<9)`（`VictimCapability.kt:15-44`；现有 0–6，**不重排**） |
| helper 机制 | `PROBE_STATUS`：open/read `/proc/self/status` raw→reply；`PROBE_COMM_SET`：校验 args≤15/BAD_ARGS，`prctl(PR_SET_NAME)`/self comm 写入→OK/errno；`PROBE_COMM`：读 self comm raw（≤16）→reply；Native 不解析 |
| ProcessVictim 行 | append `status`/`comm`/`comm-set <15B>` 三命令（token 校验：ASCII/无引号空白/≤15；越界 ERR）；既有命令语义不动 |
| JNI/ABI | 无新增 JNI/DTO/ABI；transport send/receive 通用；仅 Kotlin 方法 append；ABI/scanner 不变 |
| 协议版本 | 保持 v1（append-only opcode；同 build 双端；helper 若 `UNSUPPORTED`⇒该路径 FAIL，不静默 fallback） |

### 3.3 探针流程/防假成功

```text
1 precheck P1/P8；P2/P3/P4 父侧 status 读取 + victim self status（PROBE_STATUS/status）→ 双源比对（不一致/缺失⇒FAIL）
2 comm-set 15B marker（J PROBE_COMM_SET / ProcessVictim comm-set）→ PROBE_COMM/comm 读回 len15+bytes 确认
3 tcp exact 路径跳过写探针（direction=EXACT(policy)）；否则 runner.execute(W3,w3CommProbe)→close(clean)→settle→PROBE_COMM/comm：
    len0⇒EXACT；len8⇒+8；len15⇒本轮未命中（重试≤w3LeafProbeAttempts；每次重设 marker；J/ProcessVictim 均可）；其他/歧义⇒FAIL
4 方向确定→w3Flags→close→settle→w3SeccompMode→close→settle（既有 planner/Runner）
5 重读 P2/P3/P4 双源 + P5/P6/P7 → PASS 才可 finish
```

- **防假成功**：pre-comm 必须=marker；leaf 写后必须有可观测变化；未命中/歧义不猜方向、不盲写 flags/mode；uid/seccomp 双源任一缺失或不一致=FAIL；`Seccomp:0` 单源不足以 PASS；leaf probe 失败不进 finish/acceptance；W3 close retained=G01 fail-stop。
- **leaf 读/写**：comm-read=用户态 `/proc`/victim 自报；comm-write=既有内核写；read 失败不推出 PASS。

## 4. Kotlin/Native 边界（JNI ABI 不变）

| 层 | 内容 |
|---|---|
| Kotlin（逻辑） | `Cve43499W3HardGate`/双源 status 解析/leaf 决策/verifier 新实现/finish+acceptance 前置/fail-stop 冷重启 evidence；ProcessVictim/J 协议 append |
| Native（仅机制，append） | helper 三个 opcode（read status/self comm/set comm）；JNI 入口/DTO/ABI 零新增；`core/victim_protocol` Kotlin 镜像与 ProcessVictim shell 命令；**task-45 文件仅 append-only 新命令**（B1 授权） |
| 禁止 | Native 解析业务/选择方向/状态机/目标常量；App/su 执行 payload/finish；W3 失败后 App 侧写 enforcing |

## 5. 交互/冷重启（I3）

- **G05 prebuilt**：W3 前必须 `requireCleanPostCredPreflight`（Flow:1163）通过且 W2/prebuilt/fallback close 全 clean；失败⇒W3 不启动。
- **G07 KSU**：W1 前守卫（Flow:459-467）不被绕过；evidence 继承 `ksuGuard`。
- **A1 finish victim-only**：W3 PASS 才允许 finish；finish 仍 rooted victim `execScript`；无 rooted/非 PASS⇒拒绝。
- **E5/⑥**：E5 链加 W3 硬门 step；E5 未过不标 supported；⑥冷机 N=3 需 W3 PASS+失败后冷重启证据；**排除** W3 FAIL 后未冷启的 run（不計 PASS）。
- **冷重启（I3）**：W3 FAIL 后、G01 terminate 前把 evidence（`coldRebootRequired=true`/reason/boot_id）写入 sink 并 **flush/fsync**（生产 `filesDir/fail-stop.log`；G01 顺序 evidence→log→terminate）；外部冷重启后下一 run 比对当前 `boot_id`（`/proc/sys/kernel/random/boot_id`）；同 boot/无记录⇒E5/⑥ 拒绝该 run；确认=boot_id 变化+标记清除；不 App 内恢复 enforcing。

## 6. E7 判据矩阵/evidence（I2/I3）

| 场景 | 判据 | 结果/reason |
|---|---|---|
| P1–P8 全成立（双源一致+leaf 方向观测+clean close） | §2 | **PASS**（可 finish） |
| 父/victim source 缺失/不可读 | P2/P3/P4 | **FAIL/UNVERIFIED** `...:gate-uid-probe-missing`/`gate-seccomp-probe-missing` |
| 双源不一致 | P2/P3/P4 | **FAIL** `gate-uid-source-mismatch`/`gate-creds-source-mismatch`/`gate-seccomp-source-mismatch` |
| leaf len15 未命中（重试耗尽）/其他/歧义 | P5 | **FAIL** `gate-leaf-direction`；禁盲写 |
| W3 flags/mode 事务 Failed/RouteMiss | — | **FAIL** `gate-flags-write`/`gate-seccomp-write`（保留原 reason） |
| W3 close retained | P6 | **FAIL** `gate-close-retained`+G01 |
| uid≠0 / seccomp≠0 | P2/P4 | **FAIL** `gate-uid-not-root`/`gate-seccomp-not-cleared` |
| W3 FAIL 后未冷启（同 boot_id） | I3 | 该 run **不计** E5/⑥ PASS；E5 拒绝 |
| 设备离线/无法双源/无冷机 | — | **UNVERIFIED**（不标 supported） |

- **E7**：W3 hard gate；PASS/FAIL/UNVERIFIED+N=3 冷机（固定 CPU/KSU 未加载/单 route；失败/panic 先 pstore+冷重启；p95=max）。编号=**E7**，不占 G14/G15/G16；approval 已登记；98260 gate 表旁注记由 Lead 同步（不编辑 98260 文件）。
- **evidence 字段**：`w3Gate`、`w3UidParent/self`、`w3UidStatus`、`w3CapEffParent/self`、`w3SeccompParent/selfBefore/selfAfter`、`w3CommMarkerBefore/After`、`w3CommLen/c0`、`w3LeafDirection`、`w3ExactPolicy`、`w3VictimPid/StartTime`、`w3CloseRetained`、`w3FailReason/w3FailCategory`、`w3ColdRebootRequired`、`w3BootId`、`ksuGuard`。

## 7. Lead 裁定/approval 登记（B1/B3/I2 解）

| 裁定 | 内容 |
|---|---|
| **B1=(a)** | 批准 **task-45 增量修改**：G04 实现批可改 `ProcessVictim.kt`/`ProcessVictimSession.kt`/`VictimProtocol.kt`（append-only 新命令；**不改 finish/park/bye/rooted detach 既有语义**）；approval 登记“task-45 增量授权（G04 A2 写范围）+ 回归要求（既有 finish/park/rooted detach 测试保持不变）”。 |
| **B2** | 新增 self-probe：J `PROBE_STATUS`（raw status，Kotlin 解析）；ProcessVictim `status`；P2/P3/P4 双源一致才 PASS；不一致/缺失⇒FAIL；Seccomp:0 双源为充分证据（TIF_SECCOMP 无直接读口，可选自探针补充，真机 UNVERIFIED）。 |
| **B3** | 成对 append `PROBE_COMM_SET`/`PROBE_COMM`（+`PROBE_STATUS`）；J/ProcessVictim 均可用；协议 frame/错误码/capability/命令见 §3.2；JNI ABI 不变；某路径不可用⇒FAIL，不宣称 fallback。 |
| **I2** | gate 编号=**E7**；不占 G14/G15/G16；approval/门禁索引登记；98260 gate 表旁注记待 Lead 同步。 |
| **I3** | W3 失败⇒G01 fail-stop 后**外部冷重启**；`coldRebootRequired=true` 在 terminate 前 fsync；E5/⑥ 排除未冷启 run；reason 分类见 §6。 |

## 8. CBN/风险/TD/UNVERIFIED

| 选项 | 说明 | 结论 |
|---|---|---|
| **A 主路径** | victim self-probe 三命令（PROBE_STATUS/PROBE_COMM_SET/PROBE_COMM）+ 父侧 /proc 双源；J/ProcessVictim 均实现 | 采纳（B1 授权） |
| B fallback | 父侧 `/proc/<pid>/comm` 读（若 PROBE_COMM 不可用） | 仅当 A 某路径不可用且仍权威时；不满足/不可行⇒该路径 FAIL，不宣称 fallback |
| C 仅父侧 /proc | 无双源/self-probe | 否决（B2 阻断：不可读/假成功） |
| D 盲写/猜方向 | — | 否决（可能写坏 task 邻域/panic） |
| E Native 全 W3 逻辑 | — | 否决（违反 Kotlin 逻辑约束） |

**风险/TD**：①helper/ProcessVictim 协议 append 双端同步/单测；②shell self-comm/`od` 编码兼容；③status raw/帧限；④双源时序；⑤pid/starttime（P1）；⑥冷重启/操作员确认；⑦E7 真机数据缺；⑧TIF_SECCOMP 无直读（文档化）；⑨task-45 append-only 回归；⑩E5/⑥–E7 依赖。

**UNVERIFIED**：helper/ProcessVictim 命令真机；父侧 `/proc/<pid>/status` 可读性；`Seccomp:0` 双源一致性；leaf len0/8/15 真机；comm marker；冷重启 boot_id；G01 真机；E7/E5/⑥ 未跑；task-45 append-only 回归未跑。**不做**：不写实现、不改 Native/ABI/DTO（授权 append）、不碰 98260、不改 task-45 既有语义、不 commit、不标 supported。
