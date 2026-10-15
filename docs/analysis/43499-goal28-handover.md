# 43499 · 接手总结（第 28 轮；**第 43 / 82 轮刷新** ✓）：现状 · 剩余 · 跑法 · 纪律

> **第 79–81 轮增量（✓✓，接手请先读这段）**
>
> 1. **ghost disarm 已实现** ✓（第 76/77 轮"查旧代码"发现 ⇒ 第 79 轮落码 ✓）：
>    `multicast_route.hpp` 的 `waiter_main` 收尾（`return nullptr;` 前 ✓）补退役同形动作 ——
>    伪地址 `0x80000000|getpid()` + `Ops::futex(…, FUTEX_LOCK_PI, 0, &expired, …)` ✓；
>    真机读数 **`main: mcast ghost disarm ret=-1 errno=110`** ✓（与归档**逐字相同** ✓）。
>    配套：`needs_ghost_disarm` 配置字段 ✓ + `#include <unistd.h>` / `<linux/futex.h>` ✓。
> 2. **"有无对照"法** ✓（第 80 轮 ✓，可复用）：把归档 `[*]` 行**去数字取形状**（`LC_ALL=C` ✓）
>    与我的 `main:` 行逐类比 ✓ ⇒ 一次列出"归档有、我没有"的全部条目 ✓（归档共 36 条形状 ✓）。
> 3. **由此列出我缺失的三类** ✗：① `WNb: **private scratch repair**`（**从未实现** ✗，见下）；
>    ② `fast repair payload prebuilt` / `firing prebuilt init_cred+8 repair`（计划过、未落 ✗）；
>    ③ `child is root!` / `SELinux permissive`（成功的结果 ✓）。
> 4. **W1b「私有 scratch 修复」全貌** ✓（第 81 轮，`steps.cpp:285-321` 逐字 ✓）：
>    W1 之后那页被 **quarantine（隔离）** ✓，只有"私有 scratch 修复"成功才 **release** ✓；
>    修复请求 = **Zero + leaf=1** 写 `base + mcast.buffer_size` ✓（我树已知该式 ✓）；
>    轮数取自 **`w1_scratch_repair_attempts`** ✓（**配置键本树已有** ✓ ⇒ ⇒ **键在、行为未落** ✗）；
>    ⇒ **⇒ 若 W1 污染 scratch ⇒ 不修复 ⇒ W2 用的页/状态不干净** ✓ ⇒ 这**很可能**是"W2 写不生效"的直接原因 ✓✓。
>
> **下一步（②/④ 都已可落码 ✓）**：在"修复轮"处按退役同形加循环 + `usleep(50000)` ✓ + 打印
> `W1b: private scratch repair attempt` / `private scratch repaired` ✓ ⇒ 判据 `main: victim uid=0` ✓。

---

> **第 29–42 轮增量（✓，接手请先读这段）**
>
> 1. **抓到并修掉一个自己引入的回归** ✓：第 2 轮按退役公式把三步的 `preserve_child` 改成 false/false/true ✗
>    ⇒ 使 **Credential 写的 value 变成 0** ✗（真机 `main: step=w2 … value=0x0` ✗）⇒ 永不 root ✓；
>    第 34 轮**按写值**（而非按注释 ✓）回退为 **true/true/false** ✓ ⇒ 写值恢复 `0xffffff802abfd588` ✓（与旧目标逐字同 ✓）。
> 2. **判据互斥关系**：`write_layout.hpp:12-16` 规则句 = "preserve 的写必须带 value，不 preserve 的必须不带" ✓ ⇒
>    **写值是否非零**就是极性的正确判据 ✓（注释会骗人 ✗）。
> 3. **采纳判据早已存在** ✓：`main.cpp:886-889` 的 `layout_accepts_page` 门 + `:880 gate probe` ✓；
>    第 31 轮曾误判"缺失" ✗（grep 被 `head -10` 截断 ✗）⇒ 第 35 轮已更正 ✓、重复代码已撤回 ✓。
> 4. **真机证据**：`accepts=0` **会出现** ✓（门不是摆设 ✓）；越界目标 `0xffffffc0…` **偶发 ≈1/8** ✓。
> 5. **直映射守卫已实现并生效** ✓（第 41 轮，判据经第 39/40 轮测量修正 ✓）：
>    `target ∈ [kAliasBase, kAliasBase + (1<<38))` ✓ ⇒ 真机 `target-outside-direct-map` 触发 **3** 次 ✓。
> 6. **台账 13 条已全部对齐或无差异** ✓（docs/analysis/43499-oldcode-ledger.md ✓）。
>
> 剩余：① `fast_repair` 时序两项（预建提前 ✗ / 5000 µs ✗，锚点见 goal25 文档 ✓）；
> ② 或换方向：查"写值非零 ✓ + 页被采纳 ✓ + `successes=1` ✓ 而 `uid` 仍 2000 ✗"之间的那一段 ✓
> （⇒ 需独立读回写是否落到目标 ⇒ 别名读 ⇒ L 级 ⇒ 先计划 ✓）。

## 一、目标与硬判据（✓）

```
目标：「实现43499和43284取得uid0」。唯一硬判据 = 设备日志出现 **`main: victim uid=0`** ✓。
状态：**仍未取得** ✗（本次会话所有跑次里 `uid` 恒 2000 ✗）⇒ 目标保持 active ✓。
```

## 二、已修好的（✓，都有真机判据 ✓）

```
① 空实现 landing ✗ → 绑真 landing ✓；② 引擎未武装 ✗ → 已武装 ✓；③ 触发线程 ✗ → 归位 ✓；
④ **零写值被校验器拒绝** ✗ → 放宽 ⇒ 三步全 `won` ✓；⑤ **缺 handoff 调用** ✗ → 补上 ⇒ `handoffs=1` ✓；
⑥ **步骤级重试** ✓（与退役 `retry_write_stage` 同义：15 轮 ✓ 判据 = 受害者 uid ✓）；
⑦ **三步的 preserve 臂全反** ✗ → 按公式 `preserve = (leaf != 0)` 订正 ✓（含两处 W2b 修复请求 ✓）；
⑧ **fd 泄漏** ✗（`kMaxReclaimSteps` 64→1024 ✓）⇒ `PFOPEN fd=` 由 512→24813 ✗ 变为**恒 3** ✓；
⑨ **子进程/僵尸泄漏** ✗（成功路径补 `kill_children` ✓ + `stop_child` 改 kill+`waitpid` ✓）⇒ EAGAIN 消失 ✓；
⑩ **`slab_drain`** ✓（退役 primitives.cpp:30 同形，Android 守卫 ✓）；
⑪ **`w2` 结论具名** ✓（未 root ⇒ `w2-not-rooted` ✓，不再借 `won` 掩盖 ✗）；
⑫ **一次性页槽** ✓（`memory/page_slot.hpp` ✓ + 主机测试 ✓ 且**能失败** ✓，`check` 108→109 PASS ✓）。
```

## 三、剩余的唯一一条（✓，已拆到可落码 ✓）

```
`fast_repair` 那套机制（退役唯一未实现的功能差异 ✓；台账：docs/analysis/43499-oldcode-ledger.md ✓）：
  1. W2 分支：**主路由前** prebuild（修复请求 `data_alias(init_cred)+8, Zero, leaf=1` ✓）⇒ 存入页槽 ✓；
  2. 主写之后再 **activate** ⇒ 用**存好的页**跑修复路由 ✓（不再整重跑一遍准备 ✗）；
  3. 该次路由的进入延迟 **5000 µs** ✓（退役 `race.reset(fast_repair ? 5000 : …)` ✓；
     注意**归属 race 层** ✗ —— 本树 race 无此入口 ✓ ⇒ 与 1/2 同批 ✓）；
  4. 每条出口 `discard` ✓（守则 §5 ✓）。
判据：`main: victim uid=0` ✓；辅助：`route status=0/0 attempts=16 calls=1 successes=1` 不变 ✓。
```

## 四、跑法与预算（✓，血泪经验 ✓）

```
· 推：`adb push build/native/ghostlock /data/local/tmp/` ✓；跑：`cd /data/local/tmp && ./ghostlock < wire-43499-collide-F.framed` ✓；
· **单跑数分钟**（w1≤15 + w2≤15 + w3≤3 轮 ✓）⇒ 一次调用最多 2 跑 ✗；批量必须用**后台作业** ✓；
· 夹具 `stages` 数字被 **Lua 硬断言**核对 ✓ ⇒ 改它会报 `main: refused (plan-syntax)` ✗（来自 Lua 沙箱 ✓）；
· 门禁三连：`make -C core -j1 check`（109 PASS ✓）/ `make -C core -B android …`（0 警 ✓）/ `./gradlew markdownlint` ✓。
```

## 五、纪律（✓，本次会话的教训 ✓）

```
· Makefile 插入规则：新规则**末尾必须显式收尾** ✗（否则 Tab 行被吸走 ⇒ 产物被覆盖 ⇒ 门禁假绿 ✓）；
· 断言"能失败"时**必须同时确认"被运行的是谁"** ✓（跑一次二进制看输出 ✓）；
· 源码回位 ≠ 产物回位 ✓ ⇒ `rm -f` 后重跑 ✓（守则 §8 ✓）；
· 读数不得含糊：`won` ≠ root ✓ ⇒ 已改成具名 `w2-not-rooted` ✓；
· grep 语义而非名字 ✗（`RED` 位在本树叫 `head_word = 1U` ✓，第 21 轮差点误判 ✓）。
```
