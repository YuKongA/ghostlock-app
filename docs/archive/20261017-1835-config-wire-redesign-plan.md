> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/config-wire-redesign-plan.md；归档路径 = docs/archive/20261017-1835-config-wire-redesign-plan.md。

# 配置系统 / wire / 解析 重设计（L 级计划 · 已裁定落地中 · schema 版本仍是 3）

> **2026-10-17 作废**：本文的载体（Lua 沙箱、script/wire 面、GLKv3 文档解析、pipeline/terminal 编排、nlohmann wire_doc）已从源码与构建中删除，
> 内容仅作历史记录；现行边界见 `docs/plan/MASTER-PLAN.md` §0.1。

> **用户裁定（2026-10-10，本次）**：① 数据格式＝**A**（JSON + JCS + 生成式 schema）；② wire＝**推荐案**（JCS JSON + 只读视图）；
> ③ 载荷＝**stdin / base64**；④ **profile-core 删除**，检查改由 Kotlin 承担；⑤ **运行期仍要有 Lua**——新写步骤编排器太麻烦，
> **限制靠 Lua 运行时**；⑥ **新 JSON 配置内嵌 Lua steps**；⑦ **v1/v2 统一迁入 LegacyProfileConverter**；⑧ **schema 仅由人工设计配置结构后手动修改，不依靠自动生成**。
>
> **沿革（AGENTS §1 强制确认 · 同日）**：本次裁定**推翻**上一轮设计中的三条——(a)「gate 后不再执行 Lua」；
> (b)「录制期装配、运行期无 Lua」；(c) 本设计首稿的「数据写 Lua」。并**修订**两条既有规则：AGENTS §4「配置权威＝GLK profile + HOCON」、
> 「迁移点 v1→3（其余拒绝）」⇒ 见 §7 与 §10。

## 0. 与活跃计划的关系（AGENTS §2）

| 条目 | 关系 |
| --- | --- |
| 桥接计划「数据 × 行为如何组装」节 + 装载期校验第 3 条（gate 后不再执行 Lua） | **被本设计推翻** ⇒ 该节改为「运行期由 Lua 编排，限制靠运行时」 |
| 桥接计划待裁 6/8 | 数据归属与 HOCON 终局并入本设计 |
| `PENDING-DECISIONS` A1 | **裁定完成**（选 A） |
| `ROADMAP` §4 R10 | **裁定完成**（本文件为其结论） |
| R10 措辞「全 lua 配置文件」 | 精确化：**行为与编排是 Lua；数据是声明式 JSON**（steps 内嵌于配置） |

## 1. 诊断

复杂度根源＝**同一份事实多处表示**：HOCON 文本 · MsgPack · C++ 键目录 · Document 绑定，外加 Kotlin 合并与零填充链。
两个历史隐患仍适用：A 零填充/兜底让 required 形同虚设；B `multicast` 无调参资产 ⇒ 全表必填会拒掉真机已验证组合。

## 2. 目标（可检验）

1. 一个事实一处权威（源 JSON ＝ wire ＝ 宿主与脚本读到的**同一棵树**）。
2. 数据不可执行（纯值）；**编排是 Lua**，其行为受限由**运行时**保证（白名单 + surface + 四预算）。
3. C++ **不新增转换层**：新增件仅两处——有界 JSON 解析器、只读视图（以删除面为补偿）。
4. 密钥/端口不入任何文本：走会话帧，用后清零。
5. 对拍由 manifest 生成（native 导出、Kotlin 消费），跨端列表不手抄。

## 3. 数据与配置形态（已裁定）

配置文件＝**一份 JCS JSON**，三个顶层键：

| 键 | 内容 | 性质 |
| --- | --- | --- |
| `data` | 设备事实/几何/预算/名字表 | **纯值**（不可执行）；u64 一律十进制字符串（JSON number 是双精度） |
| `steps` | **内嵌的 Lua 源码**（一个 chunk，返回 `procedure`）；沙箱装载并执行 | 受限代码（白名单 + 预算） |
| `payloads` | 载荷清单（名 + sha256 + 通道） | 值；字节走 stdin（默认）或文档内 base64 |

- **schema＝人工设计、手动维护的唯一权威**（与数据同目录，与 C++ 无关）：由它生成三条线——① 构建期校验（JSON Schema 或生成器自校验）② manifest（Kotlin/UI/迁移消费，对拍面不变）③ 文档内 **role→path** 索引段（native 宿主按 role 取键）。**禁止由 C++ 表／数据／工具自动反推或改写 schema**；加一个键＝人工改 schema（按 L 级 wire/profile 流程），不是改 C++。
- 信任分级：`data` 无执行路径；`steps` 永远只经沙箱运行；两者在装载时**分别校验**。

## 4. 运行形态（运行期有 Lua，限制靠运行时）

- `main` → 长度前缀 framing → **有界 JSON 解析器**（arena，计入沙箱预算）→ `data` 以**只读视图**（userdata + 共享 metatable）挂成 `cfg`。
- `steps` chunk 在沙箱内执行：它经 surface（`writeop`/`copyop`/`settle`/`handoff`/`cfg`/`close`）调 `KernelAccess` 与终端 handoff，**顺序由它自己决定**（ADR-0011 一致）。
- **限制**＝白名单（无 `load`/`io`/`os`/`package`）＋ 四预算（指令/内存/时间/条目）＋ surface 的 per-call 具名拒绝；不再有「先录制、再准入」的 plan gate。
- 视图四项（必须写死）：**三件套**（`__index`/`__newindex` 具名拒绝/`__len`+`__pairs`）· **嵌套视图身份缓存**（registry per-run 表，避免 `cfg.a == cfg.a` 为假）· **深度与大小上限** · **宿主经 manifest 的 role→path 读同一棵树**（C++ 不写死键名）。

## 5. wire

- 一个版本号 3；wire ＝ **JCS JSON**，顶层键即段（`data`/`steps`/`payloads`）⇒ 不必另立二进制段表。
- framing 沿用长度前缀；载荷默认 **stdin 流式**，小件可选文档内 **base64**；身份由 JCS 摘要 + 每载荷 sha256 钉住。
- 拒绝面：装载（段缺/键缺/类型不符/steps 形状）· 运行（surface 与 `AccessStatus` 的既有名字）——**不新增名字**。

## 6. 迁移与删除（已裁定）

| 项 | 处置 |
| --- | --- |
| v1 / v2 → 3 | **统一进唯一迁移点 `LegacyProfileConverter.kt`**（其余版本仍拒绝） |
| HOCON 资产链 | 两阶段：① 双读（新链路并行）② 停读并**归档** `.conf`，不删历史 |
| `profile-core` | **删除**（布局/原生模型/插件探针），校验改由 **Kotlin**（app 模块）承担；manifest 仍由 native 导出 |
| 删除清单（解析器成立的补偿） | `core/lib/mpack`（16k 行 vendored）· `glkv3_document.*` · `key_catalog.hpp`（C++ 键表整表删除：**人工**据现有语义撰写 schema 源（工具可辅助罗列，**产物须人工审校**），核对无误后删表；role 只标宿主自己要读的键，脚本按 path 读视图）· `route_catalog`/`run_plan` 语义 · `component_catalog` 派发 · `plan_gate`（不再需要预检）· `run_pipeline_temp.cpp` · Kotlin 零填充链 · 现有 cfg 字段列表机制 |

## 7. 需同步修订的条目（批准后执行，均为替换而非追加）

- `AGENTS.md` §4 两条：① 配置权威 ⇒「GLK profile（JSON 源 + JCS wire）」；② 迁移点 ⇒「v1/v2→3，其余拒绝」。受 ≤12 KB 限制。
- 桥接计划：装配节与装载期校验第 3 条按 §0 标注推翻；`PROFILE_SCHEMA.md`、`introduction.md` 与 UML 同批更新。

## 8. 待裁（仅剩 3 条）

1. 载荷默认通道：stdin 优先（推荐）/ 文档内 base64 优先 / 按大小自动选择。
2. 是否新增 ADR（配置系统：JSON+JCS 源、内嵌 steps、运行期 Lua 编排即限制边界）——**版本号仍是 3**，本文是 v3 的同号重做。
3. `profile-core` 删除与 HOCON 退场的**批次顺序**（先建后删 vs 同批切换）。

## 9. 分批

| 批 | 内容 | 判据 |
| --- | --- | --- |
| P0 | 消隐患 A/B | 白盒 grep = 0；黑盒 multicast 组合仍被接受 |
| P1 | 有界 JSON 解析器 + 只读视图（含 RFC 边界测试） | 非法文档具名拒绝；视图只读与身份缓存用例全绿 |
| P2 | `steps` 沙箱执行 + surface 接线 + `cfg` 视图 | 真机门禁：一类 route 走通 |
| P3 | 生成器（canonical+摘要+manifest+载荷）＋ v1/v2 迁移 | 两次导出逐字节相同；两版样本迁移后行为一致 |
| P4 | 删 mpack/Document/派发/plan_gate/profile-core；HOCON 退场归档 | 净删；grep 无第二读取路径；真机回归 |

## 10. 代价与补偿（沿革要求的如实登记）

- **失去**：全盘先验检查（原 plan gate 的「动手前一次拒绝」）。**补偿**：per-call 具名拒绝 + 每步前置检查落在 surface + 真机门禁；登记为 P1/P2 的设计要求。
- **新增**：解析器 + 视图 + 运行期 Lua 常驻（内存/时间计入四预算）。**补偿**：删除清单（§6）净删远大于新增。
- **信任边界前移**：`steps` 与 `data` 同文件 ⇒ 该文件属**可信分发物**（JCS 摘要钉身份），安全由运行期限制而非来源审查保证。
