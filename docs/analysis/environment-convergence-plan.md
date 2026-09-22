# 环境变量收敛与 Profile 完备性

## 原则

- **Profile（GLK1）是唯一配置权威**：执行层（`install_profile` / `runtime_config` /
  stages / routes）不得再读任何配置类环境变量。
- 环境变量分两类：
  - **进程上下文**（路径/日志）：`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`
    —— 保留，但只用于定位文件，不表达配置。
  - **配置类**：只在 `legacy_support::legacy_profile_converter`（旧入口）里读一次，
    转换成 profile 字段；新入口（stdin GLK1 / prebuilt）完全不读。
- 若某个状态 profile 无法表达，就扩展 profile，而不是新增环境变量。

## remote/main 基线（权威）

| 变量 | remote/main 语义 | 收敛处理 |
|---|---|---|
| `GHOSTLOCK_HOME` / `TMPDIR` | 工作目录 | 进程上下文，保留 |
| `GHOSTLOCK_KSU_LOG` | 每次运行的 KernelSU 日志路径 | 进程上下文，保留 |
| `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE` | CPU 对 | legacy → `execution.recommended_*_cpu` |
| `GHOSTLOCK_TCP_ROUTE` | `0` 禁用 tcp，默认启用 | legacy → `route`（`0` → `select_stack`，否则按几何/tcp） |
| `GHOSTLOCK_DISABLE_MODULES` | safe mode | legacy → `execution_flags.safe_mode` |
| `PSELECT_DELAY_USEC` / `PSELECT_TIMEOUT_OVERRIDE_USEC` | pselect 调试覆盖 | 本分支已无，**不恢复** |

## 本分支新增（全部删除）

- `GHOSTLOCK_5X_RESIDENT` → 改为 profile 字段 `execution_flags.multicast_resident`
- `GHOSTLOCK_5X_PHASE1_PROBE` → 删除（调试）
- `GHOSTLOCK_W1_ONLY` → 删除（调试）
- `GHOSTLOCK_VERBOSE_DEBUG` → 删除（调试）

内核日志 dump 改为 CLI 参数 **`--dump-kernel-log <dir>`**（不再用任何环境变量）：

```text
ghostlock --ghostlock-app-call --dump-kernel-log <dir>
ghostlock --dump-kernel-log <dir>            # legacy 入口同样可加
```

`<dir>` 为空/缺省时不 dump；native 把目录写进 root script 的 `DEBUG_DIR`，
脚本据此落 `kernel-dmesg.log` / `kernel-info.txt` / `iomem.txt` / `pstore/`。

## Profile 扩展（GLK1 v3）

`kernel_offsets` / GLK1 布局新增 2 个字段（`flatten()` 末尾追加，`FieldCount`
86 → 88）：

| 字段 | 含义 | 来源 |
|---|---|---|
| `safe_mode` | 禁用 `/data/adb/modules` | legacy `GHOSTLOCK_DISABLE_MODULES` / App UI |
| `multicast_resident` | multicast 常驻模式 | App / 内置默认 |

GLK1 版本 `2 → 3`。旧 `.bin` 需重新生成（`exportKernelProfiles`）。legacy
`offsets.json` 不经 GLK1，由 converter 直接填内存结构，不受版本影响。

`verbose_debug`、`debug_dir` 不进入 profile（对应调试能力回滚）。

## 同步改动

- **native**
  - `profile.h`：`kernel_offsets` 加 `safe_mode` / `multicast_resident`。
  - `profile_binary.{h,cpp}`：`kVersion = 3`、`kFieldCount = 88`，parse/serialize
    读写新字段。
  - `runtime_config.{h,cpp}`：删除 `tcp_zerocopy_enabled` / `multicast_*` /
    `w1_only` / `verbose_debug` 的环境变量读取；这些状态改由 profile 提供或删除。
    仅保留 CPU（由 profile 推荐值决定）与路径类。
  - `legacy_support/legacy_profile_converter.cpp`：读 remote/main 配置类环境变量
    （CPU 对、`GHOSTLOCK_TCP_ROUTE`、`GHOSTLOCK_DISABLE_MODULES`）并写入 profile。
  - `exploit_stages.cpp` / `main.cpp`：phase1 probe 分支依赖的环境变量来源改为 profile
    （或随调试能力移除）。
- **Kotlin**
  - `NativeProfileDocument`：`flatten()/fromFields()/from()` 增加两字段。
  - `AndroidProfileConfigController.buildNativeDocument` / `Profile`：注入
    `safeMode`、`multicast_resident`。
  - `AndroidGhostlockRepository` / `GhostlockUserService`：不再传
    `GHOSTLOCK_CORE/CONSUMER_CORE/TCP_ROUTE/5X_*/W1_ONLY/VERBOSE_DEBUG/DEBUG_DIR`；
    只留路径类环境变量。
- **CI / 构建**
  - `exportKernelProfiles` 的 `fieldPaths` 追加两个字段（内置 profile 取 0）。
- **文档**
  - 更新 `docs/analysis/native-entrypoint-plan.md` 的入口描述与 README 调试说明。

## 验证

- `make -C src ghostlock` 编译零警告；`make -C src native-host-tests` 全绿。
- `profile_binary_test` / `exportKernelProfiles` 输出大小符合 v3（`12 + release + 88*8`）。
- 设备门禁：与 step 6 一起在一次设备批次里回归。
