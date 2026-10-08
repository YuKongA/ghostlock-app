# 附件：中途切换 route 的写法（待裁 · 2026-10-08）

> 分析类（≤8192 B）。**配套**：[lua-surface-design](lua-surface-design.md)（§1 暴露面表 · §6 待裁）。来源：用户提问「**中途切换 route 怎么办，新建 backend 吗**」+ Lead 答复框架。

## §A1 结论（一句话）

**不新建 backend**：**backend = 漏洞（cve id）**，**route = 同一漏洞的不同打法**；**换路发生在同一 backend 的句柄上**。

## §A2 约束（三条，决定写法）

| # | 约束 | 对写法的影响 |
|---|---|---|
| **C1** | **route 生命周期 `prepare → execute → disarm → destroy`** | **两条 route 不得同时在窗口里活着** ⇒ 切换只能**串行**（先 disarm 旧的，再 prepare 新的） |
| **C2** | **窗口内零 Lua** | 切换**只能发生在窗口之间**（脚本在窗口外 stage，实现层在窗口之间完成 disarm/prepare） |
| **C3** | **「禁止任何运行时回退」**（用户早先裁定） | **自动 fallback 不允许**；**换路必须显式表达**（谁换、换成什么，写在计划里） |

## §A3 结论：**方案 2 = clean 句柄 + 重新 acquire**（2026-10-08 采纳）

1. **route 只在 acquire 时给定**：`backend(id, { route = "tcp_zerocopy" })` ⇒ **不存在「切换」动作**，只有**释放 + 重新获取** ⇒ 「**表达一致**」**自动成立**（首设与切换不再是两套语法）。
2. **route 生命周期 ≡ 句柄生命周期**：`prepare → execute → disarm → destroy` 精确对应 **acquire → use → release**，**不引入第二套语义**。
3. **与 ADR-0010 对齐**：`close` ⇒ **generation 递增**、旧句柄失效（「忘了关」/「用了旧句柄」被**代际检查**抓住 ⇒ `handle-stale`）。
4. **暴露面新增 `close`**（句柄释放；否则句柄会泄漏到计划结束）⇒ 暴露面 **7 条**：`backend` · **`close`** · `writeop` · `copyop` · `settle` · `handoff` · `cfg`。
5. **新增 gate 判据（可失败、可证伪）**：同一 backend **同时存在两个活跃句柄** ⇒ 具名拒绝 **`handle-conflict`**；另定义「**计划结束自动释放所有句柄**」为兜底，但**换路必须显式 `close`**。
6. **被否方案（沿革不删）**：**方案 1**（统一动词如 `init_route`/`use_route` 同一调用）—— 否决理由 = 命名在「首设」与「切换」两个语境下**都别扭**，且需为新动词**定义语义**；**若日后改选方案 1，建议中性名 `route(...)`**。

- **判据（本案，可证伪）**：白盒 = 计划内**无隐式**换路（grep 无自动路径）· 同 backend 两活跃句柄 ⇒ `handle-conflict` · `close` 后旧句柄 ⇒ `handle-stale`；黑盒 = `close` → `backend(id, {route=…})` → 写操作成功且审计行连号；极端值 = close 未 acquire 的句柄 / 双 close / 换到不存在 route（具名拒绝）。
