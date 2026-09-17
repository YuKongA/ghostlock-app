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
  src/core/main.cpp \
  src/core/memory/address_space.cpp \
  src/core/memory/heap_context.cpp \
  src/core/pi_race.cpp \
  src/core/routes/route_controller.cpp \
  src/core/routes/tcp_zerocopy_route.cpp \
  src/core/routes/select_stack_route.cpp \
  src/core/routes/multicast_waiter_route.cpp \
  src/core/memory/payload_builder.cpp \
  src/core/session/runtime_config.cpp \
  src/core/offsets_json.cpp \
  src/core/util.cpp \
  src/core/routes/route_operations.cpp \
  src/core/session/exploit_session.cpp \
  src/core/support/native_resource.cpp

NATIVE_BUILD_DIR := .build/native
C_OBJS := $(patsubst %.c,$(NATIVE_BUILD_DIR)/%.o,$(C_SRCS))
CXX_OBJS := $(patsubst %.cpp,$(NATIVE_BUILD_DIR)/%.o,$(CXX_SRCS))
OBJS := $(C_OBJS) $(CXX_OBJS)

# Native interface and target headers also trigger a rebuild.
HDRS := $(wildcard src/core/*.h src/core/*.hpp src/core/*/*.h src/core/*/*.hpp)

# Device offsets are selected at runtime from uname -r.
TARGET_CONFIG ?= target.h

COMMON_FLAGS := -O2 -flto -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
  -Isrc/core -DTARGET_CONFIG_H=\"$(TARGET_CONFIG)\"
CFLAGS := $(COMMON_FLAGS) -std=gnu11
CXXFLAGS := $(COMMON_FLAGS) -std=c++20 -fno-rtti
LDFLAGS := -fPIE -pie -pthread -flto -static-libstdc++

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_BUILD_DIR := .build/host

.PHONY: all clean product

all: ghostlock

ghostlock: $(OBJS) Makefile
	@echo "Using NDK compiler: $(NDK_CC)"
	@echo "Using NDK C++ compiler/linker: $(NDK_CXX)"
	@echo "Target config: $(TARGET_CONFIG)"
	$(NDK_CXX) $(OBJS) $(LDFLAGS) -o ghostlock

$(NATIVE_BUILD_DIR)/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(NDK_CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD_DIR)/%.o: %.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(NDK_CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: cpp-link-probe-test target-constants-test native-resource-test native-host-tests
cpp-link-probe-test: $(HOST_BUILD_DIR)/cpp_link_probe_test
	$(HOST_BUILD_DIR)/cpp_link_probe_test

target-constants-test: $(HOST_BUILD_DIR)/target_constants_test
	$(HOST_BUILD_DIR)/target_constants_test

native-resource-test: $(HOST_BUILD_DIR)/native_resource_test
	$(HOST_BUILD_DIR)/native_resource_test

NATIVE_HOST_TESTS := \
  profile_test payload_builder_test heap_context_test route_status_test \
  runtime_time_test \
  kernelsnitch_scan_bounds_test route_controller_test pi_race_test \
  tcp_zerocopy_route_test select_stack_route_test \
  multicast_waiter_route_test target_constants_test native_resource_test \
  offsets_json_test

native-host-tests: $(addprefix $(HOST_BUILD_DIR)/,$(NATIVE_HOST_TESTS))
	@for test in $^; do $$test; done

$(HOST_BUILD_DIR)/cpp_link_probe_test: src/core/tests/cpp_link_probe.cpp src/core/tests/cpp_link_probe.h src/core/tests/cpp_link_probe_test.c
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c11 -Isrc/core -c src/core/tests/cpp_link_probe_test.c -o $(HOST_BUILD_DIR)/cpp_link_probe_test.o
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core -c src/core/tests/cpp_link_probe.cpp -o $(HOST_BUILD_DIR)/cpp_link_probe.o
	$(HOST_CXX) $(HOST_BUILD_DIR)/cpp_link_probe_test.o $(HOST_BUILD_DIR)/cpp_link_probe.o -o $@

$(HOST_BUILD_DIR)/target_constants_test: src/core/tests/target_constants_test.cpp src/core/target.h src/core/target_constants.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core src/core/tests/target_constants_test.cpp -o $@

$(HOST_BUILD_DIR)/native_resource_test: src/core/tests/native_resource_test.cpp src/core/support/native_resource.cpp src/core/support/native_resource.hpp src/core/support/native_result.hpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -pthread -Isrc/core src/core/tests/native_resource_test.cpp src/core/support/native_resource.cpp -o $@

$(HOST_BUILD_DIR)/route_status_test: src/core/tests/route_status_test.cpp src/core/routes/route_status.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core $< -o $@

$(HOST_BUILD_DIR)/runtime_time_test: src/core/tests/runtime_time_test.cpp src/core/runtime_time.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core $< -o $@

$(HOST_BUILD_DIR)/profile_test: src/core/tests/profile_test.cpp src/core/profile.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core $< -o $@

$(HOST_BUILD_DIR)/payload_builder_test: src/core/tests/payload_builder_test.cpp src/core/memory/payload_builder.cpp src/core/memory/payload_builder.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core src/core/tests/payload_builder_test.cpp src/core/memory/payload_builder.cpp -o $@

$(HOST_BUILD_DIR)/heap_context_test: src/core/tests/heap_context_test.cpp src/core/memory/heap_context.cpp src/core/memory/heap_context.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core src/core/tests/heap_context_test.cpp src/core/memory/heap_context.cpp -o $@

$(HOST_BUILD_DIR)/kernelsnitch_scan_bounds_test: src/core/tests/kernelsnitch_scan_bounds_test.cpp src/core/kernelsnitch/scan_bounds.h
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core $< -o $@

$(HOST_BUILD_DIR)/route_controller_test: src/core/tests/route_controller_test.cpp src/core/routes/route_controller.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -ffunction-sections -fdata-sections -Wl,-dead_strip -pthread -Isrc/core $^ -o $@

$(HOST_BUILD_DIR)/pi_race_test: src/core/tests/pi_race_test.cpp src/core/pi_race.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -pthread -Isrc/core $^ -o $@

$(HOST_BUILD_DIR)/tcp_zerocopy_route_test: src/core/tests/tcp_zerocopy_route_test.cpp src/core/routes/tcp_zerocopy_route.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -pthread -Isrc/core $^ -o $@

$(HOST_BUILD_DIR)/select_stack_route_test: src/core/tests/select_stack_route_test.cpp src/core/routes/select_stack_route.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -pthread -Isrc/core $^ -o $@

$(HOST_BUILD_DIR)/multicast_waiter_route_test: src/core/tests/multicast_waiter_route_test.cpp src/core/routes/multicast_waiter_route.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -pthread -Isrc/core $^ -o $@

$(HOST_BUILD_DIR)/offsets_json_test: src/core/tests/offsets_json_test.cpp src/core/offsets_json.cpp src/core/memory/address_space.cpp src/core/support/native_resource.cpp
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core $^ -o $@

product: ghostlock
	@echo "=== ghostlock binary ready: ./ghostlock ==="
	@echo "构建 APK: .\gradlew.bat :app:assembleDebug"

clean:
	rm -f ghostlock
	rm -rf .build/native .build/host
