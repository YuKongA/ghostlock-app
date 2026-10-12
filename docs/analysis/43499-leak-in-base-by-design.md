# 43499 · 更正：snitch 的泄漏落在 base 块内是**设计如此**（round 148）

## 更正（我上一段推错了 ✗）

```
退役：spray.cpp:600  base = leaked & ~(ORDER3_SIZE − 1)
⇒ 泄漏值**必然**落在 [base, base + 32KiB) 之内 ✓ —— 这是 by construction ✓
我的算例：base=0xffff800000028000、leak=0xffff80000002c400（差 0x4400 ⇒ 同一块内 ✓）
⇒ **不是 bug** ✗；本树的 candidate_match.hpp（canonical/tag 改写 ✓）也与退役同形 ✓
```

## 本轮真正取到的新事实（都有真机逐字 ✓）

| # | 读数 | 意义 |
| --- | --- | --- |
| ① | `main: window clean=1 violations alloc=0 log=0 indirect=0 lua=0` ×3 ✓ | **窗口干净** ✓ ⇒ "脏窗口"假设否证 ✗；也满足门禁对 clean 的要求 ✓ |
| ② | `main: victim comm after len=15 first=103` ✓ | 受害者 comm **未被改写** ✓ |
| ③ | `main: sentinel … w+0x1180=0 … +0x11c8=0 \| lock+0xe98=0` ✓ | 我们页面里的哨兵字段**全 0**（未被内核写 ✓） |
| ④ | `main: selinux enforce=1` ×3 ✓ | **直读**：`selinux_enforcing` 未变 ⇒ 写未落 ✗ |

## 因此现在的判据链（清楚 ✓）

```
奇偶（是否走到底）      ~1/3 ~ 1/2 ✓（独立问题 ✓）
窗口干净（clean=1 ✓）   ✓（已满足 ✓）
三步全赢 / 两轮修复 0/0 ✓（已满足 ✓）
**写落地**（enforce 1 → 0）✗ ← 唯一未达成 ✓（现已有直读仪器 ✓）
```
