# 43499 · 主线程 pselect 已按快照归位；又见两处快照有而我无（round 197）✓

## 本轮改动（按快照 ✓）

```
快照主循环（:517-536 ✓）：装低位 fd ⇒ 重置计数 ⇒ 设每轮延迟 ⇒ 放消费者走 ⇒ **主线程 pselect** ✓
我此前的错（round 190 ✗）：把 pselect 搬进了 waiter 线程 ✗ ⇒ fd_set 拷贝落错线程 ✓
本轮：把 pselect 放回**主线程**（attempt 循环内 ✓，有界 2 s ✓），waiter 仍 park 在 `wait` 字上 ✓。
```

## 读数（HIT2/HIT3 ✓）

```
lock=0 ✓  park=-1/110 ✓  **setattr=0 ✓  setterrno=0 ✓**  successes=0  enforce=**1** ✗
⇒ 结构对齐了（主线程拷贝 ✓ + 消费者健康 ✓ + 字干净 ✓），但写仍不落地 ✗ —— 如实 ✓。
```

## 又见两处（快照有、我没有 ✗，都来自同一段 ✓）

| # | 快照 | 我 | 影响 |
| --- | --- | --- | --- |
| 1 | `route_delay_usec = delay_for(attempt)` ✓ —— **每轮一个延迟** ✓ | 无 ✗ | 没有"让拷贝与 walk 错开"的时间窗 ✓ |
| 2 | `open_selected_fds(block_fd())` —— **每轮重装**低位 fd ✓ | 只在 `prepare` 装一次 ✗ | 上一轮 `pselect` 之后 fd 状态可能被内核改动 ✓，需重装 ✓ |

## 下一步（下一批 ✓）

1. 循环内**每轮重装**低位 fd（`dup2(pipe_read, fd)` ✓，与快照同 ✓）；
2. 引入**每轮延迟** ✓（`delay_for(attempt)` 的形状要按快照读清 ✓）；
3. 判据仍是 `enforce` 1→0 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
