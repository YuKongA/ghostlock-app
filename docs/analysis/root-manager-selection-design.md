# Root 管理器选择（默认档）设计（L 级；设计稿，待用户确认）

> **状态**：**设计稿 v2（2026-10-05）**。v2 变更：① 用户答复 = KernelSU 系**共享 ksud**、包名可选（§3 第 1 条/§5）；② FolkPatch 越狱模式机制已取证（§2.1 **E4**，官方文档）= **加载 KernelPatch 模块**，属 P2；③ 新增**一等检查**「管理器是否存在、是否可启动」（§4.1，P1 必做，**权威判定在 native**）。方向由 Lead 定（§3）；**实现代码未写**，未动 `src/**`、`app/**`、`profile-core/**`。
> **落点**：`payload` 轴（`docs/analysis/contract-design.md` §3.15）；经用户确认后由 Lead 放行实现。
> **相关**：`terminal-payload-tiers-design.md`（r2，`c335aabc`）、ADR-0006、`plugin-extract-spec-design.md` 的「native 导出、Kotlin 只消费」纪律。

## 1. 背景与问题

需求：把「默认档」展开为「**启动 root 管理器**」的单选——KernelSU（现状）之外的分支/管理器可选；未实现的标「计划中」并置灰。

现状问题：

1. **App 无法选择**：`contract::RootProgramKind` 早已存在，但 wire 侧**零键**（`src/core/profile/**` 与 Kotlin 侧 grep `root_program` 均 0 命中）——生产路径上该枚举**除测试外无人使用**；
2. **运行时硬编码**：43284 的 `set_root_program()` 把 argv 拼成 `$GHOSTLOCK_HOME/ksud` 且 `kind = KernelSU`，即「选择」实际被写死；
3. **FolkPatch 是已声明的空壳**：枚举里有值、代码里无机制、包名/入口均未知；
4. 因此今天的「默认档」= 隐式的 KernelSU；需求是**把隐式变显式**，并给未实现项一个诚实的「计划中」位。

**关键洞察（FolkPatch 越狱模式的官方机制，见 §2.1 E4）**：FolkPatch 越狱模式的**前置条件正是 GhostLock 的产物**——「设备已拥有 Root」+「SELinux Permissive」，而这两者恰好是 GhostLock 的 W1/W2 结果。因此 GhostLock → 装载 `kernelpatch.ko`（手动符号重定位 + 绕过 CRC/vermagic + `init_module`）是一条**天然衔接**的路径。

⇒ 由此确定 **`kind = folkpatch` 在 GhostLock 侧的语义**：**加载 KernelPatch 内核模块**（而不是「启动 FolkPatch 管理器 App」）；它与 `kernelsu`（ksud late-load）**机制完全不同**，两者的输入 / 加载手段 / 生效条件必须分别列清（见 §4.1 对照表）。

## 2. 代码事实与依据（全部 file:line 核实）

| # | 事实 | 依据 |
|---|---|---|
| 1 | `enum class RootProgramKind : uint8_t { KernelSU = 0, FolkPatch = 1, Custom = 2 }` 与 `struct RootProgram{kind; std::array<char,192> argv; set_argv(); argv_view();}` 已在 contract，注释原文：「Which root program the App selected for this session (single value). The program is a parameter, never a compile-time binding; both the root_child and the umh_forward terminal can launch it.」 | `src/core/contract/identity.hpp:366-380`（枚举 `:368`，结构 `:376`，`kArgvCapacity = 192` `:377`） |
| 2 | **wire 侧零键**：App 无法选择（选择只能是文档，不能是 CLI/编译期） | 本批 grep：`src/core/profile/**`、`profile-core/src/main`、`app/src/main` 的 `root_program` 命中数 = 0 |
| 3 | 运行时硬编码：`set_root_program()` 以 `$GHOSTLOCK_HOME/ksud` 构造 argv 并置 `kind = KernelSU`（注释：Kotlin 入口会把 ksud 拷到该路径） | `src/core/backend/cve_2026_43284/execution_binding.cpp:49-56` |
| 4 | 43284 走 ksud late-load：`build_late_load_command(root_program, package_name, late_load_args, selinux_exec_context, UmhCommand&, …)` → `{program, "late-load", flags…}`；注释：**无 shell、不做字符串拼接**、长度有界、控制字节受检 | `src/core/backend/cve_2026_43284/lkm/lkm_image.hpp:85-92`；调用点 `backend_terminal.cpp:145-150` |
| 5 | 包名权威：`default_root_package(kind)` = KernelSU → `me.weishu.kernelsu`，**其它 kind → 空**（从不猜包名） | `backend/cve_2026_43284/schema.hpp:68-77`（`kCve2026_43284RootPackageKernelSU` + `root_package_convention`）、`lkm/lkm_image.cpp:346-348` |
| 6 | 兼容头：`terminal/root_program.hpp` 只是 `contract::RootProgram(Kind)` 的别名转发 | `src/core/terminal/root_program.hpp` |

### 2.1 外部事实（Lead 核实；引 URL；不在本仓库代码内）

| # | 事实 | 来源 |
|---|---|---|
| E1 | **FolkPatch 是 APatch 系，不是 KernelSU 系**：README 原文「This solution is based on a **non-parallel extended branch of APatch**, with a primary focus on UI/UX design」 | <https://github.com/LyraVoid/FolkPatch>（README；本稿撰写时复核页面标题一致） |
| E2 | FolkPatch 的 Gradle 实测：**`applicationId = "me.yuki.folk"`、`namespace = "me.bmax.apatch"`**——`namespace` 直接暴露 APatch 血统 | 该仓库 `app/build.gradle.kts`（Lead 实测） |
| E3 | **具名 KernelSU 分支的包名无法从常见路径推断**：对 KernelSU-Next / SukiSU-Ultra / ReSukiSU 各自仓库的 Gradle 文件尝试核实 `applicationId`，**三条路径均 404**（模块布局/默认分支不同：KernelSU-Next 默认分支是 `dev`） | Lead 本次核实记录（三条 404 路径） |

| E4 | **FolkPatch「越狱模式」机制（官方文档）**：① 「越狱模式允许在**未修补的原厂内核**上直接加载 **KernelPatch 内核模块**，无需刷入修改后的 `boot.img` 即可获得 Root」；② **四步**：管理器**提取内置的预构建 `kernelpatch.ko`** → 通过 **MagicaService 启用 adb-root 提权** → **`apd insmod` 手动重定位**（未定义符号对照 `/proc/kallsyms`，**绕过 modversions(CRC) 与 vermagic 校验**，再经 **`init_module`** 系统调用加载；该机制**移植自 KernelSU 的 `ksuinit::load_module`**）→ **软重启**（仅重启 Android 框架、不重启内核）使 Root 生效；③ **前置条件**：**SELinux 必须 Permissive**、**设备已拥有 Root**、未安装真实 KernelPatch、内核与预构建模块兼容；④ 官方标注**不稳定/尝鲜**：兼容性受限、**重启后失效**、依赖 adb-root + Permissive、失败可能影响稳定性「请勿反复尝试」 | <https://fp.mysqil.com/guide/jailbreak/>（官方文档；本稿撰写时已 fetch 全文并逐条核对关键词：KernelPatch / apd insmod / /proc/kallsyms / modversions / vermagic / init_module / Permissive / 软重启） |

**由此得出的设计结论（写进契约）**：

1. **FolkPatch 不能复用 ksud late-load**：其内核侧加载机制与 KernelSU **不同谱系**（APatch/KernelPatch），`lkm_image` 的 `{program, "late-load", …}` 命令形态**不适用**；FolkPatch 属 **P2**，需要**自己的机制设计 + 独立真机门禁**（含「**软重启后 Root 生效**」这一覆盖项）。
   ⇒ `RootProgramKind::FolkPatch` 的占位语义应表述为「**加载 KernelPatch 模块的不同谱系**」，**不是**「KernelSU 的另一种包名」。
2. **KernelSU 系共享 ksud ⇒ 机制同一（用户口径，见 §3 第 1 条）**：所有 KernelSU 分支共享 `ksud`，因此**不需要**为每个分支建「机制白名单」；`kind=kernelsu` **默认按 ksud 走**（= 当前行为），**包名可选**（见第 3 条）。
3. **分支包名 = 可选便利项，不再是前置条件**：默认值来自代码现有权威常量（`me.weishu.kernelsu`，`schema.hpp:68-77`）；用户填 `manager` 就用它（他自己的声明，UI 提示「由你负责」）；核实过的分支可进**候选列表**供 UI 选择，未核实的允许手填或走 `custom`。**「绝不猜」纪律不变**——我们只使用代码权威默认值或用户显式声明（E3 的 404 记录即「为什么不猜」）。

**既有模式（照抄，不另创）**：

- **「计划中」token**：注册进白名单、解析接受、**选择门禁拒绝**、UI 置灰并给原因（现有 `rootchild (计划中)` 即此形态）；
- **可用性矩阵由 native 导出、Kotlin 只消费**：`stage_availability`（`src/core/plugin/schema.hpp` 的 `RuntimeBackend`/`stage_available_on()` + `plugin/probe.cpp` 的 header 第 5 行）是范本；
- **制品导入**：no-backup 目录 + 本地 SHA-256 + 原子落盘（插件 P1 那套）。

## 3. 方向（Lead 已定，逐条落实）

1. **定位（用户口径 + E4 洞察）**：归入 **`payload` 轴**——`payload.tier = "root"` + `payload.root.kind ∈ {kernelsu, folkpatch, custom}`：
   - `kernelsu`：**KernelSU 及其分支的统称**（共享 `ksud`）⇒ **默认按 ksud 走**（= 当前行为），**不要求**指定包名；`payload.root.manager`（管理器**包名**）是**可选精确选择**——填了就用它作为 late-load 的 package 参数，没填就用现有默认 `me.weishu.kernelsu`；
   - `folkpatch`：**加载 KernelPatch 模块**（E4 机制），与 kernelsu 机制完全不同，属 P2；
   - `custom`：用户指定程序/argv（不猜任何东西）；
2. **向后兼容**：**无 `payload` 段 = 今天的行为（KernelSU/ksud）逐字节不变**；**显式 `kernelsu` 与不写等价**（把隐式变显式）；
3. **UI**：默认档展开为「启动 root 管理器」单选；**未实现的管理器标「计划中」并置灰**（沿用既有模式），**不得**假装可用；
4. **一等检查（用户第三条，P1 就做）**：**任何 root 管理器都要先检查「系统里是否存在、是否可启动」**——App 侧给明确状态，**native 侧绑定/启动前再复核一次**（权威判定在 native）；不存在/不可启动 ⇒ **不静默降级、不猜替代品**，按 payload 语义「不中断链路 + 本次结论标未完成 + 逐项原因」（详见 §4.1/§7）；
5. **分批**：**P1** = wire + UI 旋钮 + **存在性/可启动检查（两侧）** + native 把 kind/manager 穿到既有 late-load（KernelSU 行为不变）；**P2** = FolkPatch 的 KernelPatch 模块加载机制（手动重定位 + 绕过 CRC/vermagic + `init_module` + **软重启确认**）+ **独立真机门禁**；
6. **纪律**：**绝不猜包名**——只用代码权威默认值或用户显式声明；每个 P2 项都要 **file:line / 官方文档依据 + 真机门禁归档**。

## 4. 契约草案（`payload.root.*`）

```hocon
payload {
  tier = "root"                     # 单档互斥：出现 exec/script/ko 的键 ⇒ 拒绝
  root {
    kind    = "kernelsu"            # 白名单 token（现存枚举：KernelSU/FolkPatch/Custom）
    manager = "<package>"            # 可选：管理器包名（精确选择）；缺省 = 代码权威默认值
    argv    = "<argv 形式，≤192B>"   # 仅 custom 必填；沿用 RootProgram::kArgvCapacity
  }
}
```

- **wire 路径**：`payload.tier`、`payload.root.kind`、`payload.root.manager`、`payload.root.argv`；
- **fail-closed（绑定期）**：`kind` 不在白名单；`custom` 缺 `argv`；`argv` 含控制字节或 ≥192 B；非 `custom` 却出现 `argv`（倾向拒绝，待确认）；`manager` 出现但不是合法包名（非空、`[A-Za-z0-9_.]`、≤128 B）；`payload.root.*` 与 `tier != "root"` 同时出现；**管理器不存在/不可启动**（见 §4.1）；均**拒绝整份文档**；
- **native 唯一权威**：白名单在 native 声明（`RootProgramKind` 已在 `contract/identity.hpp`），**导出给 Kotlin**（沿用 `stage_availability` 纪律），**Kotlin 不硬编码**；
- **argv 不是命令**：argv 以既有「无 shell、无拼接」方式传递（§2 事实 4），**永不** `sh -c`。

### 4.1 两种机制对照（`kernelsu` vs `folkpatch`）与「存在性/可启动」检查

| 维度 | `kernelsu`（含各分支） | `folkpatch`（KernelPatch 越狱模式） |
|---|---|---|
| **输入** | `ksud` 程序（现状：Kotlin 入口拷到 `$GHOSTLOCK_HOME/ksud`）+ **可选** `manager` 包名 | 用户提供的 **`kernelpatch.ko`**（从 FolkPatch 管理器提取后导入，或用户自备） |
| **加载手段** | **`ksud late-load`**：`build_late_load_command` → `{ksud, "late-load", flags…}`，**无 shell、无拼接**（`lkm_image.hpp:85-92`） | **`apd insmod` 式手动重定位**：未定义符号对照 `/proc/kallsyms`、**绕过 modversions(CRC) 与 vermagic**、经 **`init_module`** 加载（E4；不经过 `ksud`） |
| **生效条件** | late-load 即时生效（无需重启） | **软重启**（仅重启 Android 框架、不重启内核）后才 Root 生效；**重启后失效**（E4） |
| **前置条件** | 现有链已满足（uid0 + 内核写能力） | **SELinux Permissive + 设备已 Root**——**正是 GhostLock 的 W1/W2 产物**（E4，本设计的天然衔接点） |
| **成熟度** | 生产路径已验证 | 官方标注**不稳定/尝鲜**：兼容性受限、失败可能影响稳定性（「请勿反复尝试」） |

**一等检查：管理器「是否存在、是否可启动」（用户第三条；P1 就做）**

| kind | 检查内容（探测手段以实现时的代码/平台事实为准） |
|---|---|
| `kernelsu` | 管理器包是否安装（如 `pm list packages` 命中 `manager` 或默认包名）+ `ksud` 是否存在且**可执行** |
| `folkpatch` | FolkPatch 管理器/模块是否存在且可用（P2 落地时定；当前一律「计划中」） |
| `custom` | 指定 `argv[0]` 路径**存在、可执行、且（若给 `sha256`）哈希匹配** |

- **两处执行**：**App 侧**（UI 明确显示「未安装 / 不可启动」，置灰或 error）→ **native 侧**在绑定/启动前**再复核一次**（fail-closed + 具名原因）；
- **权威判定在 native**：UI 检查只是**提前提示**，不构成放行依据；
- **失败语义**：不存在/不可启动 ⇒ **不静默降级、不猜替代品**；按 payload 语义「**不中断攻击链** + 本次运行结论标**未完成** + 逐项原因」（§7）；
- 该检查与上面的 fail-closed 清单**同属一节**，两者共同构成 `payload.root.*` 的准入判定。

## 5. 可用性与「计划中」

| 选择 | 状态 | 行为 |
|---|---|---|
| 不写 `payload`（默认） | **可用（现状）** | 走 `set_root_program()` → `$GHOSTLOCK_HOME/ksud`，`kind = KernelSU` |
| `kind = "kernelsu"`（**P1**） | **可用（显式化）** | 与默认**等价**（P1 需断言 late-load argv 逐字节一致）；**默认按 ksud 走**，`manager` **可选**——填了用作 package 参数，没填用代码权威默认 `me.weishu.kernelsu` |
| 任意 KernelSU 分支（**P1**） | **可用** | 分支**共享 `ksud`** ⇒ **无需机制白名单**；包名未核实的**允许手填 `manager`**（用户自己的声明，UI 提示「由你负责」）或走 `custom`；核实过的进 UI 候选列表 |
| `kind = "custom"`（**P1**） | **可用** | 用户指定程序/argv（**不猜任何包名**）；显式授权 + 执行前摘要；argv 有界、无 shell |
| `kind = "folkpatch"`（**P2**） | **计划中** | **加载 KernelPatch 模块**（E4）：独立机制设计（手动重定位 + `init_module` + **软重启**）+ 独立真机门禁；核实前解析接受但**选择门禁拒绝**、UI 置灰 |
| 所有 kind 的「存在性/可启动」 | **P1 必做** | App 预检 + **native 绑定前复核**；不存在/不可启动 ⇒ 不降级、不猜替代品（§4.1/§7） |

- 可用性矩阵**由 native 导出**（与 `stage_availability` 同规：矩阵在 native 唯一定义，Kotlin 只消费、UI 据此置灰），**不得**在 Kotlin 里写第二份。

## 6. 兼容性与回归

- **无 `payload` 段** ⇒ Kotlin 不发射任何 `payload.*`，native 走现状 ⇒ **文档与行为逐字节不变**（P1 的硬性回归判据）；
- **显式 `kernelsu`** ⇒ 与不写等价：P1 必须断言两条路径生成的 late-load argv **完全一致**；
- 43499 `root_child` 的 root script 路径不受本设计影响（payload 是接管后策略，不改 terminal 词汇）。

## 7. 失败语义

- 沿用 `contract-design.md` §3.15.4：**payload 失败绝不中断攻击链**；用户**显式请求**而未完成 ⇒ 本次运行结论标「**未完成**」+ `payload_error=<reason>`；**提权记录照记**（`uid0` / KernelSU ready）；
- root 管理器启动失败（argv 非法 / **不存在 / 不可启动** / 哈希不符 / folkpatch 的 `init_module` 被拒）即「显式请求未完成」，**不**改变控制流、**不**放松任何检查；
- **存在性检查失败的两处口径一致**：App 预检失败 → UI 显示「未安装/不可启动」（可提前阻止发起）；native 复核失败 → 具名原因 + 结论标「未完成」；**任何一侧都不得**用别的管理器顶替。

## 8. 安全边界

| 维度 | 规则 |
|---|---|
| argv | 有界（`RootProgram::kArgvCapacity = 192`），`set_argv` 截断不溢出；控制字节受检；**无 shell、无字符串拼接**（沿用 `build_late_load_command` 纪律） |
| 授权 | `custom` 的 argv 与 `folkpatch` 的模块加载都必须**显式授权**：App 在执行前摘要里显示完整 argv / 模块路径与哈希；可撤销 |
| **软重启（folkpatch）** | **独立的、需要用户显式确认的动作**——它会重启 Android 框架（不重启内核）；UI 必须单独确认并在摘要中标注；P2 真机门禁覆盖「软重启后 Root 生效」与「重启后失效」 |
| 制品 | FolkPatch 的 `kernelpatch.ko` 首选走**既有导入机制**（no-backup 不可变目录 + 本地 SHA-256 + 原子落盘）；提取自何处/是否允许用户自备见 §11 TODO-3 |
| 包名 | **绝不猜**：默认只用代码权威常量（`me.weishu.kernelsu`）或**用户显式填写的 `manager`**（UI 提示「由你负责」）；非 KernelSU 的 kind 现状空包名（`default_root_package`） |
| 不可信内容 | 用户提供的一切不可信：结果只记账，不放宽任何既有校验 |

## 9. 分批与门禁

**P1 · 可落地：`kernelsu`（含任意分支）+ `custom` + 存在性检查**

- 范围：`payload.root.{kind,manager,argv}` 的 wire 校验与绑定；`kind=kernelsu`（**默认按 ksud 走**，`manager` 可选，行为与今天等价）与 `kind=custom`（用户指定程序/argv，**不猜任何包名**）；native 把 kind/argv 传给**既有** late-load 构造（custom 同样走 argv 直传）；Kotlin 单选 + 发射（仅用户显式选择时）+ 矩阵消费；
- **存在性/可启动检查（P1 必做）**：App 预检（包是否安装、`ksud`/路径是否可执行、哈希是否匹配）+ **native 绑定前复核**（fail-closed + 具名原因），口径见 §4.1；
- **无 `payload` 段逐字节不变**；
- 门禁：`make -C src native-host-tests` + NDK 零告警 + `make -C src lint-tidy` 0 + Gradle 测试；
- **真机**（攻击关键路径门槛）：43284 全链 **无 payload 逐字节回归** + **显式 kernelsu 等价**（argv 一致）+ 43499 冷启回归；AVB 12/0；按 `device-gates/` 归档。

**P2 · 计划中（置灰）：`folkpatch`（KernelPatch 模块加载）**

- **机制**（E4）：`kernelpatch.ko`（用户提供/导入）→ 手动符号重定位（`/proc/kallsyms`）+ 绕过 modversions(CRC)/vermagic + `init_module` → **软重启**生效；**不复用** `ksud late-load`；
- **前置条件**：SELinux Permissive + 设备已 Root（**GhostLock 的 W1/W2 产物**）——是**天然衔接**，但仍需独立机制设计与**独立真机门禁**；
- **真机门禁（P2）**：正例（`init_module` 成功 + **软重启后 Root 生效**）+ 负例（非 Permissive / 模块缺失 / 哈希不符 / 内核不兼容 → 拒绝且不执行）+ 「重启后失效」记录 + AVB 12/0；
- 官方标注**不稳定/尝鲜**：文档与 UI 必须如实标注；**未落地前保持「计划中」**。

**KernelSU 分支的口径（用户答复，§3 第 1 条）**：分支**共享 `ksud`** ⇒ 属 **P1**；包名**不再要求逐项核实入白名单**——默认用代码权威值，用户可手填 `manager`（自己负责），核实过的进候选列表。

## 10. 同批同步清单

- **UML（本批已做）**：`docs/development/full-process-uml.md` §3.1（C++ Class：`RootProgramKind`/`RootProgram` 与 payload 段的关系）与 §1 IPO（默认档 → root 管理器启动分支），提交信息写明更新了哪张图；
- **contract-design（本批已做）**：§3.15 增 `payload.root.*` 小节（占位，标「设计，待用户确认」）；
- **branch-plan（本批已做）**：记 P1/P2 拆分与门禁要求；
- **AGENTS**：`payload` owner 已在白名单（上一批），本设计不新增 owner，无需改；
- **本轮（v2）新增同步**：UML §1 增 **C4d**（管理器存在性/可启动检查，UI 预检 + native 复核）、C6d 更新为「folkpatch = KernelPatch 模块加载（P2）+ 软重启确认」；`contract-design.md` §3.15.8 同步 `manager` 可选与存在性检查；`branch-plan` 同步 P1/P2 拆分；
- 实现落地前：若形态变化，先改本节与 §3.15 再动代码。

## 11. 开放问题（本轮已消解 3 条，剩 1 条待用户确认）

- ~~**TODO-1（分支清单）**~~ **已消解（用户原话）**：「KernelSU 及其分支是个统称，因为他们都**共享 ksud**……**默认就是 ksud，无论哪个分支提供了**」⇒ 分支**不需要机制白名单**，包名**可选**（`manager`）。
- ~~**TODO-2（FolkPatch 机制）**~~ **已消解（E4 官方文档）**：越狱模式 = 提取内置 `kernelpatch.ko` → adb-root 提权 → `apd insmod` 手动重定位（绕 CRC/vermagic）+ `init_module` → 软重启；属 P2，独立机制设计 + 独立真机门禁。
- **TODO-3（制品来源；仍待用户确认）**：`kernelpatch.ko` 的**来源**——**首选已定**：走**既有导入机制**（no-backup 不可变目录 + 本地 SHA-256 + 原子落盘）；**待确认**：① 是否只接受「从 FolkPatch 管理器中提取后导入」；② 是否允许用户完全自备其它来源的 `kernelpatch.ko`；③ 是否需要在 UI 里标注「来源不可验证」。
- ~~**TODO-4（`manager` 形态）**~~ **已消解**：`manager` = **可选的管理器包名**（精确选择），不是 token 白名单；缺省用代码权威默认值；用户手填即其自己的声明（UI 提示「由你负责」）。
- **TODO-5（新）**：FolkPatch 的**内核兼容矩阵**（预构建 `kernelpatch.ko` 只对特定内核版本有效）——P2 设计时必须给出「如何判定兼容 + 不兼容时如何 fail-closed」，不得靠试错。

## 12. 参考

- `docs/analysis/terminal-payload-tiers-design.md`（r2，`c335aabc`）——payload 三档与统一失败语义；
- `docs/analysis/contract-design.md` §3.15——payload owner 契约（本设计的落点）；
- 代码：`src/core/contract/identity.hpp:366-380`、`backend/cve_2026_43284/execution_binding.cpp:49-56`、`backend/cve_2026_43284/lkm/lkm_image.hpp:85-92`、`backend/cve_2026_43284/lkm/lkm_image.cpp:346-348`、`backend/cve_2026_43284/schema.hpp:68-77`；
- 外部：FolkPatch 仓库 <https://github.com/LyraVoid/FolkPatch>（README 原文 + `app/build.gradle.kts` 的 `applicationId`/`namespace`，见 §2.1 E1/E2）；**FolkPatch 越狱模式官方文档 <https://fp.mysqil.com/guide/jailbreak/>**（E4：KernelPatch 模块 / `apd insmod` / `/proc/kallsyms` / modversions+vermagic / `init_module` / 软重启 / Permissive 前置；本稿已 fetch 全文核对）；KernelSU-Next / SukiSU-Ultra / ReSukiSU 的 Gradle 路径核实尝试**均 404**（§2.1 E3，记录「为什么不能猜」）；
- 模式先例：`stage_availability`（`src/core/plugin/schema.hpp` + `plugin/probe.cpp`）、插件导入（no-backup + SHA-256 + 原子落盘）。
