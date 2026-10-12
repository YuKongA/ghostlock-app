# 43499 · `select_stack`：引擎从未被武装（round 190）✓

## 决定性的发现（读码 + 读数一致 ✓）

```
我的 execute 里只调 `race_.run(guard_)` ✗，**从未 `race_.prepare(...)`** ✗；
而 PiRace::run 的第一行是：
    if (outcome_ != RaceOutcome::Prepared) { return outcome_; }    // :107
⇒ 于是每轮 **立刻返回**（NotPrepared ✗）⇒ 三演员根本没跑 ✓ ⇒ `successes` 恒 0 ✓
⇒ 这解释了此前一切读数（128 轮 Refused、successes=0 ✓），且与"三字/WAIT_REQUEUE_PI"两处修正无关 ✓。
```

## 本轮两处修正（都按快照 ✓）

| 修正 | 依据 | 现状 |
| --- | --- | --- |
| 补**第三个字** `wait_word`（三字：wait / chain / target ✓） | 快照 `PiRace{wait,target,chain}` ✓ | ✓ 已落 |
| waiter 改走 **`WAIT_REQUEUE_PI(wait → target)`** park ✓ | 快照 :174-175 ✓ | ✓ 已落 |
| **武装**：`race_.prepare(wait_word_, target_word_)` ✓ | 快照 :164-198 的 pair ✓ | ✓ 本轮 |

## 门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → RC=0，108 PASS ✓
```

## 真机（本批**未取到读数** ✗，如实 ✓）

```
本批 5 跑：**全部**撞奇偶门（payload-layout-not-accepted ✗）⇒ 无一到达 route ✓；
⇒ 判据（successes 0 → ≥1）**待下一批** ✓；抽样成本已登记：约 1/4 跑能过奇偶门 ⇒ 每批 4-6 跑 ✓。
```
