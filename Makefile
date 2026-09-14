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

C_SRCS := \
  src/core/main.c \
  src/core/address_space.c \
  src/core/heap_context.c \
  src/core/pi_race.c \
  src/core/route_controller.c \
  src/core/tcp_zerocopy_route.c \
  src/core/select_stack_route.c \
  src/core/multicast_waiter_route.c \
  src/core/payload_builder.c \
  src/core/runtime_config.c \
  src/core/offsets_json.c \
  src/core/util.c \
  src/core/fops.c

CXX_SRCS := \
  src/core/cpp_link_probe.cpp

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

.PHONY: cpp-link-probe-test
cpp-link-probe-test: $(HOST_BUILD_DIR)/cpp_link_probe_test
	$(HOST_BUILD_DIR)/cpp_link_probe_test

$(HOST_BUILD_DIR)/cpp_link_probe_test: src/core/cpp_link_probe.cpp src/core/cpp_link_probe.h src/core/tests/cpp_link_probe_test.c
	@mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CC) -std=c11 -Isrc/core -c src/core/tests/cpp_link_probe_test.c -o $(HOST_BUILD_DIR)/cpp_link_probe_test.o
	$(HOST_CXX) -std=c++20 -fno-rtti -Isrc/core -c src/core/cpp_link_probe.cpp -o $(HOST_BUILD_DIR)/cpp_link_probe.o
	$(HOST_CXX) $(HOST_BUILD_DIR)/cpp_link_probe_test.o $(HOST_BUILD_DIR)/cpp_link_probe.o -o $@

product: ghostlock
	@echo "=== ghostlock binary ready: ./ghostlock ==="
	@echo "构建 APK: .\gradlew.bat :app:assembleDebug"

clean:
	rm -f ghostlock
	rm -rf .build/native .build/host
