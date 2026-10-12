# 43499 · `requeue ret=0`：`wait` 上没有等待者（round 219）✓✓✓

## 直接读数（✓）

```
main: sel-exec requeue-probe **ret=0 errno=0** ✗
⇒ 快照原注释（pi_race.hpp:120-122 ✓）："a count of one is the kernel saying a waiter was really
   there, and **zero means there was none to move**" ✓ ⇒ ⇒ **wait 上没有等待者** ✓（结论唯一 ✓）。
```

## 由读数推出的候选（按可能性 ✓）

| # | 候选 | 依据/如何验 |
| --- | --- | --- |
| 1 | waiter 还**没走到** `wait_requeue_pi_until` ✗ | 它前面的步骤：`lock_pi(chain)` → `waiter_ready_=1` → 等 `actors_go_` ✓；可在每步后打点 ✓ |
| 2 | waiter 的 `lock_pi(chain)` **在等**（owner 先拿了 chain ✗） | 我的顺序是 `run_actors` → settle → `lock_pi(chain)` ✓；理论上 waiter 应先拿 ✓ ⇒ 需实测 ✓ |
| 3 | waiter 已 park 但**在另一个字**上 ✗ | 传参是 `self->wait_word_` ✓（= 我归零的那个 ✓）⇒ 不太可能 ✗ |

## 判别用的一步（下一批 ✓，小 ✓）

```
在 waiter 的每个阶段后加打点（waiter: locked ✓ / ready ✓ / go ✓ / parked ✓）⇒ 一次跑即可分行 ✓；
同时把 `lock_pi(chain)` 的**返回值**也记下 ✓（判断它是否在等 ✓）。
```

## 顺带确认的两条（✓）

```
· 路线现在**稳定可测**（5 跑 3 命中 ✓，早期"0 命中"实为卡死所致 ✗）；
· 每轮四阶段全跑完、`lock=0 ✓ setattr=0 ✓`（消费者健康 ✓）、字终值干净 ✓（每轮归零生效 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
