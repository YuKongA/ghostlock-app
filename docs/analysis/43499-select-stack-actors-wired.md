# 43499 · `select_stack`：三演员已接线（round 180）✓

## 落地（`execute` 全相位 ✓）

```cpp
prepare(chain, target, selected, count)      // 资源帧 ✓（round 177 ✓）
for attempt:  build sets ⇒ pselect ⇒ guard.enter / race.run / guard.reclaim
              if (win) { report_.successes = 1; break; }
race_.disarm(); race_.destroy(); cleanup();  // §5 顺序 ✓
return (successes > 0) ? nullptr : "select-stack-no-consumer-success";   // 不撒谎 ✓
```

| 配方步骤 | 落地 | 依据 |
| --- | --- | --- |
| `PiRace<> race_{}` + `WindowGuard guard_{}` 成员 | ✓ | 既有件 ✓（`race/pi_race.hpp:90` / `pi_window.hpp:67`） |
| `prepare(chain, target)` | 由 `execute` 的入参给 ✓（新增两参数 ✓） | 与 multicast 同源 ✓ |
| `guard.enter` → `run(guard)` → `guard.reclaim()` | ✓ 每轮配对 ✓ | 配方 ✓ |
| 收尾 `disarm()` → `destroy()` → `cleanup()` | ✓ 顺序按 §5 ✓ | 先停访问再回收 ✓ |
| 赢的判据 | **代理**：非 `Refused`/非 `NotPrepared` ✓（缺口已登记 ✓） | 快照真判据是 `consumer_success` ✗ |

## 登记缺口（诚实 ✓，不藏 ✗）

```
快照的胜负判据是 `race->consumer_success.load() > 0` ✓；本树 PiRace 的公开面只到 `outcome()` ✗
⇒ 本批用"跑过且非被拒"作**代理** ✓，并在代码注释与本页登记 ✓；
⇒ 待 PiRace 暴露 success 计数（小改动 ✓）后改为真判据 ✓。
```

## 判据（本轮 ✓）

```
make -C core -B android ⇒ RC=0，0 error / 0 warning ✓；check → RC=0，108 PASS ✓；lint → 0 ✓
本类仍无调用者 ✗（选择器未接 ✓）⇒ 既有两条路行为不变 ✓
```

## 移植进度

| 步骤 | 状态 |
| --- | --- |
| 1-2. route 核心 + 词号数据化 + 偏移换算 | ✓ |
| 3a. fd_set 组装（纯 ✓） / 3b-1. 借用账本（纯 ✓） | ✓ |
| 3b-2. syscall 面（ops ✓） / 3b-3. 资源帧 ✓ / 3b-4a. 循环骨架 ✓ | ✓ |
| 3b-4b. **三演员接线（✓ 本轮）** | ✓ |
| 3b-4c. **真判据**（PiRace 暴露 success 计数 ✓，小改动 ✗ 待做） | ✗ |
| 4. 接线选择器（`main` 的 route 分支 ✓） | ✗ |
| 5. 三级真机判据（**多次统计** ✓）：走到底/窗口 ✓ ⇒ `enforce` 1→0 ✓ ⇒ `uid=0` ✓ | ✗ |
