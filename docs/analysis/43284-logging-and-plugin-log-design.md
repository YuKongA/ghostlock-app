# 43284 全链日志 + 插件内置日志接口（设计稿 r1，待评审）

> **状态**：设计草案（L 级：攻击路径日志 + 跨端 ABI 能力位）。**未写实现代码**。
> 依据：用户需求「43284 日志写的太少，参考上游项目，并展示比上游项目更多的日志，并且插件接口也应该提供内置日志接口」；
> 上游事实由 Lead 核实（`V4bel/dirtyfrag` 的 `exp.c`：**1952 行 / 63 处 `SLOG`**，以错误路径 + 粗粒度进度为主）。
> 关联：`docs/analysis/plugin-runtime-integration-design.md`（P1/step 3a）、`docs/analysis/contract-design.md` §3.14.7、AGENTS「核心攻击代码审查」「CLI 只 7 项」。

## 0. 目标与非目标

**目标**
- A：43284 生产链（app-call → Pipeline）在**每一段**都有结构化日志，条数与信息量**超过上游 63 处**，并且失败路径**每条都有具名原因 + 上下文**；
- B：把插件日志从「ABI 里碰巧存在」升级为**一等能力**：可声明（能力位）、可检测（`host_caps`）、有质量保证（前缀/级别/截断/限速/记账/路由）。

**非目标**
- 不改 43499 链的日志（本批只做 43284；43499 已有多处 pr_*，另批再统一）；
- 不新增 CLI 开关（AGENTS：CLI 只 7 项，选择与策略不得进 CLI）；
- 不引入日志框架/第三方依赖；不落盘密钥；不做结构化文件输出（仍走 stdout/stderr，App 的 debug log 自然捕获）；
- 不改 `run_state` 的 step 词表（它是 App 粗粒度状态 + ACK 协议，不是日志）。

## A. 43284 全链日志

### A.1 现状（已核实）
- 43284 全部 `.cpp` 的 `pr_*` 计数 = **0**（`chain.cpp`/`pagecache.cpp`/`lkm_image.cpp`/`lkm_policy.cpp`/`lkm_window.cpp`/`real_ops.cpp`/`session_frame.cpp`/`shellcode.cpp`/`splice_io.cpp` 均为 0）；
- 现有输出只有三类：① `profile_resolved …` / `default_used=…`（`execution_binding`，stderr）；② `lkm_window opened=… calls=… unload_ok=…`（stderr）；③ 失败汇总 `cve_2026_43284 backend failed: error=%d fact_error=%d lkm_error=%d image_error=%d`（stderr，只有数字）；
- `pr_*`（`support/log.hpp`）：`pr_info`=`[*]` 黄、`pr_success`=`[+]` 绿、`pr_warning`=`[-]` 红、`pr_error`=`[!]` 红，**全部 printf 到 stdout**（非 DEBUG 构建不带 file:line）。

### A.2 载体与级别决策
| 决策 | 取值 | 理由 |
|---|---|---|
| 新增 CLI？ | **否** | AGENTS 明确 CLI 固定 7 项；verbosity 属策略，按「配置权威=profile」应进 profile 而非 CLI |
| verbosity 载体 | **本批 always-on（有界）**；若确需降噪，后续批次加 `backend.cve_2026_43284.log_verbosity`（uint，0=quiet/1=normal/2=verbose，默认 1） | 本批不想同时动 wire/manifest/Kotlin 三端；先给足日志再谈开关。**不用 `--enable-status-record` 承载**：它是 App 状态机开关，混用会改变状态语义 |
| 通道 | **阶段/成功 → stdout（`pr_*`）；诊断/错误明细 → stderr `run.43284 …`** | 与现有 `run.plugin`/`lkm_window` 一致；App 的 debug log 同时抓两路 |
| 级别映射 | `pr_success`=阶段完成、`pr_info`=阶段开始/进度、`pr_warning`=可继续的降级、`pr_error`=失败（非 DEBUG 构建语义为 log+exit，**只在真要中止时用**） | 沿用既有宏语义，不新增级别词汇 |

### A.3 分段锚点（每段：记什么 / 为何值得记 / 级别）
| # | 段 | 落点（file:line） | 记什么 | 级别 |
|---|---|---|---|---|
| L1 | 入口/事实 | `backend_terminal.cpp:93-142` | `release`、**派生 kmi**、`has_f4c50a4`、`degraded=<facts>`、`selinux_ctx`、`late_load_args` | info |
| L2 | 模块镜像 | `lkm/lkm_image.cpp:219-236,350-365` | 来源（bundled/custom）、路径、大小、**vermagic 比对结论**（`vermagic_outcome_name`/`vermagic_diff_reason_name` 已有枚举）、dirty 位、patch 槽位 | info/success |
| L3 | 选择/策略 | `lkm/lkm_policy.cpp:23-70` | `source`、`kmi`、`late_load_args` 解码结果；失败打印 `LkmPolicyError` **名字** | info/error |
| L4 | UMH 命令 | `backend_terminal.cpp:144-151`；`steps/chain.cpp` UMH 回调 | cmd 路径、`rc`、stdout/stderr 摘要（**各截断 ≤256 B**）、标记文件 | info/error |
| L5 | SA/会话帧 | `session_frame.cpp` 解析入口 | **spi 值、算法名、端口、帧长度**；**绝不打印密钥材料** | info |
| L6 | pagecache 写 | `steps/chain.cpp:161-273`（`apply_plan` 的 write/verify 循环） | **第 i/N 块**、`offset`、长度、pre-image 校验、回读校验、rollback 原因 | info/success/error |
| L7 | hook 安装 | `steps/chain.cpp:466+`、`steps/hook_patch.cpp`、`steps/elf_hook.cpp` | 目标 VA、符号、guard policy（Skip/Reject）、patch 偏移、restore 结果 | info/success |
| L8 | 触发/等待 | `real_ops.cpp`（trigger/wait_result）；`steps/chain.cpp:497-516` | 触发 pid、等待 `outcome`（`chain_wait_name` 已有）、超时毫秒 | info |
| L9 | 驻留窗口 | `lkm_window.cpp:31-36,89-134` | `opened/closed/calls/abi_version/unload_ok`（已有）+ 每次 `open` 重试次数与失败原因 | info/error |
| L10 | 插件 | `plugin/host.cpp`（step 3a 已接） | `host` 计数块（已有）+ **每 hook 的 stage/VA/rc**（B 批给 `<id>` 前缀） | info |
| L11 | SELinux | `lkm/*` 与 UMH 脚本侧 | 切换**前/后**值（permissive ↔ enforcing） | info/success |
| L12 | 收尾 | `steps/chain.cpp:275-305`（finish） | hook restore、窗口 close、release、**每阶段 PASS/FAIL 汇总一行** | success/error |

> 说明：L2/L3/L7 要用的 `*_name()` 函数**已经存在**（`vermagic_outcome_name`/`vermagic_diff_reason_name`/`chain_error_name`/`chain_wait_name`；`lkm_policy_error_name` 若缺则同批补），因此不需要新词表；禁止把整数错误码直接丢给用户（现状 `error=%d lkm_error=%d` 正是被用户吐槽的那类）。

### A.4 格式约定（新增，与既有对齐）
- 结构行：`run.43284 <phase> k1=v1 k2=v2 …`（stderr，单行 ≤ 256 B，超出截断并追加 `truncated=1`）；
- 失败行：`run.43284 <phase> fail reason=<Name> …`，`<Name>` 一律取既有枚举名函数；
- 阶段完成行：`pr_success("[43284] <phase> ok …")`（stdout，便于人读与 grep）；
- 所有地址/长度用 `0x%llx` / 十进制；**不打印密钥、不 dump 内存块**（最多 16 B 十六进制摘要用于 pre-image 校验失败）。

### A.5 硬约束
1. **密钥零输出**：SA 的 enc/auth key、会话帧密钥区一律不打印；只允许 spi/算法名/端口/帧长度/密钥**长度**；
2. **有界**：任何一行 ≤ 256 B；写循环按计划块数定界；全局新增行数上限 **≤ 120 行/run**（超出降级为汇总行并记 `log_truncated=1`）；
3. **无格式串语义**：所有设备/配置来源字符串走 `%s` 参数，绝不作为格式串；
4. **不改控制流**：日志不得引入新的失败路径。**注意 `pr_error` 在生产构建里是 `printf + exit(-1)`（`support/log.hpp:122-125`）**：凡是「记了但链继续」的失败/降级（本链多数失败都是 fail-soft，例如 `lkm_window_failed`）**必须**用 `pr_warning` + `run.43284 … fail reason=<Name>` 结构行；`pr_error` 只允许用在本来就会 `exit`/`FatalError` 的分支。

### A.6 与上游 63 处的覆盖关系（等价 / 新增）
| 上游类别（SLOG） | 上游规模 | 我们的对应 | 关系 |
|---|---|---|---|
| `unshare/gid_map/socket/SIOCGIFFLAGS/add_xfrm_sa` 失败 | 多数 | L1/L5/L3 同名阶段 + 具名 `reason=` | **等价且更结构化** |
| `do_one_write #i at off=0x… failed` | 若干 | L6 逐块 idx/offset/len/verify | **等价**，并新增**成功路径**逐块行 |
| `installed %d xfrm SAs` | 1-2 | L5 帧/spi + `sas=n` | 等价 |
| `trigger/createOrphanProcess` | 1-2 | L8 触发 pid + 等待结果 | 等价 |
| LKM 加载/vermagic | 少数 | L2 vermagic 结论 + dirty + patch 槽位 | **新增** |
| 窗口/hook/SELinux/收尾 | ~0 | L7/L9/L11/L12 | **新增** |
| 插件 | 0 | L10 | **新增** |
预计新增量：**~40-60 行/run**（正常路径）+ 失败时每失败点 1-3 行；相对上游「只在错误时打 63 处」，我们正常路径也有完整轨迹且每条带结构化键值。

### A.7 改动文件（A 批）
- `src/core/backend/cve_2026_43284/backend_terminal.cpp`（L1/L4/L12 汇总）
- `.../steps/chain.cpp`（L6/L7/L8/L12）
- `.../pagecache/pagecache.cpp`（写/校验明细）
- `.../lkm/lkm_image.cpp`、`.../lkm/lkm_policy.cpp`（L2/L3）
- `.../lkm_window.cpp`（L9 重试原因）
- `.../real_ops.cpp`（L5/L8）
- `.../session_frame.cpp`（L5，只 spi/算法/端口/长度）
- `src/core/tests/cve_2026_43284_*_test.cpp`（日志形状断言：前缀/键集合/上限/**不含密钥**）
- 可能新增 `.../log_line.hpp`（后端本地有界 `k=v` 拼装；能用 std::string 则不加文件）

## B. 插件内置日志接口（能力位 + 实现质量）

### B.1 ABI（append-only，不 bump `GLK_ABI_VERSION`）
- `glk_contract_abi.h` 新增 `GLK_CAP_LOG = 1u << 7`（当前 `KERNEL_HOOK=1<<6` 之后的下一个空位），头注释写清：
  - `level` 词表：`0=error, 1=warn, 2=info, 3=debug`（越界按 1 处理并记 `level_clamped`）；
  - `msg`：纯文本、**无格式串语义**、**上限 256 B**（超出截断并追加 `…`）；
  - **每次运行每模块上限 64 条**、**≥1 条/ms**（超限丢弃并计数）；
  - host 前缀统一为 `[countermeasure] <id> log(<level>): <msg>`（现状缺 `<id>`）；
  - `log` 指针仍是唯一机制（`glk_contract_ops` **结构不变**，无需 ABI bump）；
  - 该位是**能力声明**：`required_caps & GLK_CAP_LOG` 表示模块要求日志通道；未实现的宿主在注册期整模块拒绝（沿用 `CapsRejected`）。

### B.2 native 契约与词表（唯一权威在 native）
- `contract/countermeasure.hpp`：`Capability::Log = 1u << 7`；`kAllCapabilities` 与 `kHostImplementedCaps` 各加该位；`static_assert` 更新；`capability_token(Capability::Log) == "log"`；
- `plugin/probe.cpp` 的 `caps_list()`：**当前是硬编码 7 项列表**，必须加第 8 项（建议同批改为遍历 `kAllCapabilities`，消除第二份列表）；
- 产出变化：探针 `host_caps` 行变为 `kernel_read,kernel_write,alias,child_task,log`（顺序=枚举顺序）。

### B.3 host 实现质量（`plugin/host_ops.*` + `plugin/host.*`）
- **每插件包装表**：`PluginHost::dispatch` 为当前模块构造**栈上 glk_contract_ops 拷贝**：`log` 换成本模块 thunk，`ctx` 指向 host 维护的 `PluginLogContext{upstream ops, id, counters}`；其余 9 个函数指针为转发 thunk（`upstream->op(upstream->ctx, …)`），`child_task` 原样拷贝。
  - 为什么不由插件自报 id：宿主必须**按模块**配额/限速/归因，且不能信任插件自报；
  - 代价：9 个 3 行转发函数（无虚函数、无分配）。
- **前缀/级别**：`[countermeasure] <id> log(<level>): <msg>` → stderr（兼容升级；未知 id 时降级为 `-`）；
- **限长**：单条 ≤256 B（截断并计数）；
- **配额/限速**：每模块每 run 64 条、≥1 ms 一条（`CLOCK_MONOTONIC`），超限丢弃；
- **记账**：`HostDiagnostics` 新增 `log_calls` / `log_dropped`（additive；`run.plugin host` 行加这两个字段），丢弃不静默；
- **路由**：只进运行日志（stderr）+ 诊断块；**不进** `run_state`；失败/被拒不改变控制流（fail-soft）。

### B.4 三端顺序与需重生成的物（**产出端先行**）
1. **native**：ABI 位 + 契约 + probe + host 实现 + host 测试（含「模块要求 LOG ⇒ 可加载 / 老宿主拒绝」）→ 真机 `--plugin-probe` 重新取证；
2. **golden/语料**：`app/src/test/resources/plugin-probe-golden.tsv`（**设备 golden，必须用改了 host_caps 的 native 重新采集**，不手改）；`plugin-probe-conformance/{accept,reject}` 63 份**不需改**（输入子集仍合法），但 walker 的**参考串**要更新：`tools/extract_rs/src/plugin.rs:948`；
3. **Rust**：`plugin.rs` 的 capability token 识别 + 参考串；
4. **Kotlin**：`PluginProbe.kt` 的 `host_caps` 词表/未知 token fail-closed；`PluginProbeGoldenTest.kt:39` 精确集合断言加 `log`；UI 给新能力一个显示名（未识别 token 仍 fail-closed，不静默）。

### B.5 改动文件（B 批）
`src/core/contract/abi/glk_contract_abi.h`、`src/core/contract/countermeasure.hpp`、`src/core/plugin/probe.cpp`、`src/core/plugin/host_ops.{hpp,cpp}`、`src/core/plugin/host.{hpp,cpp}`、`src/core/tests/{countermeasure_abi_test,plugin_host_ops_test,plugin_host_test,plugin_window_wiring_test}.cpp`、`tools/extract_rs/src/plugin.rs`、`profile-core/src/main/kotlin/.../PluginProbe.kt`、`app/src/test/kotlin/.../PluginProbeGoldenTest.kt`、`app/src/test/resources/plugin-probe-golden.tsv`（设备重采）、示例插件（外部仓库：改用 `host->log` 并声明 `log`）。

## C. 批次拆分与门禁
| 批 | 内容 | 门禁 |
|---|---|---|
| **A** | 43284 全链日志（纯 backend + 测试，**无 wire/ABI 变更**） | host -B / lint / NDK 三项 0；真机四用例复跑并断言新日志行；无插件回归仍零新增（除 43284 链自身日志） |
| **B** | 插件日志能力位（ABI + 契约 + probe + host + Rust + Kotlin + golden） | native 三项 0；真机 `--plugin-probe` 重采 golden；Rust `cargo test`；Kotlin `testDebugUnitTest`；真机插件用例断言 `log_calls`/`log_dropped` 与前缀 |

## D. 真机门禁断言表（凭日志判定）
| 用例 | 判定行 |
|---|---|
| 正例 | `run.43284 … stage=ok` 汇总 + `lkm_window opened=1 … unload_ok=1` + `run.plugin host … called=1 hook_failed=0 log_calls>=1` + `[countermeasure] glk.probe log(2): …` |
| 负例 B | `run.43284 hook … rc=-1` + `run.plugin record reason=HookFailed`，链 `EXIT=0` |
| 负例 C | `run.plugin record reason=StageUnavailableOnBackend`（`loaded=0`） |
| 负例 D | `run.plugin record reason=LoadFailed status=HashMismatch`（`load_failed=1`） |
| 无插件回归 | `run.plugin` 行数 0；43284 链日志与带插件运行**逐行同形**（除插件两行） |

## E. 待 Lead 裁决
1. A 批 verbosity：**always-on（建议）** 还是同批加 `backend.cve_2026_43284.log_verbosity`（会引入 wire/manifest/Kotlin 三端改动）？
2. B 批配额取值：**64 条/模块/run、256 B/条、1 条/ms** 是否照准？
3. 插件日志是否**同时**进 App 可见 UI 日志（Kotlin 路由），还是只进 native 运行日志？
4. 示例插件是否由 `extractor-rs` 同批改用 `host->log`（跨仓库）？
5. A 批是否也要覆盖 43499 链（本稿只做 43284）。
