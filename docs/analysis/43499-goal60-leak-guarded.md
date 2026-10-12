# 43499 · 第 60 轮：泄漏段逐字到手（含守卫）；排除"泄漏到父进程" ✓✓

## 一、设计（逐行 ✓）

```cpp
// victim_process.cpp  child_main（:125 起）
:136  prctl(PR_SET_NAME, "ghostleaf_0123456789");   // 唯一进程名 ⇒ 用于"找自己"
:137  /* 铁律：两次一致才采信（w2 会往这个地址写）。 */
:138  std::uint64_t my_task = perf_find_task();      // 从 perf 事件推断自己的 task_struct
:140-150  两次读不一致 ⇒ my_task = 0 ⇒ 父侧按失败/重试处理
:151  写回父侧；:155 为 0 ⇒ 子进程 _exit(1)；:161  `C` 命令 ⇒ 回 getuid()（子进程自报）
⇒ perf_find_task 是启发式 ⇒ :137 的铁律正因为它可能错。
```

## 二、立即排除一格（✓✓）

```
若泄漏到了父进程的 task ⇒ w1（写 selinux）与 w2（写 cred）都会落在父进程上，
而父进程就是被观察的那个 shell（uid 2000）⇒ main: after uid= 应变成 0；
但真机一路都是 uid=2000（从未 0）⇒ ⇒ 泄漏到的不是父进程（排除）。
```

## 三、下一步（一条便宜的探针 ✓）

```
子进程已打印 VOTES vote0=0x… vote1=0x…（源码 :139/:142）⇒ 直接用已有读数判：
  · vote0 == vote1 且非零 ⇒ 采信（也只说明两次启发式一致，不证明是本人）；
  · 关键是**跨轮对照**：受害者每次都是新 fork ⇒ 其 task 必然不同；
  · 若某轮 vote 与上一轮的 victim spawned task 相同 ⇒ 泄漏到了旧的/别人的 task（即命中假设）。
判法：从日志里逐轮比对 VOTES 与 victim spawned task=（数据现在就够）。
判据：任一轮 main: victim uid=0。
```

## 四、门禁（本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
