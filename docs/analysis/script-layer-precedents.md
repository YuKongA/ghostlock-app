# 脚本层设计先例调查（Lua 配置集成度 · R10 输入）

**目的**：为「Lua 配置集成度」（ROADMAP §4 R10 待裁）找同类需求的既有设计，构成可核验的决策输入。
**方法**：只取一手文档（官方文档/规范/仓库原始文件）逐条抓取事实；未取到一手者不进入结论。
**范围**：多种功能 + 复杂调度 + 插件/脚本扩展面 —— 开源工程与**闭源但开放插件/mod 生态的游戏**并重。

---

## 一、先拆成三个独立轴（「用不用 Lua」不是一个开关）

| 轴 | 问题 | 先例给出的答案形态 |
| --- | --- | --- |
| A 数据/行为是否分阶段 | 设备事实与几何放哪；「做什么、什么顺序」放哪 | 分阶段：声明 → 装配 → 运行 |
| B 能力边界由谁定 | 脚本能碰什么、何时能碰 | 按**阶段/相位**裁剪 API，不按文件裁剪 |
| C 顺序与装载权威在哪 | 谁定执行顺序、谁定装载顺序 | 有中心计划者=计划；无计划者=依赖+名字排序 |

## 二、逐例事实（一手来源）

### 游戏（闭源 + 开放生态）

**Factorio**（[data lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html)）

- 装载顺序「takes into account their **dependencies first and their internal names second**」，该顺序决定装载序列。
- **三阶段**：settings 跑 settings.lua；prototype 阶段以只读全局 settings 构造原型；control 阶段自开局/读档起持续运行。
- 每 mod 独立 Lua 状态；**迁移脚本**在自身状态内跑且存档记住已应用的迁移文件名；版本或 mod 增删会被显式检测并走不同分支。

**World of Warcraft**（[Secure Execution and Tainting](https://warcraft.wiki.gg/wiki/Taint)）

- 「mandates human decision-making, particularly in combat, by **protecting many functions from insecure or tainted use**」。
- 执行**起始即 secure**，可调受保护函数；Blizzard 签名代码 secure，**第三方 addon 一律 tainted**。
- taint 沿**值与闭包**传播（local/global/表键/表值/闭包皆可污染）。
- 即：不可信内容仍能加载运行，但**受保护动作在 tainted 路径上被拒**。

**Minecraft**（[Data pack](https://minecraft.wiki/w/Data_pack)）

- 数据包 = 文件夹或 zip + pack.mcmeta，纯 **JSON 数据**；代码能力走 mod 加载器（数据/行为双轨）。
- 「Data packs load their data based on the **load order**」，顺序可见可改；坏包报错而非静默跳过。

**Fabric**（[fabric.mod.json](https://raw.githubusercontent.com/FabricMC/fabric-example-mod/1.21/src/main/resources/fabric.mod.json)）

- 描述符键：schemaVersion · id · version · entrypoints · mixins · depends。
- entrypoints 按**侧/阶段分列**（main/client）；depends 是**约束声明**（fabricloader/minecraft/java/fabric-api），不是排序；mixins 列表即**原生逃生口**。

### 攻击/运维工程

**Nmap NSE**（[Script Format](https://nmap.org/book/nse-script-format.html)）

- 脚本 = 描述字段（categories 等）+ **rule**（prerule/hostrule/portrule/postrule）+ **action**（仅 rule 通过时执行）。
- 即「何时」与「做什么」分离，**相位本身是契约**；参数经 script-args 传入。

**Redis**（[Lua API](https://redis.io/docs/latest/develop/programmability/lua-api/) · [Eval intro](https://redis.io/docs/latest/develop/programmability/eval-intro/)）

- 「Scripts run in a **sandboxed context** and can only access specific Lua packages」；**禁声明全局变量/函数**（防止脚本自持运行时上下文）；读未定义全局亦报错。
- 「Redis guarantees the script's **atomic execution**」；执行期间「all server activities are blocked」。

**Starlark**（[spec](https://raw.githubusercontent.com/bazelbuild/starlark/master/spec.md)，为「配置文件」而造的语言）

- 「Execution is **finite**. The language does not allow recursion or unbounded loops.」
- 「The language is **deterministic and hermetic**」；副作用必须由宿主显式提供。

**OpenResty / lua-nginx-module**（[README](https://raw.githubusercontent.com/openresty/lua-nginx-module/master/README.markdown)）

- 同一份 Lua 按 **nginx 相位**挂载：rewrite / access / content / header_filter / log。
- **能力按相位裁剪**：cosocket 在 set_by_lua、log_by_lua、header_filter_by_lua 等上下文**被禁用**；被跳过的相位产生的信息，后续相位也拿不到。

### 平台/调度工程

**Kubernetes**（[controller 概念](https://raw.githubusercontent.com/kubernetes/website/main/content/en/docs/concepts/architecture/controller.md)）

- 控制回路 + 期望状态：对象以 spec 表期望，控制器持续把实际状态推向期望；外部状态由控制器自行求取（调用方不描述步骤）。

**Neovim**（[lua.txt](https://raw.githubusercontent.com/neovim/neovim/master/runtime/doc/lua.txt)）

- 「Lua plugins and user config are automatically discovered and loaded」——配置语言=宿主语言：**零沙箱、零拒绝词汇**，换表达力。
- 同类极端：Emacs Lisp、Gradle Kotlin DSL（本仓自身即以 KTS 作构建配置语言）。

## 三、四条结论（各有上述证据）

1. **成熟生态几乎都「数据与行为分阶段」**：Factorio 三阶段、Minecraft 数据/代码双轨、Fabric 描述符与入口点分离；纯「配置即代码」只出现在宿主语言即配置语言的场景（Neovim/Gradle KTS/Emacs）。
2. **能力边界按阶段/相位划，不按文件划**：OpenResty 按相位禁 cosocket；NSE 用 rule 相位决定 action 可否执行；Redis 直接禁全局。
3. **可信度分级是闭源游戏生态的核心机关**：WoW 的 taint 让不可信 addon 能加载能运行，但受保护动作被拒，且污染沿值与闭包传播 —— 与 ADR-0009「能力硬线 + 失败即拒」同构。
4. **确定性要设计进去而不是事后补**：Factorio（确定性多人 + 每 mod 独立状态 + 迁移）、Starlark（有限执行/无递归/确定性/hermetic）、Redis（原子执行换一致性）。

## 四、落到本项目：三档形态与代价

| 档 | 形态 | 先例对照 | 代价 / 冲突 |
| --- | --- | --- | --- |
| **1（建议）** | **行为全 Lua，数据留 profile/wire**：procedure 只声明「做什么、什么顺序、失败怎么办」；设备事实/几何/预算经**只读 cfg 快照**注入 | Factorio：control 阶段 vs settings/prototype；Fabric：入口点 vs depends | 多一层对拍面（manifest 声明 cfg 路径/类型/范围），桥接计划已含 |
| 2 | 数据与行为同文件（「全 Lua 配置文件」的字面） | Minecraft 数据包与代码合一 | 设备事实被拖进脚本 ⇒ 与序 6③「运行时密钥/SPI/端口绝不进脚本」冲突 |
| 3 | 宿主语言即配置语言 | Neovim / Gradle KTS / Emacs | 零沙箱、零拒绝词汇 ⇒ 与 ADR-0009 冲突 |

**采档 1 时该抄的四条**：① 数据只读注入（Factorio 的 settings 全局）；② 能力按阶段给（OpenResty/NSE）；③ 装载期形状检查 + 具名拒绝（NSE 描述字段 / Fabric 描述符）；④ 声明式**约束**（Fabric depends）用于可用性校验。

**先例还给了一条「别抄」**：Factorio 与 Minecraft 需要 mod 装载**排序**，是因为它们**没有中心化计划**；本项目有（ADR-0011 顺序权威=计划）⇒ **不要把 depends 变成排序权威**，否则一处顺序两处权威；depends 只作能力/可用性校验。

## 五、对 R10 各待裁项的输入

| 待裁项 | 先例怎么说 | 我的读法 |
| --- | --- | --- |
| 文件名是否必须等于 procedure.name | Fabric/数据包都是「文件内有权威声明，文件名只是约定」 | 文件名自由；name 权威（装载期只认 name） |
| cfg 数据来源 | Factorio 把 settings 与 control 分文件分阶段；Minecraft 数据 JSON、行为代码 | 走文档（与 profile 同源）；procedure 文件只留行为 |
| 是否需要 depends 类字段 | Fabric 有 depends，但它是**约束**不是序列 | 可加，只作能力可用性校验；排序仍归计划 |
| steps 还是 run(ctx) | NSE 是 rule+action 两段；Factorio 是事件回调混合 | 两者皆有先例；按「宿主驱动还是脚本自驱」选，别两套并存 |

## 六、来源清单（均已一手抓取）

| 来源 | 抓到的事实 |
| --- | --- |
| [Factorio data lifecycle](https://lua-api.factorio.com/latest/auxiliary/data-lifecycle.html) | 依赖优先+名字次之；settings/prototype/control 三阶段；迁移在 mod 自身状态内跑 |
| [WoW taint](https://warcraft.wiki.gg/wiki/Taint) | 起始 secure；第三方 addon tainted；受保护函数拒绝 tainted；沿值与闭包传播 |
| [Minecraft Data pack](https://minecraft.wiki/w/Data_pack) | pack.mcmeta + JSON 数据；load order 可见可改；坏包报错 |
| [Fabric mod.json](https://raw.githubusercontent.com/FabricMC/fabric-example-mod/1.21/src/main/resources/fabric.mod.json) | 描述符键；入口点分侧；depends 约束；mixins 逃生口 |
| [Nmap NSE](https://nmap.org/book/nse-script-format.html) | 描述字段 + rule 四相位 + action |
| [Redis Lua API](https://redis.io/docs/latest/develop/programmability/lua-api/) | 沙箱+包白名单；禁全局；原子执行并阻塞服务器 |
| [Starlark spec](https://raw.githubusercontent.com/bazelbuild/starlark/master/spec.md) | 有限执行、无递归/无无界循环；确定性 hermetic |
| [OpenResty](https://raw.githubusercontent.com/openresty/lua-nginx-module/master/README.markdown) | 相位挂载；cosocket 部分相位禁用 |
| [Kubernetes controller](https://raw.githubusercontent.com/kubernetes/website/main/content/en/docs/concepts/architecture/controller.md) | 控制回路；spec=期望状态 |
| [Neovim lua.txt](https://raw.githubusercontent.com/neovim/neovim/master/runtime/doc/lua.txt) | 配置/插件自动发现（配置语言=宿主语言） |

**未取到一手（不计入结论）**：Garry's Mod Lua 参考（404）、Skyrim Creation Kit Papyrus（403 Cloudflare 拦截）、Second Life LSL 限制（404）。
