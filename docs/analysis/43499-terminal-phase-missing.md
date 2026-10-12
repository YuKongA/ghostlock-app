# 43499 · 最后一扇门：终端阶段（seccomp→root script）未实现（round 255）✓✓✓

## 本轮进展（✓✓）

```
给 43499 的两支脚本补上 `surface.handoff(h, procedure.terminal, procedure.payload_ref)` ✓（与 43284 同形 ✓）；
真机 4/4：**`handoffs=1`** ✓ ✓ 且 **`w3 => won`** ✓ 保持 ⇒ ⇒ **w1 → w2 → w3 → handoff 链完整** ✓✓。
```

## 剩下的唯一缺口（✓，代码依据 ✓）

```cpp
// main.cpp:1446-1465 —— handoff 的处理
if (name == "handoff") {
    ++report->handoffs;                     // 只**登记** ✓
    … 查 payload_ref 是否 declared …          // 只**校验** ✓
    if (!declared && …) report->refusal = "handoff-unknown-payload";
}
⇒ ⇒ **它不执行终端阶段** ✗ ⇒ 归档里 w3 之后的 `seccomp finit_module probe` ✓ →
   `child seccomp fully bypassed` ✓ → **`root script start uid=0`** ✓ 都是终端阶段的产物 ✓；
   而我的跑里这些行**一条都没有** ✗ ⇒ ⇒ **终端阶段在 host 侧尚未实现** ✓。
```

## 本晚因果链的最终形态（✓✓）

| # | 缺口 | 状态 |
| --- | --- | --- |
| 1 | 空实现 seam ✗ | **已修** ✓（真 seam ✓） |
| 2 | 引擎未武装 ✗ | **已修** ✓ |
| 3 | 触发线程 ✗ | **已修** ✓（pselect 归位 ✓） |
| 4 | 零写值被拒 ✗ | **已修** ✓ ⇒ **w1/w2/w3 全 won** ✓ |
| 5 | 脚本缺 handoff ✗ | **已修** ✓ ⇒ **handoffs=1** ✓ |
| 6 | **终端阶段未实现** ✗ | **未做** ✗（本轮定位 ✓）⇒ 这是通向 `uid=0` 的最后一段 ✓ |

## 下一步（L 级 ⇒ 计划先行 ✓）

```
实现终端阶段（= 归档成功日志 w3 之后的三步 ✓）：
  ① `seccomp finit_module probe`（读/探针 ✓）⇒ ② `child seccomp fully bypassed`（解除 ✓）
  ⇒ ③ 启动 **root script**（`uid=0` ✓）；
⇒ 判据：出现 `root script start` ✓ ⇒ **`main: victim uid=0`** ✓。
```

## 门禁

```
check → 108 PASS ✓（新 framed 通过 ✓）；android → 0 警 ✓；lint → 0 ✓
```
