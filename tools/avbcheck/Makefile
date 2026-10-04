# avbcheck - on-device AVB / partition integrity checker.
#
# Targets:
#   make            NDK aarch64 static executable -> build/avbcheck-aarch64
#   make host       host executable (unit tests / offline cross-checks)
#   make test       host unit tests + avbtool fixture cross-check
#   make clean
#
# Override the NDK with ANDROID_NDK_HOME=... or NDK=...
NDK ?= $(ANDROID_NDK_HOME)
ifeq ($(strip $(NDK)),)
NDK := /Users/nickji/Library/Android/sdk/ndk/30.0.16248370
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
HOST_TAG := darwin-x86_64
HOST_LDFLAGS := -Wl,-dead_strip
else
HOST_TAG := linux-x86_64
HOST_LDFLAGS := -Wl,--gc-sections
endif

NDK_BIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST_TAG)/bin
ANDROID_CXX := $(NDK_BIN)/aarch64-linux-android21-clang++
HOST_CXX ?= clang++

BUILD := build
CORE_SRC := src/sha2.cpp src/avb.cpp
CLI_SRC := src/main.cpp
TEST_SRC := tests/test_avbcheck.cpp

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -ffunction-sections -fdata-sections
NDK_LDFLAGS := -static -Wl,--gc-sections -Wl,-s

AVBTOOL := ../device-guard/third_party/avbtool/avbtool
PY_VERIFY := ../device-guard/avb_verify_descriptors.py

.PHONY: all host test clean

all: $(BUILD)/avbcheck-aarch64

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/avbcheck-aarch64: $(CORE_SRC) $(CLI_SRC) | $(BUILD)
	$(ANDROID_CXX) $(CXXFLAGS) $(NDK_LDFLAGS) -o $@ $(CORE_SRC) $(CLI_SRC)

host: $(BUILD)/avbcheck-host

$(BUILD)/avbcheck-host: $(CORE_SRC) $(CLI_SRC) | $(BUILD)
	$(HOST_CXX) $(CXXFLAGS) $(HOST_LDFLAGS) -o $@ $(CORE_SRC) $(CLI_SRC)

$(BUILD)/avbcheck-tests: $(CORE_SRC) $(TEST_SRC) | $(BUILD)
	$(HOST_CXX) $(CXXFLAGS) -Isrc $(HOST_LDFLAGS) -o $@ $(CORE_SRC) $(TEST_SRC)

test: $(BUILD)/avbcheck-host $(BUILD)/avbcheck-tests
	$(BUILD)/avbcheck-tests
	AVBCHECK_HOST=$(abspath $(BUILD)/avbcheck-host) AVBTOOL=$(abspath $(AVBTOOL)) \
	  PY_VERIFY=$(abspath $(PY_VERIFY)) bash tests/run_crosscheck.sh
	@echo "AVBCHECK_TESTS=OK"

clean:
	rm -rf $(BUILD)
