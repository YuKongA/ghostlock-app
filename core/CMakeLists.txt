
# core/CMakeLists.txt -- the native tree's single build entry (Makefile 迁移批次, 2026-10-14).
#
# 取代 core/Makefile（已删除；历史见 git）与旧的 CLion-only 索引模型。四类产物：
#   ghostlock            宿主/设备可执行体（src/main.cpp + 各层）
#   libghostlock_jni.so  Kotlin 加载的 JNI 边界（src/jni/**）
#   *_test               宿主测试二进制，全部经 CTest 注册（Gradle 驱动 ctest）
#
# 规则（与库无关的数字不进本文件）：几何/偏移/时序一律来自 profile，不在此定义。
#
# 单独使用：
#   cmake -S core -B build/cmake/host -DGL_TESTS=ON
#   cmake --build build/cmake/host -j
#   ctest --test-dir build/cmake/host --output-on-failure
# Android（NDK）：
#   cmake -S core -B build/cmake/android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29

cmake_minimum_required(VERSION 3.24)
project(ghostlock LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 断言是本项目的判据工具（测试里大量 assert 就是判据本身）：RelWithDebInfo/Release 会定义
# NDEBUG 把它们编译掉 ⇒ 判据静默失效（2026-10-14 复现：futex_hash_test 的断言消失、变量变"未使用"）。
# 这里统一在选项之后取消 NDEBUG，任何构建类型都保留断言。
add_compile_options(-UNDEBUG -fconstexpr-steps=8000000)

# 仓库根（core/ 的上一级）：一切产物都落在仓库内的 build/ 下，禁止外链。
get_filename_component(GL_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(GL_TESTS "构建宿主测试并经 CTest 注册" ON)
option(GL_JNI "构建 JNI 边界库" ON)

set(GL_WARNINGS -Wall -Wextra -Wconversion -Wsign-conversion)
# 2026-10-17 用户裁定：vendored mpack/lua/nlohmann 三个库全部删除，改为主仓手写：
#   MessagePack 子集 -> profile/minimpack.hpp；JSON 解析 -> script/wire_doc.cpp 内的手写 Parser。
# 因此不再有第三方库目标，也不再需要 lib/ 下的 include 目录。

# 覆盖率插桩（宿主）：clang 的 -fprofile-instr-generate -fcoverage-mapping。
option(GL_COVERAGE "宿主构建插桩覆盖率（clang profile-instr）" OFF)
if(GL_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()


# ---------------------------------------------------------------------------
# 源集合：与旧 Makefile 的四组一一对应（RUNTIME / DEVICE_EXTRA / SHARED / ANDROID_ONLY）。
# ---------------------------------------------------------------------------
set(GL_RUNTIME_SRCS
  src/support/gl_log.c
  src/platform/access_factory.cpp
  src/backend/cve_2026_43499/leak/snitch_setup.cpp
  src/support/sha256.cpp
  src/backend/cve_2026_43284/lib/helper.cpp
  src/backend/cve_2026_43499/abi.cpp
  src/support/hmac_sha256.cpp
)

set(GL_DEVICE_EXTRA_SRCS
  src/backend/cve_2026_43499/steps/prepare_mapping.cpp
  src/backend/cve_2026_43499/steps/window_steps.cpp
)

# 两侧共享的实现（测试与运行时都要）。
set(GL_SHARED_SRCS
  src/backend/cve_2026_43499/steps/route_config.cpp
  src/backend/cve_2026_43499/access_factory_43499.cpp
)

# 只有测试直接引用的实现（旧 Makefile 的逐条规则里链接的那些）。静态库只在被引用时取对象，
# 所以把它们放进宿主库不会让运行时二进制变大；反过来，少了它们对应测试就链接不过。
set(GL_TEST_SHARED_SRCS
  src/backend/cve_2026_43499/memory/credential_image.cpp
  src/backend/cve_2026_43499/memory/multicast_arm.cpp
  src/backend/cve_2026_43499/memory/prepare_plan.cpp
  src/backend/cve_2026_43499/memory/reclaim_plan.cpp
  src/backend/cve_2026_43499/memory/shaping_plan.cpp
  src/backend/cve_2026_43499/memory/write_encoder.cpp
  src/backend/cve_2026_43499/memory/write_layout.cpp
)

# 仅设备需要：受害者进程与 43499 的准备/窗口/路由。
set(GL_ANDROID_ONLY_SRCS
  src/backend/cve_2026_43499/steps/route_config.cpp
  src/backend/cve_2026_43499/steps/words_region.cpp
  src/backend/cve_2026_43499/steps/prepare_runner.cpp
  src/backend/cve_2026_43499/steps/discovery_runner.cpp
  src/backend/cve_2026_43499/steps/window_runner.cpp
  src/backend/cve_2026_43499/steps/window_attempt.cpp
  src/backend/cve_2026_43499/memory/credential_image.cpp
  src/backend/cve_2026_43499/memory/multicast_arm.cpp
  src/backend/cve_2026_43499/memory/payload_page.cpp
  src/backend/cve_2026_43499/landing/multicast_route_linux.cpp
  src/backend/cve_2026_43499/memory/prepare_plan.cpp
  src/backend/cve_2026_43499/memory/reclaim_plan.cpp
  src/backend/cve_2026_43499/memory/shaping_plan.cpp
  src/backend/cve_2026_43499/memory/write_encoder.cpp
  src/backend/cve_2026_43499/memory/write_layout.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  list(APPEND GL_DEVICE_EXTRA_SRCS ${GL_ANDROID_ONLY_SRCS})
  link_libraries(-static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# 项目库：宿主可编的全部层（RUNTIME - main + DEVICE_EXTRA + SHARED）。
# ---------------------------------------------------------------------------
# 边界的**协议/事务层**：JNI-free ⇒ 宿主也编、测试直接链它；JNI 入口（session_api.cpp）只是
# 它的薄适配器，只在 Android 的共享库里出现（需要 jni.h）。
set(GL_JNI_PORTABLE_SRCS
  src/boundary/jni/api_43499.cpp
  src/device/43499/chain.cpp
  src/boundary/protocol/session_protocol.cpp
  src/boundary/protocol/session_handles.cpp
  src/boundary/protocol/session_object.cpp
  src/boundary/protocol/ops_43499.cpp
)

set(GL_CORE_SRCS ${GL_RUNTIME_SRCS} ${GL_DEVICE_EXTRA_SRCS} ${GL_SHARED_SRCS} ${GL_TEST_SHARED_SRCS} ${GL_JNI_PORTABLE_SRCS})
list(REMOVE_DUPLICATES GL_CORE_SRCS)

add_library(ghostlock_core STATIC ${GL_CORE_SRCS})
target_include_directories(ghostlock_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/boundary
)

target_compile_options(ghostlock_core PRIVATE ${GL_WARNINGS})
if(ANDROID)
  # 日志在目标侧走 __android_log_print（liblog）；宿主侧走 stderr，不需要它。
  target_link_libraries(ghostlock_core PUBLIC log)
endif()

# ---------------------------------------------------------------------------
# 可执行体：host ⇒ build/host，android ⇒ build/native。
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(GL_BIN_DIR ${GL_REPO_ROOT}/build/native)
else()
  set(GL_BIN_DIR ${GL_REPO_ROOT}/build/host)
endif()

# ---------------------------------------------------------------------------
# JNI 边界（Kotlin 加载；见 docs/analysis/jni-two-backend-plan.md）。
# 源集合＝src/jni/*.cpp 中**当前活着**的那部分；半成品的「6 目标移植实验」在
# src/jni/legacy/ 下（默认不参与构建）。
# ---------------------------------------------------------------------------
if(GL_JNI)
  # 三个库的源清单互斥（审查第 1 点）：同一个函数面不得同时出现在两个库里。
  # 以前这里是 GLOB，于是新旧两套东西一起编进旧库，同一函数面存在多份会话状态；现在改成显式清单。
  set(GL_JNI_LEGACY_SRCS
    src/boundary/protocol/session_protocol.cpp
    src/boundary/protocol/session_handles.cpp
    src/boundary/protocol/session_object.cpp
    src/boundary/jni/native_finish.cpp
    src/boundary/jni/native_memfd.cpp
    src/boundary/jni/native_raw_esp.cpp
    src/boundary/jni/native_helper.cpp
    src/backend/cve_2026_43284/lib/raw_esp.cpp
    src/boundary/protocol/ops_43499.cpp
  )
  set(GL_JNI_43499_SRCS src/boundary/jni/api_43499.cpp src/device/43499/chain.cpp src/boundary/jni/native_43499.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/boundary/protocol/api_43499_ops.cpp)

  # D1 单表生成的读取器是两个 backend 的只读边界组件；OBJECT target 保证两库都真实编译它，
  # 又不把同一源文件伪装成任一 backend 的私有实现。
  add_library(ghostlock_attack_contract OBJECT src/contract/attack_contract_reader.cpp)
  target_compile_options(ghostlock_attack_contract PRIVATE ${GL_WARNINGS})
  target_link_libraries(ghostlock_attack_contract PRIVATE ghostlock_core)

  # 配置期硬断言：三份清单两两不相交（重叠即配置失败，不给编出来再说的机会）。
  foreach(pair "GL_JNI_LEGACY_SRCS;GL_JNI_43499_SRCS")
    list(GET pair 0 gl_left)
    list(GET pair 1 gl_right)
    foreach(source ${gl_left})
      if(source IN_LIST ${gl_right})
        message(FATAL_ERROR "JNI 源清单重叠：${source} 同时属于 ${gl_left} 与 ${gl_right}")
      endif()
    endforeach()
  endforeach()
  set(GL_JNI_SOURCES ${GL_JNI_LEGACY_SRCS})
  # 工具而不是库的一部分：协议表导出器只做宿主工具（它有自己的 main）。
  list(REMOVE_ITEM GL_JNI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/tools/jni_protocol_manifest.cpp)
  if(GL_JNI_SOURCES)
    add_library(ghostlock_jni SHARED ${GL_JNI_SOURCES})
    target_compile_options(ghostlock_jni PRIVATE ${GL_WARNINGS})
    target_link_libraries(ghostlock_jni PRIVATE ghostlock_core log)
    set_target_properties(ghostlock_jni PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${GL_BIN_DIR})

    # 每个 backend 一个 JNI 库（用户裁定 2026-10-14）：函数面是类型化的，不再有帧。
    add_library(ghostlock43499 SHARED ${GL_JNI_43499_SRCS})
    target_sources(ghostlock43499 PRIVATE $<TARGET_OBJECTS:ghostlock_attack_contract>)
    target_compile_options(ghostlock43499 PRIVATE ${GL_WARNINGS})
    target_link_libraries(ghostlock43499 PRIVATE ghostlock_core log)
    set_target_properties(ghostlock43499 PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${GL_BIN_DIR})

  else()
    message(STATUS "GL_JNI: src/jni 下暂无源文件 —— 边界库本次跳过")
  endif()
endif()

# ---------------------------------------------------------------------------
# 2026-10-17 用户裁定「只留 JNI 调用」：设备探针（src/tools/*.cpp，GL_PROBES）不是 JNI 调用路径，
# 连同 GL_PROBES 选项一并删除；历史实现见 git 历史。
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 宿主测试：src/tests/*_test.cpp + src/pipeline/tests/*_test.cpp ⇒ 逐个可执行体 + CTest。
# 每个测试只编自己的 TU，其余实现由 ghostlock_core 提供（等价于旧 Makefile 的
# filter-out src/main.cpp,HOST_ALL_SRCS）。
# ---------------------------------------------------------------------------
# 测试清单**逐条列出**（不是 glob）：清单的权威是旧 Makefile 的 EVIDENCE_BINS + P4_BINS
# （111 个）。磁盘上另有 6 个从未接线的 _test.cpp（crash_dump_bridge / encap_peer /
# kind_catalogue / multicast_route / payload_page / sentry_hook），其中 kind_catalogue_test
# 引用已被删除的符号 ⇒ 直接编译不过；它们不在本清单里，逐个评估另开批次。
set(GL_TEST_NAMES
  a2_test
  abi_payload_offsets_test
  access_chain_test
  access_route_subclass_test
  candidate_match_test
  collision_buckets_test
  collision_count_test
  collision_wait_test
  credential_image_test
  cve43499_kernel_access_test
  include_firewall_test
  discovery_result_test
  futex_hash_test
  heap_geometry_test
  api_43499_test
  attack_contract_model_test
  handle_registry_test
  hmac_sha256_test
  jni_protocol_test
  jni_session_lifecycle_test
  jni_ops_43499_test
  kernel_access_test
  leak_search_test
  leak_walk_test
  multicast_arm_test
  multicast_policy_test
  multicast_landing_test
  multicast_window_test
  page_geometry_test
  page_slot_test
  perf_ring_test
  perf_vote_test
  policy_dispatch_test
  prepare_mapping_test
  prepare_page_test
  prepare_plan_test
  prepare_steps_test
  race_action_test
  race_runtime_test
  reclaim_plan_test
  route_config_test
  route_expected_keys_test
  route_lifecycle_test
  route_policy_test
  route_status_test
  scan_bounds_test
  scan_geometry_test
  select_stack_route_test
  shaping_plan_test
  snitch_child_test
  snitch_discovery_test
  snitch_policy_test
  snitch_region_test
  snitch_run_test
  snitch_scan_test
  snitch_ops_test
  snitch_setup_test
  snitch_tuning_test
  step_runner_test
  step_targets_test
  target_state_test
  window_attempt_test
  window_steps_test
  write_encoder_test
  write_layout_test
  write_request_test
)

# 只有这两个住在 src/pipeline/tests（其余都在 src/tests）。
set(GL_PIPELINE_TESTS window_exit_paths_test plan_gate_test)

if(GL_TESTS)
  enable_testing()
  foreach(test_name ${GL_TEST_NAMES})
    if(test_name IN_LIST GL_PIPELINE_TESTS)
      set(test_src ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/tests/${test_name}.cpp)
    else()
      set(test_src ${CMAKE_CURRENT_SOURCE_DIR}/src/tests/${test_name}.cpp)
    endif()
    if(NOT EXISTS ${test_src})
      message(FATAL_ERROR "GL_TESTS: ${test_name} 没有对应的源文件：${test_src}")
    endif()
    add_executable(${test_name} ${test_src})
    target_compile_options(${test_name} PRIVATE ${GL_WARNINGS})
    target_link_libraries(${test_name} PRIVATE ghostlock_core)
    set_target_properties(${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${GL_BIN_DIR})
    # 工作目录＝core/（与旧 Makefile 的 check 一致）：kmi_catalog_test 读 ../profile-core/...，
    # payload_manifest_test 等读 src/tests/*.pin.tsv —— 都是相对路径。
    add_test(NAME ${test_name} COMMAND ${test_name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()
  list(LENGTH GL_TEST_NAMES GL_TEST_COUNT)
  message(STATUS "GL_TESTS: ${GL_TEST_COUNT} 个测试已注册；用 ctest --test-dir <build dir> 运行")
endif()

# ---------------------------------------------------------------------------
# 旧 check 配方里的结构性检查（grep/python 项）与两个文档级检查：一律注册成 CTest。
# 工作目录＝core/（与旧 Makefile 的 check 相同，它的相对路径都是相对 core/ 写的）。
# ---------------------------------------------------------------------------
if(GL_TESTS)
  add_test(NAME structural_window_body
    COMMAND python3 ${GL_REPO_ROOT}/tools/window_body_guard.py src/backend/cve_2026_43499/landing/multicast_route.hpp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_no_lua_in_pipeline
    COMMAND sh -c "h=$(grep -rIlE 'lua_State|luaL_|#include *[<\\\"]lua' src/pipeline src/contract 2>/dev/null || true); if [ -n \"$h\" ]; then echo LUA-HIT: $h; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_no_order_tokens
    COMMAND sh -c "n=$(grep -nE 'sort|order|sequenc|index|next|step|schedul|priorit|rank' src/pipeline/composition_root.hpp | wc -l | tr -d ' '); if [ \"$n\" -ne 0 ]; then echo ORDER-HIT: $n; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_surface_no_window_rows
    COMMAND sh -c "n=$(grep -cE '\\{\"window\\.' src/contract/script_surface.hpp || true); if [ \"$n\" -ne 0 ]; then echo SURFACE-WINDOW-ROW: $n; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_surface_single_site
    COMMAND sh -c "d=$(for f in $(find . -type f \\( -name '*.hpp' -o -name '*.cpp' \\) ! -name 'script_surface.hpp'); do if tr -d '\\n' < \"$f\" | grep -qE 'backend.*writeop.*settle.*handoff'; then echo \"$f\"; fi; done | wc -l | tr -d ' '); if [ \"$d\" -ne 0 ]; then echo SURFACE-DUP: $d; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_virtual_confined
    COMMAND sh -c "f=$(grep -rlE '^[[:space:]]*virtual ' $(find src -name '*.hpp' -o -name '*.cpp' | grep -v '^src/tests/') || true); s=$(echo \"$f\" | grep -vE '^src/(contract/kernel_access\\.hpp|backend/cve_2026_[0-9]+/kernel_access\\.hpp|backend/cve_2026_[0-9]+/landing/[a-z_]+\\.hpp)$' | grep -v '^$' || true); if [ -n \"$s\" ]; then echo VIRTUAL-OUTSIDE-ACCESS: $s; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_window_clean
    COMMAND sh -c "c=$(sed -e 's://.*::' src/backend/cve_2026_43499/race/pi_window.hpp src/backend/cve_2026_43499/race/pi_race.hpp src/backend/cve_2026_43499/landing/multicast_route_linux.hpp src/backend/cve_2026_43499/landing/multicast_window.hpp | grep -vE '^[[:space:]]*\\*'); n=$(echo \"$c\" | grep -cE '\\bnew\\b|malloc|calloc|realloc|printf|fprintf|std::string|std::vector|std::function|virtual|spdlog|log_step' || true); if [ \"$n\" -ne 0 ]; then echo WINDOW-DIRTY: $n; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME structural_support_neutral
    COMMAND sh -c "c=$(sed -e 's://.*::' $(find src/support -name '*.hpp' -o -name '*.cpp') | grep -vE '^[[:space:]]*\\*'); n=$(echo \"$c\" | grep -ciE 'spray|payload|route|kernel|exploit|attack|futex' || true); if [ \"$n\" -ne 0 ]; then echo SUPPORT-IMPURE: $n; exit 1; fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  # 2026-10-17：structural_plan_check 跑的是 build/host/ghostlock（Lua 启动器，已随 Lua 依赖删除），
  # 主体不在了 ⇒ 该检查一并移除（不是跳过）。
  # 2026-10-17：profile_manifest_dump 与 structural_profile_manifest_v3 检查基于 src/profile/key_catalog.cpp
  # （旧 GLKv3 键表），该键表已随文档路径删除 ⇒ 工具与检查一并移除。

  # 2026-10-17 用户裁定：跨端字段表改为手写（core/src/contract/attack_contract_fields.hpp +
  # app/.../core/generated/AttackContractDtos.kt），不再有构建期生成器。
endif()
