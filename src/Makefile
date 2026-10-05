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
  core/profile/entry.cpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp \
  core/backend/cve_2026_43284/session_frame.cpp \
  core/backend/cve_2026_43284/pagecache/pagecache.cpp \
  core/backend/cve_2026_43284/pagecache/splice_io.cpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp \
  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp \
  core/backend/cve_2026_43284/steps/crash_dump.cpp \
  core/backend/cve_2026_43284/steps/hook_patch.cpp \
  core/backend/cve_2026_43284/steps/chain.cpp \
  core/backend/cve_2026_43284/backend_terminal.cpp \
  core/backend/cve_2026_43284/diagnostic.cpp \
  core/backend/cve_2026_43284/execution_binding.cpp \
  core/backend/cve_2026_43284/real_ops.cpp \
  core/backend/cve_2026_43284/stage_runner.cpp \
  core/memory/address_space.cpp \
  core/memory/heap_context.cpp \
  core/race/pi_race.cpp \
  core/backend/cve_2026_43499/route/route_controller.cpp \
  core/backend/cve_2026_43499/route/route_middleware.cpp \
  core/platform/vivo/vr_guard.cpp \
  core/platform/vivo/vr_task_tag.cpp \
  core/platform/countermeasure/loader.cpp \
  core/platform/countermeasure/sha256.cpp \
  core/race/threads.cpp \
  core/backend/cve_2026_43499/route/tcp_zerocopy_route.cpp \
  core/backend/cve_2026_43499/route/select_stack_route.cpp \
  core/backend/cve_2026_43499/route/multicast_waiter_route.cpp \
  core/memory/payload_builder.cpp \
  core/session/runtime_config.cpp \
  core/profile/binary.cpp \
  core/profile/glkv3.cpp \
  core/profile/glkv3_parse.cpp \
  core/support/util.cpp \
  core/platform/runtime.cpp \
  core/platform/device_facts.cpp \
  core/session/core_session.cpp \
  core/backend/cve_2026_43499_state.cpp \
  core/backend/cve_2026_43499/backend_offsets.cpp \
  core/backend/cve_2026_43499/bootstrap.cpp \
  core/backend/cve_2026_43499/backend_profile.cpp \
  core/backend/cve_2026_43499/primitives.cpp \
  core/backend/cve_2026_43499/steps.cpp \
  core/terminal/root_script.cpp \
  core/terminal/root_child.cpp \
  core/terminal/umh_forward.cpp \
  core/backend/cve_2026_43499_backend.cpp \
  core/ancillary/controller.cpp \
  core/terminal/handoff_probe.cpp \
  core/backend/victim/victim_process.cpp \
  core/backend/victim/victim_context.cpp \
  core/support/native_resource.cpp \
  core/support/run_state.cpp \
  core/support/cli.cpp

# Generated output stays in the repository-root build/ directory; this Makefile
# lives in src/ and is expected to run from there (`make -C src ...`).
BUILD_ROOT ?= ../build
NATIVE_BUILD_DIR := $(BUILD_ROOT)/native

# Vendored MessagePack amalgamation (src/lib/mpack, MIT). It is C99 and
# third-party, so it is compiled by the C compiler with its own flag set and
# -w keeps its warnings out of the project zero-warning policy. Only the
# reader/expect/writer are kept: stdio, nodes, extensions and read/write
# tracking are disabled.
MPACK_DIR := lib/mpack
MPACK_SRC := $(MPACK_DIR)/mpack.c
MPACK_HDR := $(MPACK_DIR)/mpack.h
MPACK_DEFINES := -DMPACK_STDIO=0 -DMPACK_EXTENSIONS=0 -DMPACK_NODE=0 \
  -DMPACK_READ_TRACKING=0 -DMPACK_WRITE_TRACKING=0
MPACK_CFLAGS := -std=c99 -w $(MPACK_DEFINES)
MPACK_OBJ := $(NATIVE_BUILD_DIR)/lib/mpack/mpack.o

C_OBJS := $(patsubst %.c,$(NATIVE_BUILD_DIR)/%.o,$(C_SRCS))
CXX_OBJS := $(patsubst %.cpp,$(NATIVE_BUILD_DIR)/%.o,$(CXX_SRCS))
OBJS := $(C_OBJS) $(CXX_OBJS) $(MPACK_OBJ)

# Native interface and target headers also trigger a rebuild.
HDRS := $(wildcard core/*.h core/*.hpp core/*/*.h core/*/*.hpp core/*/*/*.h core/*/*/*.hpp core/*/*/*/*.h core/*/*/*/*.hpp)

# Device offsets are selected at runtime from uname -r.
TARGET_CONFIG ?= memory/target.h

COMMON_FLAGS := -O2 -flto -Wall -Wextra -Wconversion -Wsign-conversion \
  -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
  -Icore -isystem $(MPACK_DIR) -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\"
CFLAGS := $(COMMON_FLAGS) -std=gnu11
CXXFLAGS := $(COMMON_FLAGS) -std=c++23 -fno-rtti
LDFLAGS := -fPIE -pie -pthread -flto -static-libstdc++

HOST_CC ?= cc
HOST_CXX ?= c++
# Host tests keep the frozen v2 serializer for the golden/equivalence vectors.
# The device build never defines this macro, so no v2 write path is compiled in.
HOST_CXXFLAGS := -std=c++23 -fno-rtti -Wall -Wextra -Wconversion -Wsign-conversion -DGHOSTLOCK_ENABLE_V2_WRITER -isystem $(MPACK_DIR)
# The size-stripping link flag is spelled differently per linker: Darwin's ld
# uses -dead_strip, while GNU ld on a Linux host rejects it and needs
# --gc-sections instead. Keep each spelling in its own variable so the commas
# inside the flags are not parsed as $(if ...) argument separators.
HOST_LD_GC_DARWIN := -Wl,-dead_strip
HOST_LD_GC_ELF := -Wl,--gc-sections
HOST_LD_GC := $(if $(filter Darwin,$(shell uname -s)),$(HOST_LD_GC_DARWIN),$(HOST_LD_GC_ELF))
# dlopen is in libc on Darwin and musl, but glibc hosts still need -ldl.
HOST_DL_LIB := $(if $(filter Darwin,$(shell uname -s)),,-ldl)
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

$(MPACK_OBJ): $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(dir $@)
	$(NDK_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $@

.PHONY: cpp-link-probe-test target-constants-test native-resource-test native-host-tests host-attack-dataflow-test profile-manifest lint-tidy
cpp-link-probe-test: $(HOST_BUILD_DIR)/cpp_link_probe_test
	$(HOST_BUILD_DIR)/cpp_link_probe_test

target-constants-test: $(HOST_BUILD_DIR)/target_constants_test
	$(HOST_BUILD_DIR)/target_constants_test

native-resource-test: $(HOST_BUILD_DIR)/native_resource_test
	$(HOST_BUILD_DIR)/native_resource_test

NATIVE_HOST_TESTS := \
  profile_test payload_builder_test heap_context_test route_status_test \
  route_catalog_test component_catalog_test backend_contract_test route_policy_test \
  route_lifecycle_test runtime_time_test \
  kernelsnitch_scan_bounds_test route_controller_test pi_race_test \
  tcp_zerocopy_route_test select_stack_route_test \
  target_constants_test native_resource_test \
  profile_binary_test futex_hash_test number_parse_test \
  runtime_paths_test handoff_probe_test victim_context_test \
  run_state_test address_space_test session_layout_test cpp_link_probe_test ancillary_test \
  platform_vivo_test platform_abi_test \
  contract_capabilities_test countermeasure_abi_test address_discovery_test terminal_input_test \
  countermeasure_loader_test countermeasure_registry_test \
  document_schema_test owner_schema_test cve_2026_43284_schema_test \
  profile_manifest_test profile_manifest_v3_test \
  glkv3_codec_test glkv3_schema_test profile_v3_test profile_entry_test \
  session_frame_test cve_2026_43284_ipsec_test cve_2026_43284_pagecache_test \
  cve_2026_43284_lkm_test cve_2026_43284_elf_hook_test \
  cve_2026_43284_chain_test cve_2026_43284_backend_terminal_test umh_forward_test \
  cve_2026_43284_diagnostic_test \
  cve_2026_43284_stage_runner_test execution_binding_test \
  cve_2026_43284_crash_dump_test \
  cve_2026_43284_hook_patch_test \
  include_firewall_test fake_backend_test \
  host_attack_dataflow_test

native-host-tests: $(addprefix $(HOST_BUILD_DIR)/,$(NATIVE_HOST_TESTS))
	@status=0; for test in $^; do $$test || status=1; done; exit $$status

# Host tests must rebuild whenever any production header changes.
$(addprefix $(HOST_BUILD_DIR)/,$(NATIVE_HOST_TESTS)): $(HDRS)

# Host attack data-flow harness: the real backend/terminal compiled unmodified,
# with the side-effecting primitives replaced by scriptable host stubs. The
# shadow include path makes the backend resolve the host attack/ops.hpp.
HOST_ATTACK_DATAFLOW_SOURCES := \
  core/tests/host/backend_dataflow_test.cpp \
  core/tests/host/host_attack_script.cpp \
  core/tests/host/attack_stub.cpp \
  core/tests/host/support_stub.cpp \
  core/tests/host/route_stub.cpp \
  core/tests/host/victim_stub.cpp \
  core/tests/host/handoff_stub.cpp \
  core/tests/host/ancillary_stub.cpp \
  core/tests/host/glue_stub.cpp \
  core/backend/cve_2026_43499_backend.cpp \
  core/backend/cve_2026_43499/steps.cpp \
  core/backend/cve_2026_43499/backend_profile.cpp \
  core/terminal/root_child.cpp \
  core/session/core_session.cpp \
  core/backend/cve_2026_43499_state.cpp \
  core/backend/cve_2026_43499/backend_offsets.cpp \
  core/session/runtime_config.cpp \
  core/memory/heap_context.cpp \
  core/memory/address_space.cpp \
  core/race/pi_race.cpp \
  core/support/native_resource.cpp \
  core/support/run_state.cpp

HOST_ATTACK_DATAFLOW_INCLUDES := -Icore/tests/host/include -Icore/tests/host -Icore \
  -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\"

.PHONY: host-attack-dataflow-test
host-attack-dataflow-test: $(HOST_BUILD_DIR)/host_attack_dataflow_test
	$(HOST_BUILD_DIR)/host_attack_dataflow_test

$(HOST_BUILD_DIR)/host_attack_dataflow_test: $(HOST_ATTACK_DATAFLOW_SOURCES) $(HDRS)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_ATTACK_DATAFLOW_INCLUDES) \
	  $(HOST_ATTACK_DATAFLOW_SOURCES) -pthread -o $@

# CPP14 selected clang-tidy subset; the check list and its reviewed
# exclusions live in .clang-tidy. Requires the NDK so the Android and
# libc++ headers resolve.
lint-tidy:
	@test -n "$(NDK_ROOT)" || { echo "NDK_ROOT is not set"; exit 1; }
	@status=0; for f in $(CXX_SRCS); do \
	  $(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/bin/clang-tidy "$$f" \
	    --warnings-as-errors='*' -- \
	    -std=c++23 -fno-rtti -Icore -isystem $(MPACK_DIR) \
	    --target=aarch64-linux-android$(API) \
	    --sysroot=$(NDK_ROOT)/toolchains/llvm/prebuilt/$(PREBUILT)/sysroot \
	    -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\" || status=1; \
	done; exit $$status

$(HOST_BUILD_DIR)/cpp_link_probe_test: core/tests/cpp_link_probe.cpp core/tests/cpp_link_probe.h core/tests/cpp_link_probe_test.c
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c11 -Icore -c core/tests/cpp_link_probe_test.c -o $(HOST_BUILD_DIR)/cpp_link_probe_test.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore -c core/tests/cpp_link_probe.cpp -o $(HOST_BUILD_DIR)/cpp_link_probe.o
	$(HOST_CXX) $(HOST_BUILD_DIR)/cpp_link_probe_test.o $(HOST_BUILD_DIR)/cpp_link_probe.o -o $@

$(HOST_BUILD_DIR)/target_constants_test: core/tests/target_constants_test.cpp core/memory/target.h core/memory/target_constants.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/target_constants_test.cpp -o $@

$(HOST_BUILD_DIR)/native_resource_test: core/tests/native_resource_test.cpp core/support/native_resource.cpp core/support/native_resource.hpp core/support/native_result.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/native_resource_test.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/run_state_test: core/tests/run_state_test.cpp core/support/run_state.cpp core/support/run_state.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/run_state_test.cpp core/support/run_state.cpp -o $@

$(HOST_BUILD_DIR)/terminal_input_test: core/tests/terminal_input_test.cpp core/terminal/terminal_input.hpp core/terminal/root_program.hpp core/terminal/rooted_child.hpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/terminal_input_test.cpp core/support/native_resource.cpp -o $@

# B5-8 umh_forward terminal: the injected channel/probe are fakes, so no device
# is touched. Links the real production terminal unit.
$(HOST_BUILD_DIR)/umh_forward_test: core/tests/umh_forward_test.cpp \
  core/terminal/umh_forward.cpp core/terminal/umh_forward.hpp \
  core/terminal/terminal_input.hpp core/terminal/root_program.hpp \
  core/terminal/umh_command.hpp core/pipeline/component_catalog.hpp \
  core/contract/identity.hpp core/contract/stage_result.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/umh_forward_test.cpp \
	  core/terminal/umh_forward.cpp -o $@

$(HOST_BUILD_DIR)/contract_capabilities_test: core/tests/contract_capabilities_test.cpp core/contract/capabilities.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

# A3 address-discovery contract: the optional capability handle / fail-closed
# result are header-only, so the fake provider runs without a device.
$(HOST_BUILD_DIR)/address_discovery_test: core/tests/address_discovery_test.cpp core/contract/address_discovery.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

# CM-1 countermeasure plugin ABI: the ABI header is a self-contained C99 header,
# so a project include or non-C99 syntax leaking into it fails here; the C++
# mapping and the contract test are declaration-only (no loader in CM-1).
$(HOST_BUILD_DIR)/countermeasure_abi_test: core/tests/countermeasure_abi_test.cpp core/contract/countermeasure.hpp core/contract/glk_cm_abi.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c99 -Wall -Wextra -fsyntax-only -x c core/contract/glk_cm_abi.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

# CM-2 countermeasure loader: real dlopen/dlsym/dlclose against the C test
# plugins built here (C99 proves the ABI header's C usability). This target has
# its own explicit rule -- adding a name to NATIVE_HOST_TESTS alone is not
# enough; the plugins are prerequisites so they exist before the test runs.
CM_PLUGIN_DIR := $(HOST_BUILD_DIR)/cm
CM_PLUGIN_SRCS := \
  core/tests/fixtures/cm_test_plugin.c \
  core/tests/fixtures/cm_test_plugin_badabi.c \
  core/tests/fixtures/cm_test_plugin_missing_entry.c
CM_PLUGIN_SOS := $(patsubst core/tests/fixtures/%.c,$(CM_PLUGIN_DIR)/%.so,$(CM_PLUGIN_SRCS))

$(CM_PLUGIN_DIR)/%.so: core/tests/fixtures/%.c core/contract/glk_cm_abi.h Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 -Wall -Wextra -fPIC -shared -Icore $< -o $@
	@chmod 755 $@

$(HOST_BUILD_DIR)/countermeasure_loader_test: core/tests/countermeasure_loader_test.cpp \
  core/platform/countermeasure/loader.cpp core/platform/countermeasure/loader.hpp \
  core/platform/countermeasure/sha256.cpp core/platform/countermeasure/sha256.hpp \
  core/contract/countermeasure.hpp core/contract/glk_cm_abi.h $(CM_PLUGIN_SOS)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore \
	  -DGLK_CM_TEST_PLUGIN_DIR='"$(abspath $(CM_PLUGIN_DIR))"' \
	  core/tests/countermeasure_loader_test.cpp \
	  core/platform/countermeasure/loader.cpp core/platform/countermeasure/sha256.cpp \
	  -pthread $(HOST_DL_LIB) -o $@

# CM-3 runtime registry + controller external dispatch: the registry is
# header-only and borrows the fake hook table; the loader is not linked and no
# real module is opened. The same explicit-rule pattern as CM-2 applies.
$(HOST_BUILD_DIR)/countermeasure_registry_test: core/tests/countermeasure_registry_test.cpp \
  core/ancillary/external_registry.hpp core/ancillary/controller.hpp \
  core/ancillary/ancillary_policy.hpp core/contract/countermeasure.hpp \
  core/contract/glk_cm_abi.h core/session/core_session.hpp core/support/status.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -Icore core/tests/countermeasure_registry_test.cpp -o $@

$(HOST_BUILD_DIR)/address_space_test: core/tests/address_space_test.cpp core/memory/address_space.cpp core/memory/address_space.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/address_space_test.cpp core/memory/address_space.cpp -o $@

$(HOST_BUILD_DIR)/session_layout_test: core/tests/session_layout_test.cpp core/session/core_session.hpp core/backend/cve_2026_43499_state.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/session_layout_test.cpp -o $@

$(HOST_BUILD_DIR)/route_status_test: core/tests/route_status_test.cpp core/backend/cve_2026_43499/route/route_status.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/runtime_time_test: core/tests/runtime_time_test.cpp core/support/time.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/backend_contract_test: core/tests/backend_contract_test.cpp \
  core/contract/identity.hpp core/pipeline/pipeline.hpp core/pipeline/component_catalog.hpp \
  core/backend/cve_2026_43499/route/route_policy.hpp core/contract/stage_result.hpp \
  core/backend/cve_2026_43499/stage_types.hpp \
  core/backend/cve_2026_43499_backend.hpp core/backend/cve_2026_64560_backend.hpp \
  core/backend/cve_2026_31431_backend.hpp core/backend/cve_2026_43503_backend.hpp \
  core/backend/cve_2026_23274_backend.hpp core/backend/cve_2026_43284_backend.hpp \
  core/session/core_session.hpp core/backend/cve_2026_43499_state.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/backend_contract_test.cpp -o $@

$(HOST_BUILD_DIR)/route_catalog_test: core/tests/route_catalog_test.cpp core/contract/model.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/route_catalog_test.cpp -o $@

$(HOST_BUILD_DIR)/component_catalog_test: core/tests/component_catalog_test.cpp core/pipeline/component_catalog.hpp core/contract/identity.hpp core/contract/model.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/component_catalog_test.cpp -o $@

$(HOST_BUILD_DIR)/route_policy_test: core/tests/route_policy_test.cpp core/backend/cve_2026_43499/route/route_policy.hpp core/backend/cve_2026_43499/route/route_status.h core/contract/model.hpp core/memory/payload_builder.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -Icore core/tests/route_policy_test.cpp -o $@

$(HOST_BUILD_DIR)/ancillary_test: core/tests/ancillary_test.cpp \
  core/ancillary/ancillary_policy.hpp core/ancillary/controller.hpp \
  core/session/core_session.hpp core/support/status.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -Icore core/tests/ancillary_test.cpp -o $@

# platform::abi owner schema/view (A2-4-3): the platform field declaration, the
# pure offset accessors and the mechanical View -> kernel_offsets merge. Header
# only; no session or backend state is linked.
$(HOST_BUILD_DIR)/platform_abi_test: core/tests/platform_abi_test.cpp \
  core/platform/abi.hpp core/profile/schema.hpp core/profile/document.hpp \
  core/contract/model.hpp core/profile/glkv3.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/platform_abi_test.cpp -o $@

# platform::vivo vendor behaviors: the profile->View adapter, the gates and the
# pure plan arithmetic. Header-only (the device execution bodies are not linked
# and the policies' apply templates are not instantiated here).
$(HOST_BUILD_DIR)/platform_vivo_test: core/tests/platform_vivo_test.cpp \
  core/platform/vivo/registry.hpp core/platform/vivo/schema.hpp \
  core/platform/vivo/vr_guard.hpp core/platform/vivo/vr_task_tag.hpp \
  core/platform/vivo/kind.hpp core/platform/vivo/macros.h \
  core/ancillary/ancillary_policy.hpp core/ancillary/controller.hpp \
  core/session/core_session.hpp core/memory/target.h core/contract/model.hpp \
  core/support/status.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -Icore core/tests/platform_vivo_test.cpp -o $@

$(HOST_BUILD_DIR)/route_lifecycle_test: core/tests/route_lifecycle_test.cpp core/backend/cve_2026_43499/route/route_lifecycle.hpp core/backend/cve_2026_43499/route/route_status.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/route_lifecycle_test.cpp -o $@

$(HOST_BUILD_DIR)/profile_test: core/tests/profile_test.cpp core/contract/model.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

# A2-5 test-only legacy decode shim: the host vectors still call the old
# binary_profile::parse / parse_v3 names, so they link this compatibility unit
# (frame + owner bind) and the GLKv3 codec it depends on.
PROFILE_COMPAT_SRCS := core/tests/profile_bind_compat.cpp core/profile/glkv3.cpp core/profile/glkv3_parse.cpp

$(HOST_BUILD_DIR)/profile_bind_mpack.o: $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $@

$(HOST_BUILD_DIR)/profile_binary_test: core/tests/profile_binary_test.cpp core/profile/binary.cpp core/profile/binary.h core/contract/model.hpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/profile_binary_test.cpp core/profile/binary.cpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o -o $@

$(HOST_BUILD_DIR)/document_schema_test: core/tests/document_schema_test.cpp core/profile/binary.cpp core/profile/binary.h core/profile/document.hpp core/profile/schema.hpp core/contract/model.hpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/document_schema_test.cpp core/profile/binary.cpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o -o $@

$(HOST_BUILD_DIR)/owner_schema_test: core/tests/owner_schema_test.cpp core/profile/binary.cpp core/profile/binary.h core/profile/document.hpp core/profile/schema.hpp core/backend/cve_2026_43499/schema.hpp core/platform/abi.hpp core/contract/model.hpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/owner_schema_test.cpp core/profile/binary.cpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o -o $@

$(HOST_BUILD_DIR)/cve_2026_43284_schema_test: core/tests/cve_2026_43284_schema_test.cpp \
  core/profile/binary.cpp core/profile/binary.h core/profile/document.hpp \
  core/profile/schema.hpp core/backend/cve_2026_43284/schema.hpp \
  core/backend/cve_2026_43284_state.hpp core/backend/cve_2026_43499/schema.hpp \
  core/platform/abi.hpp \
  core/contract/model.hpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_schema_test.cpp core/profile/binary.cpp $(PROFILE_COMPAT_SRCS) $(HOST_BUILD_DIR)/profile_bind_mpack.o -o $@

# Native is the SSOT for the wire field universe; this test exports the owner
# Schema to app/src/test/resources/profile-manifest.tsv and checks it in.
$(HOST_BUILD_DIR)/profile_manifest_test: core/tests/profile_manifest_test.cpp core/backend/cve_2026_43499/schema.hpp core/backend/cve_2026_43284/schema.hpp core/backend/cve_2026_43284_state.hpp core/platform/abi.hpp core/profile/schema.hpp core/profile/document.hpp core/contract/model.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/profile_manifest_test.cpp -o $@

profile-manifest: $(HOST_BUILD_DIR)/profile_manifest_test $(HOST_BUILD_DIR)/profile_manifest_v3_test
	$(HOST_BUILD_DIR)/profile_manifest_test --write
	$(HOST_BUILD_DIR)/profile_manifest_v3_test --write

# GLKv3 codec: the host test compiles the codec against the vendored MPack
# amalgamation. MPack is built as C with -w (third-party warning isolation).
$(HOST_BUILD_DIR)/glkv3_codec_test: core/tests/glkv3_codec_test.cpp core/profile/glkv3.cpp core/profile/glkv3.hpp $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $(HOST_BUILD_DIR)/mpack.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/glkv3_codec_test.cpp core/profile/glkv3.cpp $(HOST_BUILD_DIR)/mpack.o -o $@

# GLKv3 owner path -> type lists (GLKv3-3): codec-linked so the test can also
# round-trip a full document through the combined root + owner Schema.
$(HOST_BUILD_DIR)/glkv3_schema_test: core/tests/glkv3_schema_test.cpp \
  core/profile/glkv3.cpp core/profile/glkv3.hpp \
  core/backend/cve_2026_43499/glkv3_schema.hpp \
  core/backend/cve_2026_43284/glkv3_schema.hpp \
  core/backend/cve_2026_43499/schema.hpp \
  core/backend/cve_2026_43284/schema.hpp core/backend/cve_2026_43284_state.hpp \
  core/platform/abi.hpp \
  core/contract/model.hpp $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $(HOST_BUILD_DIR)/glkv3_schema_mpack.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/glkv3_schema_test.cpp core/profile/glkv3.cpp $(HOST_BUILD_DIR)/glkv3_schema_mpack.o -o $@

# Production profile entry dispatch (GLKv3-4): v3 + v2 auto-detection through
# the real read_glk1_file path, linked with the entry unit itself.
$(HOST_BUILD_DIR)/profile_entry_test: core/tests/profile_entry_test.cpp \
  core/profile/entry.cpp core/profile/entry.h \
  core/profile/glkv3_parse.cpp core/profile/glkv3_parse.hpp \
  core/profile/binary.cpp core/profile/binary.h \
  core/profile/glkv3.cpp core/profile/glkv3.hpp \
  core/backend/cve_2026_43499/schema.hpp \
  core/backend/cve_2026_43499/glkv3_schema.hpp \
  core/backend/cve_2026_43284/schema.hpp \
  core/backend/cve_2026_43284/glkv3_schema.hpp \
  core/backend/cve_2026_43284_state.hpp \
  core/support/native_resource.cpp core/support/native_resource.hpp \
  core/profile/document.hpp core/profile/schema.hpp core/contract/model.hpp \
  $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $(HOST_BUILD_DIR)/profile_entry_mpack.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/profile_entry_test.cpp \
	  core/profile/entry.cpp core/profile/glkv3_parse.cpp core/profile/binary.cpp \
	  core/profile/glkv3.cpp core/support/native_resource.cpp \
	  $(HOST_BUILD_DIR)/profile_entry_mpack.o -o $@

# B5-1 runtime session-secret frame (channel B): codec + zeroize, no other
# production dependency.
$(HOST_BUILD_DIR)/session_frame_test: core/tests/session_frame_test.cpp   core/backend/cve_2026_43284/session_frame.cpp   core/backend/cve_2026_43284/session_frame.hpp   core/backend/cve_2026_43284/ipsec/ipsec.cpp   core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/session_frame_test.cpp 	  core/backend/cve_2026_43284/session_frame.cpp 	  core/backend/cve_2026_43284/ipsec/ipsec.cpp 	  core/backend/cve_2026_43284/ipsec/aes256.cpp 	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp -o $@

# B5-2 IpSec/ESP primitives: AES-256 (FIPS-197), HMAC-SHA256 (RFC 4231) and
# the ESP/CBC IV model. Links the same units as the device build.
$(HOST_BUILD_DIR)/cve_2026_43284_ipsec_test: core/tests/cve_2026_43284_ipsec_test.cpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_ipsec_test.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp -o $@

# B5-3 page-cache block write: the executable pagecache.cpp linked against
# the real ipsec units. splice_io.cpp is device-only; the host test binds a fake
# SpliceIoOps with no syscall dependency.
$(HOST_BUILD_DIR)/cve_2026_43284_pagecache_test: core/tests/cve_2026_43284_pagecache_test.cpp \
  core/backend/cve_2026_43284/pagecache/pagecache.cpp core/backend/cve_2026_43284/pagecache/pagecache.hpp \
  core/backend/cve_2026_43284/pagecache/splice_io.hpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_pagecache_test.cpp \
	  core/backend/cve_2026_43284/pagecache/pagecache.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp -o $@

# B5-4 LKM KMI policy, .ko .modinfo precheck and UMH late-load argv: host-only,
# no device and no module load. The .ko fixtures are synthesized in the test.
$(HOST_BUILD_DIR)/cve_2026_43284_lkm_test: core/tests/cve_2026_43284_lkm_test.cpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp core/backend/cve_2026_43284/lkm/lkm_policy.hpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp core/backend/cve_2026_43284/lkm/lkm_image.hpp \
  core/terminal/root_program.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_lkm_test.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_image.cpp -o $@

# B5-5 read-only ELF parsing, hook-target/trampoline planning and parameterized
# shellcode: pure byte-span units, no device or target .so. The ELF fixture is
# synthesized in the test.
$(HOST_BUILD_DIR)/cve_2026_43284_elf_hook_test: core/tests/cve_2026_43284_elf_hook_test.cpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp core/backend/cve_2026_43284/steps/elf_hook.hpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp core/backend/cve_2026_43284/steps/shellcode.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_elf_hook_test.cpp \
	  core/backend/cve_2026_43284/steps/elf_hook.cpp \
	  core/backend/cve_2026_43284/steps/shellcode.cpp -o $@

# B5-6 patch #1 + crash_dump read bridge: embedded splicehelper, fake write and
# read surfaces; no device, no fork, no file write.
$(HOST_BUILD_DIR)/cve_2026_43284_crash_dump_test: core/tests/cve_2026_43284_crash_dump_test.cpp \
  core/backend/cve_2026_43284/steps/crash_dump.cpp core/backend/cve_2026_43284/steps/crash_dump.hpp \
  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp core/backend/cve_2026_43284/embed/splicehelper_blob.hpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp core/backend/cve_2026_43284/embed/libcxx_blob.hpp \
  core/contract/capabilities.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_crash_dump_test.cpp \
	  core/backend/cve_2026_43284/steps/crash_dump.cpp \
	  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp -o $@

# B5-6 libc++ sentry hook apply/restore over fake 16-byte read/write surfaces,
# on a synthesized ELF fixture. No device and no file write.
$(HOST_BUILD_DIR)/cve_2026_43284_hook_patch_test: core/tests/cve_2026_43284_hook_patch_test.cpp \
  core/backend/cve_2026_43284/steps/hook_patch.cpp core/backend/cve_2026_43284/steps/hook_patch.hpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp core/backend/cve_2026_43284/steps/elf_hook.hpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp core/backend/cve_2026_43284/steps/shellcode.hpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp core/backend/cve_2026_43284/embed/libcxx_blob.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_hook_patch_test.cpp \
	  core/backend/cve_2026_43284/steps/hook_patch.cpp \
	  core/backend/cve_2026_43284/steps/elf_hook.cpp \
	  core/backend/cve_2026_43284/steps/shellcode.cpp \
	  core/backend/cve_2026_43284/embed/libcxx_blob.cpp -o $@

# B5-6 endgame chain: carrier fallback, per-block verify/rollback and the single
# terminus. The test binds a fake FileCacheWriteOps plus fake read/trigger/wait;
# no device, syscall or fork. It links the executable chain.cpp unit.
$(HOST_BUILD_DIR)/cve_2026_43284_chain_test: core/tests/cve_2026_43284_chain_test.cpp \
  core/backend/cve_2026_43284/steps/chain.cpp core/backend/cve_2026_43284/steps/chain.hpp \
  core/contract/capabilities.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_chain_test.cpp \
	  core/backend/cve_2026_43284/steps/chain.cpp -o $@

# B5-7 backend -> umh_forward bridge: device facts, LKM/carrier policy, UMH
# command and the B5-6 chain are all driven by injected fakes. No device,
# no fork/exec and no file write; links the real production units.
$(HOST_BUILD_DIR)/cve_2026_43284_backend_terminal_test: core/tests/cve_2026_43284_backend_terminal_test.cpp \
  core/backend/cve_2026_43284/backend_terminal.cpp core/backend/cve_2026_43284/backend_terminal.hpp \
  core/backend/cve_2026_43284_backend.hpp core/backend/cve_2026_43284_state.hpp \
  core/platform/device_facts.cpp core/platform/device_facts.hpp \
  core/backend/cve_2026_43284/steps/chain.cpp core/backend/cve_2026_43284/steps/chain.hpp \
  core/backend/cve_2026_43284/steps/steps.hpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp core/backend/cve_2026_43284/lkm/lkm_policy.hpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp core/backend/cve_2026_43284/lkm/lkm_image.hpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp \
  core/terminal/terminal_input.hpp core/terminal/umh_command.hpp core/terminal/root_program.hpp \
  core/session/core_session.cpp core/session/core_session.hpp core/contract/capabilities.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_backend_terminal_test.cpp \
	  core/backend/cve_2026_43284/backend_terminal.cpp \
	  core/backend/cve_2026_43284/steps/chain.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_image.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp \
	  core/platform/device_facts.cpp \
	  core/session/core_session.cpp -o $@

# B5-9a read-only diagnostic: CLI parsing, read-only ChainOps binding and the
# device-fact/KMI/.ko report. The device probe is a fake; links the real
# diagnostic, real_ops, lkm and platform units. No device, no write, no fork.
$(HOST_BUILD_DIR)/cve_2026_43284_diagnostic_test: core/tests/cve_2026_43284_diagnostic_test.cpp \
  core/backend/cve_2026_43284/diagnostic.cpp core/backend/cve_2026_43284/diagnostic.hpp \
  core/backend/cve_2026_43284/real_ops.cpp core/backend/cve_2026_43284/real_ops.hpp \
  core/backend/cve_2026_43284/steps/chain.hpp core/contract/capabilities.hpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp core/backend/cve_2026_43284/steps/elf_hook.hpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp core/backend/cve_2026_43284/steps/shellcode.hpp \
  core/backend/cve_2026_43284/steps/crash_dump.cpp core/backend/cve_2026_43284/steps/crash_dump.hpp \
  core/backend/cve_2026_43284/steps/hook_patch.cpp core/backend/cve_2026_43284/steps/hook_patch.hpp \
  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp core/backend/cve_2026_43284/embed/splicehelper_blob.hpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp core/backend/cve_2026_43284/embed/libcxx_blob.hpp \
  core/backend/cve_2026_43284/pagecache/pagecache.cpp core/backend/cve_2026_43284/pagecache/pagecache.hpp \
  core/backend/cve_2026_43284/pagecache/splice_io.hpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp core/backend/cve_2026_43284/lkm/lkm_policy.hpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp core/backend/cve_2026_43284/lkm/lkm_image.hpp \
  core/platform/device_facts.cpp core/platform/device_facts.hpp \
  core/support/cli.cpp core/support/cli.hpp \
  core/support/run_state.cpp core/support/run_state.hpp \
  core/contract/identity.hpp core/pipeline/component_catalog.hpp core/contract/model.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_diagnostic_test.cpp \
	  core/backend/cve_2026_43284/diagnostic.cpp \
	  core/backend/cve_2026_43284/real_ops.cpp \
	  core/backend/cve_2026_43284/steps/elf_hook.cpp \
	  core/backend/cve_2026_43284/steps/shellcode.cpp \
	  core/backend/cve_2026_43284/steps/crash_dump.cpp \
	  core/backend/cve_2026_43284/steps/hook_patch.cpp \
	  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp \
	  core/backend/cve_2026_43284/embed/libcxx_blob.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_image.cpp \
	  core/backend/cve_2026_43284/pagecache/pagecache.cpp \
	  core/backend/cve_2026_43284/pagecache/splice_io.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp \
	  core/platform/device_facts.cpp \
	  core/support/cli.cpp \
	  core/support/run_state.cpp -o $@

# B5-9c staged execution: CLI parsing, pure plan/write/trigger/full semantics
# and the real-op binding/run_ready. The chain ops are fakes (and the real
# trigger is never called), so the test never writes a target or forks.
$(HOST_BUILD_DIR)/cve_2026_43284_stage_runner_test: core/tests/cve_2026_43284_stage_runner_test.cpp \
  core/backend/cve_2026_43284/stage_runner.cpp core/backend/cve_2026_43284/stage_runner.hpp \
  core/backend/cve_2026_43284/real_ops.cpp core/backend/cve_2026_43284/real_ops.hpp \
  core/backend/cve_2026_43284/steps/chain.cpp core/backend/cve_2026_43284/steps/chain.hpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp core/backend/cve_2026_43284/steps/elf_hook.hpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp core/backend/cve_2026_43284/steps/shellcode.hpp \
  core/backend/cve_2026_43284/steps/crash_dump.cpp core/backend/cve_2026_43284/steps/crash_dump.hpp \
  core/backend/cve_2026_43284/steps/hook_patch.cpp core/backend/cve_2026_43284/steps/hook_patch.hpp \
  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp core/backend/cve_2026_43284/embed/splicehelper_blob.hpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp core/backend/cve_2026_43284/embed/libcxx_blob.hpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp core/backend/cve_2026_43284/lkm/lkm_policy.hpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp core/backend/cve_2026_43284/lkm/lkm_image.hpp \
  core/terminal/root_program.hpp core/terminal/umh_command.hpp \
  core/backend/cve_2026_43284/pagecache/pagecache.cpp core/backend/cve_2026_43284/pagecache/pagecache.hpp \
  core/backend/cve_2026_43284/pagecache/splice_io.hpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/ipsec.hpp \
  core/backend/cve_2026_43284/ipsec/aes256.cpp core/backend/cve_2026_43284/ipsec/aes256.hpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/ipsec/hmac_sha256.hpp \
  core/backend/cve_2026_43284/session_frame.cpp core/backend/cve_2026_43284/session_frame.hpp \
  core/platform/device_facts.cpp core/platform/device_facts.hpp \
  core/support/cli.cpp core/support/cli.hpp \
  core/support/run_state.cpp core/support/run_state.hpp \
  core/contract/capabilities.hpp core/backend/cve_2026_43284_state.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/cve_2026_43284_stage_runner_test.cpp \
	  core/backend/cve_2026_43284/stage_runner.cpp \
	  core/backend/cve_2026_43284/real_ops.cpp \
	  core/backend/cve_2026_43284/steps/chain.cpp \
	  core/backend/cve_2026_43284/steps/elf_hook.cpp \
	  core/backend/cve_2026_43284/steps/shellcode.cpp \
	  core/backend/cve_2026_43284/steps/crash_dump.cpp \
	  core/backend/cve_2026_43284/steps/hook_patch.cpp \
	  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp \
	  core/backend/cve_2026_43284/embed/libcxx_blob.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_image.cpp \
	  core/backend/cve_2026_43284/pagecache/pagecache.cpp \
	  core/backend/cve_2026_43284/pagecache/splice_io.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp \
	  core/backend/cve_2026_43284/session_frame.cpp \
	  core/platform/device_facts.cpp \
	  core/support/cli.cpp \
	  core/support/run_state.cpp -o $@

# B6/T5 composition-root binding: module read/plan, single-carrier selection and
# fail-closed paths over an injected fake device. The real device setup compiles
# only under __linux__ and is covered by the NDK build.
$(HOST_BUILD_DIR)/execution_binding_test: core/tests/execution_binding_test.cpp \
  core/backend/cve_2026_43284/execution_binding.cpp core/backend/cve_2026_43284/execution_binding.hpp \
  core/backend/cve_2026_43284/backend_terminal.cpp core/backend/cve_2026_43284/backend_terminal.hpp \
  core/backend/cve_2026_43284/stage_runner.cpp core/backend/cve_2026_43284/stage_runner.hpp \
  core/backend/cve_2026_43284/real_ops.cpp core/backend/cve_2026_43284/real_ops.hpp \
  core/backend/cve_2026_43284/steps/chain.cpp core/backend/cve_2026_43284/steps/chain.hpp \
  core/backend/cve_2026_43284/steps/elf_hook.cpp core/backend/cve_2026_43284/steps/elf_hook.hpp \
  core/backend/cve_2026_43284/steps/shellcode.cpp core/backend/cve_2026_43284/steps/shellcode.hpp \
  core/backend/cve_2026_43284/steps/crash_dump.cpp core/backend/cve_2026_43284/steps/crash_dump.hpp \
  core/backend/cve_2026_43284/steps/hook_patch.cpp core/backend/cve_2026_43284/steps/hook_patch.hpp \
  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp core/backend/cve_2026_43284/embed/splicehelper_blob.hpp \
  core/backend/cve_2026_43284/embed/libcxx_blob.cpp core/backend/cve_2026_43284/embed/libcxx_blob.hpp \
  core/backend/cve_2026_43284/lkm/lkm_policy.cpp core/backend/cve_2026_43284/lkm/lkm_policy.hpp \
  core/backend/cve_2026_43284/lkm/lkm_image.cpp core/backend/cve_2026_43284/lkm/lkm_image.hpp \
  core/backend/cve_2026_43284/pagecache/pagecache.cpp core/backend/cve_2026_43284/pagecache/pagecache.hpp \
  core/backend/cve_2026_43284/pagecache/splice_io.cpp core/backend/cve_2026_43284/pagecache/splice_io.hpp \
  core/backend/cve_2026_43284/ipsec/ipsec.cpp core/backend/cve_2026_43284/ipsec/aes256.cpp \
  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp core/backend/cve_2026_43284/session_frame.cpp \
  core/platform/device_facts.cpp core/terminal/umh_forward.cpp \
  core/session/core_session.cpp core/support/run_state.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/execution_binding_test.cpp \
	  core/backend/cve_2026_43284/execution_binding.cpp \
	  core/backend/cve_2026_43284/backend_terminal.cpp \
	  core/backend/cve_2026_43284/stage_runner.cpp \
	  core/backend/cve_2026_43284/real_ops.cpp \
	  core/backend/cve_2026_43284/steps/chain.cpp \
	  core/backend/cve_2026_43284/steps/elf_hook.cpp \
	  core/backend/cve_2026_43284/steps/shellcode.cpp \
	  core/backend/cve_2026_43284/steps/crash_dump.cpp \
	  core/backend/cve_2026_43284/steps/hook_patch.cpp \
	  core/backend/cve_2026_43284/embed/splicehelper_blob.cpp \
	  core/backend/cve_2026_43284/embed/libcxx_blob.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_policy.cpp \
	  core/backend/cve_2026_43284/lkm/lkm_image.cpp \
	  core/backend/cve_2026_43284/pagecache/pagecache.cpp \
	  core/backend/cve_2026_43284/pagecache/splice_io.cpp \
	  core/backend/cve_2026_43284/ipsec/ipsec.cpp \
	  core/backend/cve_2026_43284/ipsec/aes256.cpp \
	  core/backend/cve_2026_43284/ipsec/hmac_sha256.cpp \
	  core/backend/cve_2026_43284/session_frame.cpp \
	  core/platform/device_facts.cpp \
	  core/terminal/umh_forward.cpp \
	  core/session/core_session.cpp \
	  core/support/run_state.cpp -o $@

# GLKv3 decode bridge (GLKv3-4): the real codec + owner schemas + the shared
# v2/v3 binder, so the v3 wire is exercised end to end and against a v2
# document with the same logical content.
$(HOST_BUILD_DIR)/profile_v3_test: core/tests/profile_v3_test.cpp \
  core/profile/glkv3_parse.cpp core/profile/glkv3_parse.hpp \
  core/profile/binary.cpp core/profile/binary.h \
  core/profile/glkv3.cpp core/profile/glkv3.hpp \
  core/backend/cve_2026_43499/schema.hpp \
  core/backend/cve_2026_43499/glkv3_schema.hpp \
  core/backend/cve_2026_43284/schema.hpp \
  core/backend/cve_2026_43284/glkv3_schema.hpp \
  core/backend/cve_2026_43284_state.hpp \
  core/platform/abi.hpp \
  core/profile/document.hpp core/profile/schema.hpp core/contract/model.hpp \
  $(MPACK_SRC) $(MPACK_HDR)
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $(HOST_BUILD_DIR)/profile_v3_mpack.o
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/profile_v3_test.cpp \
	  core/tests/profile_bind_compat.cpp \
	  core/profile/glkv3_parse.cpp core/profile/binary.cpp core/profile/glkv3.cpp \
	  $(HOST_BUILD_DIR)/profile_v3_mpack.o -o $@

# GLKv3 path -> type manifest (GLKv3-3): native exports its FieldSpec lists and
# Kotlin (ProfileManifestV3AgreementTest) asserts its adapter table equals it.
$(HOST_BUILD_DIR)/profile_manifest_v3_test: core/tests/profile_manifest_v3_test.cpp \
  core/backend/cve_2026_43499/glkv3_schema.hpp \
  core/backend/cve_2026_43284/glkv3_schema.hpp core/platform/abi.hpp core/profile/glkv3.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/profile_manifest_v3_test.cpp -o $@

profile-manifest-v3: $(HOST_BUILD_DIR)/profile_manifest_v3_test
	$(HOST_BUILD_DIR)/profile_manifest_v3_test --write

$(HOST_BUILD_DIR)/payload_builder_test: core/tests/payload_builder_test.cpp core/memory/payload_builder.cpp core/memory/payload_builder.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/payload_builder_test.cpp core/memory/payload_builder.cpp -o $@

$(HOST_BUILD_DIR)/heap_context_test: core/tests/heap_context_test.cpp core/memory/heap_context.cpp core/memory/heap_context.h core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/heap_context_test.cpp core/memory/heap_context.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/kernelsnitch_scan_bounds_test: core/tests/kernelsnitch_scan_bounds_test.cpp core/backend/cve_2026_43499/leak/scan_bounds.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/route_controller_test: core/tests/route_controller_test.cpp core/backend/cve_2026_43499/route/route_controller.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -ffunction-sections -fdata-sections $(HOST_LD_GC) -pthread -Icore core/tests/route_controller_test.cpp core/backend/cve_2026_43499/route/route_controller.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/pi_race_test: core/tests/pi_race_test.cpp core/race/pi_race.cpp core/race/pi_race.h core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/pi_race_test.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/tcp_zerocopy_route_test: core/tests/tcp_zerocopy_route_test.cpp core/backend/cve_2026_43499/route/tcp_zerocopy_route.cpp core/backend/cve_2026_43499/route/tcp_zerocopy_route.h core/race/pi_race.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/tcp_zerocopy_route_test.cpp core/backend/cve_2026_43499/route/tcp_zerocopy_route.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/select_stack_route_test: core/tests/select_stack_route_test.cpp core/backend/cve_2026_43499/route/select_stack_route.cpp core/backend/cve_2026_43499/route/select_stack_route.h core/race/pi_race.cpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/select_stack_route_test.cpp core/backend/cve_2026_43499/route/select_stack_route.cpp core/race/pi_race.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/futex_hash_test: core/tests/futex_hash_test.cpp core/backend/cve_2026_43499/leak/futex_hash.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/number_parse_test: core/tests/number_parse_test.cpp core/support/number_parse.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/runtime_paths_test: core/tests/runtime_paths_test.cpp core/session/runtime_paths.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore $< -o $@

$(HOST_BUILD_DIR)/handoff_probe_test: core/tests/handoff_probe_test.cpp core/terminal/handoff_probe.cpp core/terminal/handoff_probe.hpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/handoff_probe_test.cpp core/terminal/handoff_probe.cpp core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/victim_context_test: core/tests/victim_context_test.cpp core/backend/victim/victim_context.cpp core/backend/victim/victim_context.hpp core/support/native_resource.cpp core/support/native_resource.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -pthread -Icore core/tests/victim_context_test.cpp core/backend/victim/victim_context.cpp core/support/native_resource.cpp -o $@

# A2-5-5 include firewall: a host-only static scan of src/core. It walks every
# production source file, reads each quoted #include and rejects an edge that
# breaks the ADR-0004 R1 allowed-edge graph; the explicit whitelist ledger names
# every tolerated legacy violation with its pending batch. GHOSTLOCK_FIREWALL_ROOT
# is baked from $(CURDIR) so the test finds the tree independently of its cwd.
$(HOST_BUILD_DIR)/include_firewall_test: core/tests/include_firewall_test.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DGHOSTLOCK_FIREWALL_ROOT='"$(CURDIR)"' -Icore core/tests/include_firewall_test.cpp -o $@

# A2-5-5/R8 fake backend: a second (backend, terminal) pair with its own State,
# schema and input, assembled through the real Pipeline template. Host-only; it
# links the neutral session container so the state lifecycle is exercised for
# real. No production backend, route or device code is linked.
$(HOST_BUILD_DIR)/fake_backend_test: core/tests/fake_backend_test.cpp \
  core/tests/fake_backend_stub.hpp \
  core/pipeline/pipeline.hpp core/contract/identity.hpp \
  core/pipeline/component_catalog.hpp \
  core/session/core_session.cpp core/session/core_session.hpp \
  core/profile/schema.hpp core/profile/document.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -Icore core/tests/fake_backend_test.cpp \
	  core/session/core_session.cpp -o $@

product: ghostlock
	@echo "=== ghostlock binary ready: $(GHOSTLOCK_BIN) ==="
	@echo "构建 APK: .\gradlew.bat :app:assembleDebug"

clean:
	rm -f $(GHOSTLOCK_BIN) ghostlock
	rm -rf $(BUILD_ROOT)/native $(BUILD_ROOT)/host
