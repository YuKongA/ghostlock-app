# 归档说明

- 原路径：`docs/plan/kotlin-clean-architecture-ui-plan-v0-checklist.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：completed/current-fact plan superseded by root execution authority；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Kotlin UI V0 视觉纠偏实时清单（2026-11-03）

> 从属关系：这是 [`kotlin-clean-architecture-ui-plan.md`](kotlin-clean-architecture-ui-plan.md) 的 V0 唯一实时进度权威，不是并行计划；V0 全绿后才重新冻结 D0。
> 身份：branch `dev-nocpp`；HEAD `ffa6497adfc12938133e6338e2f95ddbf2ed54b9`；用户指令：“ok，还有记得计划要用checkbox实时追踪进度”。
> 状态规则：`[ ]`=未开始或失败，`[~]`=全表唯一进行中，`[x]`=该行全部判据已有 UTC 时间、命令 literal、exit 与证据路径；不得先勾后补。每次变化同批追加 `docs/analysis/architecture-review-log.md`。

## 1. 写范围与不变量

Production allowlist 只有以下三个 exact path；其它 production diff 一律 FAIL：

1. `app/src/main/kotlin/com/ghostlock/app/ui/component/material/SegmentedList.kt`：Material dropdown 的 normal/selected/disabled container/content/trailing 使用成对主题角色；保留选中、禁用、check trailing、触觉与点击语义。
2. `app/src/main/kotlin/com/ghostlock/app/ui/screen/home/HomeMaterial.kt`：仅 `home_gki_warning` demo 信息态改为 neutral information surface；真实 update/unsupported/root/SELinux warning/error 的颜色、动作与可达性不变。
3. `app/src/main/kotlin/com/ghostlock/app/ui/component/statuscard/BackendStatusCard.kt`：仅 Miuix renderer 恢复 hero status card；共享 model、READY/NOT_READY tone、整卡点击、CVE/state/extra tags 与 Material renderer 行为不变。

范围外保持冻结：repository/domain/backend registry、JNI/native/wire/profile、执行路径、D0/D1-D6、B4-real 与真实攻击。

## 2. 实时 checkbox

- [x] **V0.1–V0.7 历史视觉批** — 2026-11-03T17:20Z–17:56Z；Material dropdown/demo 与首版 Miuix hero 的完整命令、失败修复、AVD 六图、rollback、hash/seal 见 `architecture-review-log.md` 17:20–17:56 UTC 及 `/tmp/ghostlock-ui-visual-fix-20261103/VERIFICATION.txt`；最终 critic `阻断数=0`。
- [x] **V0.8 GhostLock 1.2 基底批** — 2026-11-03T18:03Z–18:30Z；直接来源 HEAD=`847793ea…`/`GhostlockUI.kt:739-846`，production source=`6aec8bf6…`；source gate 26/26、compile/unit/XML/assemble、READY/NOT_READY light+dark、隔离 rollback 与 final critic 均绿。完整逐步沿革见 review log 18:03–18:30 UTC 及 `/tmp/ghostlock-miuix-status-card-base-20261103/VERIFICATION.txt`。

V0.8 保留不变量：110dp `BottomEnd+offset(27,31)` geometry；READY/NOT_READY 两态及动态/强制明暗色；项目 `isInDarkTheme()`；整卡 click/Tilt；共享 model、Material 与全部 tag/navigation 语义。

### V0.9 NOT_READY 信息层级与 badge/action 排版

- [x] **V0.9a 设计冻结** — 2026-11-03T18:45:41Z；首轮 V09-01..03 阻断 3，二轮仅余 V09-02，最终 delta 确认全关闭、`阻断数=0`。V09-01A 保持 M：固定本地化 title，下方原样完整 `statusTextRes`；三类 tag 进入唯一 `FlowRow`，navigation 独立 Row。写范围仅 `BackendStatusCard.kt` 的 import/紧邻 KDoc/Miuix body；共享契约/Material/callers/10 guard/V0.8 geometry 全冻结。gate=`b5e168b4…`/19995 B：baseline=`PASS 3/3 10/10`、candidate=`PASS TAG_BINDINGS=3/3`，8 类 false-green fixture=`8/8 rejected`。
- [x] **V0.9b 单 renderer 实现** — 2026-11-03T18:47Z；production source=`986547e865f14e51da115d7cd2238471925481dc8c2cbc3ead291233822294d6`，与隔离 candidate 逐字相等。NOT_READY title=`home_not_ready`/1 行，完整 `statusTextRes` 原样下一行/2 行；Column gap=6dp，三类 tag 位于 `FlowRow` 横6/纵4dp，navigation 独立行 top=2dp/Medium。旧 Column/TODO 临时态 source gate PASS 后已同批清零；最终 gate=`PASS ... TAG_BINDINGS=3/3 ...`/exit 0。
- [x] **V0.9c 主机门** — 2026-11-03T18:49Z；source gate=`PASS ... TAG_BINDINGS=3/3 ...`、compile=`BUILD SUCCESSFUL in 562ms`、unit=`BUILD SUCCESSFUL in 35s`、XML=`68/438/438/0/0/0/0`、assemble=`BUILD SUCCESSFUL in 1s`、diff-check empty，均 exit 0。APK=`cc9006ac29647974ce01ce804dfefd2639161db6cce7ebeab725a7732a78fba8`/28420513 B。
- [x] **V0.9d AVD 视觉门** — 2026-11-03T18:51Z；APK=`cc9006ac…` 安装 `emulator-5582`=`Success`。production renderer 的 NOT_READY/READY light+dark 四态语义、主题色与像素带均绿：literal=`V09_AVD_MATRIX=PASS CASES=4/4 NOT_READY_THEME=PASS READY_REGRESSION=PASS OVERLAP=0`/exit 0；NOT_READY 为标题1+完整原因1+CVE1+跳转1，READY 为标题1+原因0+CVE1+跳转1；四图人工重开无叠压。原 profile 已经 Paths UI 恢复，最终 Miuix/dark/Home/Ready。多 state/extra tag wrap 仍为 `UNVERIFIED_PRODUCTION_UNREACHABLE`。
- [x] **V0.9e 内部收口** — 2026-11-03T18:53Z；首轮 final critic=`阻断数=0`，授权原子收口；隔离 rollback literal=`ROLLBACK_RESULT=RESTORED HASH_MISMATCH=0 RESTORED_STATUS=PRE_V0_9`/exit 0，四 baseline SHA 全匹配且 production source 保持 `986547e8…`。真实四路径 diff、完成态 verification、modified copy/manifest 已生成并 hash/reopen；父计划置 v9 完成。极端 tag wrap 仍为 `UNVERIFIED_PRODUCTION_UNREACHABLE`，D0 未启动；最终状态 delta critic 另记 review log。

### V0.10 badge trailing 对齐

- [x] **V0.10a 设计冻结** — 2026-11-03T19:08:24Z；用户要求 badge 放右侧。唯一写范围仍为 `BackendStatusCard.kt` 的目标 KDoc 与 Miuix `FlowRow` 参数：增加 `fillMaxWidth()`，横向使用 `spacedBy(6.dp, Alignment.End)`；Column 的 120dp icon-safe end padding、tag 顺序/换行、navigation 独立行、V0.8 geometry、V0.9 标题/原因与全部共享契约冻结。baseline source=`986547e8…`，candidate=`3dbc8f23…`；gate=`69c882df…`，baseline/candidate=`PASS GUARDS=10/10 RIGHT_ALIGN=1`；exact-candidate mutation rejection=`4/4`；critic V10-01..06 全关闭、`阻断数=0`。
- [x] **V0.10b 单 renderer 实现** — 2026-11-03T19:09Z；production source=`3dbc8f23eea9844912482066e34a414c47f1881fe0312c204c788fb2b259db6e`，与冻结 candidate 逐字相等；仅 KDoc 与 `FlowRow.fillMaxWidth()+spacedBy(6.dp, Alignment.End)` 改动。source gate=`PASS EXACT_CANDIDATE=1 FLOW_WIDTH=1 RIGHT_ALIGN=1 TAG_CALLS=3/3 ACTION_AFTER=1 GUARDS=10/10`/exit 0；无旧码/TODO。
- [x] **V0.10c 主机门** — 2026-11-03T19:10Z；source gate=`PASS ... RIGHT_ALIGN=1 GUARDS=10/10`、compile=`BUILD SUCCESSFUL in 590ms`、unit=`BUILD SUCCESSFUL in 35s`、strict XML self-test=`5/5`、XML=`68/438/438/0/0/0/0 RESULT=PASS`、assemble=`BUILD SUCCESSFUL in 1s`、diff-check empty，均 exit 0。APK=`4dd13debc02cb57368c35c66f8bf3a4f9f687c05c8b727868b52cee0d931d2e9`/28420513 B。
- [x] **V0.10d 用户纠正与设计 delta** — 2026-11-03T19:15:27Z；v1 虽机器门四态通过，但用户判定 120dp end padding 让装饰大 icon 错误占用排版、badge 仍在中部，因此该视觉结果不算完成。修订 candidate=`e4167775…`：Column end 120→16dp，icon 仍先绘制并保持 110dp/BottomEnd/offset，tag full-width/trailing 由后绘制内容覆盖背景装饰；KDoc 阶段归 V0.10e。gate=`f45422d6…` 要求 `CONTENT_END_16=1 ICON_BACKGROUND_ORDER=1`，exact mutations=`5/5`；critic `阻断数=0`。
- [x] **V0.10e 修订实现** — 2026-11-03T19:16Z；production=`e416777580208f16bad92cf5a5dabb786dbfd9c97ddc68cc776f1a548b0805c9` 与 candidate v2 逐字相等；相对最终设计仅 KDoc/end 120→16dp/既定 FlowRow 两参数，无旧码/TODO。gate=`PASS ... CONTENT_END_16=1 ICON_BACKGROUND_ORDER=1 GUARDS=10/10`/exit 0。
- [x] **V0.10f 修订主机门** — 2026-11-03T19:17Z；gate=`PASS ... CONTENT_END_16=1 ICON_BACKGROUND_ORDER=1`、compile=`BUILD SUCCESSFUL in 581ms`、unit=`BUILD SUCCESSFUL in 35s`、strict XML self-test=`5/5`、XML=`68/438/438/0/0/0/0 RESULT=PASS`、assemble=`BUILD SUCCESSFUL in 1s`、diff-check empty，均 exit 0。APK=`38dbef5412605357f74d36d956c3768d586d64b09d0b065f51ad6fbac58d039d`/28420513 B。
- [x] **V0.10g 修订 AVD 测量与用户否决** — 2026-11-03T19:21:14Z；修正 validator 的单 x 轴误判后，v2 四态机器结果为 `PASS CASES=4/4 CARD_STABLE=4/4 SAFE_RIGHT=4/4 POSITIVE_SHIFT=4/4 NAV_STABLE=4/4 CONTENT_OVERLAP=0 ICON_BACKGROUND_OVERLAY=4/4`/exit 0，baseline-as-modified 为 `FAIL ... SAFE_RIGHT=0/4 POSITIVE_SHIFT=0/4 ... ICON_BACKGROUND_OVERLAY=0/4`/exit 1；但用户人工判定 tag 仍作为 Column 独立测量行，令原因与跳转之间出现一整行空档，因此 v2 视觉候选正式否决，机器通过不冒充用户视觉验收。多 tag wrap 保持 `UNVERIFIED_PRODUCTION_UNREACHABLE`。
- [x] **V0.10h 否决收口** — 2026-11-03T19:21:14Z；不生成 v2 完成 seal、不将 V0.10 标为最终视觉完成；production `e4167775…` 仅作为下一修订的 source baseline，D0 不启动。

### V0.11 badge 与跳转共用 action row

- [x] **V0.11a 用户纠正与设计冻结** — 2026-11-03T19:41:39Z；用户明确要求继续修复 badge 独占测量行造成的原因→跳转空行，V0.11 恢复。唯一 production 写范围仍为 `BackendStatusCard.kt` 的目标 KDoc 与 Miuix action 区：navigation 与既有 tag `FlowRow` 组成同一个外层 Row；navigation 固定 leading，tag FlowRow 以 `weight(1f)` 使用剩余宽度并 trailing 对齐。110dp icon geometry、16dp 内容 end padding、标题/原因、tag 顺序/颜色、click、共享 model、Material/caller/backend/readiness/字符串资源及 10 guard 全冻结。source baseline=`e4167775…`；design-critic 总判允许实施、literal=`阻断数=0`。
- [x] **V0.11b 单 renderer 实现** — 2026-11-03T19:44:53Z；旧结构 source gate literal=`ROW_FILL_WIDTH=0/1 FLOW_DIRECT_CHILD_AFTER_ARROW=0/1 FLOW_WEIGHT=0/1 STANDALONE_FLOW=1/0 RESULT=FAIL`/exit 1。按规则临时套住完整旧 FlowRow/TODO 后新增 action Row，compile=`BUILD SUCCESSFUL in 692ms`/exit 0；同批清零旧块/TODO。最终 source=`ebf4236e166a74e80fcc853928e4147f53e5e5f4ccff202aef6b57ceae931775`，gate literal=`OUTER_ACTION_ROW=1/1 ROW_FILL_WIDTH=1/1 NAV_TEXT_THEN_ARROW=1/1 FLOW_DIRECT_CHILD_AFTER_ARROW=1/1 FLOW_WEIGHT=1/1 RIGHT_ALIGN=1/1 FLOW_VERTICAL_GAP=1/1 STANDALONE_FLOW=0/0 TAG_BINDINGS=3/3 HAS_TAGS_CONDITION=1/1 CONTENT_END_16=1/1 ICON_BACKGROUND_ORDER=1/1 GUARDS=10/10 TEMP_RESIDUE=0/0 RESULT=PASS`，source gate/diff-check 均 exit 0。
- [x] **V0.11c 主机门** — 2026-11-03T19:46:18Z；final source gate PASS/exit 0；compile=`BUILD SUCCESSFUL in 554ms`、unit=`BUILD SUCCESSFUL in 35s`，均 exit 0。XML 聚合先后以错误根 `build/test-results` 与错误 glob `**/build/test-results` 两次按预期报 `NO_XML`/exit 1，停止后只读 debug 确认真正仓内实树为 `build/app/test-results`，修正为 `build/*/test-results/**/*.xml` 后 literal=`XML_FILES=68 TESTS=438 PASSED=438 FAILED=0 FAILURES=0 ERRORS=0 SKIPPED=0 RESULT=PASS`/exit 0。assemble=`BUILD SUCCESSFUL in 1s`、diff-check empty/exit 0；APK=`/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/app/outputs/apk/debug/GhostLock-v2.0.12(1345)-arm64-v8a-debug.apk`，28420513 B，sha256 `1bb68957af71a98fd94b04cc24fb6b7ed52f552dab743b55017a89c637c99b9a`。
- [x] **V0.11d AVD 用户视觉门** — 2026-11-03T19:54:45Z；final APK `04d19b8a…` 安装 `emulator-5582`=`Success`。production READY/NOT_READY × light/dark 四态均证明 navigation 与 CVE badge 共享同一 clickable action ancestor；NOT_READY 坐标 nav=`[77,918][373,966]`、badge=`[731,928][992,957]`、action=`[77,873][1003,1005]`，READY 坐标 nav=`[77,805][255,853]`、badge=`[731,815][992,844]`、action=`[77,760][1003,892]`，四态 `Y_OVERLAP=29px`、`EXTRA_BAND=0px`。矩阵 literal=`V11_AVD_MATRIX CASES=4/4 SHARED_ACTION=4/4 SAME_VERTICAL_BAND=4/4 EXTRA_BADGE_BAND=0/4 LEADING_TRAILING=4/4 LIGHT_DARK_GEOMETRY=2/2 RESULT=PASS`、`AVD_MATRIX_EXIT=0`；四张 PNG 已逐张重开，人工检查未见 badge 独占行、裁切或 nav/badge 横向重叠。证据=`build/visual-gates/v11/{not_ready_light,not_ready_dark,ready_light,ready_dark}.{png,xml}`。门前真实状态经只读复读为 empty profile/Miuix/dark/Home/NOT_READY；门后已精确恢复，literal=`Night mode: yes`、`text="Not ready"`、`text="Configure Paths"`、`RESTORE_RESULT=PASS EXIT=0`。
- [x] **V0.11e 用户纠正与最终收口中止** — 2026-11-03T19:56:26Z；exact-command 证据已补齐，但 evidence-only final critic 运行中收到用户新判据「open 和 badge 应该在卡片靠下对齐」，已立即中断旧复核。V0.11 只证明同一 action band/无 badge 独占行，不冒充最终视觉完成；source=`ebf4236e…` 作为 V0.12 直接起点，D0 未启动。

### V0.12 action row 靠卡片底部对齐

- [x] **V0.12a 用户纠正与设计冻结** — 2026-11-03T19:58:31Z；唯一 production 写范围仍为 `BackendStatusCard.kt` 的目标 KDoc/Miuix content Column：外层内容列以 `heightIn(min=110.dp)` 参与 Box 测量并按 `Arrangement.SpaceBetween` 排列「标题/原因 header」与既有 action Row；header 内保持 6dp 标题→原因 spacing 并保留 6dp action 前安全间距。navigation 与完整 tag FlowRow 仍共享同一 Row，110dp icon/16dp end/标题与原因/Material/caller/backend/readiness/resources/tags/click/10 guards 全冻结。source gate 必须输出 `CONTENT_COLUMN_MIN110/MODIFIER_ORDER_FILL_HEIGHTIN_PADDING/SPACE_BETWEEN/HEADER_COLUMN_DIRECT_CHILD/HEADER_INTERNAL_GAP_6/HEADER_BOTTOM_6/ACTION_SECOND_CHILD=1/1`、`OUTER_CHILDREN=2/2`，并复用全部 V0.11 结构与 guard 项；旧 V0.11 source 必须 FAIL、final 才 PASS。AVD 四态冻结 density=2.75：`ACTION_BOTTOM_GAP=card.bottom-action.bottom` 必须 0..5px；可见 group gap=`card.bottom-max(nav.bottom,badge.bottom)` 必须 39±5px，nav/badge bottom 差≤12px；READY nav 与 badge相对 V0.11分别至少下移70px；并要求 `Y_OVERLAP>0`、`EXTRA_BADGE_BAND=0`、`TEXT_PAIRWISE_OVERLAP=0`、`CARD_NEXT_OVERLAP=0`、leading/trailing、同状态 light/dark 全部坐标差≤2px。design-critic 复核 literal=`阻断数=0`，允许实施。
- [x] **V0.12b 单 renderer 实现** — 2026-11-03T20:00:41Z；旧 source gate 按预期以 `CONTENT_COLUMN_MIN110/SPACE_BETWEEN/HEADER/ACTION_SECOND_CHILD=0/1 OUTER_CHILDREN=1/2 RESULT=FAIL`/exit 1 判红。按 §3 暂存完整旧 Column/TODO，新结构临时态 compile=`BUILD SUCCESSFUL in 590ms`/exit 0，同批以双锚点 `1/1` 删除旧块/TODO并核验 bytes/lines/SHA。final source=`5b1f9bdd07af40ad1e43da9650d2ee148d51737e775cc0c33f8712c4b85f39fc`；gate=`CONTENT_COLUMN_MIN110=1/1 MODIFIER_ORDER_FILL_HEIGHTIN_PADDING=1/1 SPACE_BETWEEN=1/1 HEADER_COLUMN_DIRECT_CHILD=1/1 HEADER_INTERNAL_GAP_6=1/1 HEADER_BOTTOM_6=1/1 ACTION_SECOND_CHILD=1/1 OUTER_CHILDREN=2/2`，全部 V0.11 项/10 guards/`TEMP_RESIDUE=0/0` 继续 PASS，exit 0；final compile=`BUILD SUCCESSFUL in 551ms`/exit 0。gate SHA=`aa5f3f3c…`。
- [x] **V0.12c 主机门** — 2026-11-03T20:01:38Z；final source gate完整结构/guard PASS、exit 0；compile=`BUILD SUCCESSFUL in 442ms`、unit=`BUILD SUCCESSFUL in 36s`、XML=`68/438/438/0/0/0/0 RESULT=PASS`、assemble=`BUILD SUCCESSFUL in 1s`、diff-check empty，均 exit 0；组合 literal=`V12_HOST_GATE_RESULT=PASS`。APK=`build/app/outputs/apk/debug/GhostLock-v2.0.12(1345)-arm64-v8a-debug.apk`，28420513 B，SHA-256=`6a43b94cc4488bc99834d017734887b4baa74ae547c8dace40ef280bacdfe6b3`。
- [x] **V0.12d AVD 四态底部对齐门** — 2026-11-03T20:04:41Z；final APK `6a43b94c…` 安装 `emulator-5582`=`Success`。NOT_READY card/action/nav/badge=`[33,672][1047,1005]`/`[77,873][1003,1005]`/`[77,918][373,966]`/`[731,928][992,957]`；READY=`[33,672][1047,975]`/`[77,843][1003,975]`/`[77,888][255,936]`/`[731,898][992,927]`。四态均 action bottom gap=0px、visible group bottom gap=39px、nav/badge bottom差=9px、Y overlap=29px；READY nav/badge 相对 V0.11 均下移83px。literal=`V12_AVD_MATRIX ACTION_BOTTOM=4/4 VISIBLE_BOTTOM=4/4 BOTTOM_DELTA=4/4 READY_NAV_SHIFT=2/2 READY_BADGE_SHIFT=2/2 SAME_BAND=4/4 EXTRA_BADGE_BAND=0/4 TEXT_PAIRWISE_OVERLAP=0/4 CARD_NEXT_OVERLAP=0/4 LEADING_TRAILING=4/4 LIGHT_DARK_GEOMETRY=2/2 RESULT=PASS`/exit 0；四图逐张重开通过。首次 capture 发现门前实际 selected backend 已为 `43284Glued`，旧脚本假定 `43499-Glued` 导致四份均为 READY，未进入 validator完成态；修订脚本先在内存快照 session/profile/night、四态显式选 `43499-Glued`、finally 精确恢复后重跑。最终复读已恢复门前 `43284Glued`/empty 43499 profile/Miuix/dark/Home，literal=`RESTORE_RESULT=PASS EXIT=0`。
- [x] **V0.12e 最终树收口** — 2026-11-03T20:07:49Z；V12-EVID-01 已由 evidence-only critic 确认关闭，literal=`阻断数=0`。冻结身份、CWD、exact command/input、source/host/capture/validator literal/exit 均在 `architecture-review-log.md` 20:06:52Z 节闭合；source/APK/四态证据保持不变。多 state/extra 最大组合=`UNVERIFIED_PRODUCTION_UNREACHABLE`；D0 不启动。

## 3. 判据与历史证据

V0.1–V0.10 的既有 transaction/rollback 记录只说明当时真实过程，保持只读历史，不再生成、更新或作为未来批次模板。2026-11-03 起，V0.11 与后续批次禁止准备 `ROLLBACK.sh`、`MODIFIED_FILE`、`DIFF_FILE`、`VERIFICATION.txt`、baseline/rollback fixture 或隔离回滚演练。

V0.11 已恢复；source 检查直接读取最终工作树，并把冻结 source/10 guard SHA 作为只读常量核验，不依赖 baseline copy。主机门仍串行运行 `:app:compileDebugKotlin`、`:app:testDebugUnitTest`、XML 全量聚合、`:app:assembleDebug` 与 `git diff --check`；AVD 门仍须 READY/NOT_READY × light/dark 四态证明 badge/navigation 同一 action band、reason→navigation gap 收敛、trailing/leading/无裁切/无重叠。全部 exact command/input/literal/exit、source/APK SHA 与人工视觉结论直接追加本清单和 `architecture-review-log.md`。
