# 43499 · stdio 卡已修；卡点落到 `race_.run`（round 217）✓✓

## 本轮两件事（✓）

```
① 定位并修掉一处**真**卡：`dup2(high_read_, 1)` 把 stdout 换成 timerfd 的副本 ✗
   ⇒ 实测链条：sel-prep dup fd=0 ✓ → dup ok fd=0 ✓ → dup fd=1 ✓ → dup ok fd=1 ✓ → **EXIT=124** ✗；
   ⇒ 修法（最小）：低位 fd 改取 **{3,4,5}**（避开标准 IO ✓；快照正是用 reserve_standard_io() 护住 ✓）；
   ⇒ 修后四跑**都**进到引擎深处 ✓（`sel-exec attempt ✓` + `chain-locked ✓` ✓）。
② 新卡点：`race_.run(guard_)` 内部 ✗（`chain-locked` 是它之前的最后一条 ✓）。
```

## 为什么这仍是"字状态"问题（推理 ✓）

```
`PiRace::run` 做两件事：`Seam::lock_pi(target_)` ✓ 再 `Seam::cmp_requeue_pi(chain_, target_)` ✓；
而**真 landing** 之下 `lock_pi` = 真 `FUTEX_LOCK_PI` ✗ ⇒ 只要 `target_word_` **非零**（现有持有者）就**无限等** ✓；
⇒ 谁可能持有它：consumer 的兜底（`lock_pi(target)` ✓ 但我随后 `unlock` ✓）、或**上一轮残留** ✗；
⇒ 而 `execute` 开头只归零了**一次**（我 round 216 加的 ✓）⇒ **每轮之后**若未回零 ⇒ 第二轮起就会卡 ✓。
```

## 下一批（下一批 ✓，一行打点即可定 ✓）

```
在 `race_.run` 前后各打一条：`target=0x%x chain=0x%x wait=0x%x` ✓ ⇒ 立刻看出是"非零进入"还是"别的原因" ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
