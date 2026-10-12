# 43499 · 第 75 轮：**结构性差异** —— `disarm` 在退役是"动作"，在本树是"标志" ✓✓✓

## 一、两边的对照（✓✓，逐处 ✓）

```
本树（`multicast_route.hpp` ✓）：
  · **没有** `disarm()` / `cleanup()` 成员 ✗（⇒ 只有注释 :186「prepare, execute, disarm, destroy」✓）；
  · 只有两处**置标志**：`:268 status.kernel_disarmed = 1;` ✓ / `:384 status.kernel_disarmed = 1;` ✓；
退役（`route_lifecycle.hpp` + `multicast_waiter_route.cpp` ✓）：
  · `route.disarm();` 是**一个动作** ✓（契约顺序 ✓）⇒ 并**打印其返回值**：
    `[*] mcast ghost **disarm ret=-1 errno=110**` ✓ ⇒ ⇒ `ret=-1` + `errno=110`(=EAGAIN ✓)
    = **有一次真实的系统调用** ✓（⇒ 不是空动作 ✓）。
⇒ ⇒ **⇒ 两边的 `disarm` 语义不同** ✗：退役"动手" ✓，本树"记账" ✗ ✓。
```

## 二、为什么这可能是"写没发生"的最后一环（✓✓）

```
若 disarm 在内核侧**真的做了一件事**（⇒ 例如"解除已武装对象与锁的关联" ✓ ⇒ 从而让随后的 walk 走完 ✓），
⇒ 而本树只把它标成"已解除" ✗ ⇒ ⇒ **⇒ 内核侧从未被真正解除武装** ✓ ⇒ 写路径**缺最后一段** ✓；
⇒ 这与全部读数吻合 ✓：页/值/目标/编码 ✓ + `successes=1` ✓ + `clean=1/1` ✓（**而 clean 里的
   `kernel_disarmed` 恰恰是那个"记账"的布尔** ✗✓）⇒ 即：**我用自己打的标记证明了自己** ✗。
```

## 三、下一步（✓，一处必读 ✓）

```
读退役 `disarm()` 的**实现**（⇒ `route_controller.cpp` / `select_stack_route.cpp` / `tcp_zerocopy_route.cpp`
里找 `disarm` ✓）⇒ 看它**到底调用什么**（⇒ 目标是哪把锁/哪个对象 ✓，errno=110 从何而来 ✓）；
⇒ 若确为一次真实动作 ⇒ 在本树补上同一动作 ✓（⇒ 判据：`disarm` 读数与归档同形 ✓ + `victim uid=0` ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
