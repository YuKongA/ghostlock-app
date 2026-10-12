# 43499 · "读回内核侧"这条路缺实现（round 240）✓

## 查清的三件事（✓）

```
① `contract/kernel_access.hpp:48` 是 **抽象基类** `KernelAccess`（含 `read(address, bytes, …)` ✓，:176 ✓）；
② 它**在生产侧没有被实例化** ✗（`grep KernelAccess<` 非测试：**零命中** ✓）⇒ 只有测试里的假实现 ✓；
③ main 只是 `#include "contract/kernel_access.hpp"` ✓（:54 ✓），**从未调用** ✗。
```

## 由此得到的结论（✓）

```
"写后经别名读回"这条独立验证路 ✗ 需要一块**尚未存在的生产实现** ✓（seam + 目标地址 + read 调用 ✓），
⇒ 属 **L 级**（新组件/攻击路径 ✓）⇒ 按 AGENTS §2 应先出计划再写码 ✓（同 43499 写值那一条同批 ✓）。
⇒ ⇒ 因此本晚"独立写验证"这条线**到此为止**（不硬凑 ✗）；已有的读数与结论都已留档 ✓。
```

## 当前可用的判据（✓，不需要新组件）

| 判据 | 读数 | 现状 |
| --- | --- | --- |
| multicast 机制 | `status=0/0 calls=1 successes=1 stamp=-1/99` ✓ | 通过 ✓ |
| select 机制 | `status=0 step=61 attempts=4 calls=4 lock=0 park=-1/110 setattr=0` ✓ | 通过 ✓ |
| **写落地（强判据）** | `main: selinux enforce=` ✗ 恒为 **1** | **未达成** ✗ |
| 目标（最终） | `main: victim uid=` ✗ 恒为 **2000** | **未达成** ✗ |

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
