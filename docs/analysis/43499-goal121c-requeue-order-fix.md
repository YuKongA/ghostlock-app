# 43499 · 第 121 轮（终）：**requeue 与 trigger 无顺序约束** —— 精确诊断 + 精确改法 ✓✓✓

## 一、诊断（✓✓✓，三处逐字 ✓）

```
① `requeue()` 的唯一调用点：`multicast_route.hpp:256` ✓ ⇒ 在 `execute()` 内 ⇒ **由调用线程（主线程）执行** ✗；
② `owner_main`（`:510-531` ✓）的协议：`lock_pi(target)` ✓ → 等 `waiter_ready` ✓ → `owner_started = 1` ✓
   → `lock_pi(chain_word)` ✓ —— 注释逐字：**"The waiter holds it while it is parked, so this call is the chain:
   it returns only once the waiter has released it after its trigger"** ✓（⇒ owner 等的是"waiter trigger 后放链" ✓）；
③ `waiter_main`（`:471-489` ✓）：`waiter_waiting = 1` ✓ → `wait_requeue_pi_until(chain, target)` ✓
   → **`trigger()`** ✓ → `route_done = 1` ✓ → `unlock_pi(chain_word)` ✓。
⇒ ⇒ **⇒ 结论：`requeue`（主线程 ✓）与 `trigger`（waiter ✓）**分属两线程、且无任何顺序约束** ✗✓；
   ⇒ 而退役里二者是**同一线程序列**（race 线程：`CMP_REQUEUE_PI` ⇒ … ⇒ `route_done` ✓）⇒ **顺序确定** ✓。
⇒ ⇒ ⇒ **⇒ 与"14/14 comm=15（写没落）"的读数**完全吻合** ✓✓✓。
```

## 二、精确改法（✓，一处移动 ✓）

```
把 `requeue()` 的调用从 `execute()`（`:256` ✓）**移到 `owner_main`** ✓ ⇒ 位置：`owner_started = 1`（`:521` ✓）
  之后、`lock_pi(chain_word)`（`:524` ✓）**之前** ✓：
  · 于是 owner 的顺序变成：占 target ✓ ⇒ 置 owner_started ✓ ⇒ **执行 requeue（内含 `cmp_requeue_pi` + settle ✓）**
    ⇒ 再 `lock_pi(chain)` ✓（⇒ 那时 waiter 已 trigger 并释放 ✓）；
⇒ ⇒ 与退役的差别只剩"由 owner 而非 race 线程"✓（⇒ 同族 ✓，且**顺序确定** ✓）。
判据（两步 ✓）：① `main: victim comm after len=` 变 **0/8** ✓（本树自带即时旁证 ✓）；② `main: victim uid=0` ✓。
```

## 三、风险与回退（✓）

```
· 属**攻击关键路径** ⇒ 按守则 §2 先出计划（本页 ✓ + 第 121 轮 ✓）⇒ 落码后跑满门禁（含 host ✓）；
· 回退 = 把那一行移回 `execute()`（⇒ 一处 ✓）；⇒ `execute()` 侧应**保留** `requeue()` 的调用语义 ✗ ——
  确切地说：**移动**（一处删、一处加 ✓）⇒ 不得两处都调（⇒ 会二次 requeue ✗）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
