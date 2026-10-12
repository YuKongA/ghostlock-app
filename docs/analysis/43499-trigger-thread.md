# 43499 · 触发线程：select 的 pselect 应跑在 waiter 上（round 233）✓✓

## 读码对照（✓）

```
① multicast（本树 ✓，multicast_route.hpp:249-254）：
     requeue();
     while (shared_.route_done.load() == 0) Ops::yield();   // 等"park 刚返回的那个线程"完成触发 ✓
   注释原文：「触发**回到等待者线程**：退役原文说拷贝落在"调用者的栈"上（a race against skb page
              recycling）⇒ 触发它的线程必须正是"park 刚返回"的那个 ✓」
② 快照 select（cve-2026-43499-select-stack-route.cpp:176 ✓）：
     (void)futex_op(&race->wait_futex, FUTEX_WAIT_REQUEUE_PI, …);   // park ✓
     race->route_status = run_this_route(race, race->request);      // ★ **紧接着在 waiter 上跑** ✓
   而 run_this_route → SelectRoute::execute → **pselect**（:536 ✓）⇒ **pselect 在 waiter 线程上** ✓
⇒ ⇒ 我此前把 pselect 放在**主线程**（round 197 ✗）⇒ **触发线程错了** ✗ ✓。
```

## 为什么这能解释"写不落地"（✓）

```
擦除式写入的对象是"**陈旧 waiter 的形状**"✓，而那份形状的**拷贝载体**是 fd_set ✓、
其落点与**触发线程的上下文**绑定 ✓（退役原话：落在"调用者的栈"上 ✓）；
⇒ 主线程触发 ⇒ 内核 walk 到的是**主线程**的上下文 ⇒ 与页面里那份形状**对不上** ⇒ 写不发生 ✓。
```

## 下一步（下一批 ✓，改动明确）

```
把 select 的 pselect 挪到 **waiter 线程上、park 返回之后** ✓（= 快照的 run_this_route 位形 ✓）；
主线程只负责：置 go → requeue → 等 done ✓（与 multicast 的 execute 同形 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
