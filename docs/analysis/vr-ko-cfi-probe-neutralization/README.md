# 评估：vr.ko CFI probe 中和（PR #241 / hmascs）（2026-10-02）

## 来源

- merge `acc5e7b`（`Merge pull request #241 from hmascs/vr-ko-bypass-dev`）带入 4 个文件，
  **未改任何 `src/` 生产代码**：本目录的 `PR.md`、`IMPLEMENTATION_COMPLETE.patch`、
  `PR_CHECKLIST.sh`，以及移入规范路径的 `docs/profile/EXTRACT_TRACEPOINT_CONSTANTS.md`。
- 该方案引用的 `src/core/session/backend/cfi_stage.*` / `cfi_layout.hpp` /
  `WriteMode::Value` / Kotlin `vr1` **在本树不存在**；`IMPLEMENTATION_COMPLETE.patch` 也未应用。
  因此它是**提案文档**，与本分支的实现不冲突。

## 底层原理（与本仓库威胁模型一致）

vivo `vr.ko` 在两个 tracepoint 上挂探针：

- `android_rvh_commit_creds`：对拿到 euid 0 的 task 打 tag；
- `sys_exit`：在退出路径上杀掉带 tag 的 task。

所以即使绕过 SELinux 与 seccomp，`su`/`ksud` 仍立即死。这是本仓库 `VrGuard`（PreSpawn 清
`tp->funcs`）与 `VrTaskTag`（PostSpawn 清 per-task tag）对付的同一威胁。

## 它的方案

CFI 阶段建立常驻读写通道后：

1. 读 `sys_exit` 的 `probestub`（内核 no-op），要求落在镜像内；
2. 遍历 `commit_creds.funcs[]`，筛出 **镜像外**（module 区域）的探针；
3. 按固定 delta `0x60` 在每个 `sys_exit` 槽位里匹配 `commit_creds_probe - delta`，
   命中则改写为 `probestub` 并回读校验；
4. delta 不匹配时 fallback：重定向所有 module-region 的 `sys_exit` 探针；
5. 数据-only、幂等、KCFI 友好，带镜像/direct-map 边界与回读。

## 与本仓库实现对比

| 维度 | 本仓库（`VrGuard` + `VrTaskTag`） | PR #241（probestub 重定向） |
|---|---|---|
| 时机 | PreSpawn（guard）+ PostSpawn（tag） | CFI 阶段之后（需常驻读写） |
| 粒度 | guard 清 `tp->funcs` 指针；tag 清 per-task | 只重定向 vr 的 `sys_exit` 槽位 |
| 副作用 | 清指针使 `sys_exit` 对**所有**进程/用户失效 | 仅 vr 探针，保留 perf/BPF 等消费者 |
| 依赖 | 写原语 + `/proc/modules`；无需读 | 常驻 **read** + write + delta + 镜像上下界 |
| 探针定位 | 不需定位 vr 探针 | `commit_creds` → delta → `sys_exit` |
| 常量来源 | BTF 推导 `tracepoint_funcs`（每镜像） | 硬编码 probestub/funcs/stride/delta/image，文档要求按镜像实测 |
| 覆盖 | 6.1 已过真机（ANC-01/02） | 6.6 SM8750 两台文档记录；PR 自述真机功能测试 pending |

## 结论与建议

- 方向不冲突，威胁模型一致；它更“外科手术”，我们更简单且已过真机（6.1）。
- 关键取舍是**副作用范围**：清 `funcs` 指针让 `sys_exit` 对全部进程失效（含内核自身的
  perf/BPF consumer）。对本工具“提权后短窗口”的用途可接受；若将来要长期与内核 tracepoint
  共存，probestub 重定向更稳。
- 它的常量强调“不要依赖上游头文件、必须按本机镜像实测”，与本仓库提取器用 BTF 推导一致；
  **不应**把它的硬编码常量引入 profile（我们已有的 `vr_sys_exit_tp` + `misc.vr_tracepoint_funcs`
  已足够，且是每镜像推导）。
- 采用其方案的增量成本：常驻 read（当前攻击通道未必支持）、`WriteMode::Value`、
  `commit_creds` tp、delta、镜像上下界——属**独立的 L 级改动**（设计 + `cmp_disasm` + 真机）。
- 建议：**不替换**现有 `VrGuard`/`VrTaskTag`；把“delta 匹配 + 只重定向 vr 槽位”记为备选。

## 待核验 / 风险

- 常量差异：该 PR 的 `funcs_off=0x48`（6.6、无 `static_call`）与本仓库 6.1 实测
  `tracepoint_funcs=0x40` 不同——再次印证必须按镜像推导，不能跨版本复用。
- `PR_CHECKLIST.sh` 原引用不存在的 `IMPLEMENTATION.md`，已改为 `IMPLEMENTATION_COMPLETE.patch`。

## 参考：CVE-2026-43499-Neo11Plus（IonStack 适配，2026-10-03 查阅）

来源 https://github.com/boxiaolanya2008/CVE-2026-43499-Neo11Plus —— iQOO Neo11（PD2520，
SM8750，kernel 6.6.89，Android 16），与本仓库**同一 CVE、同一 IonStack 研究**，且原生具备
`pipe_phys` + `configfs` 的**任意内核读写**，所以能实现需要读的 probestub 重定向。

它的 vr.ko 处理（`exploit/src/targets/PD2520-*/root.c`）**Option A + Option B 都做**：

- **Option B `neutralize_vr()`（全局 kill switch）**：读 `sys_exit` 的 `probestub`（要求落在
  镜像内）→ 遍历 `commit_creds.funcs[]` 找 module-region 探针 → 按 intra-module delta 在
  `sys_exit.funcs[]` 定位 → 改写为 `probestub`。
- **Option A `patch_task_vr_tag()`（per-task，belt-and-suspenders）**：**先清
  `thread_info.flags` 的 0x400 位（`VR_SYSCALL_TP_FLAG`，任务离开 sys_exit 慢路径）**，再清
  `task+VR_TAG_A_OFF`、`task+VR_TAG_B_OFF` 两个标记字节；且必须在授予 root cred **之前**。
- **关键语义（我们此前未记录）**：vr **同时**校验 tag A/B 两个标记字节，**只清一个会被判为
  篡改而直接杀**；所以要先清 0x400 让探针停跑，再同步清两个字节。
- 常量 `TRACEPOINT_PROBESTUB_OFF=0x30`、`FUNCS_OFF=0x48`、`FUNC_STRIDE=0x18`、
  `VR_COMMIT_TO_SYSEXIT_DELTA` 均为机型 `target.h`（每镜像实测），对照
  `include/linux/tracepoint-defs.h` 验证——与我们“BTF 按镜像推导”的路线一致。

与本仓库对照：

| 维度 | 本仓库 | Neo11Plus |
|---|---|---|
| 原语 | 只写（PI `rb_erase`） | 读 + 写（`pipe_phys`/`configfs`） |
| 探针中和 | `VrGuard` 清 `tp->funcs` 指针（全局） | Option B 重定向 vr 槽位（需读） |
| per-task | `VrTaskTag` 清 flags **整字** + tagB 对齐**整字** | Option A 只清 **0x400 位** + 两个标记**字节** |
| 顺序 | flags 字（含 A+0x400）→ tagB 字 | 0x400 位 → A 字节 → B 字节 |
| 校验 | 无读原语，不能回读 | 每步读回校验 |

结论（记录，不触发本次改动）：

- 独立实现印证了我们的 A+B 组合与“detag 先于提权”的顺序；我们缺读原语，
  **Option B 需先新增读能力**（独立 L 级）。
- 可核实的差异：我们清零**整个 flags 字**（会一并清掉无关 `TIF_*` 位），它只清 0x400 位
  + 两个 tag 字节。我们的顺序（先清含 A+0x400 的字，再清 tagB 字）与之等效；若将来引入
  读回或 per-target tag 偏移，建议收紧为**字节级**，以贴合 vr 的 A/B 同步校验语义。
- `VR_TAG_B_OFF`/`TASK_THREAD_INFO_FLAGS_OFF` 属本计划“明确保留”，本次不改。

## 文件索引

- `docs/profile/EXTRACT_TRACEPOINT_CONSTANTS.md` — tracepoint 常量提取指南（参考）
- 本目录 `PR.md` — 方案与设备适配说明
- 本目录 `IMPLEMENTATION_COMPLETE.patch` — 未应用的实现补丁（提案）
- 本目录 `PR_CHECKLIST.sh` — 作者的提交前检查脚本（历史）
