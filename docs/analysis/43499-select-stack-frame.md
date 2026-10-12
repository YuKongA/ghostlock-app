# 43499 · `select_stack`：资源帧已落地（round 177）✓

## 落地（`SelectStackRoute<Ops>` ✓）

```cpp
const char *prepare(selected, count);   // 取 pipe 两端 + 定时器 ✓；按账本"先备份后改" ✓
void cleanup();                          // 幂等 ✓：还原 stdio → 关备份 → 关 pipe → 关定时器（非借用时 ✓）
const char *execute(selected, count);    // 先 prepare ✓，再具名拒绝 select-stack-actors-not-wired ✓
```

| 契约点（资源清单 ✓） | 实现 |
| --- | --- |
| `fd_count` 必须是 64 的倍数 | 否则 `select-stack-fd-count-not-multiple-of-64` ✓ |
| pipe 失败 | `select-stack-pipe-failed` ✓ |
| 定时器起不来 ⇒ **借用 pipe 读端** | `timer_borrows_pipe_` ✓（关闭时按契约**不**关它 ✓） |
| 备份失败 / dup2 失败 | 立即 `cleanup()` 后具名拒绝 ✓（失败路径同一还原 ✓） |
| 成功/失败/早退 | 三条路径都走 `cleanup()` ✓（§5 要求 ✓） |

## 判据（本轮 ✓）

```
make -C core -B android ⇒ RC=0，0 error / 0 warning ✓（NDK 门禁真正编译了它 ✓）
check → RC=0，108 PASS ✓；lint → 0 ✓；既有两条路行为不变 ✓（无调用者 ✗）
⇒ 演员未接时不撒谎 ✓：execute 返回具名 select-stack-actors-not-wired ✓
```

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. fd_set 组装（纯 ✓） | ✓ |
| 3b-1. fd 借用账本（纯 ✓） | ✓ |
| 3b-2. syscall 面（ops ✓） | ✓ |
| 3b-3. **资源帧（prepare/cleanup ✓）** | ✓ 本轮 |
| 3b-4. 三演员 + 真写入（复用 `PiRace`/`WindowGuard` ✓） | ✗ 下一步 |
| 4. 接线选择器 | ✗ |
| 5. 三级真机判据（多次统计 ✓） | ✗ |
