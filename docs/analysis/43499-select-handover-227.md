# 43499 · 交接（round 227）：select 路线已"跑得通、搬不动"

## 一、本晚已达成的（可核 ✓）

```
· select 路线**从零写到真机跑完**（4 轮/次 ✓，5 跑 3 命中 ✓）并打出完整报告 ✓；
· 修掉两处**真卡**：`dup2(high_read_, 1)` 换掉 stdout ✗ ⇒ 低位 fd 改 {3,4,5} ✓；
  每轮未归零 ⇒ 第二轮 `lock_pi(chain)` 无限等 ✗ ⇒ 改为**每轮归零** ✓；
· 修掉两处**结构性错**：引擎未武装 ✗ ⇒ `race_.prepare(wait,target)` ✓；
  默认 `FutexOps` 是**空实现** ✗ ⇒ 绑定真 landing ✓；
· 排除九项假因（时机/errno 陈旧/fd 借用/未归零/未武装/空 landing/多余 chain 锁/参数形状/同字 ✓）；
· 门禁全程绿 ✓（check 108 PASS ✓ / android 0 警 ✓ / lint 0 ✓）；`uid=0` **未取得** ✗。
```

## 二、当前唯一拦路虎（读数硬 ✓）

```
`CMP_REQUEUE_PI` 永远搬不到等待者：三组合分别 35 EDEADLK ✗ / 11 EAGAIN ✗ / 22 EINVAL ✗；
而等待者**确实在 park**（真 2 s 超时 ✓）、调用者**确实持 target** ✓、**同一地址** ✓、参数与快照**逐字一致** ✓；
⇒ ⇒ 结论：**失败不在参数** ✗ ⇒ 下一步应逐行对照**持有关系/时序**（详见三）。
```

## 三、下一步（按代价 ✓）

1. **逐行对照**：快照 `PiRace`/其 owner-waiter 三段的**每一步持有什么**（我 ↔ 快照）✓；
   重点：`wait` 字在 park 前后**由谁持/值为何** ✓、owner 何时持 target ✓、waiter 何时放 chain ✓；
2. `pre-target-lock` 已加 ✓ ⇒ 用它核对"owner 先持 target"这条是否达成 ✓；
3. 之后再回到 `payload.value` ✗（清单 1 ✓，L 级 ⇒ 计划 + 真机门禁 ✓）与 `buf=264` ✗（清单 2 ✓）。

## 四、清单（跨会话 ✓）

| # | 事项 | 依据 | 状态 |
| --- | --- | --- | --- |
| 1 | `payload.value` 是毒值 `0x4141414141414141` ✗；真值 `write_layout.value` 未喂 | 退役 `payload_write_layout(request, page_base, …)`（快照 :282） | **未改**（L 级 ⇒ 先计划 ✓） |
| 2 | `buf=264` ✗（几何量级不对） | 真机 `main: rv … buf=264` | 未查 |
| 3 | `CMP_REQUEUE_PI` 失败 | 三组合：35 EDEADLK ✗ / 11 EAGAIN ✗ / 22 EINVAL ✗ | 参数已排 ✗ ⇒ 查**持有关系** ✓ |
| 4 | 每轮 target 持有未释放 ✗ | `pre-target-lock=-1`（第 2 轮起） | 未修 |

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
