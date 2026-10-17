# ref/5f184760 → A 的 43284 全链逐行对照与移植清单

> **B** = `ghostlock-app-ref`（HEAD `5f184760`，树 `src/core/**`；2026-10-16 22:50 真机成功）。**A** = 工作树（HEAD `4ce37fa7`，树 `core/src/**`）。
> **路径订正**：B 没有 `src/tools/pagecache_load_lkm.cpp`。B 的 staged runner 是 `src/core/backend/cve_2026_43284/stage_runner.cpp`；`--run-cve-2026-43284 / --stage / --cve43284-*` 已在 B 删除（`src/core/support/cli.hpp:19-35`），入口只剩「文档 → Pipeline」（`src/core/main.cpp:104-105`）。A 侧对应物是 `core/src/tools/pagecache_load_lkm.cpp`。

## §一 汇总表

| 能力 | B 的位置 | A 的位置 | A 是否等价 | 若无/不等价则最小移植动作 | 影响 |
|---|---|---|---|---|---|
| 一条命令跑全链 | `execution_binding.cpp:118-290` 内建 RealChain + `backend_terminal.cpp:390-437` 一次 `run_chain` | 无；临时 UI 在 Kotlin 手写 1548 行（`tempui/TypedBackendProbe.kt:940-1455`） | 否 | 按 §四 收进 native，Kotlin 只发一条命令 | 成败 |
| staged runner（阶段/退出码/finish 计数） | `stage_runner.cpp:531-607` run_stage、`:757-791`（trigger/full 的 0/1/2/3）、`:697-742` 一行 written/verified/rolled_back/hook_restored | `steps/pagecache_step_runner.cpp:51-180`（五步无聚合）+ `tools/pagecache_load_lkm.cpp:445-650`（退出码 1-8，无 written/verified 行） | 部分 | 加 finish 聚合行与 hook_restored 字段 | 日志 |
| 写单位与对齐 | `steps/chain.hpp:51` 16B 一块、`pagecache/pagecache.cpp:168-268`；模块按 16 补齐 `stage_runner.cpp:249-272` | `steps/pagecache_steps.hpp:55` width=16、`landing/pagecache_write.hpp:131-176`、末块补零 `pagecache_steps.cpp:345-350` | 是 | — | — |
| old 块读取 | `pagecache.cpp:182`（write16 内 pread）+ `chain.cpp:285-305`（journal 另读一次） | `landing/pagecache_write.hpp:141`（write 内 pread）；无 journal 读 | 部分 | 见 journal 行 | 安全 |
| IV/ICV/seq 组帧 | `pagecache.cpp:189`（IV=old+new）、`:191-206`（SPI/seq/IV+ICV）、`:256`（成功才 seq++） | `landing/pagecache_write.hpp:147-176`（IV→hdr→ICV→send→seq++ 同序） | 是 | — | — |
| seq 接续 | `real_ops.cpp:250-257`（patch#1）、`:307-327`（hook 页接载体 seq）、`:339-344`（restore 再交回） | 靠调用方手传：`jni/api_43284_device.cpp:829`、`jni/cve43284/Assembled43284.kt:391-402`、`TypedBackendProbe.kt:1265,1306,1358` | 部分（易漏） | 链内自动接续，禁止 Kotlin 传 seq | 成败 |
| 站点几何 | 运行期自镜像算：`steps/elf_hook.cpp:371-404`（guard 则 +4）、`hook_patch.cpp:137-153` | 写死声明：`hook/sentry_hook.cpp:78-99`（site 0xB30AC / guard BTI / payload 0xFE628 / 2520 / entry 0x90） | 值等价、形式不等价 | 后续把 elf_hook 的推导搬来 | 日志/通用性 |
| 写序（载荷先、站点最后） | `hook_patch.cpp:176-190` | `pagecache_steps.cpp:172-183` → `:185-209` | 是 | — | — |
| 站点整块与 guard | `hook_patch.cpp:138-153`（16B 整块、pos、写回原文块） | `pagecache_steps.cpp:83-88`（block_at/within<4）、`:149-156`（guard/displaced 校验） | 是 | — | — |
| **restore** | `chain.cpp:350-359`（出口无条件调）+ `real_ops.cpp:331-347`（仅 applied 才算）+ `hook_patch.cpp:193-220`（先站点块再 shellcode 块） | `pagecache_steps.cpp:212-274` 函数在但**无调用方**（`pagecache_steps.hpp:108` 自述"接线是后话"）；唯一"恢复"是文件标记分支 `TypedBackendProbe.kt:1293-1307` | **否** | 接到链出口（成功/失败/早退都调），真写过才算 hook_restored=1 | **成败/安全** |
| journal / rollback | `chain.cpp:153-173`（倒序 best-effort + 清账）、`:285-305`（写前记旧块，上限 `chain.hpp:58`=128，溢出置位） | 无 | **否** | 整段移植；模块区照 B 置 rollback=true，溢出靠 journal_overflow 报 | 安全 |
| plan 闭包/目标边界 | `chain.cpp:390-435`（区内/extent/载体 size/目标 size/互不重叠）、`execution_binding.cpp:183-190` fstat | 无 | 否 | 移植 closure + target_size | 安全 |
| 载体候选与回退 | `chain.hpp:68-80`（profile 优先 + 4 个默认，去重、/vendor 约束）、`execution_binding.cpp:141-148` | 单一路径，无候选表（`TypedBackendProbe.kt:540-542,1332`） | 否 | 加候选表，首项必须可直读（见 §三） | 成败/速度 |
| trigger 形态 | `real_ops.cpp:349-392`（双 fork，孙进程 delay 后可选 exec；生产 exec_path=null `real_ops.hpp:79`，delay 500 `execution_binding.cpp:170`） | `pagecache_steps.cpp:275-298`（双 fork+delay；A 用 150/2000ms `TypedBackendProbe.kt:667,1401`）+ Kotlin 另起 crash_dump32 点火 `:1424-1428` | 部分 | 统一成 B 形态，点火者作参数而非硬编码 | 成败 |
| wait 判据与超时 | `real_ops.cpp:394-437`（先 failure 后 success、/dev/df 非终态、模块出现又消失=Failed、10ms 轮询）；默认 5000 `chain.hpp:238`，文档 15000 `state.hpp:29` | `pagecache_steps.cpp:300-331` 同判据；攻击行 5000/10 且 marker 对（`TypedBackendProbe.kt:1437-1443`），自检行仍 40ms（`:518`） | 部分 | 删 40ms 自检，统一 15000/5000 + 10ms | 成败 |
| LKM 窗口 open/close(UNLOAD) | `chain.cpp:640-661` + `lkm/lkm_window.cpp:60-123,158-179` | 无 | 否 | 后置，不阻塞 LkmLoaded | 安全 |
| patch #1 | `steps/crash_dump.cpp:38-66`（逐块写 + 单点回读） | `pagecache_step_runner.cpp:109-135`（逐块写 + 逐块回读，略慢更严） | 是 | — | — |
| payload 槽填充 | native 内：`hook_patch.cpp:274-323`，默认 `hook_patch.hpp:144-157` | Kotlin 内：`Assembled43284.kt:176-200`（同默认 vendor_modprobe //vendor/bin/insmod /dev/df attr/exec） | 功能等价、层不对 | 搬回 native，槽表一处权威 | 维护 |
| 日志粒度与计数 | 每 region 一行 `chain.cpp:231-243,337-343`，失败块一行 `chain.cpp:204-215`，finish 一行 `:374-382` | 每块 3-4 行：`pagecache_steps.cpp:359,375,394,413` | 否 | 降到每 region 一行 + 失败块 | 速度/日志 |

## §二 成败关键清单（必须移植才有 LkmLoaded）

1. **restore 接线**：B 在出口无条件调 `chain.cpp:350-359`，`real_ops.cpp:331-347` 只有真 apply 过才回 true ⇒ `hook_restored=1` 可判。A 的 `restore_hook`（`pagecache_steps.cpp:212-274`）无人调用，钩子留在页缓存里，下一次任何进程走到 sentry 就跳进可能已被换出的载荷。**这是第一优先**。
2. **journal/rollback**：失败/校验不符时必须把已写块倒序写回（`chain.cpp:153-173,285-305`）；A 全无，写坏一块即无退路。
3. **wait 判据与超时**：判据 A 已有（`pagecache_steps.cpp:300-331`），攻击行 5000/10 且 marker 对齐（`TypedBackendProbe.kt:1437-1443`）；缺的是「文档默认值覆盖链默认」这条路（B: `state.hpp:29` 的 15000 ← `backend_terminal.cpp:395`），且另一处读数仍是 40ms（`TypedBackendProbe.kt:518`），两套值并存会让真机判据不可复用。
4. **trigger 形态**：B 是双 fork + 500ms（`real_ops.cpp:355-379`、`execution_binding.cpp:170`），点火者是参数；A 是 150/2000ms 加硬编码 crash_dump32（`TypedBackendProbe.kt:1401,1424-1428`），换机型即失效。
5. **站点/载荷几何**：site 0xB30AC（跳过 BTI）+ payload 0xFE628/480B/2520 余量，载荷先写站点后写（`sentry_hook.cpp:78-99`、`pagecache_steps.cpp:172-209`）。A 已对，但值是常数，libc++ 一变即错；B 是运行期算（`elf_hook.cpp:371-404`）。
6. **seq 接续**：B 在三处自动交棒（`real_ops.cpp:250-257,307-327,339-344`）；A 每段由 Kotlin 传（`TypedBackendProbe.kt:1265,1306,1358`），漏一处内核按防重放丢包，块永不落地（A 自己的重发兜底见 `pagecache_steps.cpp:383-390`）。
7. **载体必须可直读**：否则每块 fork helper（`pagecache.cpp:158-165,231-232`），769 块≈51s（`TempUiActivity.kt:126`）。B 的 5 秒级前提就是这个（见 §三）。

## §三 速度清单

| 项 | B | A | 说明 |
|---|---|---|---|
| 模块大小 / 块数 | 23520 B / 1470 块（`stage_runner.cpp:249-261`） | 12304 B / 769 块（`tools/lkm/ghostlock/ghostlock.c:1` + `Makefile:5` 的 -Os 产物；12304 B 见 commit `f898dbbc`） | 块数不是瓶颈 |
| 每块 helper 次数 | 可直读载体 0 次；vendor 不可读 2 次（old 读 + splice，`pagecache.cpp:158-165,231-232`） | 同结构（`pagecache_write.hpp:141` 走 file_fd，door 不可读时走桥 `pagecache_step_runner.cpp:75-83`） | 差在载体是否可读 |
| 每块 16B pread | 3 次：journal `chain.cpp:288`、write16 内 `pagecache.cpp:182`、verify `chain.cpp:321` | 2 次：write16 内 `pagecache_write.hpp:141`、verify `pagecache_steps.cpp:391` | A 少 1 次（无 journal） |
| 是否逐块回读 | 是，region.verify=true（`stage_runner.cpp:265`、`chain.cpp:318-335`） | 是（`pagecache_steps.cpp:391-416`），另带 2ms 轮询 + 200ms 预算（`:373-449`、`Assembled43284.kt:495`） | 首读不中即烧预算 |
| 日志粒度 | 每 region 一行 + 失败块一行 ⇒ 1470 块约 5 行 | 每块 3-4 行（`pagecache_steps.cpp:359,375,394,413`）⇒ 769 块约 3000 行 | 差 2-3 个数量级 |
| 实测/换算 | 22:50 真机≈5s ⇒ 3.4ms/块 | 769 块 51s 实测（`TempUiActivity.kt:126`）=66ms/块，且是**不可读载体** | 4ce37fa7 已换可直读 /system/lib64/libc++.so |

换算（A 按 3.4ms/块）：769×3.4ms≈**2.6s**；若回读首读不中吃满预算：769×200ms≈**154s**；若退回不可读载体：769×69ms≈**53s**。B 块数是 A 的 1.9 倍仍快 10 倍，差别只在载体可读性与每块日志/重试。

## §四 按钮接入

B 的一条命令：`GhostlockUserService.runExploit`（`shizuku/GhostlockUserService.kt:26-55`）→ `appCallArgs`（`:111`、`shizuku/AppCallArgs.kt:19-31`）= `[bin, --ghostlock-app-call, --enable-status-record]` + GLKv3 文档走 stdin（`main.cpp:104-105`）→ Pipeline → `backend_terminal.cpp:390-437`。**参数全在文档，不在 argv。**

B 的字段名与来源（照抄即可）：

- `backend.cve_2026_43284.execution.wait_timeout_ms` — 默认 15000（`state.hpp:29`；Kotlin 写侧 `profile-core/.../NativeProfile.kt:325`，清单 `Cve2026_43284Fields.kt:37-41`）
- `...execution.module_poll_attempts` / `module_poll_interval_ms` — 40 / 5（`state.hpp:30-31`，消费点 `lkm_window.cpp:78-93`）
- `...execution.late_load_args` / `selinux_exec_context` — 0 / 默认（`schema.hpp:89-91`；argv 组装 `lkm/lkm_image.cpp:418-472`：`late-load --kmi <label> --allow-shell`）
- `carrier_path` — 文档优先，缺省取设备上第一个可用的默认（`execution_binding.cpp:141-148`）
- `kmi` / `lkm_path` — native 约定（release 推导），不进 profile（`profile-core/.../ProfileLayout.kt:186-193`）

A 的临时按钮（`tempui/TempUiActivity.kt:252` → `TypedBackendProbe.run`）要照它接，需把这些 Kotlin 常量提升为同一组字段：`carrierPath`(`:1332`)、`modulePath`(`:1330`)、`patch1Target`、`hookTarget/hookSymbol`、hook plan 四字(`:1175-1206`)、`siteOffset/payloadOffset`、`displaced`、`seq`(`:1265,1306,1358`)、`triggerDelay`(`:1401`)、`waitBudget/waitPoll/success/failure/inflight/moduleMarker`(`:1437-1443`)、`imageBudgetMs`(`:495`)、SA 材料（`saGuard/saInit`）。

## §五 结论

按 §一 的 seq 接续、restore、journal、载体候选、wait 默认值五项移植 + §二 第 1 条接线后，A **能**达到 B 的 5 秒级并出现 LkmLoaded：块数更少（769 vs 1470）、载体已可直读、每块 2 次 pread + 1 报文，与 B 同构甚至更省。

残留风险：

1. A 的站点/载荷是写死常数（`sentry_hook.cpp:78-99`），系统 libc++ 一换即错；B 是运行期算（`elf_hook.cpp:371-404`）。
2. A 的链在 Kotlin（1548 行）且与 native 步骤重复，移植后必须删旧路径，否则两条流互撞（B 只有一条 `run_chain`，`chain.cpp:437`）。
3. wait 默认值三处不一致（40ms 自检 / 5000 攻击 / 15000 文档），不统一仍会假超时。
4. restore 接线后必须保证「先停写、后还原」的顺序（`chain.cpp:350-359`），否则边回滚边写。
5. 模块身份不同（A 12304 B 退役形态、B 23520 B glk 形态），marker 都由 `root_cmd.sh` 打（A `tools/lkm/ghostlock/root_cmd.sh:46,99`；B 同文件 `:46,74`）；若 A 的模块不自卸载，`/sys/module/dirtyfrag` 常驻，"出现又消失=Failed"分支（`real_ops.cpp:421-429`）行为与 B 不同。
