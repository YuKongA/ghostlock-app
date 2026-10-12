# 43499 · select_stack 已逐条对齐快照；`enforce` 仍为 1（round 199）✓

## 本轮落地（按快照 `prepare()` ✓）

```
high_read = fcntl(block_fd(), F_DUPFD, fd_count + 16)   ✓（快照 :502 ✓）
  block_fd() = 永不就绪的 timerfd ✓（退化时是 pipe 读端 ✓）
低位 fd 改装 **high_read** ✓（此前装 pipe_read ✗）—— 安装处与"每轮重装"处都改了 ✓
cleanup 关 high_read ✓（顺序：还原 stdio → 关备份 → **关 high_read** → 关 pipe → 关定时器 ✓）
```

## 读数（HIT1 ✓）

```
lock=0 ✓  park=-1/110 ✓  setattr=0 ✓  successes=0  enforce=**1** ✗
```

## 对齐清单（今天逐条做过的 ✓）

| 维度 | 状态 |
| --- | --- |
| 词表/词号（由文档 ÷8 ✓） | ✓ |
| 移位 -2 ✓ / nfds 320 ✓ / 超时 250 ms ✓ / attempts 4 ✓ / 阶梯 8 格 ✓ | ✓ |
| 主线程 `pselect`（拷贝载体 ✓）+ 每轮重装 + 每轮错峰 | ✓ |
| 三演员：waiter（持链 + WAIT_REQUEUE ✓）/ owner（target + 链 ✓）/ consumer（setattr ✓ 首次即成功 ✓） | ✓ |
| 低位 fd 装 `high_read`（同对象 ✓） | ✓ 本轮 |
| 三组 fd_set 的构建时机（快照 prepare 一次 ✓ / 我每轮 ✓） | **✗ 待改（下一批）** |

## 诚实的方向说明（✓）

```
select 路由在**所有已枚举的维度**上已与快照一致 ✓，而 `enforce` 仍未 0 ✗；
⇒ 其余可能只剩三类：(i) 上面那一处"构建时机" ✗；(ii) 页面/payload 侧（此前做过字节级比对 ✓）；
   (iii) 尚未枚举到的某处 ✓（快照与本树的差异表并非无穷 ✓，但也不是已穷尽 ✓）。
⇒ 下一批先做 (i)（最省力且与快照直接冲突 ✓），再按读数决定是否需要重跑 (ii) 的比对 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
