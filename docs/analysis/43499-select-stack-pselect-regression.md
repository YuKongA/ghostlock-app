# 43499 · 消费者已修好（setattr=0 ✓）；并发现我自己引入的一处回退（round 196）✓

## 本轮读数（HIT8 ✓）

```
main: select route attempts=8 calls=8 successes=0 lock=0 ✓ park=-1 parkerrno=110 ✓
                         **setattr=0 ✓  setterrno=0 ✓**   ← 消费者首次即成功 ✓（拉长 park 修好了 ESRCH ✓）
main: selinux enforce=1 ✗（仍不落地 ✓）
```

## 修好的那一处（真 ✓）

| 现象 | 成因 | 修法 |
| --- | --- | --- |
| `setterrno=3 (ESRCH)` ✗ | waiter 只 park 200 ms ✗ ⇒ 消费者动手前它就退出 ✓ | park 拉到 2 s ✓（快照的"永不就绪"语义 ✓）+ attempts 8 ✓ |

## 我自己引入的回退（round 190 的那次改动 ✗）

```
我原设计：waiter = lock_pi(chain) ⇒ **pselect**（bounded ✓）⇒ unlock_pi(chain)
round 190 改成：waiter = lock_pi(chain) ⇒ **WAIT_REQUEUE_PI(wait → target)** ⇒ unlock_pi(chain)
而快照头部写着：「低编号 fd 被 dup2 成可读端，**select 才会把这些 word 带进内核并 park**」✓
⇒ ⇒ **select 的 park 就是 pselect 本身** ✓ ⇒ 换掉它等于把"fd_set 拷贝"这一步去掉了 ✗
⇒ 这解释了为什么"消费者健康了、写得出来、字也干净"而 `enforce` 仍旧 1 ✗：
   **没有 fd_set 进内核 ⇒ 没有可覆盖的陈旧 waiter ⇒ 擦除式写入没有对象** ✓
```

## 下一步（下一批 ✓）

1. 按快照把 `pselect` **放回** waiter ✓（有界、2 s ✓），并核对它与 futex park 的关系 ✓（快照三演员里谁做 pselect ✓）；
2. 若两者需共存 ⇒ 按快照的线程分工照抄 ✓（不自行发挥 ✗）；
3. 判据仍是 `enforce` 1→0 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
