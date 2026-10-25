# ADR-0020：日志子系统可写全局例外（common/gl_log）

- 日期：2026-10-22
- 状态：**Proposed（待用户确认）**
- 关系：[JNI 重写计划](../plan/43499-43284-jni-rewrite-plan.md) §3 C5；[introduction](../../development/introduction.md) §八；[43499 全局审计](../analysis/43499-global-audit-20261022.md)

## 背景

AGENTS §4 只允许两个全局：`g_exploit_session` 与启动期只读 `g_direct_map_end`。
43499 接入标准日志库 `common/gl_log` 后，DSO 内出现 4 个可写 `d`：

```text
g_export_fd  g_min_level  g_timestamps  g_start_ms
```

它们不是攻击/会话状态，但按 `readelf -SW` 属于 `.data`，与「零可写全局」字面冲突。

## 决策

1. 允许上述 4 个符号作为**日志子系统例外**；C5 扫描的允许集 = 恰好这 4 个，新增第 5 个即 FAIL。
2. 例外不扩展到攻击/会话状态；不得以日志为名新增可变全局。
3. 长期方向：把 `gl_log` 改为 session/context API，归还这 4 个符号（T-LOG）。
4. 本 ADR 未获用户确认前，C5 对该 4 符号按 **Proposed 例外**放行；用户否决则回退 session API 或删除日志接入。

## 后果

- 正面：保留现有标准日志与 fd sink，不阻塞 JNI 化。
- 代价：`.data` 非空，需在每批审计中人工确认「只有这 4 个」。
- 判据：`readelf -SW` 全量 + `llvm-nm` 交叉；允许集硬编码 4 个符号名；差异即红。

## 批准

| 项 | 内容 |
|---|---|
| 批准人 | 待用户 |
| 状态 | Proposed（2026-10-22 记录事实；批准后改 Accepted 并回填日期） |
