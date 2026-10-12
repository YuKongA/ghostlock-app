# 43499 · select_stack 常量已对齐；`prepare()` 仍有两处对象/节奏差（round 198）✓

## 本轮对齐（按快照 ✓）

```
kSelectTimeoutUs = 250000 ✓（我此前 2 s ✗ ⇒ 8 倍过长 ✓）—— 已改 ✓
kCompactMaxAttempts = 4 ✓（我此前 8 ✗）—— 夹具已改 ✓
kSelectEnterDelayUs = 30000 ✓ / ladder 8 格 ✓ / kWaiterWordShift = -2 ✓ / kPselectNfds = 320 ✓ —— 均已一致 ✓
```

## 读数（HIT2/HIT3 ✓）

```
lock=0 ✓  park=-1/110 ✓  setattr=0 ✓  successes=0  enforce=**1** ✗
⇒ 常量对齐**没有**翻过来 ✗ ⇒ 如实 ✓（下一步按下面的两处继续 ✓）。
```

## 快照 `prepare()` 与我之间的其余具体差（本轮读清 ✓）

| 快照 `prepare()` 的做法 | 我 | 差别性质 |
| --- | --- | --- |
| 低位 fd 装 `high_read`（`fcntl(block_fd(), F_DUPFD, 320+16)` ✓，**永不就绪** ✓） | 装 `pipe_read` ✗ | **对象不同** ✓（我的是空管道读端 ✓，也不就绪 ✓，但不是同一类对象 ✓） |
| `build_fdsets()` 在 prepare 做一次并**存下** `owned_in/out/ex` ✓ | 每轮重建 ✓ | 我**更**频繁 ✗ ⇒ 需核"每轮重建会不会洗掉上一轮的拷贝效果" ✓ |
| `reserve_standard_io()` + `open_selected_fds(high_read)` ✓ | 备份 + `install_at(pipe_read_, fd)` ✓ | 骨架同 ✓ |
| `kSelectTimeoutUs = 250000` ✓ | 已对齐 ✓ | ✓ |
| `kCompactMaxAttempts = 4` ✓ | 已对齐（夹具 ✓） | ✓ |

## 下一步（下一批，按可能性 ✓）

1. 低位 fd 改装 **`high_read`**（timerfd 的 dup ✓，与快照同一类对象 ✓）—— 最值得先试 ✓；
2. 三组改为 **prepare 时构建一次并保存** ✓（与快照一致 ✓）；
3. 判据仍是 `enforce` 1→0 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
