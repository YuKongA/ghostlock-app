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
  core/support/neutrals.hpp \
  core/memory/layer.hpp \
  core/memory/target_profile.hpp \
  core/race/layer.hpp \
  core/race/pi_window.hpp \
  core/race/pi_race.hpp \
  core/backend/layer.hpp \
  core/pipeline/layer.hpp \
  core/pipeline/attack_window.hpp \
  core/pipeline/composition_root.hpp \
  core/pipeline/plan_gate.hpp \
  core/terminal/layer.hpp \
  core/script/layer.hpp \
  core/session/layer.hpp \
  core/profile/layer.hpp \
  core/profile/route_catalog.hpp \
  core/contract/script_surface.hpp

P4_TESTS := core/pipeline/tests/window_exit_paths_test.cpp core/pipeline/tests/plan_gate_test.cpp
EVIDENCE_TESTS := core/tests/a2_test.cpp core/tests/surface_test.cpp core/tests/surface_manifest_test.cpp
EVIDENCE_BINS := $(BUILD_DIR)/a2_test $(BUILD_DIR)/surface_test $(BUILD_DIR)/surface_manifest_test
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

$(BUILD_DIR)/a2_test: core/tests/a2_test.cpp core/race/pi_window.hpp core/race/pi_race.hpp core/memory/target_profile.hpp core/support/neutrals.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/a2_test.cpp -o $@

$(BUILD_DIR)/surface_test: core/tests/surface_test.cpp core/contract/script_surface.hpp core/contract/layer_scope.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/surface_test.cpp -o $@

$(BUILD_DIR)/surface_manifest_test: core/tests/surface_manifest_test.cpp core/contract/script_surface.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/surface_manifest_test.cpp -o $@

# The surface manifest (same pattern as combination/vocabulary/stepset manifests):
# stdout is exactly the TSV, so it can be redirected into an artifact; the
# bare-run assertion inside the exporter compares it with the frozen pin.
SURFACE_MANIFEST := $(BUILD_DIR)/surface-manifest.tsv
.PHONY: surface-manifest surface-doc-check
$(SURFACE_MANIFEST): $(BUILD_DIR)/surface_manifest_test
	@$(BUILD_DIR)/surface_manifest_test > $@
	@rows=$$(wc -l < $@ | tr -d ' '); \
	if [ "$$rows" -lt 1 ]; then echo "surface-manifest: EMPTY artifact"; exit 1; fi; \
	echo "surface-manifest: $@ ($$rows lines)"

# Depends on the artifact rule so the manifest is (re)generated when missing.
surface-manifest: $(SURFACE_MANIFEST)

# Documentation cross-check (docs side owns the table): every manifest line must
# appear verbatim in the document, so a hand-copied table cannot drift.
#   make -C src surface-doc-check DOC=docs/development/full-process-uml.md
DOC ?=
surface-doc-check: $(SURFACE_MANIFEST)
	@if [ ! -f "$(DOC)" ]; then echo "surface-doc-check: set DOC=<path> to the document holding the table"; exit 2; fi
	@missing=$$(while IFS= read -r line; do grep -Fqx "$$line" "$(DOC)" || echo "$$line"; done < $(SURFACE_MANIFEST) | wc -l | tr -d ' '); \
	if [ "$$missing" -ne 0 ]; then echo "surface-doc-check: $$missing manifest line(s) missing from $(DOC)"; \
	  while IFS= read -r line; do grep -Fqx "$$line" "$(DOC)" || { echo "  MISSING: $$line"; break; }; done < $(SURFACE_MANIFEST); exit 1; fi; \
	echo "surface-doc-check: $(DOC) carries every manifest line"

check: $(P4_BINS) $(EVIDENCE_BINS)
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
	@echo "check: A2 window constraints and profile read path"
	@$(BUILD_DIR)/a2_test
	@echo "check: exposure surface table"
	@$(BUILD_DIR)/surface_test
	@win=$$(grep -cE '\{"window\.' core/contract/script_surface.hpp || true); \
	if [ "$$win" -ne 0 ]; then echo "SURFACE-WINDOW-ROW: $$win"; grep -nE '\{"window\.' core/contract/script_surface.hpp; exit 1; fi; \
	echo "check: no window.* row in the exposure surface"
	@dup=$$(for f in $$(find core -type f \( -name '*.hpp' -o -name '*.cpp' \) ! -name 'script_surface.hpp' ! -name 'surface_manifest_test.cpp'); do \
	  if tr -d '\n' < "$$f" | grep -qE 'backend.*writeop.*settle.*handoff'; then echo "$$f"; fi; \
	done | wc -l | tr -d ' '); \
	if [ "$$dup" -ne 0 ]; then echo "SURFACE-DUP: $$dup other file(s) restate the surface list"; exit 1; fi; \
	echo "check: surface list has a single definition site (no other file restates it; the manifest pin is a deliberate copy and is checked byte-for-byte by surface-manifest)"
	@vtables=$$(nm -C $(BUILD_DIR)/ghostlock-skeleton $(BUILD_DIR)/a2_test $(BUILD_DIR)/window_exit_paths_test $(BUILD_DIR)/plan_gate_test 2>/dev/null | grep -c '_ZTV' || true); \
	if [ "$$vtables" -ne 0 ]; then echo "VTT-HIT: $$vtables (a vtable exists in the window shape)"; exit 1; fi; \
	echo "check: nm scan _ZTV* = 0 (no vtable in any built binary)"
	@code=$$(sed -e 's://.*::' core/race/pi_window.hpp core/race/pi_race.hpp | grep -vE '^[[:space:]]*\*'); \
	dirty=$$(echo "$$code" | grep -cE '\bnew\b|malloc|calloc|realloc|printf|fprintf|std::string|std::vector|std::function|virtual' || true); \
	if [ "$$dirty" -ne 0 ]; then echo "WINDOW-DIRTY: $$dirty"; echo "$$code" | grep -nE '\bnew\b|malloc|calloc|realloc|printf|fprintf|std::string|std::vector|std::function|virtual'; exit 1; fi; \
	echo "check: race window carries no allocation, logging or indirect dispatch"
	@scode=$$(sed -e 's://.*::' $$(find core/support -name '*.hpp' -o -name '*.cpp') | grep -vE '^[[:space:]]*\*'); \
	impure=$$(echo "$$scode" | grep -ciE 'spray|payload|route|kernel|exploit|attack|futex' || true); \
	if [ "$$impure" -ne 0 ]; then echo "SUPPORT-IMPURE: $$impure"; echo "$$scode" | grep -niE 'spray|payload|route|kernel|exploit|attack|futex'; exit 1; fi; \
	echo "check: support carries neutral pieces only"

mpack: $(BUILD_DIR)/mpack.o
$(BUILD_DIR)/mpack.o: $(MPACK_SRC) $(MPACK_HDR) | $(BUILD_DIR)
	$(CC) $(MPACK_CFLAGS) -c $(MPACK_SRC) -o $@

# --- A6-prep: Lua 5.5.1 core-set wiring (integration only) ------------------
# Authority: lib/lua/PROVENANCE.md. Only the 22-file core set (parser + base
# library) is built; the interpreter (lua.c), the compiler (luac.c) and every
# standard library that ships an opener stay out of the build, so the sandbox
# lacks those capabilities AT THE LINK LAYER rather than by deleting tables at
# runtime. Third-party isolation follows mpack: C99, its own flags, warnings
# off. This batch does NOT link Lua into the skeleton (host/check unchanged).
LUA_DIR := lib/lua/src
LUA_CORE_FILES := lapi lauxlib lbaselib lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
LUA_CORE_SRCS := $(addprefix $(LUA_DIR)/,$(addsuffix .c,$(LUA_CORE_FILES)))
LUA_CORE_OBJS := $(addprefix $(BUILD_DIR)/lua/,$(addsuffix .o,$(LUA_CORE_FILES)))
LUA_CORE_LIB := $(BUILD_DIR)/liblua-core.a
LUA_CFLAGS := -std=c99 -w
# Forbidden entry points: the interpreter/compiler mains and every library
# opener the vendored tree can provide.
LUA_FORBIDDEN_SYMBOLS := luaopen_io luaopen_os luaopen_package luaopen_debug luaopen_coroutine luaopen_math luaopen_string luaopen_table luaopen_utf8 luaopen_db luaopen_loadlib luaL_openlibs

.PHONY: lua-core lua-core-size
lua-core: $(LUA_CORE_LIB)
	@echo "lua-core: built $(words $(LUA_CORE_FILES)) files into $(LUA_CORE_LIB)"
	@hits=0; for s in $(LUA_FORBIDDEN_SYMBOLS); do \
	  n=$$(nm -g $(LUA_CORE_LIB) 2>/dev/null | grep -c "$$s" || true); \
	  if [ "$$n" -ne 0 ]; then echo "LUA-FORBIDDEN: $$s ($$n)"; hits=$$((hits+1)); fi; \
	done; \
	main=$$(nm -g $(LUA_CORE_LIB) 2>/dev/null | grep -cE ' T _?main$$' || true); \
	if [ "$$main" -ne 0 ]; then echo "LUA-FORBIDDEN: main ($$main)"; hits=$$((hits+1)); fi; \
	if [ "$$hits" -ne 0 ]; then echo "lua-core: forbidden capability symbols present"; exit 1; fi; \
	echo "lua-core: nm scan clean (no library opener, no interpreter/compiler main)"

$(BUILD_DIR)/lua/%.o: $(LUA_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/lua
	$(CC) $(LUA_CFLAGS) -I$(LUA_DIR) -c $< -o $@

$(LUA_CORE_LIB): $(LUA_CORE_OBJS)
	ar rcs $@ $^

# ADR-0008 size cross-check: NDK arm64, -Os, .text* sum via llvm-size sysv.
# Name the toolchain explicitly, e.g.:
#   make lua-core-size NDK_CC=<aarch64 clang> LLVM_SIZE=<llvm-size>
NDK_CC ?=
LLVM_SIZE ?=
lua-core-size:
	@if [ ! -x "$(NDK_CC)" ] || [ ! -x "$(LLVM_SIZE)" ]; then echo "lua-core-size: set NDK_CC and LLVM_SIZE (see lib/lua/PROVENANCE.md)"; exit 2; fi
	@rm -rf $(BUILD_DIR)/lua-size; mkdir -p $(BUILD_DIR)/lua-size; \
	for f in $(LUA_CORE_FILES); do \
	  $(NDK_CC) -std=c99 -w -Os -I$(LUA_DIR) -c $(LUA_DIR)/$$f.c -o $(BUILD_DIR)/lua-size/$$f.o || exit 1; \
	done; \
	total=$$($(LLVM_SIZE) --format=sysv $(BUILD_DIR)/lua-size/*.o | awk '/^\.text/ {s+=$$2} END {printf "%d", s+0}'); \
	echo "lua-core-size: text* sum = $$total B over $(words $(LUA_CORE_FILES)) files (PROVENANCE reference: 119068 B)"

dangling-check:
	@missing=0; total=0; for p in $(SKELETON_SRCS) $(LAYER_HEADERS) $(P4_TESTS) $(EVIDENCE_TESTS) $(MPACK_SRC) $(MPACK_HDR) $(LUA_CORE_SRCS); do \
	  total=$$((total+1)); \
	  if [ ! -e "$$p" ]; then echo "DANGLING: $$p"; missing=$$((missing+1)); fi; \
	done; \
	echo "dangling-check: missing=$$missing total=$$total"; \
	[ "$$missing" -eq 0 ]

clean:
	rm -rf $(BUILD_DIR)
