API ?= 35

# Auto-detect NDK
ifeq ($(OS),Windows_NT)
  NDK_ROOT ?= $(subst \,/,$(firstword $(wildcard     $(subst \,/,$(LOCALAPPDATA))/Android/Sdk/ndk/*     $(subst \,/,$(ANDROID_HOME))/ndk/*     D:/AndroidSDK/ndk/*)))
  override NDK_ROOT := $(subst \,/,$(NDK_ROOT))
  PREBUILT := windows-x86_64
  CLANG_BASE := aarch64-linux-android$(API)-clang
  CLANGXX_BASE := aarch64-linux-android$(API)-clang++
  NDK_CC := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/$(CLANG_BASE).cmd
  NDK_CXX := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/$(CLANGXX_BASE).cmd
else
  NDK_ROOT ?= $(or $(ANDROID_NDK_HOME),$(ANDROID_NDK_ROOT))
  UNAME_S := $(shell uname -s)
  PREBUILT := $(if $(filter Darwin,$(UNAME_S)),darwin-x86_64,linux-x86_64)
  NDK_CC := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/aarch64-linux-android$(API)-clang
  NDK_CXX := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/aarch64-linux-android$(API)-clang++
endif

C_SRCS :=

CXX_SRCS := \
  core/main.cpp \
  core/attack/ops.cpp \
  core/profile/entry.cpp \
  core/legacy/legacy_profile_converter.cpp \
  core/legacy/legacy_entrypoint_starter.cpp \
  core/memory/address_space.cpp \
  core/memory/heap_context.cpp \
  core/race/pi_race.cpp \
  core/route/route_controller.cpp \
  core/race/threads.cpp \
  core/route/tcp_zerocopy_route.cpp \
  core/route/select_stack_route.cpp \
  core/route/multicast_waiter_route.cpp \
  core/memory/payload_builder.cpp \
  core/session/runtime_config.cpp \
  core/legacy/offsets_json.cpp \
  core/profile/binary.cpp \
  core/support/util.cpp \
  core/session/exploit_session.cpp \
  core/route/exploit_procedure.cpp \
  core/session/handoff_probe.cpp \
  core/session/victim_process.cpp \
  core/session/victim_context.cpp \
  core/support/native_resource.cpp

# Generated output stays in the repository-root build/ directory; this Makefile
# lives in src/ and is expected to run from there (`make -C src ...`).
BUILD_ROOT ?= ../build
NATIVE_BUILD_DIR := $(BUILD_ROOT)/native
C_OBJS := $(patsubst %.c,$(NATIVE_BUILD_DIR)/%.o,$(C_SRCS))
CXX_OBJS := $(patsubst %.cpp,$(NATIVE_BUILD_DIR)/%.o,$(CXX_SRCS))
OBJS := $(C_OBJS) $(CXX_OBJS)

# Native interface and target headers also trigger a rebuild.
HDRS := $(wildcard core/*.h core/*.hpp core/*/*.h core/*/*.hpp)

# Device offsets are selected at runtime from uname -r.
TARGET_CONFIG ?= kernel/target.h

COMMON_FLAGS := -O2 -flto -Wall -Wextra -Wconversion -Wsign-conversion \
  -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
  -Icore -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\"
CFLAGS := $(COMMON_FLAGS) -std=gnu11
CXXFLAGS := $(COMMON_FLAGS) -std=c++23 -fno-rtti
LDFLAGS := -fPIE -pie -pthread -flto -static-libstdc++

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_CXXFLAGS := -std=c++23 -fno-rtti -Wall -Wextra -Wconversion -Wsign-conversion
# The size-stripping link flag is spelled differently per linker: Darwin's ld
# uses -dead_strip, while GNU ld on a Linux host rejects it and needs
# --gc-sections instead. Keep each spelling in its own variable so the commas
# inside the flags are not parsed as $(if ...) argument separators.
HOST_LD_GC_DARWIN := -Wl,-dead_strip
HOST_LD_GC_ELF := -Wl,--gc-sections
HOST_LD_GC := $(if $(filter Darwin,$(shell uname -s)),$(HOST_LD_GC_DARWIN),$(HOST_LD_GC_ELF))
HOST_BUILD_DIR := $(BUILD_ROOT)/host-test

# Every generated artifact stays under build/ so nothing lands in the repo root.
GHOSTLOCK_BIN := $(NATIVE_BUILD_DIR)/ghostlock

.PHONY: all ghostlock clean product

all: ghostlock

ghostlock: $(GHOSTLOCK_BIN)

$(GHOSTLOCK_BIN): $(OBJS) Makefile
	@echo "Using NDK compiler: $(NDK_CC)"
	@echo "Using NDK C++ compiler/linker: $(NDK_CXX)"
	@echo "Target config: $(TARGET_CONFIG)"
	@mkdir -p $(dir $@)
	$(NDK_CXX) $(OBJS) $(LDFLAGS) -o $@

$(NATIVE_BUILD_DIR)/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(NDK_CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD_DIR)/%.o: %.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(NDK_CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: cpp-link-probe-test target-constants-test native-resource-test native-host-tests lint-tidy
cpp-link-probe-test: $(HOST_BUILD_DIR)/cpp_link_probe_test
	$(HOST_BUILD_DIR)/cpp_link_probe_test

target-constants-test: $(HOST_BUILD_DIR)/target_constants_test
	$(HOST_BUILD_DIR)/target_constants_test

native-resource-test: $(HOST_BUILD_DIR)/native_resource_test
	$(HOST_BUILD_DIR)/native_resource_test

NATIVE_HOST_TESTS := \
  profile_test payload_builder_test heap_context_test route_status_test \
  route_catalog_test component_catalog_test route_policy_test route_lifecycle_test \
  runtime_time_test \
  kernelsnitch_scan_bounds_test route_controller_test pi_race_test \
  tcp_zerocopy_route_test select_stack_route_test \
  multicast_waiter_route_test target_constants_test native_resource_test \
  offsets_json_test profile_binary_test futex_hash_test number_parse_test \
  runtime_paths_test handoff_probe_test victim_context_test \
  cpp_link_probe_test

native-host-tests: $(addprefix $(HOST_BUILD_DIR)/,$(NATIVE_HOST_TESTS))
	@status=0; for test in $^; do $$test || status=1; done; exit $$status

# Host tests must rebuild whenever any production header changes.
$(addprefix $(HOST_BUILD_DIR)/,$(NATIVE_HOST_TESTS)): $(HDRS)

# CPP14 selected clang-tidy subset; the check list and its reviewed
# exclusions live in .clang-tidy. Requires the NDK so the Android and
# libc++ headers resolve.
lint-tidy:
	@test -n "$(NDK_ROOT)" || { echo "NDK_ROOT is not set"; exit 1; }
	@status=0; for f in $(CXX_SRCS); do \
	  $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/clang-tidy "$$f" \
	    --warnings-as-errors='*' -- \
	    -std=c++23 -fno-rtti -Icore \
	    --target=aarch64-linux-android$(API) \
	    --sysroot=$(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/sysroot \
	    -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\" || status=1; \
	done; exit $$status

$(HOST_BUILD_DIR)/cpp_link_probe_test: core/tests/cpp_link_probe.cpp core/tests/cpp_link_probe.h core/tests/cpp_link_probe_test.c
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c11 -Icore -c core/tests/cpp_link_probe_test.c -o $(HOST_BUILD_DIR)/cpp_link_probe_test.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore -c core/tests/cpp_link_probe.cpp -o $(HOST_BUILD_DIR)/cpp_link_probe.o
	$(HOST_CXX) $(HOST_BUILD_DIR)/cpp_link_probe_test.o $(HOST_BUILD_DIR)/cpp_link_probe.o -o $@

$(HOST_BUILD_DIR)/target_constants_test: core/tests/target_constants_test.cpp core/kernel/target.h core/kernel/target_constants.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/target_constants_test.cpp -o $@

$(HOST_BUILD_DIR)/native_resource_test: core/tests/native_resource_test.cpp core/support/native_resource.cpp core/support/native_resource.hpp core/support/native_result.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/native_resource_test.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/route_status_test: core/tests/route_status_test.cpp core/route/route_status.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/runtime_time_test: core/tests/runtime_time_test.cpp core/support/time.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/route_catalog_test: core/tests/route_catalog_test.cpp core/profile/model.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/route_catalog_test.cpp -o $@

$(HOST_BUILD_DIR)/component_catalog_test: core/tests/component_catalog_test.cpp core/route/component_catalog.hpp core/profile/model.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/component_catalog_test.cpp -o $@

$(HOST_BUILD_DIR)/route_policy_test: core/tests/route_policy_test.cpp core/route/route_policy.hpp core/route/route_status.h core/profile/model.h core/memory/payload_builder.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -Icore core/tests/route_policy_test.cpp -o $@

$(HOST_BUILD_DIR)/route_lifecycle_test: core/tests/route_lifecycle_test.cpp core/route/route_lifecycle.hpp core/route/route_status.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/route_lifecycle_test.cpp -o $@

$(HOST_BUILD_DIR)/profile_test: core/tests/profile_test.cpp core/profile/model.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/profile_binary_test: core/tests/profile_binary_test.cpp core/profile/binary.cpp core/profile/binary.h core/profile/model.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/profile_binary_test.cpp core/profile/binary.cpp -o $@

$(HOST_BUILD_DIR)/payload_builder_test: core/tests/payload_builder_test.cpp core/memory/payload_builder.cpp core/memory/payload_builder.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/payload_builder_test.cpp core/memory/payload_builder.cpp -o $@

$(HOST_BUILD_DIR)/heap_context_test: core/tests/heap_context_test.cpp core/memory/heap_context.cpp core/memory/heap_context.h core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/heap_context_test.cpp core/memory/heap_context.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/kernelsnitch_scan_bounds_test: core/tests/kernelsnitch_scan_bounds_test.cpp core/kernelsnitch/scan_bounds.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/route_controller_test: core/tests/route_controller_test.cpp core/route/route_controller.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -pthread -Icore core/tests/route_controller_test.cpp core/route/route_controller.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/pi_race_test: core/tests/pi_race_test.cpp core/race/pi_race.cpp core/race/pi_race.h core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/pi_race_test.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/tcp_zerocopy_route_test: core/tests/tcp_zerocopy_route_test.cpp core/route/tcp_zerocopy_route.cpp core/route/tcp_zerocopy_route.h core/race/pi_race.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/tcp_zerocopy_route_test.cpp core/route/tcp_zerocopy_route.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/select_stack_route_test: core/tests/select_stack_route_test.cpp core/route/select_stack_route.cpp core/route/select_stack_route.h core/race/pi_race.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/select_stack_route_test.cpp core/route/select_stack_route.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/multicast_waiter_route_test: core/tests/multicast_waiter_route_test.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/multicast_waiter_route_test.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/offsets_json_test: core/tests/offsets_json_test.cpp core/legacy/offsets_json.cpp core/memory/address_space.cpp core/support/native_resource.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/offsets_json_test.cpp core/legacy/offsets_json.cpp core/profile/binary.cpp core/memory/address_space.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/futex_hash_test: core/tests/futex_hash_test.cpp core/kernelsnitch/futex_hash.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/number_parse_test: core/tests/number_parse_test.cpp core/kernelsnitch/number_parse.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/runtime_paths_test: core/tests/runtime_paths_test.cpp core/session/runtime_paths.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/handoff_probe_test: core/tests/handoff_probe_test.cpp core/session/handoff_probe.cpp core/session/handoff_probe.hpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/handoff_probe_test.cpp core/session/handoff_probe.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/victim_context_test: core/tests/victim_context_test.cpp core/session/victim_context.cpp core/session/victim_context.hpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/victim_context_test.cpp core/session/victim_context.cpp core/support/native_resource.cpp -o $@

product: ghostlock
	@echo "=== ghostlock binary ready: $(GHOSTLOCK_BIN) ==="
	@echo "构建 APK: .\gradlew.bat :app:assembleDebug"

clean:
	rm -f $(GHOSTLOCK_BIN) ghostlock
	rm -rf $(BUILD_ROOT)/native $(BUILD_ROOT)/host
