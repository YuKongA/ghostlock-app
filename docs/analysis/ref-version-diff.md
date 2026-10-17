# ref 版本差异审计：5f184760 为何能在真机跑通

> 只读审计（`git show/log/diff`），未跑 adb、未改代码。目标树 `/Users/nickji/Documents/Program/Analysis/ghostlock-app-ref`。
> **前提**：5f184760 是纯文档提交（`ANNOUNCEMENT.md` + `docs/archive/` 3 文件）；`git diff 5f184760^ 5f184760 -- src/` 为空，父提交 `3f329118` 与它代码逐字节相同。所以"5f 为什么通"= "5f^ 的代码为什么通"，差异只能去它取代的旧线找。
> 旧线 = 细粒度分支 `ref`（tip `6f7ded87`，2026-10-04，带真机 PASS 记录）及其备份 `backup/pre-squash-20261007`；5f 线把同一批工作压成 10-03~10-06 的 squash。代码常量里没有 769/1470，这两个数是运行时计数（`chain.cpp:380-381` 的 written/verified）。

## §一 时间线

| 日期 | 提交 | 标题（截断） | 解决的现象 |
|---|---|---|---|
| 10-03 | 9999aedb | steps chain orchestration with rollback and terminus (B5-6) | 单终点、journal/rollback 骨架 |
| 10-03 | 86b33039 | pagecache write16/block sequence with injectable splice_io (B5-3) | 直读 + 只有发送成功才 `++next_seq` |
| 10-03 | 68abcfbf | patch#1 splicehelper, crash_dump bridge, hook apply/restore, lkm precheck, terminus (B5-9e) | 写→hook→trigger→wait 顺序；终点无条件 restore |
| 10-03 | a329104d | helper splice write source for file_fd<0 (B5-9g) | vendor 文件打不开时走 helper |
| 10-04 | 088f9c40 | full vermagic compare + opt-in rewrite (B5-9h-2/3) | 子串匹配→全串构造 + 分类 + rewrite（outcome=rewritten） |
| 10-04 | 5f2c3da4 | model kernel same_magic() in module precheck | 5.15 有 `__versions` 时只比空格后的尾段 |
| 10-04 | da517110 | wire libc++ hook apply into write/trigger/full staged runs | hook 在 trigger 前应用；hook_restored 记真实返回 |
| 10-04 | 6f7ded87 | 43284 full chain PASS with self-built LKM（旧线终点） | 旧线真机证据 |
| 10-05 | 38f13430 | S4-R1 schema authority（default/source/doc） | 默认值进 schema |
| 10-05 | 98b7c0cc | versioned LKM request channel (/dev/glk), LkmProxy | LKM 驻留窗口 |
| 10-05 | d9c8ab74 | bind LKM window callbacks in production/staged roots | 生产根也开/关窗 |
| 10-05 | f5077a8f | session-bound residency, 60s watchdog, pre-UMH node, **15s terminus wait** | wait 5000→15000；fd 关闭卸载；UMH 前建节点 |
| 10-05 | 5b3fb29f | **production hook guard Skip + shell-readable LKM markers** | `bti c` 必须 skip；/dev/dfm0 对 shell 域不可探测 |
| 10-05 | a04bdb5b | batch A logging（bounded run.43284 lines） | 全链诊断行、`finish` 汇总行 |
| 10-05 | fa23331e | S4 R2b drop the staged 43284 entry | 只留生产路径，删旁路 |
| 10-05 | 66ab4984 | S4-R4 43284 policy as document paths | 15000/40/5 落到 `state.hpp`/`schema.hpp` |
| 10-06 | 77608702/12cc8897/55e5d4f5 | squash 2026-10-06 (part3/3·2/3·1/3) | 5f 线的历史载体 |
| 10-08 | 3f329118 / 5f184760 | gradle 接线 / 归档 vr guard 文档 | 5f^ 与 5f（代码不变） |

## §二 逐文件差异表

5f^→5f：`src/` 无差异（仅 3 个文档文件）。下表为旧线→5f（`git diff ref 5f184760 -- src/core/backend/cve_2026_43284`，36 文件 +2469/−954）：

| 文件 | 变化摘要 | 影响的判据 |
|---|---|---|
| `execution_binding.cpp` | **新增 350 行**：生产路径绑定（guard Skip、hook 资产、LKM 窗口、module_poll） | 成败：生产链从这里分叉 |
| `stage_runner.cpp` | 2+/323−：删 staged CLI（R2b）；保留 `run.chain/run.wait` 行 | 成败：只剩生产路径；日志不变 |
| `steps/chain.cpp` | 179+/2−：逐块 fail 行、region/rollback/finish 行；终点加 LKM close | 日志/安全：不改变写序 |
| `lkm/lkm_image.cpp` | 16+/8−：late-load 加 `--kmi/--allow-shell`；包名走 schema 约定 | 成败：UMH 参数与 43499 对齐 |
| `lkm_window.cpp/.hpp` | 新增 189/200 行：/dev/glk 版本化通道、close 发 UNLOAD | 安全/成败：防模块常驻 |
| `real_ops.cpp/.hpp` | 76+/34−：alt 标记、wait 接受双标记、hook_applied 先置位、seq 交接 | 成败：LkmLoaded 判定与 restore |
| `schema.hpp` | 233+/28−：wait/poll 默认值与策略路径 | 速度：15000/40/5 |
| `glkv3_schema.hpp` | 49：wait/module_poll 进 wire | 契约 |
| `pagecache/pagecache.cpp` | **0（两线完全相同）** | 写原语不是差异点 |

## §三 五项核对结果

1. **vermagic 改名——一直有，不是 5f 新增。** 088f9c40 把 `vermagic_fragment` 子串匹配改为 `required_vermagic()`（`lkm_image.cpp:388-416`）+ `classify_vermagic_diff()`（`:219`）+ `rewrite_vermagic()`（`:574-621`）；`outcome=rewritten` 见 `:365`、`stage_runner.cpp:630`。5f^ 与 5f 相同；5f2c3da4 只补 same_magic 尾段比较（`:541-548`）。
2. **stage_runner finish 行与退出码——行一直有，`finish` 汇总行是 5f 线新增。** `run.chain` 的 `written/verified/rollback_incomplete/hook_restored` 旧线已有（ref `stage_runner.cpp:742-756`，5f `:699-726`）；`finish` DiagLine 由 a04bdb5b 加在 `chain.cpp:374-382`；`hook_restored` 由 da517110 从"恒 true"改为真实返回（`chain.cpp:358`）；退出码 `:757-791` 自 68abcfbf 未变。
3. **chain terminus/journal——一直有。** 无条件 restore 自 68abcfbf（5f `chain.cpp:354-359`）；journal/rollback 自 9999aedb/68abcfbf（`:153-173`、`:285-305`）；5f 只增 LKM close（`:360-366`）与日志，写序未变。
4. **pagecache use_helper 与 seq——一直有，且两线文件相同。** `file_fd>=0` 直连、否则 helper（`pagecache.cpp:221-238`，a329104d）；`next_seq` 只在整条 datagram 发送成功后 `+1`（`:255-258`，86b33039）。
5. **wait 15000 与 module_poll——5f 线新增。** 旧线是 5000（ref `backend_terminal.hpp:71`、`stage_runner.cpp:860`）；f5077a8f 把生产改成 15000，66ab4984 落到 schema 默认（`state.hpp:29-31`、`schema.hpp:183-203`；运行时取值 `backend_terminal.cpp:395`）。module_poll 40/5 也只在 5f 线出现。

## §四 "必须有的逻辑"清单（按重要性）

1. **生产 hook guard = Skip**（`execution_binding.cpp:241`，5b3fb29f）。本机 sentry 入口有 `bti c`；Reject ⇒ `HookFailed(12)`、hook=0、不 insmod ⇒ 永远等不到 LkmLoaded。缺必失败。
2. **wait 接受 shell 可读标记**（`real_ops.cpp:408-414` + `real_ops.hpp:67-68` + `root_cmd.sh`，5b3fb29f）。只有 /dev/dfm0 时，Enforcing 下 shell 域 `getattr` 被拒 ⇒ WaitTimeout(15)，即使 LKM 已加载。缺必假超时。
3. **wait 预算 15000**（`state.hpp:29`、`schema.hpp:187`、`backend_terminal.cpp:395`，f5077a8f/66ab4984）。UMH 扫 /data/app 冷缓存可超 5s；5000 会假超时。
4. **全串 vermagic + 尾段比较 + 可重写**（`lkm_image.cpp:388-416/541-548/574-621`，088f9c40/5f2c3da4）。自建 ko 的选项尾段不同；旧子串判定放过错镜像或拒绝可修镜像，rewrite 后 precheck 才通过（outcome=rewritten）。
5. **hook 在 trigger 前应用、restore 在终点无条件执行**（`chain.cpp:588-607/354-359`，da517110/68abcfbf）。trigger 必须命中 libc++ 才会 insmod；restore 缺失则 hook_restored=0 且 libc++ 留补丁。
6. **LKM 窗口 close/UNLOAD**（`chain.cpp:360-366` + `lkm_window.cpp`，98b7c0cc/d9c8ab74/f5077a8f）。开窗后终点必须关；fd 关闭卸载 + 60s watchdog 防常驻。
7. **journal/rollback**（`chain.cpp:153-173/285-305`，68abcfbf）。写/验失败按新→旧回滚，防半写与 rollback_incomplete 静默。
8. **pagecache 直连优先 + seq 成功才递增**（`pagecache.cpp:26-31/221-238/255-258`，a329104d/86b33039）。可直读时不走 helper；失败不消耗 seq，避免 anti-replay 丢后续包。
9. **只保留生产路径**（fa23331e 删 staged 入口）。保证真机跑的是 App-call 同一条链，而不是旁路。

## §五 与 ghostlock-app（当前 HEAD 3c391b2f，2026-10-16）的差距

- 重写已删 `src/core`（b0e9f499），现为 `core/src/backend/cve_2026_43284/{landing,steps,lkm,hook}`；无 `stage_runner.cpp`/`chain.cpp`/`pagecache.cpp`。
- 已有：pagecache 直连/helper 分派与"成功才 ++seq"（`landing/pagecache_write.hpp:164/206`）。
- 缺/不同：`lkm/module_facts.cpp:212-224` 只有 `vermagic_matches_kernel` 尾段比较，无 `required_vermagic` 构造与 `rewrite_vermagic`（grep 无 rewrite）；wait 预算探针仍传 5000（`TypedBackendProbe.kt:1499`），非 15000；hook guard 由 `hookPlan(guardPolicy=0)` 传入（`Assembled43284.kt:258`），无具名 Skip/Reject 判定；`hook_restored`/`rollback_incomplete` 在 core 无对应符号（收尾由 Kotlin 分步完成，`Assembled43284.kt:561`）。
- 结论：写原语两条路已对齐；§四 的 1/2/3/4 在现有树中没有等价物或仍是 5000——这四项正是 5f 真机通过、当前树仍可能失败的分水岭。
