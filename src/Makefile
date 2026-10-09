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

# The runtime binary: the entry plus the layers it reaches. The script layer is
# not in it yet (its Lua core would have to be cross-built too), and neither is
# the executor: the entry resolves a document and says what it would run.
RUNTIME_SRCS := core/main.cpp \
  core/profile/glkv3_document.cpp core/profile/payload_keys.cpp \
  core/profile/document_pairs.cpp core/profile/run_plan.cpp \
  core/support/sha256.cpp

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
  core/contract/script_surface.hpp \
  core/script/call_sink.hpp \
  core/script/staged_plan.hpp \
  core/script/stage_gate.hpp \
  core/script/handle_table.hpp \
  core/support/sha256.hpp \
  core/support/sha256.cpp \
  core/terminal/payload.hpp \
  core/terminal/payload_store.hpp \
  core/terminal/payload_store.cpp \
  core/terminal/execution.hpp \
  core/terminal/execution.cpp \
  core/profile/payload_keys.hpp \
  core/profile/payload_keys.cpp \
  core/profile/glkv3_document.hpp \
  core/profile/glkv3_document.cpp \
  core/tests/payload_manifest_test.cpp \
  core/tests/payload-manifest.pin.tsv \
  core/contract/step_catalog.hpp \
  core/tests/step_catalog_test.cpp \
  core/race/futex_seam_linux.hpp \
  core/tools/futex_probe.cpp \
  core/tools/requeue_probe.cpp \
  core/memory/write_request.hpp \
  core/tests/write_request_test.cpp \
  core/memory/write_encoder.hpp \
  core/memory/write_encoder.cpp \
  core/tests/write_encoder_test.cpp \
  core/memory/write_layout.hpp \
  core/memory/write_layout.cpp \
  core/tests/write_layout_test.cpp \
  core/memory/credential_image.hpp \
  core/memory/credential_image.cpp \
  core/tests/credential_image_test.cpp \
  core/race/race_runtime.hpp \
  core/tests/race_runtime_test.cpp \
  core/contract/kmi_catalog.hpp \
  core/contract/kmi_catalog.cpp \
  core/tests/kmi_catalog_test.cpp \
  core/backend/cve_2026_43499/route/lifecycle.hpp \
  core/tests/route_lifecycle_test.cpp \
  core/profile/document_pairs.hpp \
  core/profile/document_pairs.cpp \
  core/tests/document_pairs_test.cpp \
  core/tests/real_document_test.cpp \
  ../profile-core/src/test/resources/payload-document-fixture.hex \
  core/backend/cve_2026_43499/abi.hpp \
  core/backend/cve_2026_43499/abi.cpp \
  core/tests/abi_offsets_test.cpp \
  core/backend/cve_2026_43499/route/expected_keys.hpp \
  core/tests/route_expected_keys_test.cpp \
  core/memory/multicast_arm.hpp \
  core/memory/multicast_arm.cpp \
  core/tests/multicast_arm_test.cpp \
  core/contract/chain_catalog.hpp \
  core/tests/chain_catalog_test.cpp \
  core/profile/queue_keys.hpp \
  core/tests/step_queue_test.cpp \
  core/profile/run_plan.hpp \
  core/profile/run_plan.cpp \
  core/tests/run_plan_test.cpp \
  core/script/lua_whitelist.hpp \
  core/script/sandbox.hpp

P4_TESTS := core/pipeline/tests/window_exit_paths_test.cpp core/pipeline/tests/plan_gate_test.cpp
EVIDENCE_TESTS := core/tests/a2_test.cpp core/tests/surface_test.cpp core/tests/surface_manifest_test.cpp core/tests/surface_sandbox_test.cpp core/tests/surface_stage_test.cpp core/tests/handle_generation_test.cpp core/tests/terminal_payload_test.cpp core/tests/terminal_store_test.cpp core/tests/terminal_execution_test.cpp core/tests/payload_keys_test.cpp core/tests/document_reader_test.cpp \
  core/tests/payload_manifest_test.cpp core/tests/step_catalog_test.cpp core/tests/write_request_test.cpp core/tests/write_encoder_test.cpp core/tests/write_layout_test.cpp core/tests/credential_image_test.cpp core/tests/race_runtime_test.cpp core/tests/kmi_catalog_test.cpp core/tests/route_lifecycle_test.cpp core/tests/document_pairs_test.cpp core/tests/real_document_test.cpp core/tests/abi_offsets_test.cpp core/tests/route_expected_keys_test.cpp core/tests/multicast_arm_test.cpp core/tests/chain_catalog_test.cpp core/tests/step_queue_test.cpp core/tests/run_plan_test.cpp
EVIDENCE_BINS := $(BUILD_DIR)/a2_test $(BUILD_DIR)/surface_test $(BUILD_DIR)/surface_manifest_test $(BUILD_DIR)/surface_sandbox_test $(BUILD_DIR)/surface_stage_test $(BUILD_DIR)/handle_generation_test $(BUILD_DIR)/terminal_payload_test $(BUILD_DIR)/terminal_store_test $(BUILD_DIR)/terminal_execution_test $(BUILD_DIR)/payload_keys_test $(BUILD_DIR)/document_reader_test $(BUILD_DIR)/payload_manifest_test $(BUILD_DIR)/step_catalog_test $(BUILD_DIR)/write_request_test $(BUILD_DIR)/write_encoder_test $(BUILD_DIR)/write_layout_test $(BUILD_DIR)/credential_image_test $(BUILD_DIR)/race_runtime_test $(BUILD_DIR)/kmi_catalog_test $(BUILD_DIR)/route_lifecycle_test $(BUILD_DIR)/document_pairs_test $(BUILD_DIR)/real_document_test $(BUILD_DIR)/abi_offsets_test $(BUILD_DIR)/route_expected_keys_test $(BUILD_DIR)/multicast_arm_test $(BUILD_DIR)/chain_catalog_test $(BUILD_DIR)/step_queue_test $(BUILD_DIR)/run_plan_test
P4_BINS := $(BUILD_DIR)/window_exit_paths_test $(BUILD_DIR)/plan_gate_test
ORDER_TOKENS := sort|order|sequenc|index|next|step|schedul|priorit|rank

.PHONY: all host check mpack dangling-check clean
all: host

host: $(BUILD_DIR)/ghostlock-skeleton
	@echo "host: running the skeleton"
	@$(BUILD_DIR)/ghostlock-skeleton

$(BUILD_DIR)/ghostlock-skeleton: $(RUNTIME_SRCS) $(LAYER_HEADERS) $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(RUNTIME_SRCS) $(BUILD_DIR)/mpack.o -o $@

# The device binary: the same entry, cross-built for arm64 with the NDK that every
# Android build already needs. NDK_CC overrides the wrapper when one is not on the
# default path. Only the layers above are compiled; the executor and the script
# layer are named in the entry's refusal.
NDK_TOOLCHAIN ?= $(HOME)/Library/Android/sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/darwin-x86_64/bin
NDK_CXX ?= $(NDK_TOOLCHAIN)/aarch64-linux-android29-clang++
ANDROID_OUT := ../build/native/ghostlock
.PHONY: android plan-check futex-probe requeue-probe

android: $(ANDROID_OUT)

$(ANDROID_OUT): $(RUNTIME_SRCS) core/contract/layer_scope.hpp core/contract/step_catalog.hpp \
  core/profile/run_plan.hpp core/profile/glkv3_document.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_TOOLCHAIN)/aarch64-linux-android29-clang -std=c99 -w -O2 -c $(MPACK_SRC) \
	  -o $(BUILD_DIR)/mpack-android.o
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Icore -isystem $(MPACK_DIR) \
	  $(RUNTIME_SRCS) $(BUILD_DIR)/mpack-android.o -o $@
	@echo "android: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

# The device probe for the futex seam: one real priority-inheritance round trip.
# Its evidence is a transcript from a device, which is why it is a separate
# binary rather than part of host: the host has no futex to call.
FUTEX_PROBE := ../build/native/futex_probe
futex-probe: $(FUTEX_PROBE)

# The device probe for the handoff primitive: the race's reason for existing.
REQUEUE_PROBE := ../build/native/requeue_probe
requeue-probe: $(REQUEUE_PROBE)

$(REQUEUE_PROBE): core/tools/requeue_probe.cpp core/race/futex_seam_linux.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Icore -pthread $< -o $@
	@echo "requeue-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(FUTEX_PROBE): core/tools/futex_probe.cpp core/race/futex_seam_linux.hpp \
  core/race/pi_race.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Icore -pthread $< -o $@
	@echo "futex-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

# The host entry, fed the document the device produced: the plan it reports is
# the evidence that the document path works end to end here.
plan-check: $(BUILD_DIR)/ghostlock-skeleton ../profile-core/src/test/resources/device-document-fixture.hex
	@python3 -c "import sys;h=open('../profile-core/src/test/resources/device-document-fixture.hex').read().strip();b=bytes.fromhex(h);sys.stdout.buffer.write(len(b).to_bytes(4,'big')+b)" > $(BUILD_DIR)/plan-check.stdin
	@$(BUILD_DIR)/ghostlock-skeleton --ghostlock-app-call < $(BUILD_DIR)/plan-check.stdin > $(BUILD_DIR)/plan-check.out 2>&1 || true
	@grep -q 'plan chain=mcast_rootchild steps=w1,w2,w3 source=queue' $(BUILD_DIR)/plan-check.out \
	  && echo "plan-check: PASS ($$(head -1 $(BUILD_DIR)/plan-check.out))" \
	  || { echo "plan-check: FAIL"; cat $(BUILD_DIR)/plan-check.out; exit 1; }
	@grep -q 'race main=0 consumer=1 settle=50000 poll=1000 rounds=300000 ok' $(BUILD_DIR)/plan-check.out \
	  && echo "plan-check: the profile's own race settings are read and coherent" \
	  || { echo "plan-check: FAIL: the race settings did not come out of the document"; cat $(BUILD_DIR)/plan-check.out; exit 1; }
	@grep -q 'executor-unavailable' $(BUILD_DIR)/plan-check.out \
	  && echo "plan-check: the run is refused, as this tree must" \
	  || { echo "plan-check: FAIL: no refusal was reported"; exit 1; }


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
	$(CXX) $(CXXFLAGS) -DSURFACE_MANIFEST_PIN='"$(SURFACE_MANIFEST_PIN)"' core/tests/surface_manifest_test.cpp -o $@

$(BUILD_DIR)/run_plan_test: core/tests/run_plan_test.cpp \
  core/profile/run_plan.hpp core/profile/run_plan.cpp \
  core/profile/glkv3_document.hpp core/profile/glkv3_document.cpp \
  core/profile/payload_keys.hpp core/profile/payload_keys.cpp \
  core/profile/queue_keys.hpp core/contract/chain_catalog.hpp \
  core/contract/step_catalog.hpp core/support/sha256.hpp core/support/sha256.cpp \
  ../profile-core/src/test/resources/device-document-fixture.hex $(BUILD_DIR)/mpack.o \
  | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/run_plan_test.cpp core/profile/run_plan.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/step_queue_test: core/tests/step_queue_test.cpp \
  core/profile/glkv3_document.hpp core/profile/glkv3_document.cpp \
  core/profile/payload_keys.hpp core/profile/payload_keys.cpp \
  core/profile/queue_keys.hpp core/contract/step_catalog.hpp \
  core/support/sha256.hpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/step_queue_test.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/chain_catalog_test: core/tests/chain_catalog_test.cpp \
  core/contract/chain_catalog.hpp core/contract/step_catalog.hpp \
  core/contract/kind.hpp core/profile/route_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/chain_catalog_test.cpp -o $@

$(BUILD_DIR)/multicast_arm_test: core/tests/multicast_arm_test.cpp \
  core/memory/multicast_arm.hpp core/memory/multicast_arm.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/multicast_arm_test.cpp core/memory/multicast_arm.cpp -o $@

$(BUILD_DIR)/route_expected_keys_test: core/tests/route_expected_keys_test.cpp \
  core/backend/cve_2026_43499/route/expected_keys.hpp core/profile/route_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/route_expected_keys_test.cpp -o $@

$(BUILD_DIR)/abi_offsets_test: core/tests/abi_offsets_test.cpp \
  core/backend/cve_2026_43499/abi.hpp core/backend/cve_2026_43499/abi.cpp \
  core/memory/target_profile.hpp core/profile/document_pairs.hpp \
  core/profile/document_pairs.cpp core/profile/glkv3_document.hpp \
  core/profile/glkv3_document.cpp core/profile/payload_keys.hpp \
  core/profile/payload_keys.cpp core/support/sha256.hpp core/support/sha256.cpp \
  $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/abi_offsets_test.cpp core/backend/cve_2026_43499/abi.cpp core/profile/document_pairs.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/real_document_test: core/tests/real_document_test.cpp \
  core/profile/glkv3_document.hpp core/profile/glkv3_document.cpp \
  core/profile/payload_keys.hpp core/profile/payload_keys.cpp \
  core/support/sha256.hpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o \
  ../profile-core/src/test/resources/payload-document-fixture.hex | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/real_document_test.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/document_pairs_test: core/tests/document_pairs_test.cpp \
  core/profile/document_pairs.hpp core/profile/document_pairs.cpp \
  core/profile/glkv3_document.hpp core/profile/glkv3_document.cpp \
  core/memory/target_profile.hpp $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/document_pairs_test.cpp core/profile/document_pairs.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/route_lifecycle_test: core/tests/route_lifecycle_test.cpp \
  core/backend/cve_2026_43499/route/lifecycle.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/route_lifecycle_test.cpp -o $@

$(BUILD_DIR)/kmi_catalog_test: core/tests/kmi_catalog_test.cpp \
  core/contract/kmi_catalog.hpp core/contract/kmi_catalog.cpp \
  ../profile-core/src/main/resources/lkm-kmi-manifest.tsv | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DKMI_MANIFEST='"$(KMI_MANIFEST)"' core/tests/kmi_catalog_test.cpp core/contract/kmi_catalog.cpp -o $@

KMI_MANIFEST := ../profile-core/src/main/resources/lkm-kmi-manifest.tsv

$(BUILD_DIR)/race_runtime_test: core/tests/race_runtime_test.cpp \
  core/race/race_runtime.hpp core/memory/target_profile.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/race_runtime_test.cpp -o $@

$(BUILD_DIR)/credential_image_test: core/tests/credential_image_test.cpp \
  core/memory/credential_image.hpp core/memory/credential_image.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/credential_image_test.cpp core/memory/credential_image.cpp -o $@

$(BUILD_DIR)/write_layout_test: core/tests/write_layout_test.cpp \
  core/memory/write_layout.hpp core/memory/write_layout.cpp \
  core/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/write_layout_test.cpp core/memory/write_layout.cpp -o $@

$(BUILD_DIR)/write_encoder_test: core/tests/write_encoder_test.cpp \
  core/memory/write_encoder.hpp core/memory/write_encoder.cpp \
  core/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/write_encoder_test.cpp core/memory/write_encoder.cpp -o $@

$(BUILD_DIR)/write_request_test: core/tests/write_request_test.cpp \
  core/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/write_request_test.cpp -o $@

$(BUILD_DIR)/step_catalog_test: core/tests/step_catalog_test.cpp \
  core/contract/step_catalog.hpp core/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/step_catalog_test.cpp -o $@

$(BUILD_DIR)/payload_manifest_test: core/tests/payload_manifest_test.cpp \
  core/profile/payload_keys.hpp core/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DPAYLOAD_MANIFEST_PIN='"$(PAYLOAD_MANIFEST_PIN)"' core/tests/payload_manifest_test.cpp -o $@

# The payload manifest: the lists the other end must not hand-copy (the segment,
# the selectable terminals, the four keys, their kinds, the suffix bound). Same
# pattern as the other manifests: the exporter's stdout is the artifact, and its
# bare run compares that text byte for byte with the frozen pin.
PAYLOAD_MANIFEST := $(BUILD_DIR)/payload-manifest.tsv
PAYLOAD_MANIFEST_PIN := core/tests/payload-manifest.pin.tsv
.PHONY: payload-manifest payload-manifest-regen

$(PAYLOAD_MANIFEST): $(BUILD_DIR)/payload_manifest_test $(PAYLOAD_MANIFEST_PIN)
	@$(BUILD_DIR)/payload_manifest_test > $@
	@echo "payload-manifest: $@ ($$(wc -l < $@ | tr -d ' ') lines)"

payload-manifest: $(PAYLOAD_MANIFEST)

# Explicit only: prints the difference for review, then writes the pin.
payload-manifest-regen: $(BUILD_DIR)/payload_manifest_test
	@tmp=$$(mktemp "$${TMPDIR:-/tmp}/payload-manifest.XXXXXX"); \
	$(BUILD_DIR)/payload_manifest_test --export-only > $$tmp; \
	echo "payload-manifest-regen: diff (pin -> export)"; \
	diff -u $(PAYLOAD_MANIFEST_PIN) $$tmp || true; \
	cp $$tmp $(PAYLOAD_MANIFEST_PIN); \
	rm -f $$tmp; \
	echo "payload-manifest-regen: pin updated -- review the diff above and commit it with its cause"

# The surface manifest (same pattern as combination/vocabulary/stepset manifests):
# stdout is exactly the TSV, so it can be redirected into an artifact; the
# bare-run assertion inside the exporter compares it with the frozen pin.
SURFACE_MANIFEST := $(BUILD_DIR)/surface-manifest.tsv
SURFACE_MANIFEST_PIN := core/tests/surface-manifest.pin.tsv
.PHONY: surface-manifest surface-manifest-regen surface-doc-check

# The artifact is the exporter's stdout; the exporter itself asserts the pin
# (byte equality) and the table invariants, so a mismatch fails here. There is
# deliberately NO second row-count check: the pin comparison is the single gate,
# and an empty artifact cannot match a 19-line pin.
$(SURFACE_MANIFEST): $(BUILD_DIR)/surface_manifest_test $(SURFACE_MANIFEST_PIN)
	@$(BUILD_DIR)/surface_manifest_test > $@
	@echo "surface-manifest: $@ ($$(wc -l < $@ | tr -d ' ') lines)"

# Depends on the artifact rule so the manifest is (re)generated when missing.
surface-manifest: $(SURFACE_MANIFEST)

# Regeneration path -- EXPLICIT ONLY. Never a prerequisite of check/host/all.
# It prints the diff for human review and then writes the new pin:
#   the pin is a frozen copy that a human approved, so after running this the
#   diff MUST be reviewed and committed together with the change that caused it.
surface-manifest-regen: $(BUILD_DIR)/surface_manifest_test
	@tmp=$$(mktemp "$${TMPDIR:-/tmp}/surface-manifest.XXXXXX"); \
	$(BUILD_DIR)/surface_manifest_test --export-only > $$tmp; \
	echo "surface-manifest-regen: diff (pin -> export)"; \
	if diff -u $(SURFACE_MANIFEST_PIN) $$tmp; then \
	  echo "surface-manifest-regen: no change; pin left untouched"; \
	else \
	  cp $$tmp $(SURFACE_MANIFEST_PIN); \
	  echo "surface-manifest-regen: pin written to $(SURFACE_MANIFEST_PIN) -- review the diff above"; \
	fi; \
	rm -f $$tmp

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
	@echo "check: Lua sandbox (whitelist assembly + four budgets)"
	@$(BUILD_DIR)/surface_sandbox_test
	@echo "check: staged plan (one run recorded, deterministic, bounded)"
	@$(BUILD_DIR)/surface_stage_test
	@echo "check: typed handles (generations, conflicts, bound)"
	@$(BUILD_DIR)/handle_generation_test
	@echo "check: finishing payload (digest + declared bytes)"
	@$(BUILD_DIR)/terminal_payload_test
	@echo "check: payload store (atomic, 0600)"
	@$(BUILD_DIR)/terminal_store_test
	@echo "check: declarative finishing (on_success, always, on_failure)"
	@$(BUILD_DIR)/terminal_execution_test
	@echo "check: payload key family (terminal-bound names)"
	@$(BUILD_DIR)/payload_keys_test
	@echo "check: GLKv3 document (schema first, payload family read)"
	@$(BUILD_DIR)/document_reader_test
	@echo "check: payload manifest (the other end's lists, exported)"
	@$(BUILD_DIR)/payload_manifest_test
	@echo "check: step catalogue (tokens + declared orders)"
	@$(BUILD_DIR)/step_catalog_test
	@echo "check: write request (pinned shape + own invariants)"
	@$(BUILD_DIR)/write_request_test
	@echo "check: write encoder (arm rules + bounded write)"
	@$(BUILD_DIR)/write_encoder_test
	@echo "check: write layout (value, parent word and the page rule)"
	@$(BUILD_DIR)/write_layout_test
	@echo "check: credential image (the copy a credential write installs)"
	@$(BUILD_DIR)/credential_image_test
	@echo "check: race runtime (the settings an attempt needs)"
	@$(BUILD_DIR)/race_runtime_test
	@echo "check: kmi catalogue (the labels, and the release that wants one)"
	@$(BUILD_DIR)/kmi_catalog_test
	@echo "check: route lifecycle (prepare, execute, disarm, destroy)"
	@$(BUILD_DIR)/route_lifecycle_test
	@echo "check: document pairs (segment to the memory read path)"
	@$(BUILD_DIR)/document_pairs_test
	@echo "check: real document (bytes written by the other end)"
	@$(BUILD_DIR)/real_document_test
	@echo "check: abi offsets (typed task fields from pairs)"
	@$(BUILD_DIR)/abi_offsets_test
	@echo "check: route expected keys (what each route asks the document for)"
	@$(BUILD_DIR)/route_expected_keys_test
	@echo "check: multicast arm (geometry checked before the words land)"
	@$(BUILD_DIR)/multicast_arm_test
	@echo "check: chain catalogue (selections resolve to a wired chain)"
	@$(BUILD_DIR)/chain_catalog_test
	@echo "check: step queue (a backend's selected steps, in order)"
	@$(BUILD_DIR)/step_queue_test
	@echo "check: run plan (a document resolves to a chain and its steps)"
	@$(BUILD_DIR)/run_plan_test
	@$(MAKE) --no-print-directory plan-check
	@win=$$(grep -cE '\{"window\.' core/contract/script_surface.hpp || true); \
	if [ "$$win" -ne 0 ]; then echo "SURFACE-WINDOW-ROW: $$win"; grep -nE '\{"window\.' core/contract/script_surface.hpp; exit 1; fi; \
	echo "check: no window.* row in the exposure surface"
	@dup=$$(for f in $$(find core -type f \( -name '*.hpp' -o -name '*.cpp' \) ! -name 'script_surface.hpp'); do \
	  if tr -d '\n' < "$$f" | grep -qE 'backend.*writeop.*settle.*handoff'; then echo "$$f"; fi; \
	done | wc -l | tr -d ' '); \
	if [ "$$dup" -ne 0 ]; then echo "SURFACE-DUP: $$dup other file(s) restate the surface list"; exit 1; fi; \
	echo "check: surface list has a single definition site (the generated pin lives in a .tsv and is checked byte-for-byte)"
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

# --- A6-1: the Lua sandbox (implementation + its evidence test) -------------
# The sandbox TU is compiled together with its test so the check path really
# builds it; the test links the 22-file Lua core set from lib/lua/src.
SCRIPT_SRCS := core/script/sandbox.cpp

.PHONY: lua-core lua-core-size

$(BUILD_DIR)/surface_sandbox_test: core/tests/surface_sandbox_test.cpp $(SCRIPT_SRCS) \
  core/script/sandbox.hpp core/script/call_sink.hpp core/script/lua_whitelist.hpp \
  core/contract/script_surface.hpp $(LUA_CORE_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(LUA_DIR) core/tests/surface_sandbox_test.cpp $(SCRIPT_SRCS) $(LUA_CORE_LIB) -o $@

$(BUILD_DIR)/handle_generation_test: core/tests/handle_generation_test.cpp \
  core/script/handle_table.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/handle_generation_test.cpp -o $@

$(BUILD_DIR)/terminal_payload_test: core/tests/terminal_payload_test.cpp \
  core/terminal/payload.hpp core/support/sha256.hpp core/support/sha256.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/terminal_payload_test.cpp core/support/sha256.cpp -o $@

$(BUILD_DIR)/terminal_store_test: core/tests/terminal_store_test.cpp \
  core/terminal/payload_store.hpp core/terminal/payload_store.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/terminal_store_test.cpp core/terminal/payload_store.cpp -o $@

$(BUILD_DIR)/document_reader_test: core/tests/document_reader_test.cpp \
  core/profile/glkv3_document.hpp core/profile/glkv3_document.cpp \
  core/profile/payload_keys.hpp core/profile/payload_keys.cpp \
  core/support/sha256.hpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/document_reader_test.cpp core/profile/glkv3_document.cpp core/profile/payload_keys.cpp core/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/payload_keys_test: core/tests/payload_keys_test.cpp \
  core/profile/payload_keys.hpp core/profile/payload_keys.cpp core/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/payload_keys_test.cpp core/profile/payload_keys.cpp -o $@

$(BUILD_DIR)/terminal_execution_test: core/tests/terminal_execution_test.cpp \
  core/terminal/execution.hpp core/terminal/execution.cpp \
  core/terminal/payload_store.hpp core/terminal/payload_store.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) core/tests/terminal_execution_test.cpp core/terminal/execution.cpp core/terminal/payload_store.cpp -o $@

$(BUILD_DIR)/surface_stage_test: core/tests/surface_stage_test.cpp $(SCRIPT_SRCS) \
  core/script/staged_plan.hpp core/script/sandbox.hpp core/script/call_sink.hpp \
  core/contract/script_surface.hpp $(LUA_CORE_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(LUA_DIR) core/tests/surface_stage_test.cpp $(SCRIPT_SRCS) $(LUA_CORE_LIB) -o $@

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
	@missing=0; total=0; for p in $(SKELETON_SRCS) $(LAYER_HEADERS) $(P4_TESTS) $(EVIDENCE_TESTS) $(MPACK_SRC) $(MPACK_HDR) $(LUA_CORE_SRCS) $(SCRIPT_SRCS); do \
	  total=$$((total+1)); \
	  if [ ! -e "$$p" ]; then echo "DANGLING: $$p"; missing=$$((missing+1)); fi; \
	done; \
	echo "dangling-check: missing=$$missing total=$$total"; \
	[ "$$missing" -eq 0 ]

clean:
	rm -rf $(BUILD_DIR)
