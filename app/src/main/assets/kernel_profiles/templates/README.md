# 模板字段总说明 / Template Field Reference

本页解释四个模板共有的每个字段；各内核族文档解释其具体默认值和差异。This page documents every shared field; family pages explain concrete defaults and differences.

| 字段 / Field | 作用 / Purpose | 影响 / Impact | 模板值理由 / Template rationale |
|---|---|---|---|
| `schema_version` | 选择解析契约。Selects the parser contract. | 不支持的值会在 Kotlin/Native 边界拒绝。Unsupported values are rejected. | 固定 `1`，对应当前唯一 schema。`1` is the only current schema. |
| `release` | 精确绑定 `uname -r`。Exact kernel identity. | 不一致时安全退出，防止错写内核。Mismatch aborts safely. | 使用占位符强迫适配者替换。A placeholder prevents accidental use. |
| `kernel_major` | 选择 5.x 或 6.x 验证分支。Selects validation family. | 错误值会采用错误 waiter/cred 约束。Wrong value selects invalid constraints. | 5.x 为 `5`，其余为 `6`；不能仅凭它复用地址。Family marker only. |
| `task_prio` / `task_normal_prio` | `task_struct` 调度优先级字段偏移。Scheduler-priority offsets. | 错误会破坏邻接字段并使 PI 操作失效。Bad offsets corrupt adjacent state. | 取对应 GKI 族的已验证布局起点，仍须用目标 BTF 复核。Seeded from verified family layouts; recheck BTF. |
| `task_sched_task_group` | task 调度组指针偏移。Scheduling-group pointer offset. | 影响 root task group 写入和调度稳定性。Affects group reassignment and stability. | 族内模板值仅为结构布局基线。Family baseline, not cross-build proof. |
| `task_pi_lock` | PI 锁偏移。PI-lock offset. | 错误可能死锁或立即崩溃。Can deadlock or crash. | 来自对应族 BTF/反汇编布局。Derived from family layout. |
| `task_pi_waiters` | PI waiter 树/列表头偏移。PI-waiter root offset. | 决定攻击对象定位；错误将遍历无关内存。Controls victim lookup. | 使用对应 waiter 布局的已验证基线。Verified family baseline. |
| `task_pi_top_task` | 顶层 PI task 指针偏移。Top PI-task pointer offset. | 影响 owner 链解析。Affects owner-chain traversal. | 与同一模板的 PI 字段成组，禁止混用。Must stay paired with the same layout. |
| `task_pi_blocked_on` | blocked-on 指针偏移。Blocked-on pointer offset. | 错误会误判/破坏 waiter 状态。Can misread or corrupt waiter state. | 取同一族结构布局值。Same-family structural value. |
| `task_pid` / `task_tgid` | 线程和进程 ID 偏移。PID/TGID offsets. | 用于识别 victim；错误会选错 task。Wrong values target the wrong task. | 取目标族已验证 `task_struct` 布局。Verified family layout. |
| `task_atomic_flags` | task 原子标志偏移。Atomic-flags offset. | 影响线程状态判断；误写风险高。Affects state checks and is unsafe if wrong. | 族内基线，适配时必须 BTF 复核。Family seed requiring BTF validation. |
| `task_real_cred` / `task_cred` | credential 指针偏移。Credential-pointer offsets. | W2 的核心目标；错误会导致任意指针破坏。Core W2 target; errors cause arbitrary corruption. | 来自同一目标布局，二者必须共同验证。Validate both together. |
| `task_comm` | task 名称偏移。Task-name offset. | 用于识别和日志；错误会使定位不可靠。Affects identification. | 使用族内已验证值。Verified family seed. |
| `task_tasks` | 全局 task 链节点偏移。Global task-list node offset. | 错误会导致链表遍历越界。Can derail task-list traversal. | 必须与 `init_task` 地址来自同一固件。Must match the same image as `init_task`. |
| `task_seccomp` | seccomp 状态偏移。Seccomp-state offset. | W3 依赖；错误会使绕过失败或破坏 task。W3-critical. | 取族内模板基线并用 BTF 复核。Family baseline requiring BTF. |
| `cred_copy_size` | credential 模板复制长度。Credential copy size. | 太短缺字段，太长覆盖邻接对象。Too short omits state; too long overwrites neighbors. | 5.x 为较大旧布局，6.x 为 `0x88` 已验证范围。Matches verified family layout. |
| `cred_usage_value` | 新 credential 的引用计数镜像。Credential usage image. | 错误会导致 UAF、泄漏或拒绝。Affects lifetime correctness. | 使用对应内核族的原子计数表示。Family-specific refcount representation. |
| `cred_caps_offset` | capability 区域起始偏移。Capability-region start. | 错误起点会覆盖 uid/gid 或安全指针。A wrong start overwrites unrelated credential state. | 各模板为 48 (`0x30`)，来自已验证 cred 布局；仍须 BTF/源码复核。Verified layout seed. |
| `cred_caps_count` | 连续 capability 64-bit word 数量。Number of capability words. | 太少会残留权限限制，太多会越过 cred 边界。Too few leaves restrictions; too many overruns. | 5.x 为 3、6.x 为 5，匹配各自模板范围。Family-specific verified span. |
| `cred_caps_value` | 每个 capability word 的写入位图。Capability bitmap value. | 决定授予哪些 capability；错误符号/宽度会截断。Controls granted capabilities. | 使用该族已验证的全 capability 位模式。Uses the verified all-capabilities image. |
| `cred_ref0_offset` | 第 1 个 5.x credential 引用槽偏移。First legacy reference slot. | 错误会把修复指针写入无关字段。Wrong values corrupt unrelated fields. | 5.x 模板为 128，来自已验证连续引用区。Verified legacy slot. |
| `cred_ref1_offset` | 第 2 个引用槽偏移。Second reference slot. | 同上；必须与 image 1 配对。Must pair with image 1. | 5.x 模板为 136，即下一 64-bit 槽。Next verified pointer slot. |
| `cred_ref2_offset` | 第 3 个引用槽偏移。Third reference slot. | 同上；错误会破坏 credential 生命周期。Affects credential lifetime. | 5.x 模板为 144。Verified slot. |
| `cred_ref3_offset` | 第 4 个引用槽偏移。Fourth reference slot. | 同上；受 `cred_ref_count` 边界控制。Bounded by reference count. | 5.x 模板为 152。Verified slot. |
| `cred_ref0_image` | 第 1 个引用的目标 64-bit 位模式。First reference image. | 固件相关指针，复用可能立即崩溃。Build-specific pointer; reuse can crash. | 模板值只示范有符号 JSON 表示，必须替换。Example representation only. |
| `cred_ref1_image` | 第 2 个引用位模式。Second reference image. | 必须与 offset 1 和同一固件匹配。Must match slot and firmware. | 无通用默认；模板值必须重测。No portable default. |
| `cred_ref2_image` | 第 3 个引用位模式。Third reference image. | 错误会产生无效引用关系。Can create invalid references. | 无通用默认；模板值必须重测。No portable default. |
| `cred_ref3_image` | 第 4 个引用位模式。Fourth reference image. | 错误会破坏释放/引用路径。Can corrupt release paths. | 无通用默认；模板值必须重测。No portable default. |
| `kernelsnitch_collisions` | 目标 hash bucket 碰撞数量。Required hash collisions. | 更高值增加准备成本和热量；过低可能无法成形。Affects heap shaping and heat. | 5.x 取 8，6.x 取 4，来自现有稳定路线。Preserves stable family behavior. |
| `pselect_waiter_shift` | select 栈内 waiter 相对位移。Relative waiter displacement. | 直接决定写窗口位置；错误通常只会 miss，也可能写错栈位置。Directly shifts the target window. | 5.x/6.6 `-2`、6.1 `1`、6.12 `0` 是已验证族基线，必须反汇编复核。Verified seeds requiring disassembly. |
| `off_init_task` | `init_task` 相对内核基址地址。Kernel-relative symbol offset. | task 遍历根；错误会使全部地址推导失效。Root of task traversal. | 模板为 `0`，明确要求逐固件提取。Zero means unresolved. |
| `off_init_cred` | `init_cred` 相对地址。Relative `init_cred`. | W2 credential 来源。W2 credential source. | `0` 防止跨固件复用。Zero prevents reuse. |
| `off_root_task_group` | root 调度组相对地址。Relative root task-group address. | 影响调度组恢复/提升。Affects scheduling-group handling. | 每个固件重新提取，模板为 `0`。Per-build extraction required. |
| `off_selinux_enforcing` | enforcing 状态相对地址。Relative enforcing-state address. | W1 写入目标；错误是高风险任意写。W1 target; wrong value is dangerous. | 必须逐固件提取，模板为 `0`。Always per-build. |
| `off_selinux_blob_sizes` | SELinux blob 布局元数据地址。SELinux blob-size metadata. | 影响 security blob 解释。Affects security-blob parsing. | 固件相关，模板为 `0`。Build-specific. |
| `off_security_hook_heads` | LSM hook 表地址。LSM hook-head address. | 用于安全状态处理；错误会指向任意链表。Wrong values target arbitrary lists. | 固件相关，模板为 `0`。Build-specific. |
| `off_slide_nfulnl_logger` | 用于 KASLR slide 的 logger 地址。Logger symbol used for slide. | 错误会系统性平移全部运行地址。A wrong slide poisons every address. | 必须从同一镜像提取，模板为 `0`。Extract from the same image. |
| `off_slide_loggers_0_1` | logger slot 参考地址。Logger-slot slide reference. | 与上一字段共同校准 slide。Calibrates KASLR slide. | 反汇编推导，模板为 `0`。Disassembly-derived. |
| `off_slide_boot_id` | boot-id 数据参考地址。Boot-ID slide reference. | 作为地址泄露/校准候选；错值使验证失败。Used for address calibration. | 符号随固件变化，模板为 `0`。Per-build symbol. |
| `execution` | 对 [公共默认值](../defaults.md) 的可选覆盖。Optional override of shared defaults. | 改变尝试次数、等待、热量和成功率。Changes timing, heat, and reliability. | 模板复制兼容默认值以便审查；无设备证据不要修改。Copied for review; change only with evidence. |

所有数字偏移以字节计；`off_*` 是相对内核基址，`task_*`/`cred_*` 是结构内偏移。All structural offsets are bytes; `off_*` values are kernel-base-relative.
