# 43499 · `CMP_REQUEUE_PI` 三种取值组合的硬数据（round 226）✓✓

## 读数表（✓，每格都有真机读数 ✓）

| 组合 | errno | 解释 |
| --- | --- | --- |
| `val=1`, `wait=0` | **35 EDEADLK** ✗ | 内核判"调用者已持有 `uaddr1`"或会成环 ✓ |
| `val=1`, `wait=1` | **11 EAGAIN** ✗ | `*uaddr1 != val`（值语义是活的 ✓） |
| `val=0`, `wait=0` | **22 EINVAL** ✗ | 参数被拒（`val` 与内核期望不符 ✓） |

## 由此得到的结论（✓）

```
1. **失败不在 `val` 的取值** ✗ —— 三种组合都失败 ✓；`EINVAL` 说明 val=0 被拒、`EAGAIN` 说明
   val=1 时字值不匹配、而 **EDEADLK 最有信息量** ✓：内核判"这次 requeue 会成环 / 调用者已持有 uaddr1" ✓；
2. ⇒ 与快照逐项对照时，要查的不是参数，而是**字的持有关系**（谁持 chain/wait/target ✓）；
3. 顺带确认：`pre-target-lock` 读数显示**上一轮的 target 持有未被释放** ✗（第 2 轮起 -1 ✗）
   ⇒ 我"每轮归零"发生在**其后** ✗ ⇒ 这一点已登记 ✓。
```

## 下一批（✓）

```
逐项对照快照的**持有关系**：waiter 持 chain ✓（我同 ✓）→ 谁持 wait ✗（快照的 wait 是它自己的字 ✓）
⇒ 检查我是否需要让 **wait 字在 park 前有特定状态** ✓（快照里它初值 0 ✓、无人持 ✓ ⇒ 与我相同 ✗）；
⇒ 以及 owner 持 target 的**时机**（我已加 pre-target-lock ✓，读数正常 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
