# PR note：`very-not-stable-dev` → `main`

- Head：`very-not-stable-dev`（`53184e8`）
- Base：`main`（`bddfea4`）
- 规模：236 commits、404 files、+38,513 / −6,726（`git diff --stat main...very-not-stable-dev`）

## 概览

本分支在 `main` 之上完成三件事：

1. Native 攻击链解耦（S01–S15）与 C++20/RAII 迁移（CPP00–CPP17）
2. 上游 catch-up（U01-A..H、Z）与新设备 profile
3. 配置/UI 管线（JSON profile、编辑器、建议值化）与可靠性加固

## 相对 `main` 的主要变化

| 领域 | 变更 |
|---|---|
| `src/core` | 81 files, +9,835/−3,452：C → C++20 单语言；`main.c`/`fops.c`/`util.c`/`offsets_json.c` 迁移为 `.cpp` 并按 `ops/race/stages/victim/route/support/memory/session` 分层；三个引用别名 façade 收归；resident Multicast 类化；`run_exploit` 并入 `main` |
| `app` | 75 files, +5,626/−1,333：JSON profile 解析/合并管线（Kotlin 为唯一配置源）、执行参数编辑器与推荐核心、Shizuku 双入口、每次运行独立 Native/KSU 日志 |
| `kernel_profiles` | 50 files, +4,397：48 个内置 JSON profile（取代 `src/kernels/**/offsets.h`），含新设备与 Tensor SoC |
| `docs` | 184 files, +22,343：解耦/迁移计划、路线与数据流、门禁证据链、支持设备文档（`analysis/` 已移至 `docs/analysis/`） |
| `tools` | 提取器 `yaxpeax-arm` 0.5、`opt-level="z"`；新增 `tools/cmp_disasm.py` 反汇编对比工具 |
| 构建 | Makefile 切换 `clang++` 驱动、静态 libc++；Gradle/CMake 源清单同步 |

## 与上游行为对齐（U01）

- KernelSnitch range-end 截断与 direct-map 末端测量（`.ghostlock_iomem` 缓存、`in_direct_map()` 越界拒绝）
- compact value/leaf 统一编码、arm-target 一致性校验、W1 页面字节过滤
- W3 probe 失败退休 child（不再猜测方向盲写）、每次运行独立 KernelSU 日志路径、TCP 尝试上限与可恢复失败日志
- `SLIDE_*` direct-map task alias（`U01-D`）
- Tensor/Google SoC 检测；Pixel 9 Pro / 9 Pro Fold、Honor Magic V5、NX809J/NX888J 等新设备 profile

## 删除项（相对 `main`，reviewer 注意）

- `src/kernels/**/offsets.h`（48 个）：由 `app/src/main/assets/kernel_profiles/` JSON 取代
- `src/core/{fops,main,util,offsets_json}.c`：同内容迁移为 `.cpp`（非功能删除）
- `app/src/main/kotlin/com/ghostlock/app/data/ota/*.kt`（5 个，上游 OTA extractor）：本分支的 `-s ours` ancestry 未带入。**若上游希望保留，请在合并冲突中采用上游版本或 cherry-pick 回该特性**

## 验证

- 主机：`make native-host-tests`（15 组固定向量/所有权测试）全绿；`make lint-tidy` 0 用户代码告警
- 真机：Multicast 完整证据链见 `docs/analysis/device-gates/`（CPP00–CPP17；含冷机复跑与 `KERNEL-PANIC-01` 的同构建 PASS/panic/PASS 归因）
- TCP/Select：仅主机固定测试；无对应设备，设备门禁待补
- 最新构建：APK 338 / native `e13ed9dd`（Multicast 冷机 PASS）

## 风险与未验证

- Select compact 4 次重试（`SELECT-01`）无设备验证，按主机测试与形状对比放行
- `KERNEL-PANIC-01`：三个同型实例覆盖两个构建、同构建同日 PASS→panic→PASS，判定为环境/时序而非布局因果（证据在 `docs/analysis/device-gates/`）
- OTA extractor 删除项见上，需要 reviewer 决策
