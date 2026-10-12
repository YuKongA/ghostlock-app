# 43499 · 等待者"在 park 中却不在 requeue 的可搬集合"（round 223）✓✓

## 本轮补齐的读数（✓）

```
① `errno = 0` 加在 park 之前 ✓ ⇒ 读数**不变** ⇒ **2 s 超时是真的** ✓（排掉"errno 陈旧" ✗）；
② 完整时序（4 轮一致 ✓）：
     attempt → waiter-created → waiter ready → ready-wait done → go-set
     → waiter pre-park → **parked=-1/110（2 s）** → actors settle done → words-in → requeue ret=0
   ⇒ requeue 发生在 park **开始后约 51 ms** ✓、park **结束前** ✓ ⇒ **等待者当时确实在等** ✓。
```

| 事实 | 读数/依据 |
| --- | --- |
| park 真的等满 2 s ✗ | 加 `errno = 0` 之后**读数不变** ✓（`parked=-1 errno=110` ✓） |
| requeue 时等待者仍在 park ✓ | `actors settle done`（≈51 ms 后）**晚于** `pre-park` ✓、早于 park 结束 ✓ |
| requeue 仍 ret=0 ✗ | `requeue-probe ret=0 errno=0` ✓（4/4 ✓） |
| 调用者持有 target ✓ | `errno=0` ✓（否则内核给 EPERM ✗） |
| 三个字终值干净 ✓ | 每轮归零生效 ✓（`wait/chain/target = 0x0` ✓） |

## 由此得到的结论（收窄到内核语义层 ✓）

```
等待者"在等"、调用者"持 target"、字值"干净" ⇒ 而 `CMP_REQUEUE_PI` 仍回报 0 个可搬 ✗；
⇒ 只剩一层可查：**这次 park 在内核侧是否以 `WAIT_REQUEUE_PI` 的形态入队** ✗ ✓；
⇒ 下一批的检查点（按代价排序 ✓）：
   a. 我在 `wait_requeue_pi_until` 里传的 **`val`（第 3 参数）** 是否与内核期望一致 ✓（现在是 0 ✓，需核）；
   b. `wait_word_` 是否落在**同一 futex 键**下（同进程同一映射 ✓ ⇒ 应无问题 ✗，但要核）；
   c. 快照在该调用上的**逐字参数**（`:174-175` ✓）与我的对照 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
