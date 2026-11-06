# 归档说明

- 原路径：`docs/plan/cmake-backend-chunks.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：superseded overlapping execution/config/rollback authority；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# CMake 整理：每个 backend 一个 chunk + 一个 include 组装层 + 一个编译开关（2026-11-03 用户裁定）

> 状态：**代码侧已交付**（2026-11-03 修订版：`include()` 组装 + OBJECT `gl_common`；交付记录见 §6）。
> 用户原话：「整理cmake，按照每个后端一个chunk，每个后端都由一个选项控制是否编译」；
> 修订要求：「cpp根下的cmake只负责include和组装common，所有子文件夹都有一个自己的cmake，common编译成.o，其余的编译成so」。
> 范围：只重组 CMake 结构/选项，不改任何 backend 源码/逻辑；不改 Kotlin/AIDL/manifest（M0 仍是 debug-only 专用路径）。

## 1 现状（问题）
- 根 `app/src/main/cpp/CMakeLists.txt` 原为**单体**：`gl_common`、`ghostlock_general`、glued 的 `add_subdirectory`、旧 43499/43284/98260 targets/victim helper/98260 helper+JNI 全部堆在一个文件；仅一个总开关 `GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS`，无 per-backend 选项。
- `gl43284/CMakeLists.txt`（旧树）是**未被根引用的独立 dfroot project**（重复入口；随目录整理已移除）。
- `app/src/main/cpp/cmake-build-debug/` 是 `.gitignore` 的旧 CMake 构建目录（source tree 内残留，已删）。
- `app/build.gradle.kts` M0 任务仍引用旧路径 `cve43284glued/...`（实际目录为 `43284glued`，M0 默认关闭故未暴露；已改为 `backends/43284glued/...`）。

## 2 目标结构（2026-11-03 修订：include 组装，不再 `add_subdirectory`）
```
app/src/main/cpp/
  CMakeLists.txt              # 只做 project/标准设置；include(common/、glgeneral/、backends/)
  common/CMakeLists.txt       # OBJECT library `gl_common`（编译成 .o；恒编译）
  glgeneral/CMakeLists.txt    # SHARED `ghostlock_general`（lib*.so；恒编译）
  backends/
    CMakeLists.txt            # backend 组装：声明 6 个 option，按 option include 各 chunk
    43499glued/CMakeLists.txt # target glued43499_exec（PIE executable，OUTPUT_NAME 43499-Glued）
    43284glued/CMakeLists.txt # targets glued43284_exec + glued43284_bootstrap（PIE executables）
    43499/CMakeLists.txt      # targets ghostlock43499(SHARED) + ghostlock_victim_helper(executable)
    43284/CMakeLists.txt      # target ghostlock43284(SHARED)
    98260/CMakeLists.txt      # 源缺失 fail-closed（ON ⇒ FATAL_ERROR；OFF 不定义 target）
```

### 2.1 include 语义（关键）
- 根 CMake 用**绝对路径 include**：`include(${CMAKE_CURRENT_LIST_DIR}/common/CMakeLists.txt)`、`.../glgeneral/...`、`.../backends/...`；根不定义任何 backend target/option。
- `include()` **不会**改变 `CMAKE_CURRENT_SOURCE_DIR`（仍指向 cpp 根），因此每个 chunk 的全部路径改用 `${CMAKE_CURRENT_LIST_DIR}` 绝对路径（源文件、include 目录、`OBJECT_DEPENDS`、`-Wa,-I` 等）。
- 每个 chunk 顶部 `include_guard(GLOBAL)`，避免重复 include；直接子文件夹（`common/`、`glgeneral/`、`backends/`、`backends/<5 backend>`）均有自己的 CMakeLists；根 `grep add_subdirectory` = 0。

### 2.2 .o / .so 边界（不改目标类型语义）
- `common`：`add_library(gl_common OBJECT gl_log.c)` ⇒ 只编译成可重定位对象（构建树中如 `gl_common.dir/common/gl_log.c.o`），由各 DSO/可执行目标链接进自身（每目标一份同名符号，等价原先 STATIC 行为，但不产 `libgl_common.a`）。
- 其余目标类型/输出名不变：`ghostlock_general` 等 DSO 仍是 `SHARED`（lib*.so）；**攻击 payload 仍是 executable ELF**（`glued43499_exec`/`glued43284_exec`/`glued43284_bootstrap`/victim helper），仅沿用 `PREFIX "lib"` + `SUFFIX ".so"` 供 AGP 打包进 `nativeLibraryDir`，**不改成 SHARED**（会改变攻击执行语义）。
- 原版 backend（`ghostlock43499` DSO + victim helper、`ghostlock43284` DSO）默认 OFF，由各自 option 打开。

## 3 选项（声明位置：`backends/CMakeLists.txt`）
| Option | 默认 | 控制 chunk | 说明 |
|---|---|---|---|
| `GHOSTLOCK_BACKEND_43499_GLUED` | ON | `backends/43499glued` | 生产可选 |
| `GHOSTLOCK_BACKEND_43284_GLUED` | ON | `backends/43284glued` | 生产可选 |
| `GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS` | OFF | （legacy 兼容别名） | 作为下两项默认值，保留旧 CLI |
| `GHOSTLOCK_BACKEND_43499` | `${GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS}` | `backends/43499` | 未完工原版 |
| `GHOSTLOCK_BACKEND_43284` | `${GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS}` | `backends/43284` | 未完工原版 |
| `GHOSTLOCK_BACKEND_98260` | OFF | `backends/98260` | 源码已手动删除；ON ⇒ 明确 FATAL_ERROR（不静默跳过） |
- 每个 chunk CMakeLists 只定义自己的 target（target 名不变，保持 APK native lib 名称不变）。
- Gradle 侧 `externalNativeBuild.cmake.arguments` 透传 6 项 `-DGHOSTLOCK_BACKEND_*=...`（gradle property 优先，否则默认值；两个原版默认继承 legacy），便于 CI/本地逐 backend 开关验证；不改变默认构建行为。

## 4 门禁（2026-11-03 修订版）
- 默认构建：`:app:assembleDebug`、`:app:assembleRelease` PASS；APK native libs 恰为 `lib43499-Glued.so`、`lib43284Glued.so`、`libbootstrap.so`、`libghostlock_general.so`（无原版/98260/victim helper）。
- 结构检查：根 `grep add_subdirectory` = 0；根 `include(` 覆盖 common/glgeneral/backends；每个直接子目录有 CMakeLists；chunk 内无 `${CMAKE_CURRENT_SOURCE_DIR}`；每 chunk `include_guard(GLOBAL)`。
- `gl_common` OBJECT 证据：构建树出现 `gl_common.dir/.../gl_log.c.o`（路径见 §6）。
- 开关矩阵（至少）：43499_GLUED=OFF（无 43499-Glued）；43284_GLUED=OFF（无 43284Glued/bootstrap）；43499=ON（出现 ghostlock43499 + victim helper）；43284=ON（出现 ghostlock43284）；98260=ON ⇒ 配置期 FATAL_ERROR，消息含 `GHOSTLOCK_BACKEND_98260` 且说明源缺失。可用 Gradle `-P...` 或直接 NDK CMake configure。
- 全量 `:app:testDebugUnitTest` EXIT=0；`git diff --check` EXIT=0。
- 设备：默认 debug APK 的 probe/B2/Shizuku/菜单回归（Lead）。
- 备份：`/tmp/ghostlock-cmake-chunks-backup-<UTC>/`（旧 add_subdirectory 初版）+ `/tmp/ghostlock-cmake-include-backup-<UTC>/`（修订中间态）+ MANIFEST.sha256；不 commit。

## 5 不做
- 不改 backend 源码与 target 名；不删除 unfinished 源码；不恢复 98260 源码。
- 不动 `app/src/main/cpp/common/gl_log.c|h`：它们是既有已交付日志标准（AGENTS §12 / `docs/analysis/log-levels-and-sites.md`）的一部分，非本批改动（见 §6 证据）。

## 6 交付记录（2026-11-03，include/OBJECT 版）

> 修订说明：初版按 `add_subdirectory` 实现；用户随后裁定「cpp根下的cmake只负责include和组装common，所有子文件夹都有一个自己的cmake，common编译成.o，其余的编译成so」，本记录为 include/OBJECT 终版。初版与修订中间态均有备份（见文末）。

- **MOVED 5 个目录 / 456 文件**（整目录移动，不删源码）：`43499glued/`→`backends/43499glued/`（123）、`43284glued/`→`backends/43284glued/`（36）、`gl43499/`→`backends/43499/`（262）、`gl43284/`→`backends/43284/`（24）、`gl98260/`→`backends/98260/`（11）；旧 `gl43284/CMakeLists.txt`（未引用的旧 dfroot 入口）备份后移除；`cmake-build-debug/`（git-ignored 生成物，`git check-ignore -v` 已证明 `.gitignore:6 cmake-build-*/`）删除；另删除 1 个批处理期间出现的 macOS `.DS_Store`（ignored 非源码）。移动文件 **452/456 字节一致**，差异 4 个均为有意：`43499glued/CMakeLists.txt`、`43284glued/CMakeLists.txt`（改写为 include 语义绝对路径）、`gl43284/CMakeLists.txt`（备份后删除）、`gl98260/README.md`（批处理期间被外部进程更新为更大版本，未回退，见风险）。
- **CMake 结构**：根 `cpp/CMakeLists.txt` 仅 `project`/标准设置 + 3 个 `include()`（common/glgeneral/backends），`grep add_subdirectory`=0、无 target/option；新增 `common/CMakeLists.txt`（OBJECT `gl_common`）、`glgeneral/CMakeLists.txt`（SHARED）、`backends/CMakeLists.txt`（6 个 option + 按 option include）；5 个 chunk 均 `include_guard(GLOBAL)` 且无 `${CMAKE_CURRENT_SOURCE_DIR}`（全部 `${CMAKE_CURRENT_LIST_DIR}` 绝对路径）。共 9 个 CMakeLists（根 + 3 组装/基础设施 + 5 chunk）。
- **.o / .so 边界**：`gl_common` 为 OBJECT；构建树证据 `app/.cxx/Debug/14485ro2/arm64-v8a/CMakeFiles/gl_common.dir/common/gl_log.c.o`（release 变体 `app/.cxx/RelWithDebInfo/5w37j4gj/arm64-v8a/CMakeFiles/gl_common.dir/common/gl_log.c.o`）。其余目标类型不变：`ghostlock_general` 与两个原版 backend 为 SHARED；两个 glued payload 与 victim helper 仍是 executable ELF（`lib*.so` 仅为 AGP 打包名，未改成 SHARED，攻击执行语义不变）。
- **Gradle**：`externalNativeBuild.cmake.arguments` 透传 6 个 `-DGHOSTLOCK_BACKEND_*`（gradle property 优先；两个原版默认继承 legacy `GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS`）；M0 的 4 处旧路径 `cve43284glued` 全改 `backends/43284glued`（`grep cve43284glued`=0），M0 build 目录名同步为 `43284glued`。
- **common 源码未动**（本批未修改）：`sha256(gl_log.c)=40ae26e3e64050dfb36c8abc8f1f75a8e63c567a993cc3cfe1873af61d21b54d`、`sha256(gl_log.h)=ba62afbb21829f345100e205d4918a3d4775cfda3a64c16bb766052256f4e777`；mtime 2026-10-29（早于本批），且与修订中间态备份逐字节一致。其相对 HEAD 的既有 diff 属已交付的日志标准批（AGENTS §12 / `docs/analysis/log-levels-and-sites.md`），非本批改动。
- **门禁（2026-11-03 本机，literal）**：
  - 默认 `./gradlew :app:assembleDebug :app:assembleRelease --no-daemon --console=plain` → **BUILD SUCCESSFUL**（native configure/build task 已执行）；debug/release APK native libs 各恰 4 个：`lib43284Glued.so`、`lib43499-Glued.so`、`libbootstrap.so`、`libghostlock_general.so`。
  - Gradle `-P` 矩阵：`GHOSTLOCK_BACKEND_43499_GLUED=OFF` → BUILD SUCCESSFUL，APK 不含 `43499-Glued`（3 libs：43284Glued/bootstrap/ghostlock_general）；`GHOSTLOCK_BACKEND_43284_GLUED=OFF` → 不含 43284Glued/bootstrap（2 libs：43499-Glued/ghostlock_general）；`GHOSTLOCK_BACKEND_43499=ON` → +`libghostlock43499.so` + `libghostlock_victim_helper.so`；`GHOSTLOCK_BACKEND_43284=ON` → +`libghostlock43284.so`；`GHOSTLOCK_BACKEND_98260=ON` → **EXIT=1**，configure FATAL：`GHOSTLOCK_BACKEND_98260=ON but native sources are missing`。
  - 直接 NDK CMake configure 矩阵亦逐项一致；两个原版 chunk 实编译通过（`libghostlock43499.so`+`libghostlock_victim_helper.so`、`libghostlock43284.so`），legacy alias ON 同时出现两个原版 target。
  - 全量 `:app:testDebugUnitTest` → **BUILD SUCCESSFUL / EXIT=0；438 tests / 0 failures / 0 errors / 0 skipped（68 XML）**。8 个测试的源码路径常量随目录迁移同步、断言语义不变；其中 `NativeHygieneContractTest` 的“原版 backend 默认关闭”契约改为核对 include 组装结构（root include → backends 组装 → option 门控 → chunk target）。未改生产 Kotlin/AIDL/manifest。
  - `git diff --check` → **EXIT=0**；结构检查：根 `add_subdirectory`=0、`include(`=3、root option/target=0；`backends/CMakeLists.txt` 6 个 option；直接子目录（common/glgeneral/backends）均有 CMakeLists；8 个组装/chunk CMakeLists 全带 `include_guard(GLOBAL)`；chunk 中 `${CMAKE_CURRENT_SOURCE_DIR}` 出现 0 次。
- 备份：初版 `/tmp/ghostlock-cmake-chunks-backup-20261103T015750Z`（475 文件，MANIFEST sha256 `4260161dac7da23f3147a56a52f8cc3a59959647a40b036598f95267d8c92f27`）；修订中间态 `/tmp/ghostlock-cmake-include-backup-20261103T020315Z`（29 文件，MANIFEST sha256 `14aed4e43f94d59a3cec37130021a83b7fca329e58162d9bd03616b8dbfce093`）；两份 MANIFEST 均 `shasum -c` 全 OK；未 commit。
- **UNVERIFIED（待 Lead 设备回归）**：默认 debug APK 的 probe/B2/Shizuku/菜单走查；native lib 打包名与 exec 路径未变但建议设备侧复核。
- 风险/遗留：① 本机 CLion 并发打开该工程并自动重配 `cpp/cmake-build-debug`（本批多次删除后又被重建；最终复核为 **ABSENT**，若之后再出现属 IDE 行为，不参与构建）；② `gl98260/README.md` 在批处理期间被外部进程更新（保持较新版本，未回退；备份保留移动前版本）；③ M0 fixture 路径已改但默认关闭，未执行 DDK/podman 构建；④ 98260 源码仍缺失（ON 明确 fail-closed）；⑤ 未 commit：目录搬迁+结构改写在 git 中体现为 D + untracked，需 Lead 门禁绿后 `git add -A` 提交。

## 7 Lead 验证（2026-11-03）

- 结构：根 `CMakeLists.txt` 仅 project/标准 + 3 个 `include()`（common/、glgeneral/、backends/）；`add_subdirectory`=0、root target=0；`backends/CMakeLists.txt` 含 6 个 option 并按 option include 5 个 chunk；9 个 CMakeLists，chunk 内 `${CMAKE_CURRENT_SOURCE_DIR}`=0。
- `common/CMakeLists.txt` 为 **OBJECT** `gl_common`；`.o` 实测存在：`app/.cxx/Debug/14485ro2/arm64-v8a/CMakeFiles/gl_common.dir/common/gl_log.c.o`（及 release 变体）。
- 默认门禁：`testDebugUnitTest` **438/0/0/0**；`assembleDebug`+`assembleRelease` PASS；debug/release APK native libs 各恰 4 个（`lib43499-Glued.so`、`lib43284Glued.so`、`libbootstrap.so`、`libghostlock_general.so`），无原版/98260/victim helper。
- 开关矩阵（agent 执行 + Lead 抽验结构）：43499_GLUED=OFF/43284_GLUED=OFF 均从 APK 移除对应产物；43499=ON/43284=ON 新增原版 DSO（+victim helper）；98260=ON 配置期 FATAL（源缺失）。
- common 源码零修改（裁决：2026-10-29 日志规范是既有交付，不随 HEAD 回退）；sha256 见 agent 报告。
- 设备 A301SO：默认 debug APK 的 `DebugProbeHostSmokeTest` **OK(2)**、B2 **OK(4)**、`ShizukuFlowHostSmokeTest` **OK(4)**；fixture `Flow43499GluedHostSmokeTest`/`Flow43284GluedHostSmokeTest` **OK(1)/OK(1)**（首次失败是 androidTest APK 未重建的旧类名；重建后 PASS，与 CMake 无关）；已恢复 debug+test APK。
- `cmake-build-debug/`（ignored，CLion 自动生成）最终不在树；未 commit（D+untracked，提交前 `git add -A`）。
