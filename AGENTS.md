# AGENTS.md — 流程 · 纪律 · 判据

> 项目正重构中，以用户要求为准

## 1. 最高思想与质疑义务（2026-10-07 用户裁定 · 强制）

`docs/development/软件工程守则.md` **是本项目的最高思想**，效力高于本文件及一切文档、计划、规范与个人偏好。

- **层级**：守则 > 本文件与其它文档（只能作**经验条目**；不得冲突/放宽/绕过/并列；冲突以守则为准，被冲突条目**待修订**）。
- **当责**：任何人（用户/开发者/Lead/子智能体）的要求、设计、决策或流程改动，若 ① 违反守则；② 破坏结构约束（阶段门、单变量、一处读取、单一权威、临时物零容忍、真机判据、产物身份核验）；③ 打断开发—设计—规划流程（越阶段门、无判据即继续、多线并行、改判据过门禁、以旁路替代设计）⇒ **当场质疑、拒绝执行、并教育提出者**（引条文 + 物证 + 冲突点与代价 + 合规替代）。**不得沉默服从、阳奉阴违、先做了再说。**
- **唯一例外**：用户**明确知情强制**越权 ⇒ 可执行，但**同批留沿革记录**（越过哪条/谁确认/代价）并登记技术债与偿还时机。**未经强制确认一律拒绝。**
- **反向义务**：合规要求必须高效执行，不得以守则为由拖延/扩范围/拒绝正常需求。**记录义务**：每次质疑/拒绝/教育/强制确认留一行可检索记录（时间·条文·结论）。

## 2. 改动流程：先设计，后改动（强制）

先读 `docs/development/design-philosophy.md`；规范见 `engineering-standards.md`。

1. **Explore**：读代码与文档；`git log --all -- <path>` 查历史设计与门禁证据。
2. **Design**：S 直接改；M 写清动机/影响文件/行为差异/验证计划；**L**（攻击关键路径、wire/profile、跨端契约、公共数据结构、新 route）**先出计划文档并获用户认可再写码**。
3. **Implement**：最小改动，只碰设计列出的文件；不动 `LegacyProfileConverter.kt` v1 转换与「明确保留」清单；`kernelsnitch/` 冻结（大重构可拆，须先设计与测试）。
4. **Verify**：跑满 §8 门槛留证据；**给命令与结果，不写「应该没问题」。**

- 攻击路径改动 ＝ **真机门禁 + 门禁记录**，缺一不可；`cmp_disasm` 仅可选诊断。
- 按批次：**一批一类事**，上批验过再进；**同批门禁连续失败 2 次即停，回 debug**。
- **新实验从生产路径启动**，禁止加 CLI 旗标/旁路；仅服务实验的入口同批登记清理计划（默认技术债）。
- **计划前先查活跃计划（2026-10-10 用户裁定 · 强制）**：改或建计划前先读在办计划，判定「无冲突／重复／被推翻」：重复则并入、推翻则标注取代关系，**禁两份并存**。

## 3. 代码修改流程：注释套旧码 · 新码标注 · 标准化注释（2026-10-07 用户指令 · 强制）

**不得在原有代码上直接改掉它**（例外须用户确认）：① **套住旧码**（原文保留）→ ② 标 `//TODO(reason)`（**写清为何改/何为完成**）→ ③ 写新码 → ④ **确认完成**＝该批判据全绿 + Lead/用户确认 → ⑤ **立即删除旧码注释**并核对无残留。

- **有界性**：旧码参考块＝**临时物**（守则 §6.2），**同批清零**，不得跨批/入基线；计划登记待删处。
- **标准化注释**（新增即写，触旧补注）：C++/C＝Doxygen（`@param`/`@return`/`@note`，禁 `//` 碎片）；Kotlin/Java/KTS＝KDoc；Rust＝rustdoc（`# Arguments`/`# Errors`）；HOCON/shell/Makefile＝`#`。最低内容：目的·参数·返回·失败语义·前提·阶段。
- **例外**：机械改动可直接改，但须补注释并注明「机械改动」；无法套码处用独立 `//TODO(reason)` 说明。

## 4. 代码约定（规则；枚举见 introduction.md）

- <del>C++23、**RTTI 启用**（2026-10-12 用户裁定取消 `-fno-rtti`；`dynamic_cast` 限于接口边界）、静态 libc++；`-Wall -Wextra -Wconversion -Wsign-conversion`，**新码零告警**；include 相对 `core/src`。<del>
- **禁用 `using`**（别名 · using 声明 · 其它 `using namespace`）；唯一例外 `using namespace ghostlock;`。
- **全局状态**只允许 `g_exploit_session` 与启动期只读 `g_direct_map_end`；不得新增可变全局/引用别名。
- **R1 include 防火墙**（`include_firewall_test`）：**白名单必须为空**；越层 include 与 stale 条目皆 FAIL；豁免须登记 owner 批次。
- **双侧一致性**：改一侧同批改另一侧；manifest 清单与生成命令见 introduction.md §八。
- **构建逻辑一律 Gradle KTS**；`.sh` 仅限设备端与运维；不得依赖 `shasum`/`sha256sum`/`mkdir -p`/`mv`/`cp`/`find`/bash（用 JVM/Gradle API）；工具链由 AGP `ndkDirectory` 解析；**跨端列表不得手抄**（由 native 导出 manifest）。
- **产物在仓库内真实 `build/`，禁止任何链接**；出问题 `./gradlew clean`；疑似旧树产物 `realpath` 核实后清理，不得并存两棵树。
- **版本号统一为 3**；唯一迁移点 `LegacyProfileConverter.kt`（v1/v2→3，其余拒绝）；extractor 只认/只产最新版。**静态策略进文档，运行时密钥/SPI/端口绝不进文档**（走会话帧，用后清零）。
- 配置权威＝GLK profile（JSON 源 + JCS wire + **人工维护的 schema**）；执行层不得读配置类环境变量（只允许 `GHOSTLOCK_HOME`/`TMPDIR`/`GHOSTLOCK_KSU_LOG`）；新状态扩展 profile。

## 5. 核心攻击代码审查（仅触及时执行）

范围：waiter/race/payload/route/exec 及其资源的准备/持有/访问/回收。

- **所有权与生命周期**：Rust 式列出每个对象/指针/资源的创建、所有者、借用者、访问区间、终结点、释放者；访问者须先于终结点停止；转移后旧所有者不得再释放或访问。
- **UAF**：逐条查；仅「作为漏洞原语被有意利用的目标」例外，**不得外扩**到辅助对象/race 状态/waiter/缓冲区/映射/同步资源。
- **终结点与顺序**：销毁/回收须在合理边界，覆盖成功/失败/重试/取消/提前返回；**PI 内存与 waiter**：停止访问 → 同步/解除关联 → 回收，先后**不得因重构改变**；缺依据不得合入。
- **记录**：标出核心攻击函数及其资源准备/存活/回收并逐条核对。

## 6. 子智能体委派与停止规则

- 可并行、自包含的工作**优先委派**；可再拆则递归拆，写范围切到互不重叠。委派须给：目标·写范围·命令与期望·验收标准；只做被委派的事，**并同样先读技能/MCP 目录、调用匹配者**。
- **复用**：用 `spawn_teammate` 建常驻 teammate；同一模块/写范围固定同一成员。同一时刻**只允许一条写入流**，未指派者只读勘察。
- **共享缓存构建串行**（镜像层 / `<repo>/build/**` 同时刻单 writer）；并行须 `-PlkmLabel=` + 独立 workdir 或排队。
- **Gradle 门禁串行**（`testDebugUnitTest`/`buildLkmImages`/`verifyLkmLedger` 共享 build 目录）。
- **编辑锚点＝语义边界**（整语句/整块，含分号与注释）；禁「关键词→右花括号」式切法。批量编辑**先校验全部锚点，任一不匹配整体不写**。
- **载荷禁反引号**（脚本生成/替换文本时）；需字面用转义或临时文件；**运行前自检，不通过整体不写**。
- **提交纪律（Lead）**：① 门禁在最终树上绿（脚本**必须检查退出码**）② 无 writer 在改被覆盖文件（否则假红 ⇒ 不得据此提交，应等停手或**隔离 worktree 按提交重跑**）。
- **「是否有 writer」看成员状态不看 mtime**（刚停手者 mtime 极新 ⇒ 假跳过）。
- **死代码**：跨进程/第三方契约成员须先查外部调用方（如 Shizuku 会调 `destroy()`）。
- **禁用而非删除用最可逆形式**（用户字面要求优先）；恢复清单到 file:line。
- **禁止改变工作树的 git 命令与等效写回**（`checkout`/`restore`/`stash`/`reset`/`clean`、`git show REV:path > path`）；只读形式：`git show <rev>:<path>`、`git diff <rev> -- <path>`、`git log`。
- **禁止分片读 + 全文写回**；确需写回须核对字节/行数并报告。真机门禁归档于 `device-gates/`；子智能体返回原始日志与退出码。

## 7. 设计与规划审查岗 design-critic（2026-10-07 用户裁定 · 强制）

- 常驻 teammate，**只读**（不写文件/不跑门禁/不碰代码）；同岗固定同一成员。
- 对象：L 级设计、ADR、批次计划、wire/profile 契约、新增组件/功能/结构提案。依据：`design-review-principles.md`；权威序 **守则 > AGENTS > engineering-standards/design-philosophy > analysis**。
- 规则：非作者评审；**冻结快照**（记 HEAD/哈希/时间；期间有 writer 记「不覆盖」并复跑）；输出＝`编号|严重度|位置|问题|必须怎么改|依据` + 总判 + 单列「验证/未验证」；依据可核验（file:line）；严重度＝阻断/重要/建议，**阻断未关不得进批准 gate**；设计者**逐条回应**；强制越权须核沿革与技术债；触发＝L 级定稿前·批次计划变更·原则更新；每次留一行记录（时间·版本·总判·阻断数）。
- 分工：原则/简并＝design-critic；native＝native-core；Kotlin/配置＝kotlin-app；文档/配额＝docs-uml；冲突以原则与物证裁决。

## 8. 验证门槛

- **守卫/断言须证「能失败」**（造错 → 预期失败 → 撤回并核验无残留）。*2026-10-07：由 D1 取代，保留为可选手段。*
- 证伪/验证实验一律 **`make -B`**；实验后 **`rm -f <artifact>` + `make -B` 复核**（源码回位 ≠ 产物回位）。
- **数产物不数目录**；**失败数全量读 `build/test-results/**/*.xml`**（三桶计数以 XML 为准）。
- 普通改动：`make -C core check`（host 测试）+ `make -C core android`（NDK 零告警）。
- 攻击关键路径：① **真机门禁**（冷机、固定 CPU 对、单 route、KernelSU 未加载）② 设备 `Download/ghostlock-debug-log/<时间>/*.log.txt`（含 `profile.conf`/`profile.bin`）确认 route 命中与写验证 ③ 归档 `docs/analysis/device-gates/*.md`（单份 ≤8 KB）。
- `KERNEL-PANIC-01` 已知环境/时序：同构建可 PASS/panic/PASS；判因果须同构建复现 + 冷机复跑。**新 profile 未过真机不得标 supported。**

## 9. 文档约定

- 规范见 `docs/development/documentation-standards.md`。**结构性改动必须同批更新 UML**：`docs/development/full-process-uml.md`（IPO/状态机/Class/Sequence），提交信息写明「更新了哪张图」；一处结构只留一处权威图。
- 双语：`README.md` + `README_ZH.md`；`*_ZH.md` 保持同步。**本文件 ≤12 KB。**
- 历史文档在 `docs/archive/`（命名 `YYYYMMDD-HHMM-原名`，头部四要素）；索引 `docs/archive/INDEX-device-gates.md`；兜底 `git show <commit>:<path>`；**不得删历史**。

## 10. 排障 · 提交

- NDK：设 `ANDROID_NDK_HOME`/`ANDROID_NDK_ROOT` 或 `local.properties` 的 `ndk.dir`。Rust 交叉编译用 rustup 的 `rustc` + `rustup target add aarch64-linux-android`。
- 不提交 `build/`、`*.so`、`local.properties`；分支 `very-not-stable-dev` → `main`；提交信息 `type(scope): summary`；**未经要求不 commit/push**。

## 11. 指针表（旧 → 新，R9）

- 三层架构（结构/CLI/资产/组件模型/owner 白名单/根级标量/vr_guard/命名空间）⇒ `introduction.md` §一 §三；决策 `adr/0001`·`0003`·`0004`·`0006`。
- 常用命令 · `cmp_disasm` ⇒ `introduction.md` §二；代码约定之枚举 ⇒ §一 §八；排期 ⇒ `docs/plan/ROADMAP.md`。

## 12. 通用日志

- 级别 V/D/I/W/E，默认 I；V 行级、D 分支、I 里程碑、W 可降级、E 无回退。
- 来源：Kotlin `<>`、native `[]`、shell `()`；fd 行 `[级/毫秒] body`，logcat `file:line body`。
- 一行一次 write、ASCII、禁密钥/令牌；PI 窗口禁日志；失败不影响攻击。
- 细则见 `docs/analysis/log-levels-and-sites.md`。
