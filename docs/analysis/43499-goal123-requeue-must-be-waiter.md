# 43499 · 第 123 轮：**"落在*调用者*的栈"是字面的** —— requeue 的宿主线程 ✓✓✓

## 一、退役里 park 与 requeue **同线程**（✓✓✓，逐字已证 ✓）

```
`race/threads.cpp:58-80`（第 77 轮逐字 ✓）：**同一个函数**（race 线程体）里依次做：
  `race->waiter_waiting.store(1)` ✓ ⇒ `futex_op(&wait_futex, FUTEX_WAIT_REQUEUE_PI, …, &target_futex)` ✓（**park** ✓）
  ⇒ `controller.execute(request)` ✓（⇒ 内含 `CMP_REQUEUE_PI` = **requeue** ✓）⇒ ghost disarm ✓ ⇒ `route_done.store(1)` ✓；
⇒ ⇒ **⇒ 即：**被 park 的线程自己**做 requeue** ✓ ⇒ ⇒ **⇒ "The copy landing on the **caller's** stack" 的"caller"
   指的就是**这个 waiter 线程** ✓✓（⇒ 字面含义 ✓）。
```

## 二、本树（✗✓，线程不同 ✓）

```
`waiter_main`：park ✓（`:479` ✓）⇒ `trigger()` ✓（`:484` ✓）⇒ … ✓ ⇒ **waiter 不做 requeue** ✗；
`requeue()`（含 `cmp_requeue_pi` ✓）：由 `execute()` 调用 ✓（`:256` ✓）⇒ ⇒ **⇒ 调用者是 main 线程** ✗；
⇒ ⇒ ⇒ **⇒ 于是"拷贝落到调用者栈"会落到 **main 的栈** ✗ —— 而链上被替换的目标是 **waiter 的结构** ✗✓；
⇒ 第 122 轮已让"park ⇒ requeue"**串行** ✓（顺序对了 ✓）⇒ ⇒ **⇒ 但**宿主线程仍不同** ✗ ⇒ 落点仍不对 ✓。
```

## 三、改法（✓，第 121 轮计划①的精确版 ✓）

```
把 `requeue()` 的调用**从 `execute()`（`:256` ✓）移到 `waiter_main`** ✓ ⇒ 位置 = `trigger()`（`:484` ✓）**之前**、
  park 返回之后 ✓ ⇒ ⇒ 于是顺序与宿主都对齐退役 ✓：**park ⇒ requeue ⇒ trigger ⇒ route_done ⇒ unlock(chain)** ✓；
⇒ 一处删（`:256`）+ 一处加（waiter 内 ✓）✓ ⇒ 不得两处都调 ✗（⇒ 二次 requeue ✗）；
⇒ 该次路由的"链锁"顺序需复核 ✓（⇒ `owner_main` 的 `lock_pi(chain_word)`（`:524` ✓）等的是"waiter trigger 后放链" ✓
  ⇒ 若 requeue 也由 waiter 做 ⇒ 该握手仍成立 ✓）。
判据：① `main: victim comm after len=` 变 **0/8** ✓；② `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
