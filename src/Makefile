# src/Makefile -- A0 skeleton build entry (rewrite-plan-v2 section 2).
#
# This file references ONLY the new tree (core/**) plus the vendored mpack under
# lib/mpack. It replaces the previous build entry wholesale: nothing here points
# at the retired sources.
#
# Targets:
#   host            build and run the skeleton binary (default)
#   check           run the P4 evidence: window exit paths, plan gate, and the
#                   two structural scans (zero Lua in the window layer, zero
#                   ordering tokens in the composition root)
#   mpack           compile the vendored MessagePack amalgamation (C99, isolated
#                   flags, warnings off -- see lib/mpack/README.md)
#   dangling-check  every path this file references must exist on disk
#   clean           remove the host build directory
#
# Numbers policy (Q2 rule 1): no geometry/offset/timing/tuning value may be
# defined here; those come from the GLK profile and its HOCON assets.

CXX ?= c++
CC ?= cc

BUILD_DIR := ../build/host
MPACK_DIR := lib/mpack
MPACK_SRC := $(MPACK_DIR)/mpack.c
MPACK_HDR := $(MPACK_DIR)/mpack.h

CXXFLAGS := -std=c++23 -Wall -Wextra -Wconversion -Wsign-conversion -Icore -isystem $(MPACK_DIR)
# Third-party isolation (lib/mpack/README.md): C99, its own flags, warnings off,
# so the project's zero-warning line above stays meaningful.
MPACK_CFLAGS := -std=c99 -w

SKELETON_SRCS := core/main.cpp
LAYER_HEADERS := \
  core/contract/layer.hpp \
  core/contract/layer_scope.hpp \
  core/contract/kind.hpp \
  core/contract/resource_scope.hpp \
  core/contract/op_registry.hpp \
  core/support/layer.hpp \
  core/memory/layer.hpp \
  core/race/layer.hpp \
  core/backend/layer.hpp \
  core/pipeline/layer.hpp \
  core/pipeline/attack_window.hpp \
  core/pipeline/composition_root.hpp \
  core/pipeline/plan_gate.hpp \
  core/terminal/layer.hpp \
  core/script/layer.hpp \
  core/session/layer.hpp \
  core/profile/layer.hpp \
  core/profile/route_catalog.hpp

P4_TESTS := core/pipeline/tests/window_exit_paths_test.cpp core/pipeline/tests/plan_gate_test.cpp
P4_BINS := $(BUILD_DIR)/window_exit_paths_test $(BUILD_DIR)/plan_gate_test
ORDER_TOKENS := sort|order|sequenc|index|next|step|schedul|priorit|rank

.PHONY: all host check mpack dangling-check clean
all: host

host: $(BUILD_DIR)/ghostlock-skeleton
	@echo "host: running the skeleton"
	@$(BUILD_DIR)/ghostlock-skeleton

$(BUILD_DIR)/ghostlock-skeleton: $(SKELETON_SRCS) $(LAYER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SKELETON_SRCS) -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/window_exit_paths_test: core/pipeline/tests/window_exit_paths_test.cpp core/pipeline/attack_window.hpp core/pipeline/composition_root.hpp core/contract/resource_scope.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/pipeline/tests/window_exit_paths_test.cpp -o $@

$(BUILD_DIR)/plan_gate_test: core/pipeline/tests/plan_gate_test.cpp core/pipeline/plan_gate.hpp core/contract/op_registry.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/pipeline/tests/plan_gate_test.cpp -o $@

check: $(P4_BINS)
	@echo "check: P4 window exit paths"
	@$(BUILD_DIR)/window_exit_paths_test
	@echo "check: P4 plan gate"
	@$(BUILD_DIR)/plan_gate_test
	@lua=$$(grep -rIlE 'lua_State|luaL_|#include *[<"]lua' core/pipeline core/contract 2>/dev/null || true); \
	if [ -n "$$lua" ]; then echo "LUA-HIT: $$lua"; exit 1; fi; \
	echo "check: zero Lua references in the window/pipeline layer"
	@hits=$$(grep -nE '$(ORDER_TOKENS)' core/pipeline/composition_root.hpp | wc -l | tr -d ' '); \
	if [ "$$hits" -ne 0 ]; then echo "ORDER-HIT: $$hits"; grep -nE '$(ORDER_TOKENS)' core/pipeline/composition_root.hpp; exit 1; fi; \
	echo "check: zero ordering tokens in the composition root"

mpack: $(BUILD_DIR)/mpack.o
$(BUILD_DIR)/mpack.o: $(MPACK_SRC) $(MPACK_HDR) | $(BUILD_DIR)
	$(CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $@

dangling-check:
	@missing=0; total=0; for p in $(SKELETON_SRCS) $(LAYER_HEADERS) $(P4_TESTS) $(MPACK_SRC) $(MPACK_HDR); do \
	  total=$$((total+1)); \
	  if [ ! -e "$$p" ]; then echo "DANGLING: $$p"; missing=$$((missing+1)); fi; \
	done; \
	echo "dangling-check: missing=$$missing total=$$total"; \
	[ "$$missing" -eq 0 ]

clean:
	rm -rf $(BUILD_DIR)
