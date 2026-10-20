# ROADMAP：排期视图（R9 三层拆分之三）

> **2026-10-19 43284 重置**：旧实现已删除，重写按 `docs/plan/43284-reset-batch.md`；设计见 `docs/analysis/43284-refactor-design-v4.md`。

> **角色**：本文件只回答「先做什么、后做什么、卡在哪、过什么门」。
> **进度**的权威是 `docs/plan/MASTER-PLAN.md`（唯一进度入口）；**R 项全文与判据**见 `docs/plan/B0.0-REVISION.md`；
> **待裁定**见 `docs/plan/PENDING-DECISIONS.md` 与本文件 §4；事实基线见 `docs/development/introduction.md`；纪律与门槛见 `AGENTS.md`。

## §1 现在在哪（滚动更新）

| 面 | 状态 |
| --- | --- |
| 攻击链（43499 · multicast） | 临时入口已在真机跑通「文档→…→三步→终端具名拒绝」；`KernelAccess` 的生产装配 **未接线** |
| 攻击链（43284 · pagecache + LKM） | **已真机 PASS**（`docs/analysis/device-gates/B5-9h-20261004-full-chain-lkm-pass.md`） |
| 文档（R9） | ① `introduction.md` ✓ ② `AGENTS.md` ≤12 KB ✓ ③ 本文件 ✓ ④ 指针表＝AGENTS §11 ✓ |
| KernelAccess 实现（2026-10-10 交付） | **43284 读面补齐**（块读 16 B + 字读＝块前 8 B；`PageCacheWindow::read_block`/`PageCacheWrite`/`PageCacheLanding`/`Access::read`+`read64`）✓；**43499 路由引擎**（`landing/multicast_route.hpp`：waiter/owner/consumer 三线程按样板顺序，含 requeue→copy、ghost disarm、consumer 才判胜；平台边＝`RouteOps` 策略，Linux 实现 `landing/multicast_route_linux.hpp`）✓；**43499 载荷页**（`memory/payload_page.hpp/.cpp`：样板 `fill_fake_page_compact` 的锁/waiter/擦除元组/task，逐字段偏移皆输入）✓。**未接线**：两者尚未进 `initialize`/`prepare_page` |
| 裁定（R10） | **已裁定（2026-10-10）**：① 数据格式＝**JSON + JCS + 生成式 schema**；② wire＝**JCS JSON + 只读视图**；③ 载荷＝**stdin/base64**；④ **删除 `profile-core`**，检查归 Kotlin；⑤ **运行期由 Lua 编排，限制靠 Lua 运行时**（不新写步骤编排器）；⑥ **配置内嵌 Lua steps**；⑦ **v1/v2 统一迁入 `LegacyProfileConverter`**。结论＝[config-wire-redesign-plan.md](../archive/20261017-1835-config-wire-redesign-plan.md)（含被推翻条目的沿革）；**版本号不变，仍是 3**——前一套 v3 未发布生产，判为验证失败，本次为**同号重做** |
| 真机 PASS（2026-10-10 第三轮 · **全链 + root**） | hook 站点 `0x14012d83` @ 0xb30ac、载荷 480 B @ file 0xfe628、`phase 4 ok /dev/dfm0 after 1520 ms`；**判据三连**：`su -c id` → uid=0 context=u:r:ksu:s0、`getenforce` → Enforcing、`grep -c kernelsu /proc/modules` = 1；**`su -c ls /` 以 root 打印**。整链由新 kernel access 驱动（patch #1 → 模块 → hook → 触发）。剩：hook 计划改运行期推导、入口归 main、43499 |
| 真机 PASS（2026-10-10 第二轮 · patch #1 + 桥 + **vendor 载体**） | **helper 镜像 90/90 块写入 `/apex/com.android.runtime/bin/crash_dump64@0`；模块 769/769 块写入 `/vendor/lib64/libbinderdebug.so` 并回读校验通过**；经桥直读 vendor 页 与 `.ko` 同偏移逐字节一致。测出并修掉：① ESP 序号须按 SA 连续（per-window 重置 ⇒ anti-replay 全丢）；② block 0 的校验对 ELF 目标是假阳性（真判据 offset 0x10）。**未达成**：模块未加载（hook 链未移植，phase 4 超时） |
| 真机 PASS（2026-10-10 · 43284 新 kernel access 写面） | **769/769 块写入并逐块回读通过**（12304 B 的 `ghostlock.ko`），载体页缓存哈希 `4fe7b59a…` → `ad423012…`、头 16 B = 模块 ELF 头；判据与证据见 `docs/analysis/device-gates/43284-native-access-20261010-write-pass.md`。**由真机测出并修掉两个真实缺陷**：① CBC IV 少了 desired 一半（写成零）；② ICV 按 desired 而非页里 existing 字节计算（authenc 先验后解 ⇒ 全丢包）。**未达成**：模块未被加载（hook/patch#1/trigger 未移植）。SA 由临时 Kotlin 启动器（锁屏下 am start 即建）供给 |
| 真机实测（2026-10-10 晚 · 43284 示例工具） | 相 1 ✓（真机会话帧：spi=0x4a1f0011 icv_len=16）· 相 2 ✓（**发现并修掉一个真实缺陷**：树里只比 release 词，内核 `same_magic()` 在模块带 CRC 时只比首个空格后的特性尾——设备上二者给出相反答案；已改 `lkm/module_facts` 并加设备真值用例）· 相 3 ✓ 机制（access 起、写路径真跑、回读校验在 offset 0 判失败、载体页缓存哈希前后不变＝无误报成功）· **落盘未成**：设备 121 s 前重启、无活动 SA（只有 app 的 IpsecManager 能建），且 `/vendor/lib64/libbinderdebug.so` 对 shell 不可读（errno 13，需 crash_dump64 桥，未移植）· 目标值对照：libc++ 符号 vaddr 0xb70a8＝文件偏移 0xb30a8，其上正是 `bti c`，补丁点 0xb30ac ✓ |
| 门禁现状（2026-10-10 晚） | `make -C core check` **101 PASS**，唯一红＝`plan-check`（`main.cpp` 清空所致，随序 1/2 恢复）；`make -C core android` 绿、零告警 ✓；`./gradlew markdownlint` 见本文件末次提交 |

## §2 阶段门与硬约束（不可越）

- **2026-10-17 用户裁定（取代本表序 2 / 序 4）**：vendored `lua` / `mpack` / `nlohmann` 三个库从构建中删除，且不再引入第三方库：
  JSON 解析改为 `core/src/script/wire_doc.cpp` 内的手写 Parser，MessagePack 改为 `core/src/profile/minimpack.hpp`，
  跨端字段表与 Kotlin DTO 改手写（`core/src/contract/attack_contract_fields.hpp` + `app/.../core/generated/AttackContractDtos.kt`）；
  随之删除 Lua 启动器 `core/src/main.cpp`（`ghostlock` 可执行体）与 `script/sandbox`、`script/plan_load` 及其测试、`structural_plan_check`。
  序 2「main＝Lua 启动器」与序 4「R2 Lua↔C++ 边界」因此作废，不再排期；ADR-0008/0009/0010/0011/0015/0016/0017 同批废除（文首有横幅）。

- 门禁：**G1**（host 测试 + NDK 零告警 + `lint-tidy` + markdownlint）、**G2**（真机）、**G3**（文档/归档）。
- **动攻击路径 ⇒ 真机门禁 + 门禁记录**，缺一不可（AGENTS §2 / §8）。
- **R3 阻塞于 R10**；**R5 延期**（触发＝B0.1→B3 完成后评审）。
- 一批一类事；**同批门禁连续失败 2 次即停**，回 debug。

## §3 排期（Now / Next / Later）

| 序 | 项 | 门禁 | 动攻击路径 | 依赖 / 阻塞 |
| --- | --- | --- | --- | --- |
| 0 | R9 收尾（③ 本文件 + lint + 归档） | G1+G3 | 否 | — |
| 1 | **43499 装配接线（能力面）**：`initialize` 读文档装配（`MulticastRoute`→`MulticastWindow`→`MulticastLanding`→`Access::adopt`），并把 `build_payload_page` 接进 `prepare_page`（该页正是 `TargetState.page_base` 所指对象） | G1 | **是** | 引擎/载荷页/读面**已交付**（2026-10-10），剩接线 |
| 2 | **`main` ＝ Lua 启动器**（2026-10-10 用户裁定）：读 wire 文档 → 起 Lua（`core/Makefile` 的 `lua-core`）→ 运行**胶水程序**；胶水经 `OpRegistry` 调 `KernelAccess` 与终端 handoff | G1 | **是** | R2 + ADR-0008/0009/0010 批准 + R10（胶水来源与形态） |
| 2b | **43499 真机门禁**：`ls /` 以 root 打印（判据三连：`id` / `getenforce` / `grep -c kernelsu /proc/modules`） | **G2** | 是 | 1 + 2 |
| 3 | R1 契约中性化（参数结构各 backend 自持） | G1 | 否 | — |
| 4 | R2 Lua↔C++ 边界（`OpRegistry` 唯一接口 + `orchestrator` → `composition_root`） | G1 | 否 | — |
| 5 | R4 E4 实证版：窗口静态守卫接进 host 测试 | G1 | 否 | — |
| 6 | R6 E3 重写：编译期只锁「存在性与合法性」 | G1 | 否 | — |
| 7 | R7 生命周期回退规则（按守则原文） | G3 | 否 | — |
| 8 | R8 三端打通 | 见其计划 | 否 | B1 |
| 9 | **R10 Lua 配置结构设计**（2–3 形态 + 代价，供裁定） | G1 + 用户裁定 | 否 | **等裁定** |
| 10 | R3 无默认值（manifest `default` 作废 / accessor 默认删除） | G1（+App 迁移） | 否 | **R10** |
| 11 | R5 窗口内 `sched_setattr` 处置 | G1+G2 | **是** | 延期（触发＝B0.1→B3 完成后评审） |

## §4 待用户裁定（裁定前不得推进）

1. **R10：已裁定关闭**（2026-10-10）——数据/ wire / 载荷 / 运行期编排见上表裁定行与 [config-wire-redesign-plan.md](../archive/20261017-1835-config-wire-redesign-plan.md)；本条同时**关闭 R3**。原文（形态对比与代价）：Lua 配置结构（2–3 形态 + 代价）。**用户 2026-10-10 陈述已收窄形态**：① **全 lua 配置文件**（HOCON 退位的终局形态）；② **内置胶水层**提供 `procedure` 基类，**外部 lua 配置文件实现 `procedure`**（必需成员：`name` / `steps|run(ctx)` / `on_success|on_failure` / `terminal`+`payload_ref`；装载期校验与待裁 5–7 见桥接计划新节）。**并入本条裁定**：`steps` 还是 `run(ctx)` 为唯一形态、`procedure` 的 cfg 数据来源。
2. 「HOCON → document **只由 Lua 读取**」是否为既定决定？（会话 2026-10-09 提出；docs 内暂无同文记录）
3. `docs/analysis/profile-lua-bridge-plan.md`（未跟踪）中「待用户批准的决策」一节逐条裁定；该文件已新增「**用户裁定与 `procedure` 契约（2026-10-10 · 待批准）**」节（必需成员表 + 装载期校验 + 待裁 5–7），一并裁定。
4. **产物归属**：`KernelAccess` 的返回**不含**「root 产物」；root 后的产物由**终端**承担——是否按此写入 ADR-0014 附或新开 ADR。
6. **胶水程序（glue）的来源与信任**（由「`main`＝Lua 启动器」引出、待裁）：① 来源＝文档段（wire 携带）／`assets/` 内置／构建期生成物——R10 的「形态」需一并定；② 信任模型按 **ADR-0009**（能力硬线 + 失败即拒）；③ **不得进脚本**的东西＝运行时密钥 / SPI / 端口（走会话帧，用后清零）。
5. **pipeline 上层由 Lua 取代**（用户 2026-10-10 陈述）：范围＝`component_catalog.hpp`（分派）/ `executors.hpp`（路径执行器）/ `composition_root.hpp` / `plan_gate.hpp` / `run_pipeline_temp.cpp`（临时入口）。**与 ADR-0011（顺序权威归 Lua）· ADR-0014（上层步骤上移为 Lua 工具函数）方向一致**；**待裁的是与 ADR-0004（组合编译期固定 + 逐组合 `static_assert`）的边界**：① 哪些留 C++（内核原语面 `KernelAccess`、`OpRegistry`、include 防火墙、manifest 对拍、终端 `fork`+`payload_store`）；② 编译期绑定失去后由什么补偿（运行期门禁 / manifest 对拍）；③ 门禁如何迁移（`make -C core check`、`plan-check`、真机判据）。与 R2、R10 合并裁定。**同日已就 `main` 形态裁定：`main` 直接启动 Lua 执行胶水程序**（见 §3 序 2）；`component_catalog` / `executors` / `composition_root` / `plan_gate` 四件的迁移边界仍待裁。

## §5 完成定义（DoD）

- 攻击路径项：真机判据通过 + 按 `docs/analysis/device-gates/*.md` 归档（单份 ≤8 KB）。
- 文档项：G1+G3 绿；`AGENTS.md` ≤12 KB；**一处权威图/一处权威表**，无重复。
- 临时物：**同批清零**（AGENTS §3「有界性」）。

## §6 指针

- 进度：`docs/plan/MASTER-PLAN.md`　·　R 项全文：`docs/plan/B0.0-REVISION.md`　·　待议：`docs/plan/PENDING-DECISIONS.md`
- 事实：`docs/development/introduction.md`　·　纪律：`AGENTS.md`　·　决策：`docs/analysis/adr/`
