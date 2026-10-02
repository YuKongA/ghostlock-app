# PR note：`vr-guard-pr` → `vr-ko-bypass-dev`

- Head：`vr-guard-pr`（原生二进制构建自 C++ 树最后一次变更 `2912fca4`，其后仅 Kotlin / 测试 / 文档）
- Base：`vr-ko-bypass-dev`（`deff0b1b`）
- 规模：37 files、+1413 / −36（`git diff --stat vr-ko-bypass-dev...vr-guard-pr`；提交数见 PR 界面）

本 PR 实现 `docs/analysis/ancillary-controller-guide.md` §9 留下的任务：`VrGuardPolicy`
（vivo/iQOO `vr.ko` 探针中和），并让测试设备能真正跑完一轮以做真机验证。完整计划与证据
（含逐函数反汇编核对记录、真机门禁）在 `docs/analysis/vr-guard-plan.md`。

## 概览

1. **行为**（guide §4–§6）：`PreSpawn` 清 `__tracepoint_sys_exit.funcs`，一次写入覆盖
   本轮全部进程，含 ksud 与它派生的 shell。
2. **profile 契约**：gate（`meta.vr_guard`）+ 符号（`offset.vr_sys_exit_tp`）+ 布局
   （`vr_guard.tracepoint_funcs`，由镜像自身 BTF 推导）；native / `profile-core` / 提取器
   三侧同步。不改 wire 版本，既有 profile 字节不变（冻结 golden 未重算）。
3. **multicast route**：单次投毒 → 重复投毒 / 重复 walk。本机跑通的前提，见下节。

问题背景（#154 / #201 / #61）：临时 root 成功后，vr.ko 在 `commit_creds` 上给 uid 0 进程
打 tag，在 `__tracepoint_sys_exit` 上按 tag 杀进程——ksud 与它 fork 的 shell 连锁死亡
（管理器白屏、应用黑屏、系统不稳定直到重启）。现有的逐任务清 tag 只覆盖 exploit 子进程。
清 `funcs` 后，tracepoint 迭代器见 `funcs == NULL` 跳过全部探针；打 tag 的探针照常运行，
但无人响应。

## 主要变化

| 领域 | 文件 | 变更 |
|---|---|---|
| 行为（纯函数，host 可编译） | `src/core/session/ancillary/vr_guard.hpp` | `plan_vr_guard()`：profile → `{image_offset, width_bytes}`，缺符号或布局任一事实返回 `nullopt`（fail closed）；`enabled()` 只做 profile gate |
| 行为（设备） | `src/core/session/ancillary/vr_guard.cpp` | 运行时 `/proc/modules` 确认 vr.ko（guide §5，不按 `uname -r`）；≤5 次尝试写 `sys_exit tp->funcs`；失败只告警 |
| 原语注入 | `src/core/session/ancillary/ancillary_policy.hpp` | `AncillaryContext` 增加 `AncillaryZeroFn write_zero`（plain 函数指针，无虚分派） |
| 调用点 | `src/core/session/backend/cve_2026_43499_backend.{hpp,cpp}` | `w1()` 末尾固定 `PreSpawn` 调用点；`zero_word<M>()` 适配器绑定 session 全局（见设计点 2） |
| profile 模型 / wire | `src/core/profile/{model.h,binary.cpp}` | gate / 符号 / 布局与 multicast 调参；新字段全部落既有 padding，`sizeof`/`offsetof` 不变 |
| 读取点 | `src/core/attack/ops.cpp`、`src/core/route/multicast_waiter_route.cpp` | 调参经 `mcast_tuning()` 读取 |
| multicast route | `src/core/route/multicast_waiter_route.cpp` | 重复投毒 / 重复 walk；`attempts/arm_sequence/arm_hold` 默认 128/16/20000，profile 可覆盖，0 保持默认 |
| 提取器 | `tools/extract_rs/src/{symbols,report}.rs` | `__tracepoint_sys_exit`（optional）；`struct tracepoint.funcs` 由 BTF 取；`--format conf` 输出三项 |
| profile-core / app | `NativeProfile.kt`、`ProfileResolver.kt`、`MulticastConfig.kt`、编辑器（`AndroidProfileConfigController`/`FieldLabels`）、往返测试、内置 profile + `index.conf` | 三字段读写与白名单；multicast `attempts/arm_sequence/arm_hold` 的 Kotlin 镜像补齐（原先只存在于 native 字段表），并收录进编辑器（换路由时作为占位播种、补标签）与范围校验（8/8/16 位，越界上报而不是被类型转换绕回）；新增 iQOO 12（`6.1.145-android14-11-maybe-dirty`）内置条目 |
| 修复 1 | `profile-core/.../ProfileMerger.kt` | 合并基准改为深拷贝共享 execution 预设（见下节） |
| 修复 2 | `app/src/test/resources/remote-main-6x-offsets.json`、`ProfileMigrationEquivalenceTest.kt` | 新内置进入 remote/main 迁移夹具（实测值、嵌套 `route` 声明）；夹具尺寸断言 52 → 53 |
| 主机测试 | `src/core/tests/ancillary_test.cpp` | gate 开/关、fail-closed plan、目标算术 |
| 计划 / 证据 | `docs/analysis/vr-guard-plan.md` | 计划、反汇编核对记录、真机门禁 |

## 新内置条目附带的两项修复

新内置条目把两个既有缺陷变成可见失败，随本 PR 一并修复：

1. **导出器合并状态泄漏**（`ProfileMerger.resolveMerged`）：合并基准曾直接引用共享的
   `execution` 预设实例，而 `deepMergeValues` 原地写入 —— 首个覆盖 execution 值的内置
   （本 profile 的 `heap.prepare_max_attempts = 12`）把它写进共享预设，导出器在同一进程
   合并全部内置，其后的 profile 因此全部继承 12，直到某个 profile 显式写回 4。
   `ExporterAgreementTest` 在 9 个 profile 上复现。修复为种子前深拷贝该预设。
   该缺陷此前不可见：没有任何内置把 execution 值改成非默认。
2. **迁移夹具缺口**（`ProfileMigrationEquivalenceTest`）：该测试要求 remote/main 夹具覆盖
   每个现行 6.x 内置。remote/main 时代没有 multicast 路由，新条目以嵌套 `route` 对象声明
   （转换器对该形态原样透传），迁移解析结果与内置文档逐字节一致。

## 参考的方案与同期工作

实现依据：

- 本分支自己的设计文档 `docs/analysis/ancillary-controller-guide.md` §4–§9（behavior contract、
  stage、运行时适用性、profile 契约）与分支上的 `VrGuardPolicy` 骨架；
- 分支内既有的 **per-task 清 tag**（`cve_2026_43499_backend.cpp` 的 w2b 路径，`VR_TAG_B_OFF`，
  注释注明 ported from root.c）—— 它只覆盖 exploit 子进程；本 PR 补的是 root 之后 ksud 与
  它派生的 shell 的存活（#154/#201/#61）；
- 设备侧对 vivo `vr.ko` 的确认：`commit_creds` 上给 uid 0 打 tag、`__tracepoint_sys_exit` 上
  执行；
- 内核侧依据：tracepoint 迭代器在 `funcs == NULL` 时跳过全部探针
  （`for (func = tp->funcs; func && func->func; func++)`），因此清空是安全的全局中和；
- 参考套件 `zhubaohe123/ghostlock-kit` 在同内核线上的实测参数与流程（multicast 几何、
  重复投毒 128/16/20000、CPU 4/5、iomem 缓存、锁屏低噪声窗口）；
- 布局不按内核版本查表，而由**镜像自身 BTF** 推导（guide §5/§6 的要求）。

提交前核对了上游同期/历史同类工作（**本 PR 未使用其代码**）：

| 编号 | 作者 | 状态 | 做法 | 与本 PR 的差别 |
|---|---|---|---|---|
| #141 | abdulla-li | closed（未合并） | 旧 C 架构的 vr.ko 全局中和 | 架构已迁移；思路相同，本 PR 按新架构重做 |
| #220 / #221 | abdulla-li | #220 closed；#221 open（base 为 `main`） | 在 `main` 的 w2b 块里清 `funcs`；布局用 `target.h` 里**按版本硬编码的常量**（6.1=0x40 / 6.6=0x48），符号只加进一个内置 profile | 本 PR 在 `vr-ko-bypass-dev` 的 ancillary 架构内实现（guide 契约、`PreSpawn` 固定调用点、profile gate + fail-closed）；布局由**镜像 BTF** 推导、提取器对所有镜像产出；结构体只用既有 padding 以保持 `cmp_disasm` 冻结；并带真机门禁 PASS（见上） |
| #201 / #206 | issues | open | 需求 / 问题报告 | 本 PR 提供实现 |

## 关键设计点

1. **布局冻结**：攻击函数按偏移读写 session 成员，`cmp_disasm` 要求其机器码不变，所以新
   字段一律放进既有 padding（`kernel_offsets` 尾部 4 字节、`KernelMisc` 的 2+4 字节）。
   主机探针在基线与本分支逐项核对：`sizeof(kernel_offsets)=448`、`sizeof(TargetProfile)=712`、
   `misc@248 / geometry@288 / execution@328` 一致。
2. **LTO 常量折叠**：适配器最初把 session 参数经函数指针转发，LTO 无法再证明其恒为常量，
   `attack_write` 寄存器分配变化（126 → 130 条指令）。改为适配器内部引用 `g_exploit_session`
   后恢复 IDENTICAL (strict, 126)。
3. **不按内核版本推断**：`funcs` 偏移 6.1 为 `0x40`、6.6 因插入 `probestub` 为 `0x48`；
   写错只会静默改到 `unregfunc`。因此由镜像自身 BTF 取（guide §5/§6），同一份代码覆盖两代。
4. **失败不致命**：无 BTF / 无符号 / 无 vr.ko 的机器上行为保持关闭；写入失败只告警，不改变
   主流程返回码。

## 验证

| 项 | 命令 | 结果 |
|---|---|---|
| 主机测试 | `make -C src native-host-tests` | **25/25 PASS**；`tcp_zerocopy_route_test` 因 AArch64 `yield` 汇编在 x86 主机上不可构建，`deff0b1b` 基线上同样失败，非本 PR 引入 |
| NDK 构建 | `make -C src ghostlock`（ONDK r30.1） | 0 告警（二进制 md5 `02f2be01`） |
| lint | `make -C src lint-tidy` | rc=0，用户代码 0 findings |
| 反汇编核对 | `tools/cmp_disasm.py <baseline> build/native/ghostlock` | 见下 |
| Rust | `cargo test --release`（extract_rs） | 34/34；`--format conf` 对本机 boot.img 复核输出 `vr_sys_exit_tp=37532032`、`tracepoint_funcs=64` |
| Kotlin | `./gradlew :profile-core:test :app:testDebugUnitTest` | 全绿：**80 app 测试 + 17 profile-core**（含新增 vr.ko 往返夹具、multicast 调参键向量，及上节两处修复的回归） |
| 真机门禁 | 冷启动、锁屏、multicast、`main=4 consumer=5` | **PASS**：2026-09-30（旧二进制）与 2026-10-02（最终二进制）各一次，日志如下；归档记录 `docs/analysis/device-gates/ANC-01-20261002-multicast-direct-pass.md` |

反汇编核对（基线 `deff0b1b` 干净构建，md5 `27879fa7`；候选本分支，md5 `02f2be01`；工具为
本分支自带的 `tools/cmp_disasm.py`，另用 `origin/main` 的两级版本交叉核对）：

- `owner_thread`、`do_one_write`（3 个 middleware 实例）：**IDENTICAL (strict)**。
- `waiter_thread` / `consumer_thread` / `run_main_route_threads`：5 个未改行为的攻击函数合计
  **102 条**差异指令，逐条按 `adrp` 页基址 + 立即数解析并折算为符号+偏移后，**全部是同一
  对象成员或同一字符串字面量，0 例指向不同目标**（1 条手工解析至 `g_exploit_session+0x5d0`）。
  结构体大小与偏移未动，见设计点 1。
- `do_kernel5_fake_lock_route`：174 → 222 条，**既定行为改动**（multicast 重复投毒循环包裹
  既有 poison+walk 主体；主体内语句顺序与 prepare/recycle 顺序不变）。
- `multicast_owner_worker` / `multicast_waiter_worker`：两侧都不存在（本分支未实例化这两个
  worker，与 `kernel-phys-offset-plan.md` 的同类记录一致）。

真机门禁日志（2026-10-02，最终二进制；冷启动、锁屏未解锁、运行起步 `boot_ms≈79s`）：

```
[*] cpu pair: main=4 consumer=5
[*] === W1: SELinux === target=0xffffff802a558f40 mode=1 leaf=0
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1
[+] SELinux permissive
[*] [T+11100ms] Write 1 complete
[*] vr guard: neutralizing __tracepoint_sys_exit.funcs image=ffffffc0823cb1c0 target=ffffff802a3cb1c0 width=8
[+] vr guard: sys_exit probe disabled (attempt 1)
[*] child uid = 0
[+] child is root!
[*] handoff: root shell worker pid=10055
[*] enforce=1 (enforcing)
[+] KernelSU ready
```

设备侧根脚本日志：`late-load kmi=android14-6.1`、`late-load exit=0`、`[+] KernelSU module loaded`。

`su -c id` → `uid=0(root) context=u:r:ksu:s0`（连续 3 次 `id -u` = 0）；
`grep kernelsu /proc/modules` → `Live`；设备未 panic、SELinux 回到 Enforcing（2026-09-30
的旧二进制门禁结论一致）。

关于成功率（如实）：本次 9 次完整尝试 1 次通过，失败全部发生在 route 阶段的内核 panic
（multicast 单次未命中，属 AGENTS.md 的 `KERNEL-PANIC-01` 类环境/时序问题，不归因代码；
失败与通过同等记录在归档里）。另外观察到本机开机后 ≈60–90s 的低噪声窗口（参考套件 README
的经验）明显优于 `uptime ≥ 240s` 窗口，后者 6/6 失败。

## 为什么 multicast 提交在同一个 PR

`7bbaab08` 把 multicast route 从「每阶段一次 poison/walk」改为「重复投毒 + 重复 walk」。它
不是 guide 的任务，但没有它，本机在 W1 就是单张彩票：miss 通常直接 panic，vr.ko 行为无法
在真机上验证。改动自包含（计数来自 profile，既有 profile 不受影响），如果需要，我可以
rebase 掉它另开 PR。

## reviewer 注意（可调整项）

1. multicast 是否留在本 PR：见上节。若要拆分，我保留 vr.ko 部分、另开一个 PR 放重复投毒。
2. `AncillaryContext` 的写原语现为 `write_zero(session, target, desc)`（语义化「清零一个字」，
   避免行为引用 middleware 的 `WriteRequest`）。若倾向更通用的 `write` 形态，我可以改。
3. `vr_guard.tracepoint_funcs` 现为 `u8` 标量（0 = 未提供，与 `offset.*` 的惯例一致）。若希望
   保留 presence 语义（0 与缺失不同）需要更多字节，会超出 padding，需重新评估布局冻结方案。
4. 与主线新约定的对齐（`origin/main` 2026-09-30 起、本分支基线尚未包含）：`2d9d8016` 为
   profile/提取器引入「整域 + 显式 null」规则（缺值写 null 而不是省略），`a9518142` 新增可配
   `kernel_phys_offset`。本 PR 已把三个调参键收录进编辑器与校验（见改动表）；其余按本分支既有
   presence 语义实现（缺值不写键），合并到含该规则的主线时，把
   `vr_guard.tracepoint_funcs` 与 `route.multicast_waiter.{attempts,arm_sequence,arm_hold}`
   纳入提取器/编辑器的 field universe（缺值写显式 null）即可，属机械改动。若需要，我可以现在
   就把这部分对齐做进 PR。

## 风险与未验证

- 6.6 内核的 `funcs=0x48` 路径只有 BTF 推导，无该内核设备实测。
- BTF / 符号缺失的镜像：提取器不输出对应字段，行为保持关闭（fail closed）。
- 门禁环境注记：运行机当前未安装 KernelSU 管理器，handoff 的 ksud 取自参考套件
  `files/ksud`（`ksud 3.3.0 (uapi: 2)`，与官方管理器 32601 同版），与套件 README 的手动步骤同一
  路径；不涉及本 PR 的代码路径。另：本机 `/data/local/tmp` 开机即清空，验证时文件在 boot 后重新
  推送；`.ghostlock_iomem` 用本机实测存档播种（native 在无缓存时按内置几何继续，行为等价于
  更宽的界，缓存只会收窄它）。

## 明确保留

- 不动 `kernelsnitch/`、v1 转换路径（`LegacyProfileConverter.kt`）与保留清单。
- 不改 wire 版本；既有 profile 字节不变。
- 不修改 `tools/cmp_disasm.py`。
