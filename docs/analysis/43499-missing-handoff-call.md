# 43499 · 最后缺口：脚本缺 handoff 调用（round 254）✓✓✓

## 读数（✓，逐字 ✓）

| 夹具 | `surface.handoff` 调用 | 步骤 |
| --- | --- | --- |
| `wire-43499-collide-F` | **0** ✗ | `{w1,w2,w3}` ✓ |
| `wire-43499-select-stack` | **0** ✗ | `{w1,w2,w3}` ✓ |
| `wire-43284-kernelcall` | **1** ✓ | —— |
| `wire-43284-lowlevel` | **1** ✓ | —— |

## 机制（✓，代码依据 ✓）

```cpp
// main.cpp:1446-1447
if (name == "handoff") { ++report->handoffs; … }    // Lua 调一次 surface.handoff(...) 就 +1 ✓
⇒ 而 43499 的两支脚本（已解码 ✓）只有：
    procedure.steps = { "w1", "w2", "w3" }  +  for … surface.step(h, name)  +  surface.close(h)
⇒ ⇒ **没有 handoff 调用** ✗ ⇒ `handoffs=0` ✗ ⇒ 终态阶段（root script）永不启动 ✓。
```

## 与归档成功的对照（✓）

```
成功日志收尾：`child seccomp fully bypassed` ✓ → **`root script start uid=0 euid=0`** ✓；
而那一步正是 **handoff 的产物** ✓（⇒ 43284 的脚本有这一步 ⇒ 也符合"handoff 是终态阶段"的设计 ✓）。
```

## 至此本晚的因果链闭合（✓✓）

```
① 空实现 landing ✗ → 绑真 landing ✓；② 未武装 ✗ → 已武装 ✓；③ 触发线程 ✗ → 归位 ✓；
④ 零写值被拒 ✗ → 放宽后 **w1/w2/w3 全 won** ✓；⑤ 缺 handoff 调用 ✗ → **本轮的结论** ✓；
⇒ ⇒ 只差最后一步：**让 43499 的脚本也 handoff** ✓（⇒ 之后即是 `uid=0` 的判据 ✓）。
```

## 下一步（✓，一个决定即可推进）

```
两条路（都小 ✓）：
  A) 在夹具的 `procedures/43499_window.lua` 末尾加 `surface.handoff(h, "root_shell", "procedures/43499_window.lua")` ✓
     （⇒ 与 43284 脚本同形 ✓）；
  B) 或在 host 侧：三步成功后**自动进入 handoff** ✓（⇒ 不动 wire ✗ ⇒ 更稳 ✓）。
```
