# 43499 · `select_stack`：尝试循环骨架已落地（round 178）✓

## 落地（`execute` 的循环相位 ✓）

```cpp
for (attempt = 1 … config_.attempts) {
    build the three fd_sets        // 每轮重建 ✓ = re-poison 同族 ✓
    FD_SET(low fds → in) / FD_SET(installed_at → ex)
    Ops::pselect_now(fd_count, in, out, ex)          // 无限等 ✓
    if (failed && errno != EINTR) ⇒ 具名拒绝 + cleanup ✓
}
cleanup();  ⇒ 返回体名的 select-stack-actors-not-wired ✓（演员尚未接 ✗）
```

| 骨架要点 | 实现 | 依据 |
| --- | --- | --- |
| 每轮**重建三组** | 循环内调 `select_stack_build_fdsets` ✓ | 退役 re-poison 同族 ✓ |
| 低位借用者置位 + 安装位进 ex 组 | `FD_SET(low_fds[i], &in_set)` ✓ / `FD_SET(installed_at, &ex_set)` ✓ | 快照 :457-458 ✓ |
| 一次 `pselect`（无限等 ✓） | `Ops::pselect_now(fd_count, in, out, ex)` ✓ | 快照 :536 ✓ |
| `EINTR` 容忍 ✓ | 仅非 `EINTR` 才具名拒绝 ✓ | 退役自旋语义 ✓ |
| 每轮计数 | `report_.attempts` / `report_.calls` ✓ | 报告与判据同形 ✓ |
| 任何路径都还原 | 三条早退 + 正常结束都 `cleanup()` ✓ | 资源清单 §四 ✓ |

## 本轮自己踩到的两处（登记 ✓）

```
1. config 里没有 attempts ✗ ⇒ 按"数字走文档"补上 ✓（与 multicast 的 attempts 同族 ✓）；
2. 主机侧 `EINTR` 未声明 ✗（缺 <cerrno> ✓）⇒ 补头 ✓ ⇒ 说明跨端头文件要成套 ✓。
```

## 判据（本轮 ✓）

```
check → RC=0，**108 PASS** ✓；android → RC=0，0 error / 0 warning ✓；lint → 0 ✓
自测：build/host/select_stack_route_test ⇒ 通过 ✓（TEST_RC=0 ✓）
既有两条路行为不变 ✓（本类仍无调用者 ✗）
```

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. fd_set 组装（纯 ✓） | ✓ |
| 3b-1. fd 借用账本（纯 ✓） | ✓ |
| 3b-2. syscall 面（ops ✓） | ✓ |
| 3b-3. 资源帧（prepare/cleanup ✓） | ✓ |
| 3b-4a. **尝试循环骨架（✓ 本轮）** | ✓ |
| 3b-4b. 三演员 + 武装/写入（复用 `PiRace`/`WindowGuard` ✓） | ✗ 下一步 |
| 4. 接线选择器 | ✗ |
| 5. 三级真机判据（多次统计 ✓） | ✗ |
