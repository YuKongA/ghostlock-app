# 真机发现：注入的策略在调用内被写入，调用方读回的是初始值（2026-10-08）

> 级别：**攻击关键路径相关**（页准备↔搜索的接合依赖它）⇒ 按 AGENTS 归档
> 设备 `QV770MFGJ1` · 探针 `tools/snitch_prepare_probe.cpp`

## 1 现象（原始输出）

```
[id] probe &policy=0x7ff046f9f0
[id] prepare call site &snitch=0x7ff046f9f0     <- 身份相同 ✓
[id] start this=0x7ff046f9f0 ran=0
[id] after search status=plan-missing ran=0 this=0x7ff046f9f0
[id] set ran=1 this=0x7ff046f9f0                <- 调用内确实写了 1 ✓
  policy:  ran=0 has=0 addr=0 outcome=geometry-missing rounds=0 probes=0 wanted=0 region=0x0
  prepare: status=plan-missing at=7 opened=5 closed=3   <- 准备自己的结果却正常 ✓
```

## 2 已经排除的

- **不是拷贝**：`&policy` 与 `&snitch` 与 `this` **同一地址** ✓；
- **不是旧二进制**：`-B` 强制重建后现象不变 ✓（AGENTS 警告过的 mtime 陷阱已排 ✓）；
- **不是没调用**：同一 `start` 里前后两行都打出来了 ✓；
- **不是 printf 的问题**：探针用的**访问器**（`has_collision()`）也读到 0 ✓；
- **探针自己先调一次则完全正常** ✓（`started=1 ran=1 rounds=10` ✓）。

## 3 结论与下一步

**写入发生在调用内 ✓、身份同一 ✓、而调用方读回初始值 ✗** ⇒ 这是**内存/ABI 层面的分歧**（最可能是两侧对 `SnitchPolicy` 的**布局**理解不同 ✗，或优化器对别名做了假设 ✗）。

**下一轮的决定性测量**（不再猜 ✓）：**在 `start` 内与探针内各打印 `sizeof(*this)` / `sizeof(policy)` 与 `offsetof` 关键字段** ⇒ 若两侧不一致 ⇒ 布局/ODR 问题 ✓；若一致 ⇒ 优化器/别名问题 ✓。

**顺带的影响** ⚠：在解释清楚之前，**该探针自身的报告不可信** ✗（它读的是陈旧的策略状态 ✓）⇒ 不得据此判定接合成功/失败 ✓。
