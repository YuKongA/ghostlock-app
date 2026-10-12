# 43499 · `select_stack` 两个缺失演员的移植规格（round 184）

## 一、multicast 的演员（读码 ✓，作为对照）

| 线程 | 关键点 | 位置 |
| --- | --- | --- |
| waiter | **先 `lock_pi(chain)`** ✓ ⇒ park 在**自己持有的那个字**上 ✓ ⇒ park 返回后**在本线程**调 `trigger()` ✓（拷贝要落在它的栈上 ✓） | `:449-476` ✓ |
| owner | 先 `lock_pi(target)` ✓ ⇒ 等 waiter_ready ✓ ⇒ 再 `lock_pi(chain)`（**这就是链** ✓） | `:480-499` ✓ |
| consumer | 窗口内 `sched_setattr`（被拒则 `FUTEX_LOCK_PI` 兜底 ✓），按 `nice_modulus` 轮转 ✓ | `:503-…` ✓ |

## 二、select 的结构差异（**这是关键** ✓）

```
multicast：载荷经 `setsockopt(MCAST_BLOCK_SOURCE)` 拷进内核 ⇒ 由 **waiter 线程**在 park 后触发 ✓；
select   ：载荷是 **fd_set 本身** ⇒ **执行 `pselect` 的线程就是 waiter** ✓；
           ⇒ 内核在 `pselect` 内部 park 该线程，而其形状来自**被拷进去的 fd_set 词** ✓
           ⇒ 因此：select 的 waiter = "拿着 chain 字去 pselect 的线程" ✓（不需要 trigger ✗）；
           ⇒ 而 owner（`PiRace` ✓）与 consumer（本树尚缺 ✗）与 multicast 同形 ✓。
```

## 三、要补的两枚（本 route 内 ✓）

1. **waiter 线程**：`lock_pi(chain)` ⇒ 置 ready ⇒ 等 owner 起来 ⇒ 调 `Ops::pselect_now(...)`（**在窗口内** ✓）
   ⇒ 返回后置 done ✓ ⇒ `unlock_pi(chain)` ✓；
2. **consumer 线程**：窗口内循环 `sched_setattr(waiter_tid, nice)` ✓（被拒时 `FUTEX_LOCK_PI` 兜底 ✓），
   按文档的 `nice_modulus` 轮转 ✓、`burst_calls`/`max_calls` 约束 ✓；
3. 主线程（owner 侧）：`PiRace` 照旧 ✓（它已覆盖 `lock_pi(target)` + `cmp_requeue_pi(chain, target)` ✓）。

## 四、判据（落码后 ✓）

```
真机（**多次统计** ✓）：main: select route … successes= 由 **0 变 ≥1** ✓ ⇒ 内核搬动过等待者 ✓；
随后再看 `main: selinux enforce` 1→0 ✓、`main: victim uid=0` ✓。
```

## 五、风险（§5 ✓）

- 新增两线程 ⇒ 生命周期（创建/等待/回收 ✓）与 `PiRace` 的 `disarm/destroy` 顺序要对 ✓；
- `pselect` 在窗口内阻塞 ⇒ 必须有**解除**路径（`close`/信号 ✓）否则收尾会挂 ✓ ⇒ 落码时先写解除 ✓。
