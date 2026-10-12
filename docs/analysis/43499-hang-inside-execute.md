# 43499 · 卡点定位：`select_route.execute()` 内部（round 215）✓✓

## 打点读数（一次命中 ✓）

```
q1: main: sel-branch enter ✓
    main: sel-branch words filled ✓      ⇒ 填词号**不是**卡点 ✓
    main: sel-branch before execute ✓    ⇒ 构造也**不是** ✓
    EXIT=**124** ✗                       ⇒ **卡在 execute 内部** ✓
q2/q3: accepts=0 ✗ ⇒ 早退（EXIT=9 ✓ ⇒ "被门拒"的退出码是 9 ✓，可作日后判据 ✓）
```

## execute 内部的候选（按可能性 ✓）

| # | 位置 | 为什么会卡 |
| --- | --- | --- |
| 1 | `race_.run` 的 `lock_pi(target_word_)` ★ | **真 seam 之后**（round 203 ✓）这是**真** `FUTEX_LOCK_PI` ✗ ⇒ 字非零就会**无限等** ✓ |
| 2 | `execute` 开头 `race_.prepare` 之后的第一轮 `run_actors` | 线程创建/等待 ✓（有界 ✓） |
| 3 | `pselect`（250 ms × 4 ✓） | 不会卡（有界 ✓） |
| 4 | `stop_actors`（自旋上限 + detach ✓） | 有界 ✓ |
| 5 | `cleanup`（`close`/`dup2` ✓） | 不会卡 ✓ |

⇒ ⇒ 头号候选 = **#1**：与 round 205 那次"chain 字非零导致卡"**同族** ✗（那次修了 chain ✓，这次可能是 **target** ✓）。

## 下一步（下一批 ✓）

```
① 在 `prepare` 里**把等待者用的三个字都归零**（chain/target/wait ✓）—— 避免被"编辑器"或上一轮残留弄脏 ✓；
② 若仍卡 ⇒ 再给 `lock_pi` 加**有界**写法（试锁或超时 ✓），并据此具名拒绝 ✓；
③ 之后回到 `value=0x4141…` ✗ 与 `buf=264` ✗ 两条已确证线索 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
