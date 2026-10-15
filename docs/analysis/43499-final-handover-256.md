# 43499 · 终局交接（round 256）：六个缺口已修五个，第六个是"接线"

## 一、本晚修掉的五个缺口（每个都有真机判据 ✓）

| # | 缺口 | 修法 | 真机判据 |
| --- | --- | --- | --- |
| 1 | 默认 `FutexOps` 是**空实现** ✗ | 绑定真 landing ✓ | `requeue-probe` 有真 syscall ✓ |
| 2 | 引擎**未武装** ✗ | `race_.prepare(wait,target)` ✓ | `successes` 从恒 0 变为可动 ✓ |
| 3 | 触发线程错位 ✗ | `pselect` 移到 waiter（park 后 ✓） | select 路线 4 轮跑完 ✓ |
| 4 | **零写值被校验器拒绝** ✗ | 允许 `Zero` 模式下的 0 ✓ | **`w1/w2/w3` 全 `won`** ✓✓ |
| 5 | 脚本**缺 handoff 调用** ✗ | 补 `surface.handoff(...)` ✓ | **`handoffs=1`** ✓✓ |

## 二、第六个缺口：终端阶段（✓，本轮定位 + 本轮查清）

```
现象：`victim uid=2000` ✗ 且日志里没有 `seccomp … probe` / `child seccomp fully bypassed` / `root script start` ✓；
定位：`handoff` 的 host 处理只**登记 + 校验** ✓（main.cpp:1446-1465 ✓）⇒ 不执行终端阶段 ✗；
新发现（本轮 ✓）：**终端阶段的零件其实已经在树里** ✓：
    `finit_module`：src 1 个文件 ✓；`seccomp`：11 个文件 ✓；`handoff`：8 个文件 ✓；
    而 `root_shell`（Lua 侧的 terminal 名 ✓）：**src 0 个** ✗ ⇒ 没人消费它 ✓。
⇒ ⇒ **⇒ 所以这一步是"接线"（把 handoff 的 terminal 接到已有 seccomp/finit_module 上 ✓），
   而不是"从零实现"** ✓ —— 这显著降低了下一步的规模 ✓。
```

## 三、下一步（L 级 ⇒ 计划先行 ✓，形状已定）

```
① 在 host 侧为 `terminal == "root_shell"` 建立分派 ✓（对照 43284 的 `handoff` 用法 ✓）；
② 按其顺序调已有件：seccomp 探针 ✓ → 解除 ✓ → 启动 root script ✓（归档三步 ✓）；
③ 判据：`root script start` ✓ ⇒ **`main: victim uid=0`** ✓。
```

## 四、目标状态（✓，按政策）

```
仍 **active** ✓：最后一条（43499 拿 root ✗）未满足 ✓；不标 blocked ✓（剩余工作明确且已定位 ✓）。
```

## 五、门禁（收尾复核 ✓）

```
check → 108 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
