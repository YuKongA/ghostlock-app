# 43499 · 本轮总交接（round 243）

## 一、目标条文（round 241 核对 ✓）

```
七条已落地并给出位置 ✓（wire→json/lua ✓、lua 可控可读 ✓、sa ✓、required_param ✓、
w1w2w3 与 43284 同步 ✓、lua 直调 kernelaccess（**可执行** ✓）、pagecache 封装与 lua 重写 ✓）；
唯二未完全满足：① "lua 直调…**正常执行攻击**" ✗（可执行 ✓ 但未生效 ✗）；② "43499 拿 root" ✗。
```

## 二、本晚修掉/证成的七件事（✓）

| # | 已修/已证 | 依据 |
| --- | --- | --- |
| 1 | 默认 `FutexSeam` 是**空实现** ✗ ⇒ 绑真 seam ✓ | `pi_race.hpp:62-86` ✓ |
| 2 | 引擎**未武装** ✗ ⇒ `race_.prepare(wait,target)` ✓ | `run` 第一行 gate ✓ |
| 3 | `dup2(high_read_,1)` 吃掉 stdout ⇒ **卡** ✗ ⇒ 低位 fd 改 {3,4,5} ✓ | `EXIT=124` → `EXIT=9/0` ✓ |
| 4 | 每轮未归零 ⇒ 第二轮 `lock_pi(chain)` 死等 ✗ ⇒ 每轮归零 ✓ | `words-out chain=0x…` ✓ |
| 5 | 写值"算而未用" ✗ ⇒ 真值全程流通（guard 满足 ✓） | `value-compare same=0` ✓ |
| 6 | select 的触发线程 ✗ ⇒ pselect 移到 waiter ✓ | 快照 :176 ✓ + 本树注释 ✓ |
| 7 | 九项假因被读数逐一排除 ✓ | 见 device-gates 与各轮页 ✓ |

## 三、统一结论（四路径并排 ✓）

```
43499 multicast / 43499 select / 43284 kernelcall / 43284 lowlevel ⇒ **机制全通** ✓（各有硬读数 ✓），
而 **uid 恒 2000** ✗ / **enforce 恒 1** ✗ / **verdict none** ✗ ⇒ ⇒ 共同缺口 = "**写/生效**"这一层 ✓。
```

## 四、下一步的两条 L 级候选（都已写到可落码 ✓）

```
① 写值实验已做并留档 ✓（真值流通 ✓、读数与改前一致 ✓）⇒ 若要"定为正式"需你认可 ✓；
② "经别名读回"的生产实现 ✗（`KernelAccess` 只有抽象基类 ✓，生产侧零实例化 ✓）
   ⇒ 这是**独立写验证**的前提 ✓ ⇒ 属新组件 ⇒ 按 AGENTS §2 先计划后写码 ✓。
```

## 五、环境限制（✓，本轮新证）

```
`dmesg` → **Permission denied** ✗（无 root ✓）⇒ 内核日志这条路**关着** ✓；
⇒ 设备未 root ✓ ⇒ 一切"需要 root 的取证/验证"都不可用 ✓ ⇒ 判据只能落在 `enforce` / `uid` / 自有读数 ✓。
```

## 门禁（全绿 ✓）

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
