# 43499 · 50 ms 沉降仍 ret=0 ⇒ 不是时机问题（round 221）✓✓

## 判别实验（✓）

```
把 requeue 前的沉降从 1 ms 拉到 **50 ms** ⇒ 4/4 仍 `requeue-probe ret=0 errno=0` ✗
⇒ ⇒ **排掉"requeue 早于 waiter 入队"** ✓（50 ms 远超任何启动竞态 ✓）；
⇒ 同时 `errno=0` ✓ 说明调用者（owner）**确实持有 target** ✓（否则内核给 EPERM ✗）。
```

## 新读数（关键 ✓）

```
main: sel-exec words-out wait=0x0 **chain=0x62d0 target=0x62d0** ✗
⇒ chain 与 target **同值** ⇒ 同一个 tid 同时持有两者 ✓（我 owner 侧先 lock chain ✓、引擎再 lock target ✓ ⇒ 同线程 ✓）；
⇒ 而 waiter 报 110（ETIMEDOUT ✓）⇒ 它**在等** ✓，却**不在 `wait` 的队列**上 ✗。
```

## 收窄后的唯一自洽解释（✓）

| # | 解释 | 判别方式 |
| --- | --- | --- |
| 1 | waiter 的 `WAIT_REQUEUE_PI` **未把请求挂到 `wait` 的 PI 队列** ✗ | 需在 waiter 内打印**紧随其后的 errno**（现在只存了值 ✗） |
| 2 | waiter 等的字**不是我打印的那个** ✗ | 可能性低（同一成员 ✓） |

## 下一批（✓，一行）

```
在 waiter 的 `wait_requeue_pi_until` 之后**立刻**打印它的返回值与 errno ✗（现值 `park=-1/110` 是汇总读数 ✓，
不能区分"第一次调用就失败"与"等满 2 s 超时" ✓）⇒ 用它把 #1 与 #2 分开 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
