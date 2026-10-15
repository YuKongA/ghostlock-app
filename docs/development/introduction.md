# 从代码可辨识的事实（introduction）

> 本文件由 **R9（文档三层拆分）** 设立：把"读代码即可确认的事实"从 `AGENTS.md` 移到这里，
> 使 `AGENTS.md` 只留**流程、纪律、判据**。判据与沿革见 `docs/plan/B0.0-REVISION.md` R9。
> 本文只写**可从代码读出**的内容；每条都可就地核验（给出路径）。

## 一、三层结构

| 层 | 位置 | 产物 | 说明 |
| --- | --- | --- | --- |
| Android | `app/` | APK | Kotlin/Compose；HOCON profile 解析/合并；Shizuku UserService；日志 |
| Native | `core/src/` | `build/native/ghostlock` | C++23 攻击 runtime；`core/Makefile` |
| Extractor | `tools/extract_rs/` | CLI | Rust；boot.img/OTA → `--format conf` / `--format json` |

- **Native 不是 JNI**：`libghostlock.so` 是**可执行 ELF**，由 Kotlin `ProcessBuilder` 启动；
  入口为 stdin 上的**长度前缀 GLKv3 文档**（`--ghostlock-app-call`），其后可接运行时密钥会话帧。
- 内置 profile 在 `app/src/main/assets/profile/`：`index.conf`、`<uname-r>.conf`、
  `execution-*.conf`、`credential-6x.conf`、`kernelsnitch-6x.conf`；格式 HOCON（支持 `include`）。
- **Kotlin 侧为旧架构**（ADR-0013）：跨端对拍降级为参考，native 侧为准。

## 二、常用命令

```sh
# Native：Gradle 是唯一入口（CMake 构建 + CTest 测试；core/Makefile 已于 2026-10-14 删除）
./gradlew nativeTestHost       # 宿主：配置 + 构建 + 跑全部 CTest（测试/证据 + 结构性检查）
./gradlew nativeBuildHost      # 只要构建（build/host/ghostlock + 各 *_test）
./gradlew nativeBuildAndroid   # NDK 构建 -> build/native/{ghostlock,libghostlock_jni.so}
./gradlew prepareGhostlockJniLibs   # 把 libghostlock_jni.so 复制进 app 的 jniLibs
./gradlew checkAll             # 集中测试：原生 CTest + :app:testDebugUnitTest

# 直接驱动 CMake（等价，调试用）
cmake -S core -B build/cmake/host -G Ninja -DGL_TESTS=ON
cmake --build build/cmake/host -j
ctest --test-dir build/cmake/host --output-on-failure
# 设备探针（按需）：加 -DGL_PROBES=ON 才会构建 src/tools/*（futex/requeue/snitch/perf/prepare-page/heap-landing）

# Android / 提取器
./gradlew :app:assembleDebug
./gradlew :app:testDebugUnitTest
./gradlew :profile-core:exportProfiles  # 生成 GLKv3 .bin 到 build/profile/
./gradlew markdownlint                  # 文档渲染级门禁（docs/archive 豁免）
(cd tools/extract_rs && cargo test --release)
```

> **旧命令名已失效（2026-10-10 实测）**：`make -C core ghostlock`、`make -C core native-host-tests`、
> `make -C core lint-tidy` 在 `core/Makefile` 中均无对应目标。

- 生成物统一在 `build/` 下；**构建产物必须在仓库内真实 `build/`，禁止任何链接**。
- `python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock` = **可选诊断**，非门槛。

## 三、契约面（`core/src/contract/`）

| 文件 | 权威内容 |
| --- | --- |
| `kernel_access.hpp` | `KernelAccess`：`create(BackendId)` / `initialize(params)` / `read` / `write` / `read64` / `write64` / `write_zero` / `shutdown`；`AccessResult` / `AccessState` / `AccessChannel` / `AccessCarrier` / `AccessMiscParameters`（ADR-0014） |
| `kind.hpp` | `BackendId`（值为 CVE 号；`Cve_2026_43499` / `Cve_2026_43284` 等） |
| `identity.hpp` | `kCombinationCatalog`（组合权威：token → backend/route/steps/terminal/available） |
| `chain_catalog.hpp` | `ChainSpec`（`backend` / `step_set` / `terminal` / `wired`）：分派的输入行 |
| `script_surface.hpp` | 脚本面：`backend` / `writeop` / `copyop` / `settle` / `handoff` / `cfg` / `close`；`handoff = {handle, terminal, payload_ref}`（ADR-0009/0012） |
| `step_catalog.hpp` | `kStepSetAliases`（别名 → 有序 `StepId`） |
| `capability_catalog.hpp` / `resource_scope.hpp` / `layer_scope.hpp` / `op_registry.hpp` | 能力与资源作用域声明 |

## 三·补、拒绝名的约定（2026-10-11 定，设备侧定位用）

- **名字的权威在各单元自己的具名表里**，中心文档不复制清单（复制就是第二份权威）。
- 形式约定：`<名>` 或 `<名>:<细节>`。细节的三种来源固定如下，读到它们就知道卡在哪一层：
  - **errno**：`pagecache-door-unopenable:<errno>`、`step-encap-peer-failed:<errno>` —— 系统调用给的；
  - **步骤码**：`step-write-refused:<状态名>/splice=<码>` —— 正数是发送路径上的第几步（1–4 前置、10 管道、20/21/22/23 vmsplice 与切页、24 短送），负数是 `-errno`；
  - **状态机码**：`session-frame-invalid:<码>` —— 帧读取器的状态（Missing/Truncated/BadLength/…）。
- 名字要**说缺的是哪一种**：`session-frame-{path,storage}-missing` / `unreadable` / `invalid:<码>` 四者分开；
  `step-initialize-bad-params` 与 `step-initialize-landing-failed` 分开；**不把「缺路径」说成「缺设备」**。

## 四、组装与执行（`core/src/pipeline/`）

| 文件 | 职责 |
| --- | --- |
| `access_factory.cpp` | `KernelAccess::create` 的唯一实现处（依赖两个 backend 的地方只此一处） |
| `component_catalog.hpp` | `DispatchTarget` 与 `dispatch_target_of(ChainSpec)`：**分派权威** |
| `executors.hpp` | 每路径执行器：`Cve43499W1W3_RootChild`（`selinux_cred_seccomp` + `root_child`）等 |
| `composition_root.hpp` | 组合根（不可拷贝） |
| `plan_gate.hpp` | 计划闸门 |
| `run_pipeline_temp.cpp` | **临时入口（登记待删）**：逐步跑通链 |

> 注意：早前文档提到的 `pipeline/orchestrator.hpp` **不存在**；分派现在住在 `component_catalog.hpp`。

## 五、backend 面（`core/src/backend/`）

### `cve_2026_43499`（PI-futex）

| 文件 | 职责 |
| --- | --- |
| `kernel_access.hpp` | `Access`（`KernelAccess` 实现）+ `WordLanding`（`bring_up` / `tear_down` / `land_word` / `can_read` / `read_word`） |
| `access_params.hpp` | `AccessParams{route, offset, offset_set, document}` |
| `session.hpp` | `TargetState{mm_struct, page_base, fake_task, fake_lock}` |
| `landing/multicast_landing.hpp` | `MulticastLanding : WordLanding`（只接 multicast） |
| `landing/multicast_window.hpp` | `Window{write_word}` + `RouteExec{execute, arm}` + `MulticastWindow` |
| `landing/multicast_exec.hpp` | `MulticastExec`（彩票循环：stamp → setsockopt） |
| `landing/multicast_consumer.hpp` | `MulticastConsumerThread`（`sched_setattr` 第三位 actor） |
| `route/multicast_policy.hpp` | `MulticastLimits` / `MulticastConsumer` / `MulticastHold` / `run_multicast_handshake` |
| `memory/multicast_arm.hpp` | `MulticastGeometry` / `encode_multicast_arm` |
| `steps/action.hpp` | `RaceAction`（`request_for` / `arm_for` / `encode_waiter_arm`） |
| `abi.hpp` | `PayloadOffsets` / `fill_payload_offsets` / `kPayloadFields` |
| `layout_from_document.hpp` | `ImageArithmetic` / `offset_to_direct` / `layout_from_document` |

### `cve_2026_43284`（page cache）

| 文件 | 职责 |
| --- | --- |
| `kernel_access.hpp` | `Access`（43284 实现） |
| `landing/pagecache_landing.hpp` / `pagecache_write.hpp` | `PageCacheLanding` / `PageCacheWindow` |
| `landing/real_splice_io.hpp` / `real_splice_io.cpp` | `SpliceIoOps`（可替换系统调用面）+ 设备绑定 |
| `landing/pagecache_doors.hpp` / `pagecache_door_linux.*` | 门 |
| `landing/esp_crypto.hpp` | 帧加密 |
| `session_frame.{hpp,cpp}` | 会话帧 |
| `lkm/` | LKM 策略 |

## 五·补、43499 的链形状与入口约定（2026-10-11 定）

- **链**：`wire` → 值映射（键只写一处）→ 区域排布（`layout_snitch_region`）→ 发现（`run_discovery`）→
  准备（`run_prepare`，产出 `prepare_base`）→ 两个字（`steps::WordsRegion`，**调用方持有**）→
  载荷页（`memory/payload_page`，落在 `WordsRegion::payload()`）→ 窗口三步（`w1/w2/w3`）→ 路由引擎（`landing::MulticastRoute`）。
- **归属规则（反复踩过）**：凡"要活过某一段"的东西（两个字、载荷页、引擎、守卫）都归**调用方**；
  单元返回后其内部映射即失效 ✓。
- **路由按文档选**：`main` 读 `available.<id>.route`；本树只移植了 `multicast_waiter`，其余给
  **具名拒绝** `route-not-ported:<名字>`（目录/键集/链目录里三支都有预留 ✓）。
- **每个 backend 的 required_param**：`cve_2026_43284` = `["sa"]`（会话帧路径，值不进文档 ✓）；
  `cve_2026_43499` = `["value"]`（载荷值，**数据**不是密钥 ✓）；未知名一律 `required-param-unknown` ✓。
- **窗口三步的判据**在引擎里（`MulticastRoute::execute` 的三角色 + 改挂 ✓）；阶段层只报自己的名字 ✓
  （`step-words-unprepared` / `step-window-not-won` 等 ✓）。
- **配置构建**：`steps::build_route_config`（纯函数）——任一位为 0 即 `route-config-missing:<字段>` ✓，
  越界即 `route-config-too-big:<字段>` ✓；**绝不填默认值** ✓。
- **设备复核纪律**：夹具改了必须重推（旧副本会让你误判 ✓）；取证行的**位置**也是证据的一部分 ✓。

## 六、终端面（`core/src/terminal/`）

| 文件 | 职责 |
| --- | --- |
| `execution.hpp` / `.cpp` | `FinishMoment{OnSuccess, Always, OnFailure}`；`run_payload(path)`（补 x 位 + `fork`） |
| `payload.hpp` | `Payload{bytes, size, declared_sha256, declared_size}`；`PayloadVerdict` |
| `payload_store.hpp` / `.cpp` | `store_payload(dir, name, …)` → `StoreResult`（0600 原子） |
| `layer.hpp` | 终端层入口 |

## 七、profile / memory 面

| 文件 | 职责 |
| --- | --- |
| `profile/glkv3_document.hpp` | `Document` / `DocumentVerdict`（GLKv3 解析） |
| `profile/document_pairs.hpp` | `read_section_pairs(document, section, names, …)` |
| `profile/route_catalog.hpp` | `RouteId`（`MulticastWaiter` / `TcpZerocopy` / `SelectStack`） |
| `profile/key_catalog.hpp` | 键目录（`backend.cve_2026_43499.abi.*` 等） |
| `memory/target_profile.hpp` | `KeyValue` / `KeyView` |

## 八、可核验的不变量

- **全局状态**只允许 `g_exploit_session`（进程唯一）与启动期只读的 `g_direct_map_end`。
- **R1 include 防火墙**：`core/src/tests/include_firewall_test.cpp` 强制分层，白名单当前为空。
- **双侧一致性**由导出 manifest 对拍（生成命令见 `core/Makefile`）：
  `profile-manifest-v3.tsv` / `combination-manifest.tsv` / `vocabulary-manifest.tsv` /
  `stepset-steps.tsv` / `lkm-kmi-manifest.tsv`。
- **wire**：GLKv3 MessagePack，根 map 必填 `schema == 3`，canonical = 最短整数 + 键按 UTF-8 字节序；
  `v2` 已弃用，旧 bin 一律拒绝。
