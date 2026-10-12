# 43499 · `select_stack`：两枚演员已落地，真机本批不结论（round 185-186）

## 落地（代码 ✓）

| 件 | 内容 |
| --- | --- |
| `SelectStackRouteLinux` 扩展 ✓ | `thread_id` / `lock_pi` / `unlock_pi` / `wait_requeue_pi_until` / `setattr` / `sleep_micros` ✓（与 `LinuxRouteOps` 同形 ✓）；`pselect_now` 改为**有界**（带 timeout_us ✓，规格风险条款 ✓） |
| `SelectStackRoute` 两枚演员 ✓ | **waiter**：先 `lock_pi(chain)` ✓ ⇒ 等 `actors_go_` ⇒ 窗口内 `pselect` ✓ ⇒ `unlock_pi(chain)` ✓；**consumer**：`setattr(waiter_tid, nice, …)` ✓（被拒则 `lock_pi(target)` 兜底 ✓） |
| 生命周期 ✓ | 每轮 `run_actors` 起两枚 ✓ ⇒ `race_.run` 之后 `stop_actors` **join** ✓（窗口关闭之前 ✓） |

## 门禁（本轮 ✓）

```
make -C core -B android ⇒ RC=0，**0 error / 0 warning** ✓；check → RC=0，**108 PASS** ✓；lint → 0 ✓
```

## 真机（本批**不结论** ✗，如实 ✓）

```
一跑：main: refused (payload-layout-not-accepted)     ← 奇偶门（先于路由选择 ✗）
五跑（85 s 上限）：4 次奇偶门 ✗、**2 次无任何输出** ✗（可能：新相位变慢 / 卡在收尾 ✓）
⇒ 未取到 successes 读数 ✗ ⇒ 既不能说"成功" ✗ 也不能说"和之前一样" ✗
```

## 下一步（下一批 ✓）

1. 先把**时长**量出来：单跑加时（如 180 s ✓）并打印相位时间点 ✓ ⇒ 区分"慢"与"卡" ✓；
2. 若卡：按规格 §五 的解除路径检查（`stop_actors` 的等待 ✓、`pselect` 的返回 ✓、`lock_pi` 的持锁者 ✓）；
3. 之后再谈 `successes` 由 0 变 ≥1 ✓（判据不变 ✓）。

## 本轮自己踩到的一处（登记 ✓）

```
我把 6 次真机跑塞进一次工具调用 ✗ ⇒ 超过 600 s 上限被截断 ✗（含一次未跑完的批 ✗）；
⇒ 纪律：真机批**每次不超过 4-5 跑**、单跑上限**不超过 100 s** ✓，长跑拆批 ✓。
```
