# 附件：Document 的定义与执行规则（2026-10-08）

> 分析类（≤8192 B）。**配套**：[lua-surface-design](lua-surface-design.md) §1.2（摘要）· [route 切换附件](lua-surface-annex-route-switch.md) · [A6 待批单](../plan/A6-baseline-approval.md)。来源：用户澄清「**现在的 document 不就是 lua 吗**」。

## §B1 Document = Kotlin resolve 的产物（三段）

| 段 | 内容 | 产出者 |
|---|---|---|
| ① **数据面** | 几何 / 偏移 / 调参 / 可用性（**纯数据**） | HOCON（builtin / imported / overrides 合并后） |
| ② **计划** | **Lua 文本 + 大小 + sha256** | 来自 HOCON `available[]` 项（脚本是**值**） |
| ③ **收尾 payload** | 执行脚本 / LKM（**bin + sha256**） | **Kotlin 在 resolve 后生成**（磁盘 HOCON 不定义具体 handoff） |

## §B2 执行规则（**零隐性回退**）

计划在 root 前 **stage 一次** ⇒ `plan_gate` **校验 staged** ⇒ **按相位执行** ⇒ 收尾**按 ③ 段 payload 执行**。

## §B3 三条推论

1. **脚本内的循环/条件/重试 = document 行为（允许）**：脚本就是 document ⇒ 其控制流属于 document；**被禁止的是 runtime 自己加的回退**（= 不按 document 执行）。**界线：显式（写在计划里）允许；隐性（runtime 自加）禁止**。
2. **换路必须写进 document**：要么计划里显式（~~`b:use_route("tcp_zerocopy")`~~（**已废，见方案 2**：[route 附件](lua-surface-annex-route-switch.md) §A3 = `close` + 重新 `backend(id,{route})`），staged 动作、只能在窗口之间），要么 **Kotlin 在 resolve 阶段定**（产出不同计划/参数）；**runtime 永不自己换路**（候选/约束见 [route 切换附件](lua-surface-annex-route-switch.md)）。
3. **`run` 与 ③ 段一致地移除**：收尾命令属 **document ③ 段**（Kotlin 产出），**不作为 Lua 侧能力** ⇒ 暴露面收口为 **7 条**：`backend` · **`close`** · `writeop` · `copyop` · `settle` · `handoff` · `cfg`；**`TD-RW2-2` 随之作废**。
