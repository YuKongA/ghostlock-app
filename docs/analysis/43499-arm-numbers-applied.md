# 43499 · 退役的三个 arm 常数：已应用，尚未改变落地（round 150）

## 从退役路线头部读到的硬数字（multicast_waiter_route.cpp:33-36 ✓）

```cpp
constexpr int32_t kMulticastMaxAttempts = 128;
constexpr int32_t kMulticastArmSequence = 16;
constexpr int32_t kMulticastArmHold     = 20000;   // 注释：spins on yield (no syscall) between
                                                   //        the copy and the walk so the poisoned frame survives
⇒ 我此前：attempts=20、arm_sequence=15、**arm_hold=64** ✗（差 300 倍）
```

## 已改（纯数据 ✓，向退役对齐 ✓）

```
夹具 execution.{attempts:128, arm_sequence:16, arm_hold:20000}
真机 3 跑（try1 完整）：
  main: selinux enforce=1 ✗   main: victim comm after len=15 ✗   main: victim uid=2000 ✗
  try2/3：奇偶不过 ⇒ 提前停 ✓（分布不变 ✓）
⇒ 结论：**三个常数不是那个杠杆** ✗（但保留：更贴近退役 ✓）
```

## 同段代码里还看到一处**结构**差异（下一项 ✓）

```cpp
unsigned char stamp[stamp_size];              // ★ 栈上 VLA（= 注释里的 "the caller's stack" ✓）
memset(stamp, 0, sizeof(stamp));
encode_multicast_waiter(…, fake_task, fake_lock);   // 用**内核地址**填 task/lock ✓
memcpy(stamp + 8, &family, 2);                // 第 8 字节写 AF_UNSPEC(0) ✓
socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0) // AF_INET DGRAM ✓
⇒ 退役把"形状"放在**自己这一帧的栈**上；本树对应物是引擎的成员缓冲 ✗ ⇒ 待核 ✓
```

## 门禁

- android → RC=0，0 警 ✓；check → 108 PASS ✓；lint → 0 ✓。
