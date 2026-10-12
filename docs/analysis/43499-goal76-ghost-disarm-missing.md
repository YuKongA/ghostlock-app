# 43499 · 第 76 轮：**决定性发现** —— 本树完全没有「post-race ghost disarm」✓✓✓

## 一、两树对照（✓✓✓，逐条 ✓）

```
本树：`grep ghost_disarm|route_needs_ghost_disarm|needs_ghost` ⇒ **全空** ✗✗；
退役：
  · `route_policy.hpp:249`  `[[nodiscard]] inline bool route_needs_ghost_disarm(...)` ✓
    —— 注释逐字：**"True when the resolved policy needs the post-race ghost disarm."** ✓；
  · **`race/threads.cpp:66` 调用它** ✓ ⇒ ⇒ 该动作发生在 **race 线程层** ✓（⇒ 紧接 race 之后 ✓）；
  · 测试 `route_policy_test.cpp:117-118` **断言**：`!select && !tcp && **mcast**` ✓✓
    ⇒ ⇒ **⇒ multicast 路线有一个"赛后 ghost disarm"的必须动作** ✗✓。
```

## 二、与本会话最硬的线索对上（✓✓✓）

```
归档日志（逐字 ✓）：`[*] mcast ghost **disarm ret=-1 errno=110**` ✓
  ⇒ `ret=-1` + `errno=110`(=EAGAIN ✓) ⇒ 一次**真实**的系统调用 ✓；
  ⇒ 位置：紧跟 `mcast` 路线之后 ✓（与 `race/threads.cpp:66` 的位置一致 ✓）；
⇒ ⇒ ⇒ **⇒ 我树既不做这个动作、也不打印它** ✗✗ ⇒ ⇒ 这与"页/值/目标/编码全对 ✓ 而写不生效 ✗"
   **完全吻合** ✓：缺的正是"赛后的那一次解除" ✓。
```

## 三、下一步（✓，两处必读 ✓）

```
① 读退役 `route_policy.hpp:249` 的 **判据**（⇒ 什么使 mcast "需要" ✓）与
   `race/threads.cpp:66` 附近的**动作实体**（⇒ 它到底调用什么 ⇒ errno=110 从何而来 ✓）；
② 在本树同位置（race 线程层、路线之后 ✓）补上**同一动作** ✓，并打印 `disarm ret/errno` ✓；
③ 判据：真机出现与归档同形的 `disarm ret=-1 errno=110` ✓ ⇒ ⇒ **再看 `main: victim uid=0`** ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
