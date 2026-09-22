# Native 入口重构计划（legacy / app / prebuilt）

## 目标

把 native 可执行文件的入口从当前单一的 `--profile <resolved-profile.json|GLK1>`
收敛为三条明确的路径，并保证「配置读入后立刻转成 `TargetProfile`，下游只认
`TargetProfile`」。同时停止把 `--profile` 当作通用入口。

## CLI 契约

```text
ghostlock                                  → legacy：<GHOSTLOCK_HOME>/offsets.json
                                              （顶层数组，按 uname -r 匹配 release）
ghostlock --ghostlock-app-call             → App：从 stdin 读 GLK1 序列化文档
ghostlock --load-prebuilt-profile <path>   → 从文件读 GLK1 序列化文档
```

约束：

- 三种路径都先得到 `struct kernel_offsets`，再 `target_profile_snapshot()` 成
  `TargetProfile`；`run_setup_stage` 之后不得再出现路径、JSON、环境或旧字段。
- `--ghostlock-app-call` 的 GLK1 走 stdin：二进制安全、读到 EOF、上限 1 MiB、
  `read()` 处理 `EINTR`、空输入报错。
- `--load-prebuilt-profile` 读文件（复用现有 `binary_profile::parse`）。
- 其它参数组合一律 usage 退出。

## 模块划分

```text
src/core/main.cpp                              // 仅 argv 分流 + 退出码
src/core/legacy_support/legacy_profile_converter.{h,cpp}
        // 旧 JSON/offsets.json → kernel_offsets：顶层数组、release 匹配、
        // symbols/struct_fields/扁平键、旧 route/fallback 写法
src/core/legacy_support/legacy_entrypoint_starter.{h,cpp}
        // 无参数入口：uname + <GHOSTLOCK_HOME>/offsets.json → TargetProfile
src/core/profile_binary.{h,cpp}                // 保留：GLK1 ⇄ kernel_offsets
src/core/offsets_json.{h,cpp}                  // 收窄：仅供 legacy converter 复用的 JSON 原语
```

内置表**不打包进二进制**。需要内置 profile 时由 App/调用方通过
`--load-prebuilt-profile`（或 CI 产出的 `build/kernel-profiles/*.bin`）提供；
`bin` 的生成由既有 `exportKernelProfiles` Gradle 任务负责。

## 数据流

```text
argv ──┬─ 无参数 ───────── legacy_entrypoint_starter ─┐
       ├─ --ghostlock-app-call ─ stdin(GLK1) ─────────┤
       └─ --load-prebuilt-profile ─ 文件(GLK1) ───────┤
                                                     ▼
                                             TargetProfile
                                                     ▼
                              stages::run_setup_stage(profile) → W1/W2-W3/handoff
```

## 需要解耦的旧版残留

1. `run_setup_stage(const char *profile_path)`：改为接收已加载的 `TargetProfile`，
   移除「路径必填 / resolved-profile.json」文案。
2. `ops::select_offsets(profile_path)`：拆成 `load_profile`（入口层）与
   `resolve_profile_addresses()`（执行层）。
3. `offsets_json.cpp:load_resolved_profile()` 的 magic 自动分流：入口已决定格式，
   这条分流删除。
4. `runtime_config` 的双权威：`GHOSTLOCK_TCP_ROUTE`、`GHOSTLOCK_5X_RESIDENT`、
   `GHOSTLOCK_5X_PHASE1_PROBE`、`GHOSTLOCK_W1_ONLY`、`GHOSTLOCK_CORE/CONSUMER_CORE`
   与 `profile.route/execution` 重复；收敛为 profile 权威，环境变量只保留进程/路径类
   （`GHOSTLOCK_HOME`、`TMPDIR`、`GHOSTLOCK_KSU_LOG`、`GHOSTLOCK_DEBUG_DIR`、
   `GHOSTLOCK_DISABLE_MODULES`）。
5. 执行路径对 `PROFILE_VALUES->off_*/mcast_*` 的直读（`profile_macros.h`、
   `exploit_stages.cpp`、`util.cpp`、`memory/address_space.cpp`）：统一走
   `target_profile_*` 语义访问器。
6. `util.cpp` 的 `tcp_route_selected()` / `kernel5_route_selected()`：收敛到 route
   controller / profile 视图。

## Kotlin 侧

- `AndroidGhostlockRepository.runExploitBinary()` 与 `GhostlockUserService.runExploit()`：
  `ProcessBuilder(binary, "--ghostlock-app-call")`，把 GLK1 字节写入进程 stdin，
  删除 `active-profile.bin` 文件路径传递与相关 chmod。

## 实施步骤

1. 落 `legacy_support` 两个文件；把 `offsets_json.cpp` 的旧 JSON 读取能力暴露给
   converter（`fill_external_entry` 等），核心仅保留 GLK1。
2. 改 `main.cpp`：三分支分流 + stdin 读取。
3. 改 `run_setup_stage` 与 `select_offsets` 签名，执行层只吃 `TargetProfile`。
4. 删除 `load_resolved_profile` 的 magic 自动分流。
5. 收敛 `runtime_config` 双权威。
6. 执行路径收口到语义访问器。
7. Kotlin 改 stdin + flag。
8. 更新 `src/CMakeLists.txt` / `src/Makefile` 源清单与文档。

## 验证

- `make -C src ghostlock` 编译通过；`make -C src native-host-tests` 全绿。
- 无参数：放一份旧 `offsets.json` 到 `GHOSTLOCK_HOME`，确认按 uname 匹配并进入
  `run_setup_stage`。
- `--ghostlock-app-call`：`./gradlew exportKernelProfiles` 产出的某个 `.bin` 通过
  `cat x.bin | ghostlock --ghostlock-app-call` 解析为一棵 `TargetProfile`。
- `--load-prebuilt-profile build/kernel-profiles/<release>.bin` 同上。
- `./gradlew :app:assembleDebug` 通过（APK 内 `libghostlock.so` 走 stdin）。
