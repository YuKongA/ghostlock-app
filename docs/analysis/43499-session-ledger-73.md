# 43499 · 本会话总账（第 73 轮；**第 86 轮刷新** ✓）· 现状 / 已修 / 已排除 / 剩余

> **第 74–85 轮增量（✓✓，接手先读）**
>
> 1. **`ghost disarm` 已实现** ✓（第 76/77 轮查旧码发现 ⇒ 第 79 轮落码 ✓）：
    `waiter_main` 收尾补退役同形动作（伪地址 `0x80000000|getpid()` + `FUTEX_LOCK_PI` 超时 0 ✓）⇒
    真机 **`main: mcast ghost disarm ret=-1 errno=110`** ✓（与归档**逐字相同** ✓）；
    ⇒ 但 `uid` 仍 2000 ✗ ⇒ **该动作非充分条件** ✓（是否必要 ⇒ 无从判 ✓）。
> 2. **"有无对照"法** ✓（第 80 轮）：归档 `[*]` 行**去数字取形状**（`LC_ALL=C` ✓）逐类比 ⇒
    列出"归档有、我没有"**三类** ✗：`WNb: private scratch repair` / `fast repair prebuilt+firing` / 成功结果行 ✓。
> 3. **W1b 全貌** ✓（第 81 轮 `steps.cpp:285-321` 逐字 ✓）：W1 后那页被 **quarantine** ✓，修复成功才 **release** ✓；
    修复请求 = **Zero + leaf=1** 写 `base + buffer_size` ✓；轮数键 `w1_scratch_repair_attempts` ✓（**已在** ✓）。
> 4. **但第 85 轮实测**：`main: repair-round step=w1 target=0xffff800000038108` ✓ ⇒
    **w1 的修复确实执行** ✓ ⇒ ⇒ **W1b 不是"缺失"，而是"少跑几次 + 无声"** ✗ ⇒ 优先级下调 ✓
    （⇒ 只在执行路径**之前**曾误判过两次，均已收回 ✓）。
> 5. **补读数三连** ✓（⇒ 每次都换回决定性读数 ✓）：`clean=`（⇒ 与归档 `clean=1/1` 同 ✓）、
    `disarm ret/errno`（✓）、`repair-round step/target`（✓）；⇒ 另修了自己引入的 host 构建断裂 ✓（第 85 轮 ✓）。
>
> **★★★★★ 第 130–135 轮：根因已定位并改正（最重要的一段）**
>
> 1. **根因**（第 130/131 轮）：本树的写目标原为 **`task->comm`（abi 0x7a8）** ✗ —— 而退役 `steps.cpp:165` 是
>    `"W2: cred", child_task + **task_cred_off()**`（abi **0x798** ✓）⇒ ⇒ **⇒ 写一直落在"任务名"字段，
>    而不是 `cred` 指针 ⇒ 于是"机制读数全对、`uid` 恒为 2000"有了唯一自洽解释** ✓；
>    另：修复轮的请求目标原为 `init_cred + 8` ✗ ⇒ 那**根本不改变任何进程身份** ✓。
> 2. **改正**（第 132/133 轮）：`main.cpp:654` 改用 `victim_cred_off` ✓；`main.cpp:843` 的修复请求改用
>    `victim_target` ✓ ⇒ 传导链已核：`repair_request.target` → `repair_values.target` → `repair_stage.payload_target` ✓。
> 3. **真机验证**（第 132/133/135 轮）：w2 目标全部为 `task + cred_off` 形态 ✓；`value=0xffffff802abfd588`
>    （= 归档 `:46` 的 `alias=ffffff802abfd588` **逐字相同** ✓）；`layout matches=1 accepts=1` ✓。
> 4. **门禁**：`check` **109 PASS** ✓；`android` RC=0，**0 error / 0 warning** ✓；`lint` 0 ✓；整跑形态正常（`w1`+`w2` ✓）。
>
> **⇒ 现状**：**形式侧已无一处遗留** ✓（target ✓ / value ✓ / accepts ✓ / 页 ✓ / 编码 ✓ / disarm ✓ / fast repair ✓ / settle ✓ / 顺序 ✓）；
> **而 `main: victim uid=0` 仍未出现** ✗ ⇒ ⇒ **⇒ 剩余一环在**内核侧**（"写是否真正执行"）** ✗。
> **⇒ 建议接手者**：① 先跑一次确认上述形态 ✓；② 再专攻"内核是否落实"✗（⇒ 可试：把 `accepts`/`stamp`/`arm` 三类读数
> 与归档 `:114/:133/:139` 逐行对照 ✓，或对"写入点相对 requeue 的相位"做受控小实验 ✗）。
>
> ---
>
> **第 116–124 轮增量（✓✓✓，最重要的四件）**
>
> 1. **判据被验证为真** ✓（第 117b/118 轮）：本树自带旁证 `main: victim comm after len=` 在 **14/14 轮**恒为 `15` ✗
>    （⇒ 按本树自己的注释"len 变 0/8 ⇒ 写落了；仍 15 ⇒ 没落"）⇒ ⇒ **⇒ 写从未落过** ✗ ⇒ "机制全对而 uid 不动"**不是判据问题** ✓。
> 2. **`ghost disarm` / `fast repair` 预建+激活均已落地** ✓（第 79/103/105 轮，读数与归档同形 ✓）；
>    第 119/120 轮**更正**：requeue 前的 settle **本就存在**（`config.settle_us`）⇒ 我第 109/111 轮加的 `pre_requeue_settle_us` 是**重复件** ✗ ⇒ 已撤回 ✓。
> 3. **顺序与宿主已逐条对齐退役** ✓（第 122/124 轮）：`waiter_waiting` 改为 park **之后**置位 ✓；`requeue()` 从 `execute()`（main 线程）**移入 `waiter_main`**（park 之后、`trigger()` 之前）⇒ **park 与 requeue 同线程** ✓；⇒ 结果**仍未翻动** ✗（`comm=15`、`uid=2000`）⇒ 该候选已排除 ✓。
> 4. **域假设已否证** ✓（第 114 轮）：`run-as com.ghostlock.app`（`u:r:runas_app:s0`）在归档那次的路径形态（`/data/user/0/com.ghostlock.app/files`）跑同一夹具 ⇒ 读数与 shell 域**逐字相同** ✗。
>
> **⇒ 剩余候选仅一条**：**内核侧对该目标的语义校验**（`_ref`/大小/`pi_blocked_on` 指向）✗ —— 无内核源码可查 ⇒ 只能由读数反推 ✓。
> **⇒ 建议下一步**：以 `comm after len=` 为即时判据做小步试错 ✓（每次只改一处、跑真机、看它是否变 0/8，而不是再看 `uid`）。
>
> ---
>
> **剩余（按优先级 ✓）**：① `fast_repair` 的**预建/激活**（计划 ✓ + 页槽已接 ✓，差"预建提前 + 5000 µs" ✓）
> —— **搬移清单已备**（`docs/analysis/43499-goal91-move-checklist.md` ✓，**现行为准** ✓）：
> 提前点 `if (!run_prepare(prepare, prepared2, &repair_refusal) ||` ✓ ⇒ 上移到主路由
> `stage_refusal = run_window_stage_with_attempt(` ✓ 之前；发送点 `send_bytes(prepared2.shaping_fd, repair_page, …)`
> **已在用页槽** ✓ ⇒ 故这只是一次搬移 ✓；另加"该次 5000 µs" ✓ 与出口 `discard` ✓；
> ⇒ ⚠ **绝对行号会漂移** ✗（第 91 轮教训 ✓）⇒ **一律用内容锚点** ✓；
> ② W1b 的轮数 ✗ **已闭合**（第 88 轮：归档亦 `attempt 1/3` ✓ ⇒ 两边第 1 次即过 ✓，只差打印 ✓ 已补 ✓）；
> ② W1b 的**轮数循环**（键已在 ✓，纯行为 ✓）；③ **域/宿主**实验 ✗（需 app 侧 ✓，已留档 ✓）。
> 判据始终一条：`main: victim uid=0` ✓（**仍未取得** ✗）。

## 一、目标与硬判据

```
目标：「实现43499和43284取得uid0，遇到瓶颈则查阅旧代码」；
唯一硬判据 = 设备日志出现 **`main: victim uid=0`** ✓ ⇒ **仍未取得** ✗ ⇒ 目标**保持 active** ✓。
```

## 二、本会话修好并各有真机判据的（✓，共 14 项）

```
① 空实现 landing ⇒ 真 landing ✓；② 引擎武装 ✓；③ 触发线程归位 ✓；④ 零写值被拒 ⇒ 放宽 ✓；
⑤ 脚本补 handoff ⇒ `handoffs=1` ✓；⑥ **步骤级重试**（与退役 `retry_write_stage` 同义 ✓，跑满 15 轮 ✓）；
⑦ **三步臂全反** ⇒ 按**写值**判定极性并订正 ✓（含两处 W2b 修复 ✓）；
⑧ **fd 泄漏**（`kMaxReclaimSteps` 64→1024 ⇒ `PFOPEN fd=` 由 512→24813 变为**恒 3** ✓）；
⑨ **子进程/僵尸泄漏**（成功路径 `kill_children` + `stop_child` 补 `waitpid` ⇒ EAGAIN 消失 ✓）；
⑩ `slab_drain` ✓（退役 primitives.cpp:30 同形 ✓）；⑪ `w2` 结论具名 `w2-not-rooted` ✓；
⑫ **一次性页槽** + 主机测试（且**能失败** ✓，check 108→**109 PASS** ✓）；
⑬ **直映射守卫**（判据经第 39/40 轮测量修正 ⇒ 真机触发 3 次 ✓）；⑭ 补 `clean=` 读数 ✓。
```

## 三、已逐项排除（✓，均有真机读数或归档基线 ✓）

```
写值（= init_cred 别名 ✓）/ 目标同窗 ✓ / 臂 ✓ / 编码（`arm_for`→`put_word`→偏移 ✓）/
页内容（**逐字相等** ✓）/ 采纳判据（**早已存在** ✓，非缺失 ✗）/ 触发六项（与退役同形 ✓）/
`attempts=16`（与归档同 ✓）/ 轮间重建（无害 ✓）/ CPU 绑定（链路完整 + 无缺键 ✓）/
`cred` 偏移（夹具内自洽 + 与 5.15 布局吻合 ✓）/ 泄漏身份（每轮新 task、守卫每轮通过 ✓）/
路由判据：`status=0` ✓ **`clean=1/1`** ✓ `calls=1` ✓ `successes=1` ✓ `stamp=-1/99` ✓。
```

## 四、剩余（✗，两条）

```
① **域/宿主**：归档在 `u:r:untrusted_app:s0` ✓，我在 `u:r:shell:s0` ✗ ⇒ 抢占/perf/内存策略随域变 ✓；
   ⇒ 需"在 app 域里跑同一夹具"⇒ 当前树 app 侧源码处于**已删除**状态 ✗ ⇒ 已留档（一击即用 ✓）；
② **"写是否发生"的独立证据**：契约自述"43499 先写，任意读要等模块" ✗ ⇒ ⇒ 一般路径上**读回不可得** ✓；
   ⇒ 因此下一步应回到"让那一轮落上" ✓（而不是造读回 ✓）。
```

## 五、门禁（✓，全绿）

```
check → **109 PASS** ✓；android → **0 error / 0 warning** ✓；lint → 0 ✓。
```
