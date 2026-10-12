# 43499 · 暖机/CPU 两个维度也对上之后的账（round 101-102）

## 本轮两处（都按归档/退役的门禁要求）

1. **固定 CPU 对**（退役门禁明文要求 ✓）：wire 键 `execution.recommended_cpus.{main,consumer}` ✓，
   演员入口 `pin_self(...)` ✓；真机打印 `main: cpus main=0 consumer=1` ✓；
2. **暖机轮数**（归档成功那次的引擎自报行是唯一的权威 ✓）：

```
归档：multicast route status=0 clean=1/1 step=0 attempts=16 calls=1 success=1
我（改后）：main: route status=0/0 attempts=15/17 calls=1 successes=1 stamp=-1/99
```

⇒ `arm_sequence` 由 2 改为 **15**（文档里本来就是 `w1_attempts=15` ✓）：真机上 `TEMPTRIG attempt=15
ret=-1 errno=99 armed=1` ✓ ⇒ 前 14 轮"只复制、不 arm"（退役注释：copies overwrite each other ✓）✓。

## 现在的账（如实）

| 项 | 状态 |
| --- | --- |
| 移植差异（14 处） | 全部修齐 ✓（每处带注释、有退役行号为据） |
| 几何 / warm-up / requeue 语义 / 拷贝落地 | 与退役一致 ✓ |
| CPU 固定对 / 暖机轮数 | 已对齐 ✓（本轮） |
| 页面进内核 | ✓（`page sent value=65536`） |
| 写落地（bisect） | **依然零** ✗（`at2000=0x0`，多次不同配置下一致） |
| `victim uid=0` | **从未** ✗ |

## 结论与建议（诚实）

- 所有**可量化、可比对**的维度都已与退役一致 ✓，而写仍不落 ✗ ⇒ 剩下的差别不在这些数里 ✓；
- 按纪律（同批门禁连续失败 2 次即停 ⇒ 回 debug ✓），这条链已经远远超过这个阈值 ✗ ⇒
  **建议转入"计划—评审"模式**：把 14 处修复与全部否定结果整理成一份 L 级文档 ✓，
  下一轮（或下一次会话）以**新的假设集**重启，而不是继续逐处猜测 ✓；
- 可选的两条**换证据来源**的路径：
  ① 用 `perf`/`ftrace` 之类**内核侧观测**（若设备允许 ✓）直接看那条 PI 链是否被走过 ✓；
  ② 回到本目标的**另一半**（43284 的 wire/schema 与 Lua 接口 ✓）—— 那一半的判据更明确 ✓，
     且不依赖这条尚未落地的链 ✓。
