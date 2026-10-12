# 43499 · 第 79 轮：ghost disarm 已实现，读数与归档逐字相同 ⇒ 但仍不 root ✓✓

## 一、落码（✓）

```cpp
// multicast_route.hpp  waiter_main 收尾（`return nullptr;` 之前 ✓，栈帧尚活 ✓）
if (config_.needs_ghost_disarm) {                     // 退役判据 = policy.multicast ✓ ⇒ 本结构只服务 multicast ⇒ 默认开 ✓
    const std::uint32_t dummy_pi = 0x80000000U | static_cast<std::uint32_t>(::getpid());
    const timespec expired{.tv_sec = 0, .tv_nsec = 0};
    errno = 0;
    const int disarm = Ops::futex(const_cast<std::uint32_t *>(&dummy_pi), FUTEX_LOCK_PI, 0U, &expired, nullptr);
    std::printf("main: mcast ghost disarm ret=%d errno=%d", disarm, errno);
}
// 同批：补 `#include <unistd.h>` ✓ `<linux/futex.h>` ✓；配置加 `bool needs_ghost_disarm = true;` ✓
```

## 二、真机读数（✓✓✓，与归档逐字同 ✓）

```
`main: mcast ghost disarm ret=-1 errno=110` ✓（×4 ✓）— 归档那行为 `[*] mcast ghost disarm ret=-1 errno=110` ✓；
`route status=0/0 clean=1/1 attempts=16 calls=1 successes=1` ✓；
`step result w1 => won` ✓ / `w2 => **w2-not-rooted**` ✗；`main: victim uid=2000` ✗。
```

## 三、诚实结论（✓）

```
本会话最重的一处缺失（第 76/77 轮发现 ✓）已**实现** ✓ 且读数与归档**逐字相同** ✓；
⇒ 但 `uid` 仍为 2000 ✗ ⇒ ⇒ **⇒ 该动作不是充分条件** ✗（⇒ 是否为必要条件 ✗ 无法由"仍不 root"判定 ✓）；
⇒ 因此「实现43499/43284 取得 uid0」**仍未达成** ✗ ⇒ 目标保持 active ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓。
```
