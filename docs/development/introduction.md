# 从代码可辨识的事实（introduction）

> **2026-10-21 重写（docs-uml）**。本文原为 R9「事实层」快照（2026-10-18）；其中
> 「`core/src/{pipeline,profile,terminal}`」「`ghostlock` CLI + stdin 上的 GLKv3 文档（`--ghostlock-app-call`）」
> 「Lua / script 执行体」「Native = 可执行 ELF，由 Kotlin `ProcessBuilder` 启动」等叙述**已失效**，
> 依据：MASTER-PLAN §0.1（2026-10-17 用户裁定：Lua / GLKv3 文档 / CLI / script 面删除）、
> 2026-10-19 43284 重置、2026-10-20 43499 kt-step 生产化。原文整篇归档在
> [20261018-2322-introduction.md](../archive/20261018-2322-introduction.md)，不删除。
>
> **一句话区分（本文最重要的一条）**：生产面 = **Kotlin 编排 + JNI 库**；**没有 native CLI、没有 wire 文档、
> 没有 Lua / script 执行体**。旧文档里「native 是进程、吃文档」的描述一律按归档件读。
>
> 本文只写**当前树可核验的事实**（给出路径）。进度见 `docs/plan/MASTER-PLAN.md`（唯一进度入口）；
> 排期见 `docs/plan/ROADMAP.md`；执行层唯一权威见 `docs/plan/execution-architecture.md`。

## 零、目录结构（2026-10-21 核实）

```text
core/src/
  boundary/jni/        Java_*/JNI 导出（薄壳：DTO ↔ 值，再调 platform 或 device 层）
  boundary/protocol/   会话协议 / 句柄 / 编组 / op 表（与 JNI 无关）
  contract/            跨端契约：attack_contract_{fields,model,reader,values} + kernel_access/kind/resource_scope/step_catalog
  device/43499/        43499 设备侧编排（chain.cpp）
  platform/            KernelAccess::create（access_factory.cpp）+ route_catalog + target_profile
                       + syscalls_43499 + native_{attempt,discover,prepare}_43499
  support/             sha256 / hmac / gl_log
  backend/cve_2026_43284/   重置后只剩 interface.hpp / access_params.hpp / lib/{raw_esp,helper} / README
  backend/cve_2026_43499/   按机制分：landing/ leak/ memory/ race/ route/ steps/ + kernel_access/session/abi
  tests/               宿主测试（75 个测试源 + 3 个 manifest pin）
  tools/               构建期工具（当前为空；设备探针已随 MASTER-PLAN §0.1 删除）
```

- **已不存在的旧路径**：`core/src/jni/`、`pipeline/`、`profile/`、`terminal/`、`script/`、`device/43284/`、`core/src/main.cpp`、`core/Makefile`。
  文档、Gradle 任务与 include 防火墙均已改到上表路径。

## 一、三层结构与产物

| 层 | 位置 | 产物 | 说明 |
| --- | --- | --- | --- |
| Android | `app/` | APK | Kotlin/Compose；**唯一生产编排层**（kt-step）；HOCON profile 解析/合并；Shizuku UserService；日志 |
| Native | `core/src/` + `core/CMakeLists.txt` | 静态核心库 + **JNI 动态库** | C++23；**没有可执行体**（CLI 已删） |
| Extractor | `tools/extract_rs/` | `libextract.so` / CLI | Rust；boot.img/OTA → `--format conf` / `--format json` |

CMake 目标（`core/CMakeLists.txt`）：

| 目标 | 形态 | 用途 |
| --- | --- | --- |
| `ghostlock_core` | STATIC | 全部纯逻辑/协议/设备编排的宿主可测核心 |
| `ghostlock_jni` | SHARED | 43284 侧 JNI：helper / raw_esp / protocol |
| `gl43499syscalls` | SHARED | **43499 生产**：逐 syscall 薄包装（K1） |
| `gl43499native` | SHARED | 43499 actor batch / attempt / discover / prepare（AVD 探针与 N3 实验用） |
| `ghostlock43499` | SHARED | 43499 legacy 逻辑面；**默认不进 APK**（K0a 隔离） |

- `libghostlock43284.so` 由 `prepareGhostlockJniLibs` 从 `build/native/` 就位；`core/CMakeLists.txt` 无同名 target
  （生成链不在本文件断言）。
- 内置 profile 在 `app/src/main/assets/profile/`：`index.conf`（`schema_version`）、`common/`、`43499/`；格式 HOCON（支持 `include`）。
  **native 不读 profile**：Kotlin 解析后以参数/字节数组过 JNI（kt-step 不变量）。
- 43284 侧同样由 Kotlin 编排阶段（`engine/pagecache/stage/`），C++ 只留纯函数（`lib/raw_esp`、`lib/helper`）。

## 二、常用命令（Gradle 是唯一构建入口）

```sh
# Native
./gradlew nativeTestHost          # 配置 + 构建 + 跑全部 CTest（含结构性检查）
./gradlew nativeBuildHost         # 只构建宿主（build/cmake/host，CTest 不跑）
./gradlew nativeBuildAndroid      # NDK 构建 -> build/native/*.so
./gradlew prepareGhostlockJniLibs # 把 JNI 库复制进 app 的 jniLibs/arm64-v8a
./gradlew verifyJniLibs           # 三库符号互不重叠
./gradlew exportJniProtocolManifest  # 导出跨端协议表到 app/build（Kotlin 侧对拍读它）

# 汇总
./gradlew checkAll                # nativeTestHost + :app:testDebugUnitTest + verifyJniLibs
./gradlew gateAll                 # checkAll + deviceGate（真机）

# Android / 提取器
./gradlew :app:assembleDebug
./gradlew :app:testDebugUnitTest
./gradlew buildGhostlockExtract   # Rust 交叉编译（tools/extract_rs）
cargo test --release              # 在 tools/extract_rs 下
```

- **已失效的命令名**：`make -C core check` / `make -C core android`（`core/Makefile` 已删）⇒ 分别对应
  `nativeTestHost` / `nativeBuildAndroid`；`./gradlew markdownlint`（仓内无此 task，grep 0 命中）；
  `./gradlew :profile-core:exportProfiles`（`:profile-core` 已禁用，`settings.gradle.kts` 有注释）。
- **技术债 T4**（MASTER-PLAN §13）：`exportJniProtocolManifest` 依赖的生成器 `build/host/jni_protocol_manifest`
  已不存在，`:app:testDebugUnitTest` 目前需 `-x` 才能跑；未偿还前全量门禁不能称绿。
- 生成物统一在仓库内真实 `build/` 下；**禁止任何链接**；跨端列表由 native 导出，不得手抄。

## 三、契约面（`core/src/contract/`）

| 文件 | 权威内容 |
| --- | --- |
| `kernel_access.hpp` | `KernelAccess`：`create(BackendId)` / `initialize` / `read` / `write` / `read64` / `write64` / `write_zero` / `shutdown`；`AccessResult` / `AccessState` / `AccessChannel` / `AccessCarrier` / `AccessMiscParameters`（ADR-0014） |
| `kind.hpp` | `BackendId`（值为 CVE 号：`Cve_2026_43499` / `Cve_2026_43284` …） |
| `step_catalog.hpp` | `kStepSetAliases`（别名 → 有序 `StepId`） |
| `resource_scope.hpp` | 能力与资源作用域声明 |
| `attack_contract_{fields,model,reader,values}` | 跨端字段表 + 生成式 DTO 的唯一来源（Kotlin 侧 `core/generated/AttackContractDtos.kt`） |

> 已删除（R9 后、随 §0.1 与后续重构）：`identity.hpp`、`chain_catalog.hpp`、`script_surface.hpp`、
> `capability_catalog.hpp`、`layer_scope.hpp`、`op_registry.hpp`。文档中不应再引用它们。

## 三·补、具名拒绝的写法

- **名字的权威在各单元自己的具名表里**，中心文档不复制清单（复制就是第二份权威）。
- 形式：`<名>` 或 `<名>:<细节>`；细节优先给 **errno / 原始计数 / 阶段码**，不给形容词。
- 现行例子（各单元具名）：`precondition:<token>-<input>`、`precondition:<token>-no-calls`、
  `victim-start:<errno|类型>`、`victim-uid:<uid>`、`probe:<errno>`、`page-send:<stage>`。
- **不说成「缺设备」**：缺路径 / 缺权限 / 参数不合法 / 内核不可读 要分开报。

## 四、组装与执行（`core/src/platform/`）

| 文件 | 职责 |
| --- | --- |
| `access_factory.cpp` | `KernelAccess::create` 的唯一实现处（依赖两个 backend 的地方只此一处） |
| `route_catalog.hpp` | `RouteId` 登记（`MulticastWaiter` / `SelectStack` / `TcpZerocopy`） |
| `target_profile.hpp` | `KeyValue` / `KeyView`（值视图） |
| `syscalls_43499.{hpp,cpp}` | 逐 syscall 薄包装（无状态、无决策） |
| `native_attempt_43499.{hpp,cpp}` | actor batch 的 native 侧（参数编组、page 发送、结果编码） |
| `native_discover_43499.{hpp,cpp}` | discover 端口（Kotlin 给参数、解释结果） |
| `native_prepare_43499.{hpp,cpp}` | prepare 端口（`WordsRegion` handle、shaping fd） |

> 旧 `pipeline/`（`component_catalog` / `executors` / `composition_root` / `plan_gate` / `run_pipeline_temp`）
> **整体不存在**；分派与装配现由 Kotlin 执行层 + `access_factory.cpp` 承担。

## 五、backend 面（`core/src/backend/`）

### `cve_2026_43499`（PI-futex）

| 目录/文件 | 关键单元 |
| --- | --- |
| `kernel_access.hpp` / `session.hpp` | `Access`（`KernelAccess` 实现）+ `TargetState` |
| `access_params.hpp` / `abi.{hpp,cpp}` | `AccessParams` / `PayloadOffsets` |
| `access_{multicast_waiter,select_stack,tcp_zerocopy}.hpp` | 三条 route 的访问面 |
| `landing/` | `MulticastLanding` / `MulticastWindow` / `MulticastRoute`（`_linux`）/ `SelectStackRoute`（`_linux`） |
| `leak/` | kernelsnitch discover：`snitch_*` / `collision_*` / `leak_*` / `candidate_match` |
| `memory/` | 页/凭据/布局：`payload_page`、`credential_image`、`write_layout`、`write_encoder`、`prepare_*`、`page_*`、`multicast_arm` |
| `race/` | `PiRace` / `PiWindow` / `RaceRuntime` |
| `route/` | `lifecycle` / `route_policy` / `policy_dispatch` / `route_status` / `expected_keys` / `multicast_policy` + `*_legacy.cpp` |
| `steps/` | `step_runner` / `window_*` / `prepare_*` / `discovery_runner` / `words_region` / `route_config` / `access_binder` |

### `cve_2026_43284`（page cache，2026-10-19 重置后）

| 文件 | 说明 |
| --- | --- |
| `interface.hpp` | `AccessParamsPod`（唯一 POD 定义） |
| `access_params.hpp` | 旧参数面（按 v4 §2 计划在 B5 删除；当前仍在树） |
| `lib/raw_esp.{hpp,cpp}` | `gl_esp_open/write_block/close` 薄 syscall |
| `lib/helper.{hpp,cpp}` | `gl_helper_exec/read`、`gl_fadvise_drop` |
| `README.md` | 本目录边界说明 |

> 旧 43284 实现（会话/帧/门/ESP 链/LKM 策略）已在 2026-10-19 删除；新设计与批次见
> `docs/analysis/43284-refactor-design-v4.md` 与 `docs/plan/43284-reset-batch.md`。

## 六、JNI 边界面（`core/src/boundary/jni/`）

| 文件 | 面向 | 生产? |
| --- | --- | --- |
| `native_syscalls_43499.cpp` | `gl43499syscalls`：逐 syscall | **是**（kt-step K1） |
| `native_attempt_jni.cpp` / `native_discover_jni.cpp` / `native_prepare_jni.cpp` | `gl43499native`：actor batch / discover / prepare | 实验（N2/N3），未过 N5 不标 supported |
| `native_select_stack_jni.cpp` / `native_select_legacy_jni.cpp` / `native_tcp_route_jni.cpp` | 43499 select / tcp 路由端口 | select 有 AVD 门禁；tcp 无通过结果 |
| `api_43499.cpp` / `native_43499.cpp` | 43499 legacy 逻辑面 | 否（K0a 已隔离） |
| `native_helper.cpp` / `native_raw_esp.cpp` | 43284 helper / ESP（`ghostlock_jni`） | 是（43284 阶段链） |
| `native_finish.cpp` / `native_memfd.cpp` / `native_perf_task_jni.cpp` | 收尾 / memfd / perf 辅助 | 按调用方 |

- **没有通用 read/write JNI**：读写经 Kotlin 会话与能力接口（见 §七）；执行节点直接走 backend 端口。

## 七、Kotlin 执行面（生产）

| 位置 | 角色 |
| --- | --- |
| `core/engine/PipelineEngine.kt` | **执行唯一权威**：节点顺序 / 重试 / 冻结 / 清理 / 反向 close |
| `core/engine/BackendRun.kt` | 每次执行的边界（一次性）+ `DelegatingExecutionEngine` + `ExecutionEngineFactory` |
| `core/engine/ExecutionEngine.kt` | UI 面状态机接口（`ExecutionState`） |
| `core/NativeSession.kt` + `NativeSessionFactory.kt` | 会话生命周期底座（`SessionPhase` / `CleanupDisposition` / `close`） |
| `core/access/AccessCapability.kt` | `AccessSession` + `KernelMemoryAccess` / `PageCacheAccess` 能力接口 |
| `core/access/{LkmKernelAccess,PageCacheAccess}.kt` | LKM 与 43284 页缓存能力实现 |
| `core/steps/{StepId,StepInputs,StepPlans,OpsInvoker}.kt` | 步骤数据与调用边界（**不是**执行权威） |
| `engine/ktstep/` | 43499 kt-step：布局/引擎/端口/pirace/victim |
| `backend/cve43499/ktstep/run/` | 43499 生产 run/flow/session/route 端口 |
| `backend/cve43284/pagecache/` + `engine/pagecache/` | 43284 Stage 节点与阶段实现 |

- 结构、状态机与「会话 close 与执行 close 互斥」的规则见 `docs/plan/execution-architecture.md`（唯一权威）。
- `StepEngine` / `StageRunner` / `StagePageCacheEngine` 已删除；`core/steps` 只留数据与翻译器。

## 八、可核验的不变量

- **攻击状态零可写全局**：43499/43284 的 `llvm-nm` `b/B` = 0，会话由调用方持有；剩余 `d` 为 `.data.rel.ro` 常量，libc 的 `fini_array_with_sentinels` 除外。
  例外（2026-10-22）：日志子系统 4 个 DSO-local `d` 见 [ADR-0020](../analysis/adr/0020-gl-log-exception.md)（Proposed）；长期改 session API（T-LOG）。
- **R1 include 防火墙**：`core/src/tests/include_firewall_test.cpp`，白名单当前为空；越层 include 与 stale 条目皆 FAIL。
- **跨端对拍**：`core/src/tests/{combination,payload,surface}-manifest.pin.tsv`；生成/校验入口是
  `exportJniProtocolManifest` 与 Kotlin 侧 `NativeProtocolTest`（T4 债务见 §二）。
- **步骤数据无虚表/无继承**：`StepSpec`/`StepPlan` 是数据；backend 差异用目录表与端口，不用子类。
- **PI 窗口内**：零分配、零打印、零间接分派、零 JNI 回调；`sched_setattr` 只在窗口外调用。
- **owner 在 Kotlin**：fd / 映射 / actor 线程 / child 进程的创建与释放都在 Kotlin；C++ 不缓存 fd/指针。
  沿革（2026-10-22）：actor 线程创建方按 [JNI 计划](../plan/43499-43284-jni-rewrite-plan.md) §4.3 第 2 层（待批 T-PI2）；批准前仍按本行。
- **构建产物**在仓库内真实 `build/`；禁止链接；疑似旧树用 `realpath` 核实后清理，不得并存两棵树。
