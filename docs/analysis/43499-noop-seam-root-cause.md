# 43499 · 根因：`PiRace` 的**默认 seam 是空实现**（round 203）✓✓

## 发现（读码即得 ✓，一行不放）

```cpp
// pi_race.hpp:62-86
struct FutexSeam final {                                   // ← 默认模板参数 ✗
    static int lock_pi(std::uint32_t *word)        { (void) word; return 0; }   // 空实现 ✗
    static int wait_requeue_pi(std::uint32_t *, std::uint32_t *) { return 0; }   // 空实现 ✗
    static int cmp_requeue_pi(std::uint32_t *, std::uint32_t *)  { return 0; }   // 空实现 ✗
    static int unlock_pi(std::uint32_t *word)      { (void) word; return 0; }   // 空实现 ✗
};
```

⇒ 我此前写的是 `race::PiRace<> race_{}` ✗ ⇒ 引擎跑在**空实现**上 ✓ ⇒ `cmp_requeue_pi` 恒 0 < 1 ✓
⇒ 每轮都走 `Refused` 分支 ✓ ⇒ `successes` 恒 0 ✓ —— **与观测完全一致** ✓；
⇒ 而 multicast 那条路用的是自己的 `LinuxRouteOps`（真 futex ✓）⇒ 这正是两支的**根本差别** ✓。

## 本轮修复（按快照的 val 语义 ✓）

```cpp
// 1) ops 补 CMP_REQUEUE_PI（快照 :243-245 ✓：val2 = 1、val3 = 0 ✓）
static std::int32_t cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) {
    return (std::int32_t)::syscall(SYS_futex, chain, FUTEX_CMP_REQUEUE_PI, 1, 1, target, 0);
}
// 2) route 里绑定真 seam（引擎从空实现换到真 syscall ✓）
struct SelectStackSeam { lock_pi → Ops::lock_pi ✓ / unlock_pi ✓ / cmp_requeue_pi ✓ / wait_requeue_pi ✓ };
race::PiRace<SelectStackSeam> race_{};
```

## 门禁与真机（本轮 ✓/✗ 如实）

```
android → RC=0，0 error / 0 warning ✓（新 seam 编译通过 ✓）
真机：s1/s2 撞奇偶门 ✗；s3/s4/s5 **无输出** ✗ ⇒ 真 syscall 之后跑动变慢或卡 ✓
⇒ 下一批先量"慢还是卡"（单跑给足时长 ✓），再取 successes/enforce ✓。
```

## 意义（✓）

```
这是今晚第二条 route 上**第二个根因级**发现 ✓（前一个是"引擎从未被武装" ✗）；
两者都说明：空实现/未调用的路径会让读数**看似合理**而其实什么都没发生 ✓ ⇒
⇒ 判据必须落在**真 syscall 的返回值**与 `enforce` 上 ✓（这正是本轮之后能做的事 ✓）。
```
