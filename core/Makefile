# src/Makefile -- A0 skeleton build entry (rewrite-plan-v2 section 2).
#
# This file references ONLY the new tree (src/**) plus the vendored mpack under
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

CXXFLAGS := -std=c++23 -Wall -Wextra -Wconversion -Wsign-conversion -Isrc -isystem $(MPACK_DIR)
# Third-party isolation (lib/mpack/README.md): C99, its own flags, warnings off,
# so the project's zero-warning line above stays meaningful.
MPACK_CFLAGS := -std=c99 -w

# The runtime binary: the entry plus the layers it reaches. The script layer is
# not in it yet (its Lua core would have to be cross-built too), and neither is
# the executor: the entry resolves a document and says what it would run.
RUNTIME_SRCS := src/main.cpp src/contract/kmi_catalog.cpp src/contract/contract.cpp src/memory/memory.cpp \
  src/backend/cve_2026_43284/lkm/module_facts.cpp \
  src/profile/glkv3_document.cpp src/profile/payload_keys.cpp \
  src/profile/document_pairs.cpp src/profile/run_plan.cpp \
  src/profile/plan_delivery.cpp \
  src/support/sha256.cpp

SKELETON_SRCS := src/main.cpp
LAYER_HEADERS := \
  src/contract/layer.hpp \
  src/contract/layer_scope.hpp \
  src/contract/kind.hpp \
  src/contract/resource_scope.hpp \
  src/contract/op_registry.hpp \
  src/support/layer.hpp \
  src/support/neutrals.hpp \
  src/memory/layer.hpp \
  src/memory/target_profile.hpp \
  src/backend/cve_2026_43499/race/layer.hpp \
  src/backend/cve_2026_43499/race/pi_window.hpp \
  src/backend/cve_2026_43499/race/pi_race.hpp \
  src/backend/layer.hpp \
  src/pipeline/layer.hpp \
  src/pipeline/attack_window.hpp \
  src/pipeline/composition_root.hpp \
  src/pipeline/plan_gate.hpp \
  src/terminal/layer.hpp \
  src/script/layer.hpp \
  src/session/layer.hpp \
  src/profile/layer.hpp \
  src/profile/route_catalog.hpp \
  src/contract/script_surface.hpp \
  src/script/call_sink.hpp \
  src/script/staged_plan.hpp \
  src/script/stage_gate.hpp \
  src/script/handle_table.hpp \
  src/support/sha256.hpp \
  src/support/sha256.cpp \
  src/terminal/payload.hpp \
  src/terminal/payload_store.hpp \
  src/terminal/payload_store.cpp \
  src/terminal/execution.hpp \
  src/terminal/execution.cpp \
  src/profile/payload_keys.hpp \
  src/profile/payload_keys.cpp \
  src/profile/glkv3_document.hpp \
  src/profile/glkv3_document.cpp \
  src/tests/payload_manifest_test.cpp \
  src/tests/payload-manifest.pin.tsv \
  src/contract/step_catalog.hpp \
  src/tests/step_catalog_test.cpp \
  src/backend/cve_2026_43499/race/futex_seam_linux.hpp \
  src/tools/futex_probe.cpp \
  src/tools/requeue_probe.cpp \
  src/backend/cve_2026_43499/memory/heap_seam.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp \
  src/backend/cve_2026_43499/memory/heap_geometry.hpp \
  src/tools/heap_seam_probe.cpp \
  src/tools/prepare_page_probe.cpp \
  src/tools/snitch_seam_probe.cpp \
  src/tools/snitch_run_probe.cpp \
  src/tools/perf_seam_probe.cpp \
  src/tools/snitch_prepare_probe.cpp \
  src/tools/snitch_fork_probe.cpp \
  src/backend/cve_2026_43499/memory/write_request.hpp \
  src/tests/write_request_test.cpp \
  src/backend/cve_2026_43499/memory/write_encoder.hpp \
  src/backend/cve_2026_43499/memory/write_encoder.cpp \
  src/tests/write_encoder_test.cpp \
  src/backend/cve_2026_43499/memory/write_layout.hpp \
  src/backend/cve_2026_43499/memory/write_layout.cpp \
  src/tests/write_layout_test.cpp \
  src/backend/cve_2026_43499/memory/credential_image.hpp \
  src/backend/cve_2026_43499/memory/credential_image.cpp \
  src/tests/credential_image_test.cpp \
  src/backend/cve_2026_43499/race/race_runtime.hpp \
  src/tests/race_runtime_test.cpp \
  src/contract/kmi_catalog.hpp \
  src/contract/kmi_catalog.cpp \
  src/tests/kmi_catalog_test.cpp \
  src/memory/memory.hpp \
  src/memory/memory.cpp \
  src/contract/contract.hpp \
  src/contract/contract.cpp \
  src/session/device_facts.hpp \
  src/session/device_facts_linux.hpp \
  src/tests/device_facts_test.cpp \
  src/backend/cve_2026_43284/lkm/module_facts.hpp \
  src/backend/cve_2026_43284/lkm/module_facts.cpp \
  src/tests/module_facts_test.cpp \
  src/tests/staged_plan_test.cpp \
  src/backend/cve_2026_43499/route/lifecycle.hpp \
  src/tests/route_lifecycle_test.cpp \
  src/profile/document_pairs.hpp \
  src/profile/document_pairs.cpp \
  src/tests/document_pairs_test.cpp \
  src/tests/real_document_test.cpp \
  ../profile-core/src/test/resources/payload-document-fixture.hex \
  src/backend/cve_2026_43499/abi.hpp \
  src/backend/cve_2026_43499/abi.cpp \
  src/tests/abi_offsets_test.cpp \
  src/backend/cve_2026_43499/perf_ring.hpp \
  src/tests/perf_ring_test.cpp \
  src/backend/cve_2026_43499/perf_vote.hpp \
  src/tests/perf_vote_test.cpp \
  src/backend/cve_2026_43499/route/multicast_policy.hpp \
  src/tests/multicast_policy_test.cpp \
  src/contract/kernel_access.hpp \
  src/tests/kernel_access_test.cpp \
  src/backend/cve_2026_43499/session.hpp \
  src/backend/cve_2026_43499/access_params.hpp \
  src/backend/cve_2026_43499/seam/multicast_seam.hpp \
  src/backend/cve_2026_43284/access_params.hpp \
  src/backend/cve_2026_43284/seam/pagecache_seam.hpp \
  src/tests/pagecache_seam_test.cpp \
  src/tests/multicast_seam_test.cpp \
  src/profile/route_catalog.hpp \
  src/tests/target_state_test.cpp \
  src/contract/capability_catalog.hpp \
  src/backend/cve_2026_43499/capabilities.hpp \
  src/tests/capability_agreement_test.cpp \
  src/backend/cve_2026_43499/steps/step_runner.hpp \
  src/tests/step_runner_test.cpp \
  src/backend/cve_2026_43499/route/route_policy.hpp \
  src/tests/route_policy_test.cpp \
  src/backend/cve_2026_43499/route/route_status.hpp \
  src/tests/route_status_test.cpp \
  src/backend/cve_2026_43499/leak/collision_count.hpp \
  src/tests/collision_count_test.cpp \
  src/backend/cve_2026_43499/leak/collision_buckets.hpp \
  src/tests/collision_buckets_test.cpp \
  src/backend/cve_2026_43499/leak/discovery_result.hpp \
  src/tests/discovery_result_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_discovery.hpp \
  src/tests/snitch_discovery_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_policy.hpp \
  src/tests/snitch_policy_test.cpp \
  src/profile/key_catalog.hpp \
  src/profile/key_catalog.cpp \
  src/tools/profile_manifest_dump.cpp \
  src/backend/cve_2026_43499/leak/snitch_tuning.hpp \
  src/tests/snitch_tuning_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_child.hpp \
  src/tests/snitch_child_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_run.hpp \
  src/tests/snitch_run_test.cpp \
  src/backend/cve_2026_43499/leak/leak_search.hpp \
  src/tests/leak_search_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_scan.hpp \
  src/tests/snitch_scan_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_seam.hpp \
  src/backend/cve_2026_43499/leak/snitch_seam_linux.hpp \
  src/tests/snitch_seam_test.cpp \
  src/backend/cve_2026_43499/leak/collision_wait.hpp \
  src/tests/collision_wait_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_region.hpp \
  src/tests/snitch_region_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_setup.hpp \
  src/backend/cve_2026_43499/leak/snitch_setup.cpp \
  src/tests/snitch_setup_test.cpp \
  src/backend/cve_2026_43499/leak/leak_walk.hpp \
  src/tests/leak_walk_test.cpp \
  src/backend/cve_2026_43499/leak/futex_hash.hpp \
  src/tests/futex_hash_test.cpp \
  src/backend/cve_2026_43499/leak/candidate_match.hpp \
  src/tests/candidate_match_test.cpp \
  src/backend/cve_2026_43499/leak/scan_bounds.hpp \
  src/backend/cve_2026_43499/leak/scan_geometry.hpp \
  src/tests/scan_geometry_test.cpp \
  src/tests/scan_bounds_test.cpp \
  src/contract/address_discovery.hpp \
  src/tests/address_discovery_test.cpp \
  src/backend/cve_2026_43499/route/expected_keys.hpp \
  src/tests/route_expected_keys_test.cpp \
  src/backend/cve_2026_43499/memory/multicast_arm.hpp \
  src/backend/cve_2026_43499/memory/multicast_arm.cpp \
  src/tests/multicast_arm_test.cpp \
  src/contract/chain_catalog.hpp \
  src/tests/chain_catalog_test.cpp \
  src/profile/run_plan.hpp \
  src/profile/run_plan.cpp \
  src/tests/run_plan_test.cpp \
  src/profile/plan_delivery.hpp \
  src/profile/plan_delivery.cpp \
  src/tests/plan_delivery_test.cpp \
  src/script/lua_whitelist.hpp \
  src/script/plan_load.hpp \
  src/script/plan_load.cpp \
  src/tests/plan_load_test.cpp \
  src/backend/cve_2026_43499/memory/reclaim_plan.hpp \
  src/backend/cve_2026_43499/memory/reclaim_plan.cpp \
  src/tests/reclaim_plan_test.cpp \
  src/backend/cve_2026_43499/memory/prepare_steps.hpp \
  src/backend/cve_2026_43499/memory/page_geometry.hpp \
  src/backend/cve_2026_43499/memory/prepare_plan.hpp \
  src/backend/cve_2026_43499/memory/prepare_plan.cpp \
  src/tests/prepare_plan_test.cpp \
  src/backend/cve_2026_43499/memory/heap_geometry.hpp \
  src/tests/heap_geometry_test.cpp \
  src/backend/cve_2026_43499/memory/prepare_page.hpp \
  src/tests/prepare_page_test.cpp \
  src/backend/cve_2026_43499/memory/shaping_plan.hpp \
  src/backend/cve_2026_43499/memory/shaping_plan.cpp \
  src/tests/shaping_plan_test.cpp \
  src/tests/page_geometry_test.cpp \
  src/tests/prepare_steps_test.cpp \
  src/script/sandbox.hpp

P4_TESTS := src/pipeline/tests/window_exit_paths_test.cpp src/pipeline/tests/plan_gate_test.cpp
EVIDENCE_TESTS := src/tests/a2_test.cpp src/tests/surface_test.cpp src/tests/surface_manifest_test.cpp src/tests/surface_sandbox_test.cpp src/tests/surface_stage_test.cpp src/tests/handle_generation_test.cpp src/tests/terminal_payload_test.cpp src/tests/terminal_store_test.cpp src/tests/terminal_execution_test.cpp src/tests/payload_keys_test.cpp src/tests/document_reader_test.cpp \
  src/tests/payload_manifest_test.cpp src/tests/step_catalog_test.cpp src/tests/write_request_test.cpp src/tests/write_encoder_test.cpp src/tests/write_layout_test.cpp src/tests/credential_image_test.cpp src/tests/race_runtime_test.cpp src/tests/kmi_catalog_test.cpp src/tests/device_facts_test.cpp src/tests/module_facts_test.cpp src/tests/staged_plan_test.cpp src/tests/route_lifecycle_test.cpp src/tests/document_pairs_test.cpp src/tests/real_document_test.cpp src/tests/abi_offsets_test.cpp src/tests/route_expected_keys_test.cpp src/tests/multicast_arm_test.cpp src/tests/chain_catalog_test.cpp src/tests/run_plan_test.cpp
EVIDENCE_BINS := $(BUILD_DIR)/pagecache_seam_test $(BUILD_DIR)/multicast_seam_test $(BUILD_DIR)/cve43499_kernel_access_test $(BUILD_DIR)/kernel_access_test $(BUILD_DIR)/target_state_test $(BUILD_DIR)/capability_agreement_test $(BUILD_DIR)/step_runner_test $(BUILD_DIR)/combination_manifest_test $(BUILD_DIR)/stepset_steps_manifest_test $(BUILD_DIR)/a2_test $(BUILD_DIR)/prepare_steps_test $(BUILD_DIR)/page_geometry_test $(BUILD_DIR)/prepare_plan_test $(BUILD_DIR)/shaping_plan_test $(BUILD_DIR)/heap_geometry_test $(BUILD_DIR)/prepare_page_test $(BUILD_DIR)/perf_ring_test $(BUILD_DIR)/perf_vote_test $(BUILD_DIR)/multicast_policy_test $(BUILD_DIR)/route_policy_test $(BUILD_DIR)/route_status_test $(BUILD_DIR)/collision_count_test $(BUILD_DIR)/collision_buckets_test $(BUILD_DIR)/discovery_result_test $(BUILD_DIR)/snitch_discovery_test $(BUILD_DIR)/snitch_policy_test $(BUILD_DIR)/snitch_tuning_test $(BUILD_DIR)/snitch_child_test $(BUILD_DIR)/snitch_run_test $(BUILD_DIR)/leak_search_test $(BUILD_DIR)/snitch_scan_test $(BUILD_DIR)/snitch_seam_test $(BUILD_DIR)/collision_wait_test $(BUILD_DIR)/snitch_region_test $(BUILD_DIR)/snitch_setup_test $(BUILD_DIR)/leak_walk_test $(BUILD_DIR)/futex_hash_test $(BUILD_DIR)/candidate_match_test $(BUILD_DIR)/scan_geometry_test $(BUILD_DIR)/scan_bounds_test $(BUILD_DIR)/address_discovery_test $(BUILD_DIR)/reclaim_plan_test $(BUILD_DIR)/plan_load_test $(BUILD_DIR)/plan_delivery_test $(BUILD_DIR)/surface_test $(BUILD_DIR)/surface_manifest_test $(BUILD_DIR)/surface_sandbox_test $(BUILD_DIR)/surface_stage_test $(BUILD_DIR)/handle_generation_test $(BUILD_DIR)/terminal_payload_test $(BUILD_DIR)/terminal_store_test $(BUILD_DIR)/terminal_execution_test $(BUILD_DIR)/payload_keys_test $(BUILD_DIR)/document_reader_test $(BUILD_DIR)/payload_manifest_test $(BUILD_DIR)/step_catalog_test $(BUILD_DIR)/write_request_test $(BUILD_DIR)/write_encoder_test $(BUILD_DIR)/write_layout_test $(BUILD_DIR)/credential_image_test $(BUILD_DIR)/race_runtime_test $(BUILD_DIR)/kmi_catalog_test $(BUILD_DIR)/device_facts_test $(BUILD_DIR)/module_facts_test $(BUILD_DIR)/staged_plan_test $(BUILD_DIR)/route_lifecycle_test $(BUILD_DIR)/document_pairs_test $(BUILD_DIR)/real_document_test $(BUILD_DIR)/abi_offsets_test $(BUILD_DIR)/route_expected_keys_test $(BUILD_DIR)/multicast_arm_test $(BUILD_DIR)/chain_catalog_test $(BUILD_DIR)/run_plan_test
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
.PHONY: android plan-check futex-probe requeue-probe heap-seam-probe prepare-page-probe snitch-seam-probe snitch-run-probe snitch-fork-probe perf-seam-probe snitch-prepare-probe profile-manifest-v3 profile-manifest-v3-check

android: $(ANDROID_OUT)

$(ANDROID_OUT): $(RUNTIME_SRCS) src/contract/layer_scope.hpp src/contract/step_catalog.hpp \
  src/profile/run_plan.hpp src/profile/glkv3_document.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_TOOLCHAIN)/aarch64-linux-android29-clang -std=c99 -w -O2 -c $(MPACK_SRC) \
	  -o $(BUILD_DIR)/mpack-android.o
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc -isystem $(MPACK_DIR) \
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

# The device probe for the heap seam: every platform call it makes, once.
HEAP_SEAM_PROBE := ../build/native/heap_seam_probe
heap-seam-probe: $(HEAP_SEAM_PROBE)

# The device probe for the orchestration: the phases, over the real seam, with the
# search stubbed because that mechanism is not ported yet.
PREPARE_PAGE_PROBE := ../build/native/prepare_page_probe
prepare-page-probe: $(PREPARE_PAGE_PROBE)

# The device probe for the collision search edge: the four calls it needs, once each.
SNITCH_SEAM_PROBE := ../build/native/snitch_seam_probe
snitch-seam-probe: $(SNITCH_SEAM_PROBE)

# The device probe for the whole search: real forks, real pokes, real hashing.
SNITCH_RUN_PROBE := ../build/native/snitch_run_probe
snitch-run-probe: $(SNITCH_RUN_PROBE)

# Diagnostic: does a forked child run, and do its writes land in the shared mapping.
SNITCH_FORK_PROBE := ../build/native/snitch_fork_probe
snitch-fork-probe: $(SNITCH_FORK_PROBE)

# The device probe for the sampling primitive: open, map, sample, and read the ring.
PERF_SEAM_PROBE := ../build/native/perf_seam_probe
perf-seam-probe: $(PERF_SEAM_PROBE)

# The device probe for the join: find an address, then prepare a page with it.
SNITCH_PREPARE_PROBE := ../build/native/snitch_prepare_probe
snitch-prepare-probe: $(SNITCH_PREPARE_PROBE)

$(SNITCH_PREPARE_PROBE): src/tools/snitch_prepare_probe.cpp src/backend/cve_2026_43499/leak/snitch_policy.hpp \
  src/backend/cve_2026_43499/memory/prepare_page.hpp src/backend/cve_2026_43499/memory/prepare_plan.cpp \
  src/backend/cve_2026_43499/memory/shaping_plan.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp \
  src/backend/cve_2026_43499/leak/snitch_setup.cpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< src/backend/cve_2026_43499/memory/prepare_plan.cpp src/backend/cve_2026_43499/memory/shaping_plan.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp src/backend/cve_2026_43499/leak/snitch_setup.cpp -o $@
	@echo "snitch-prepare-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(PERF_SEAM_PROBE): src/tools/perf_seam_probe.cpp src/backend/cve_2026_43499/perf_ring.hpp \
  src/backend/cve_2026_43499/perf_seam_linux.hpp src/backend/cve_2026_43499/perf_vote.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< -o $@
	@echo "perf-seam-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(SNITCH_FORK_PROBE): src/tools/snitch_fork_probe.cpp src/backend/cve_2026_43499/leak/snitch_seam_linux.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< -o $@
	@echo "snitch-fork-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(SNITCH_RUN_PROBE): src/tools/snitch_run_probe.cpp src/backend/cve_2026_43499/leak/snitch_run.hpp \
  src/backend/cve_2026_43499/leak/snitch_seam_linux.hpp src/backend/cve_2026_43499/leak/futex_hash.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp src/backend/cve_2026_43499/leak/snitch_setup.cpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< src/backend/cve_2026_43499/leak/snitch_setup.cpp -o $@
	@echo "snitch-run-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(SNITCH_SEAM_PROBE): src/tools/snitch_seam_probe.cpp src/backend/cve_2026_43499/leak/snitch_seam.hpp \
  src/backend/cve_2026_43499/leak/snitch_seam_linux.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< -o $@
	@echo "snitch-seam-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(PREPARE_PAGE_PROBE): src/tools/prepare_page_probe.cpp src/backend/cve_2026_43499/memory/prepare_page.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp src/backend/cve_2026_43499/memory/prepare_plan.cpp \
  src/backend/cve_2026_43499/memory/shaping_plan.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< src/backend/cve_2026_43499/memory/prepare_plan.cpp src/backend/cve_2026_43499/memory/shaping_plan.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp -o $@
	@echo "prepare-page-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(HEAP_SEAM_PROBE): src/tools/heap_seam_probe.cpp src/backend/cve_2026_43499/memory/heap_seam.hpp \
  src/backend/cve_2026_43499/memory/heap_seam_linux.hpp src/backend/cve_2026_43499/memory/heap_geometry.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc $< -o $@
	@echo "heap-seam-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(REQUEUE_PROBE): src/tools/requeue_probe.cpp src/backend/cve_2026_43499/race/futex_seam_linux.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc -pthread $< -o $@
	@echo "requeue-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

$(FUTEX_PROBE): src/tools/futex_probe.cpp src/backend/cve_2026_43499/race/futex_seam_linux.hpp \
  src/backend/cve_2026_43499/race/pi_race.hpp | $(BUILD_DIR)
	@mkdir -p ../build/native
	$(NDK_CXX) -std=c++23 -O2 -fno-rtti -static-libstdc++ -Isrc -pthread $< -o $@
	@echo "futex-probe: $@ ($$(wc -c < $@ | tr -d ' ') bytes)"

# The host entry, fed the document the device produced: the plan it reports is
# the evidence that the document path works end to end here.
plan-check: $(BUILD_DIR)/ghostlock-skeleton ../profile-core/src/test/resources/device-document-fixture.hex
	@python3 -c "import sys;h=open('../profile-core/src/test/resources/device-document-fixture.hex').read().strip();b=bytes.fromhex(h);sys.stdout.buffer.write(len(b).to_bytes(4,'big')+b)" > $(BUILD_DIR)/plan-check.stdin
	@$(BUILD_DIR)/ghostlock-skeleton --ghostlock-app-call < $(BUILD_DIR)/plan-check.stdin > $(BUILD_DIR)/plan-check.out 2>&1 || true
	@if grep -q queue-removed $(BUILD_DIR)/plan-check.out; then echo plan-check: PASS - the device document is refused for the queue it still carries; else echo plan-check: FAIL; cat $(BUILD_DIR)/plan-check.out; exit 1; fi


$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/window_exit_paths_test: src/pipeline/tests/window_exit_paths_test.cpp src/pipeline/attack_window.hpp src/pipeline/composition_root.hpp src/contract/resource_scope.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/pipeline/tests/window_exit_paths_test.cpp -o $@

$(BUILD_DIR)/plan_gate_test: src/pipeline/tests/plan_gate_test.cpp src/pipeline/plan_gate.hpp src/contract/op_registry.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/pipeline/tests/plan_gate_test.cpp -o $@

$(BUILD_DIR)/a2_test: src/tests/a2_test.cpp src/backend/cve_2026_43499/race/pi_window.hpp src/backend/cve_2026_43499/race/pi_race.hpp src/memory/target_profile.hpp src/support/neutrals.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/a2_test.cpp -o $@

$(BUILD_DIR)/surface_test: src/tests/surface_test.cpp src/contract/script_surface.hpp src/contract/layer_scope.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/surface_test.cpp -o $@

$(BUILD_DIR)/surface_manifest_test: src/tests/surface_manifest_test.cpp src/contract/script_surface.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSURFACE_MANIFEST_PIN='"$(SURFACE_MANIFEST_PIN)"' src/tests/surface_manifest_test.cpp -o $@

$(BUILD_DIR)/run_plan_test: src/tests/run_plan_test.cpp \
  src/profile/run_plan.hpp src/profile/run_plan.cpp \
  src/profile/glkv3_document.hpp src/profile/glkv3_document.cpp \
  src/profile/payload_keys.hpp src/profile/payload_keys.cpp \
  src/contract/chain_catalog.hpp \
  src/contract/step_catalog.hpp src/support/sha256.hpp src/support/sha256.cpp \
  ../profile-core/src/test/resources/device-document-fixture.hex $(BUILD_DIR)/mpack.o \
  | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/run_plan_test.cpp src/profile/run_plan.cpp src/profile/plan_delivery.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/plan_delivery_test: src/tests/plan_delivery_test.cpp \
  src/profile/plan_delivery.hpp src/profile/plan_delivery.cpp \
  src/profile/glkv3_document.hpp src/profile/glkv3_document.cpp \
  src/profile/payload_keys.hpp src/profile/payload_keys.cpp \
  src/contract/script_surface.hpp src/support/sha256.hpp src/support/sha256.cpp \
  $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/plan_delivery_test.cpp src/profile/plan_delivery.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/chain_catalog_test: src/tests/chain_catalog_test.cpp \
  src/contract/chain_catalog.hpp src/contract/step_catalog.hpp \
  src/contract/kind.hpp src/profile/route_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/chain_catalog_test.cpp -o $@

$(BUILD_DIR)/multicast_arm_test: src/tests/multicast_arm_test.cpp \
  src/backend/cve_2026_43499/memory/multicast_arm.hpp src/backend/cve_2026_43499/memory/multicast_arm.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/multicast_arm_test.cpp src/backend/cve_2026_43499/memory/multicast_arm.cpp -o $@

$(BUILD_DIR)/route_expected_keys_test: src/tests/route_expected_keys_test.cpp \
  src/backend/cve_2026_43499/route/expected_keys.hpp src/profile/route_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/route_expected_keys_test.cpp -o $@

$(BUILD_DIR)/abi_offsets_test: src/tests/abi_offsets_test.cpp \
  src/backend/cve_2026_43499/abi.hpp src/backend/cve_2026_43499/abi.cpp \
  src/memory/target_profile.hpp src/profile/document_pairs.hpp \
  src/profile/document_pairs.cpp src/profile/glkv3_document.hpp \
  src/profile/glkv3_document.cpp src/profile/payload_keys.hpp \
  src/profile/payload_keys.cpp src/support/sha256.hpp src/support/sha256.cpp \
  $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/abi_offsets_test.cpp src/backend/cve_2026_43499/abi.cpp src/profile/document_pairs.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/real_document_test: src/tests/real_document_test.cpp \
  src/profile/glkv3_document.hpp src/profile/glkv3_document.cpp \
  src/profile/payload_keys.hpp src/profile/payload_keys.cpp \
  src/support/sha256.hpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o \
  ../profile-core/src/test/resources/payload-document-fixture.hex | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/real_document_test.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/document_pairs_test: src/tests/document_pairs_test.cpp \
  src/profile/document_pairs.hpp src/profile/document_pairs.cpp \
  src/profile/glkv3_document.hpp src/profile/glkv3_document.cpp \
  src/memory/target_profile.hpp $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/document_pairs_test.cpp src/profile/document_pairs.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/route_lifecycle_test: src/tests/route_lifecycle_test.cpp \
  src/backend/cve_2026_43499/route/lifecycle.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/route_lifecycle_test.cpp -o $@

$(BUILD_DIR)/staged_plan_test: src/tests/staged_plan_test.cpp \
  src/script/staged_plan.hpp src/script/call_sink.hpp src/contract/script_surface.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/staged_plan_test.cpp -o $@

$(BUILD_DIR)/module_facts_test: src/tests/module_facts_test.cpp \
  src/backend/cve_2026_43284/lkm/module_facts.hpp \
  src/backend/cve_2026_43284/lkm/module_facts.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/module_facts_test.cpp src/backend/cve_2026_43284/lkm/module_facts.cpp -o $@

$(BUILD_DIR)/device_facts_test: src/tests/device_facts_test.cpp \
  src/session/device_facts.hpp src/contract/kmi_catalog.hpp \
  src/contract/kmi_catalog.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/device_facts_test.cpp src/contract/kmi_catalog.cpp -o $@

$(BUILD_DIR)/kmi_catalog_test: src/tests/kmi_catalog_test.cpp \
  src/contract/kmi_catalog.hpp src/contract/kmi_catalog.cpp \
  ../profile-core/src/main/resources/lkm-kmi-manifest.tsv | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DKMI_MANIFEST='"$(KMI_MANIFEST)"' src/tests/kmi_catalog_test.cpp src/contract/kmi_catalog.cpp -o $@

KMI_MANIFEST := ../profile-core/src/main/resources/lkm-kmi-manifest.tsv

$(BUILD_DIR)/race_runtime_test: src/tests/race_runtime_test.cpp \
  src/backend/cve_2026_43499/race/race_runtime.hpp src/memory/target_profile.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/race_runtime_test.cpp -o $@

$(BUILD_DIR)/credential_image_test: src/tests/credential_image_test.cpp \
  src/backend/cve_2026_43499/memory/credential_image.hpp src/backend/cve_2026_43499/memory/credential_image.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/credential_image_test.cpp src/backend/cve_2026_43499/memory/credential_image.cpp -o $@

$(BUILD_DIR)/write_layout_test: src/tests/write_layout_test.cpp \
  src/backend/cve_2026_43499/memory/write_layout.hpp src/backend/cve_2026_43499/memory/write_layout.cpp \
  src/backend/cve_2026_43499/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/write_layout_test.cpp src/backend/cve_2026_43499/memory/write_layout.cpp -o $@

$(BUILD_DIR)/write_encoder_test: src/tests/write_encoder_test.cpp \
  src/backend/cve_2026_43499/memory/write_encoder.hpp src/backend/cve_2026_43499/memory/write_encoder.cpp \
  src/backend/cve_2026_43499/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/write_encoder_test.cpp src/backend/cve_2026_43499/memory/write_encoder.cpp -o $@

$(BUILD_DIR)/write_request_test: src/tests/write_request_test.cpp \
  src/backend/cve_2026_43499/memory/write_request.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/write_request_test.cpp -o $@

$(BUILD_DIR)/step_catalog_test: src/tests/step_catalog_test.cpp \
  src/contract/step_catalog.hpp src/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/step_catalog_test.cpp -o $@

$(BUILD_DIR)/payload_manifest_test: src/tests/payload_manifest_test.cpp \
  src/profile/payload_keys.hpp src/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DPAYLOAD_MANIFEST_PIN='"$(PAYLOAD_MANIFEST_PIN)"' src/tests/payload_manifest_test.cpp -o $@

# The payload manifest: the lists the other end must not hand-copy (the segment,
# the selectable terminals, the four keys, their kinds, the suffix bound). Same
# pattern as the other manifests: the exporter's stdout is the artifact, and its
# bare run compares that text byte for byte with the frozen pin.
PAYLOAD_MANIFEST := $(BUILD_DIR)/payload-manifest.tsv
PAYLOAD_MANIFEST_PIN := src/tests/payload-manifest.pin.tsv
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

# The combination-token manifest, restored the same way and for the same reason: the artifact
# on the app side named a catalogue that no longer exists (contract::kCombinationCatalog) and
# spelled two of its columns in ways the tree does not. This dumps contract::kChainCatalog as
# it is, doc column derived from the row.
COMBINATION_MANIFEST := $(BUILD_DIR)/combination-manifest.tsv
COMBINATION_MANIFEST_PIN := src/tests/combination-manifest.pin.tsv
.PHONY: combination-manifest combination-manifest-regen

$(BUILD_DIR)/pagecache_seam_test: src/tests/pagecache_seam_test.cpp \
  src/backend/cve_2026_43284/seam/pagecache_seam.hpp | $(BUILD_DIR)
		$(CXX) $(CXXFLAGS) src/tests/pagecache_seam_test.cpp -o $@

$(BUILD_DIR)/multicast_seam_test: src/tests/multicast_seam_test.cpp \
  src/backend/cve_2026_43499/seam/multicast_seam.hpp | $(BUILD_DIR)
		$(CXX) $(CXXFLAGS) src/tests/multicast_seam_test.cpp -o $@

$(BUILD_DIR)/cve43499_kernel_access_test: src/tests/cve43499_kernel_access_test.cpp \
  src/backend/cve_2026_43499/kernel_access.hpp src/backend/cve_2026_43499/access_params.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/cve43499_kernel_access_test.cpp -o $@

$(BUILD_DIR)/kernel_access_test: src/tests/kernel_access_test.cpp \
  src/contract/kernel_access.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/kernel_access_test.cpp -o $@

$(BUILD_DIR)/target_state_test: src/tests/target_state_test.cpp \
  src/backend/cve_2026_43499/session.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/target_state_test.cpp -o $@

$(BUILD_DIR)/capability_agreement_test: src/tests/capability_agreement_test.cpp \
  src/contract/capability_catalog.hpp src/backend/cve_2026_43499/capabilities.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/capability_agreement_test.cpp -o $@

$(BUILD_DIR)/step_runner_test: src/tests/step_runner_test.cpp \
  src/backend/cve_2026_43499/steps/step_runner.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/step_runner_test.cpp -o $@

$(BUILD_DIR)/combination_manifest_test: src/tests/combination_manifest_test.cpp src/contract/chain_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DCOMBINATION_MANIFEST_PIN='"$(COMBINATION_MANIFEST_PIN)"' src/tests/combination_manifest_test.cpp -o $@

$(COMBINATION_MANIFEST): $(BUILD_DIR)/combination_manifest_test $(COMBINATION_MANIFEST_PIN)
	@$(BUILD_DIR)/combination_manifest_test --export-only > $@
	@diff -u $(COMBINATION_MANIFEST_PIN) $@ && echo "combination-manifest: matches the pin"

combination-manifest: $(COMBINATION_MANIFEST)

combination-manifest-regen: $(BUILD_DIR)/combination_manifest_test
	@$(BUILD_DIR)/combination_manifest_test --export-only > $(COMBINATION_MANIFEST_PIN) && echo "combination-manifest-regen: pin updated -- review and commit it with its cause"

# The stepset -> step sequence manifest, restored after the rewrite retired it: the artifact
# in the app still carried the older tokens and pointed at a command that had stopped existing.
# Same pattern as the surface manifest: stdout is exactly the TSV, and the bare run compares
# itself with the frozen pin.
STEPSET_STEPS_MANIFEST := $(BUILD_DIR)/stepset-steps.tsv
STEPSET_STEPS_PIN := src/tests/stepset-steps.pin.tsv
.PHONY: stepset-steps-manifest stepset-steps-manifest-regen

$(BUILD_DIR)/stepset_steps_manifest_test: src/tests/stepset_steps_manifest_test.cpp src/contract/step_catalog.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSTEPSET_STEPS_PIN='"$(STEPSET_STEPS_PIN)"' src/tests/stepset_steps_manifest_test.cpp -o $@

$(STEPSET_STEPS_MANIFEST): $(BUILD_DIR)/stepset_steps_manifest_test $(STEPSET_STEPS_PIN)
	@$(BUILD_DIR)/stepset_steps_manifest_test --export-only > $@
	@diff -u $(STEPSET_STEPS_PIN) $@ && echo "stepset-steps-manifest: matches the pin"

stepset-steps-manifest: $(STEPSET_STEPS_MANIFEST)

stepset-steps-manifest-regen: $(BUILD_DIR)/stepset_steps_manifest_test
	@$(BUILD_DIR)/stepset_steps_manifest_test --export-only > $(STEPSET_STEPS_PIN) && echo "stepset-steps-manifest-regen: pin updated -- review and commit it with its cause"

# The surface manifest (same pattern as combination/vocabulary/stepset manifests):
# stdout is exactly the TSV, so it can be redirected into an artifact; the
# bare-run assertion inside the exporter compares it with the frozen pin.
SURFACE_MANIFEST := $(BUILD_DIR)/surface-manifest.tsv
SURFACE_MANIFEST_PIN := src/tests/surface-manifest.pin.tsv
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
# The profile manifest is generated from the table that owns it, into both copies the app
# reads. The check is what keeps them from drifting: it regenerates and compares, and the
# check target runs it, so a key added in one place and not the other fails the gate.
PROFILE_MANIFEST_DUMP := $(BUILD_DIR)/profile_manifest_dump
PROFILE_MANIFEST_COPIES := ../app/src/test/resources/profile-manifest-v3.tsv \
  ../profile-core/src/main/resources/profile-manifest-v3.tsv

$(PROFILE_MANIFEST_DUMP): src/tools/profile_manifest_dump.cpp src/profile/key_catalog.hpp src/profile/key_catalog.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tools/profile_manifest_dump.cpp src/profile/key_catalog.cpp -o $@

profile-manifest-v3: $(PROFILE_MANIFEST_DUMP)
	@$(PROFILE_MANIFEST_DUMP) > ../build/host/profile-manifest-v3.tsv
	@cp ../build/host/profile-manifest-v3.tsv ../app/src/test/resources/profile-manifest-v3.tsv
	@cp ../build/host/profile-manifest-v3.tsv ../profile-core/src/main/resources/profile-manifest-v3.tsv
	@echo "profile-manifest-v3: wrote both copies from the key table"

profile-manifest-v3-check: $(PROFILE_MANIFEST_DUMP)
	@$(PROFILE_MANIFEST_DUMP) > ../build/host/profile-manifest-v3.tsv
	@cmp -s ../build/host/profile-manifest-v3.tsv ../app/src/test/resources/profile-manifest-v3.tsv || { echo "profile-manifest-v3-check: FAIL (app copy differs from the table)"; exit 1; }
	@cmp -s ../build/host/profile-manifest-v3.tsv ../profile-core/src/main/resources/profile-manifest-v3.tsv || { echo "profile-manifest-v3-check: FAIL (profile-core copy differs)"; exit 1; }
	@echo "profile-manifest-v3-check: both copies are the table"

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
	@lua=$$(grep -rIlE 'lua_State|luaL_|#include *[<"]lua' src/pipeline src/contract 2>/dev/null || true); \
	if [ -n "$$lua" ]; then echo "LUA-HIT: $$lua"; exit 1; fi; \
	echo "check: zero Lua references in the window/pipeline layer"
	@hits=$$(grep -nE '$(ORDER_TOKENS)' src/pipeline/composition_root.hpp | wc -l | tr -d ' '); \
	if [ "$$hits" -ne 0 ]; then echo "ORDER-HIT: $$hits"; grep -nE '$(ORDER_TOKENS)' src/pipeline/composition_root.hpp; exit 1; fi; \
	echo "check: zero ordering tokens in the composition root"
	@echo "check: A2 window constraints and profile read path"
	@$(BUILD_DIR)/a2_test
	@echo "check: exposure surface table"
	@$(BUILD_DIR)/surface_test
	@echo "check: surface manifest (the exported table is byte-identical to its pin)"
	@$(BUILD_DIR)/surface_manifest_test
	@echo "check: Lua sandbox (whitelist assembly + four budgets)"
	@$(BUILD_DIR)/surface_sandbox_test
	@echo "check: stage gate (what a call may reach, per phase)"
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
	@echo "check: page cache seam (block width, doors, word refused)"
	@$(BUILD_DIR)/pagecache_seam_test
	@echo "check: multicast seam (one route, refusals by name, fields unchanged)"
	@$(BUILD_DIR)/multicast_seam_test
	@echo "check: cve-2026-43499 access (route from parameters, window per access)"
	@$(BUILD_DIR)/cve43499_kernel_access_test
	@echo "check: kernel access (read and write to implement, the rest inherited)"
	@$(BUILD_DIR)/kernel_access_test
	@echo "check: target state (the first missing fact is the answer)"
	@$(BUILD_DIR)/target_state_test
	@echo "check: capability agreement (declared is provided, provided is declared)"
	@$(BUILD_DIR)/capability_agreement_test
	@echo "check: step runner (walks the declared order, stops at the first failure)"
	@$(BUILD_DIR)/step_runner_test
	@echo "check: combination manifest (restored, matches its pin)"
	@$(BUILD_DIR)/combination_manifest_test
	@echo "check: stepset-steps manifest (restored, matches its pin)"
	@$(BUILD_DIR)/stepset_steps_manifest_test
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
	@echo "check: device facts (what the runtime reads about its device)"
	@$(BUILD_DIR)/device_facts_test
	@echo "check: module facts (a module's own declaration, read without loading)"
	@$(BUILD_DIR)/module_facts_test
	@echo "check: staged plan (what one script run recorded)"
	@$(BUILD_DIR)/staged_plan_test
	@echo "check: plan delivery (the plan a document carries, and its two judgements)"
	@$(BUILD_DIR)/plan_delivery_test
	@echo "check: plan load (one delivered script, run in the sandbox)"
	@$(BUILD_DIR)/plan_load_test
	@echo "check: reclaim plan (which descriptors are given back, and in what order)"
	@$(BUILD_DIR)/reclaim_plan_test
	@echo "check: perf ring (reads the samples it should, stops where it must)"
	@$(BUILD_DIR)/perf_ring_test
	@echo "check: perf vote (tag restored, bounds strict, ties go first)"
	@$(BUILD_DIR)/perf_vote_test
	@echo "check: multicast policy (arms, waits, stops on the attempt that worked)"
	@$(BUILD_DIR)/multicast_policy_test
	@echo "check: route policy (capabilities are compile-time)"
	@$(BUILD_DIR)/route_policy_test
	@echo "check: route status (clean needs both sides)"
	@$(BUILD_DIR)/route_status_test
	@echo "check: collision count (the count tells the object apart)"
	@$(BUILD_DIR)/collision_count_test
	@echo "check: collision buckets (the rule agrees with the hash)"
	@$(BUILD_DIR)/collision_buckets_test
	@echo "check: discovery result (canonical when found)"
	@$(BUILD_DIR)/discovery_result_test
	@echo "check: snitch discovery (one object, no pretence about the others)"
	@$(BUILD_DIR)/snitch_discovery_test
	@echo "check: snitch policy (one run, then answers the polls)"
	@$(BUILD_DIR)/snitch_policy_test
	@echo "check: snitch tuning (from the profile, or refused by name)"
	@$(BUILD_DIR)/snitch_tuning_test
	@echo "check: snitch child (a poke counts and touches)"
	@$(BUILD_DIR)/snitch_child_test
	@echo "check: snitch run (pokes first, then the walk)"
	@$(BUILD_DIR)/snitch_run_test
	@echo "check: leak search (stops at the answer, finds tagged objects)"
	@$(BUILD_DIR)/leak_search_test
	@echo "check: profile manifest (both copies are the key table)"
	@$(MAKE) -s profile-manifest-v3-check
	@echo "check: snitch scan (pokes finish it, silence ends it, failure tidies up)"
	@$(BUILD_DIR)/snitch_scan_test
	@echo "check: snitch seam (complete answers pass, partial ones do not)"
	@$(BUILD_DIR)/snitch_seam_test
	@echo "check: collision wait (how many pokes, how many collisions)"
	@$(BUILD_DIR)/collision_wait_test
	@echo "check: snitch region (five parts in order, sized from the counts)"
	@$(BUILD_DIR)/snitch_region_test
	@echo "check: snitch setup (the shape derived, the missing inputs named)"
	@$(BUILD_DIR)/snitch_setup_test
	@echo "check: leak walk (the nesting, the tags, and where it stops)"
	@$(BUILD_DIR)/leak_walk_test
	@echo "check: futex hash (the retired fixed vectors)"
	@$(BUILD_DIR)/futex_hash_test
	@echo "check: candidate match (agreement decides, nothing to compare is refused)"
	@$(BUILD_DIR)/candidate_match_test
	@echo "check: scan geometry (range aligned, walk bounded)"
	@$(BUILD_DIR)/scan_geometry_test
	@echo "check: scan bounds (one stride, clamped, saturating)"
	@$(BUILD_DIR)/scan_bounds_test
	@echo "check: address discovery (what failure is allowed to carry)"
	@$(BUILD_DIR)/address_discovery_test
	@echo "check: prepare steps (the order a page preparation runs in)"
	@$(BUILD_DIR)/prepare_steps_test
	@echo "check: page geometry (alignment, and geometry that cannot be acted on)"
	@$(BUILD_DIR)/page_geometry_test
	@echo "check: prepare plan (the order read, the sizes carried, the stride derived)"
	@$(BUILD_DIR)/prepare_plan_test
	@echo "check: shaping plan (which pair, which end, in what order)"
	@$(BUILD_DIR)/shaping_plan_test
	@echo "check: heap geometry (the region follows the page, and a mismatch is refused)"
	@$(BUILD_DIR)/heap_geometry_test
	@echo "check: prepare page (the orchestration, on a fake platform)"
	@$(BUILD_DIR)/prepare_page_test
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
	@echo "check: run plan (a document resolves to a chain and its steps)"
	@$(BUILD_DIR)/run_plan_test
	@$(MAKE) --no-print-directory plan-check
	@win=$$(grep -cE '\{"window\.' src/contract/script_surface.hpp || true); \
	if [ "$$win" -ne 0 ]; then echo "SURFACE-WINDOW-ROW: $$win"; grep -nE '\{"window\.' src/contract/script_surface.hpp; exit 1; fi; \
	echo "check: no window.* row in the exposure surface"
	@dup=$$(for f in $$(find core -type f \( -name '*.hpp' -o -name '*.cpp' \) ! -name 'script_surface.hpp'); do \
	  if tr -d '\n' < "$$f" | grep -qE 'backend.*writeop.*settle.*handoff'; then echo "$$f"; fi; \
	done | wc -l | tr -d ' '); \
	if [ "$$dup" -ne 0 ]; then echo "SURFACE-DUP: $$dup other file(s) restate the surface list"; exit 1; fi; \
	echo "check: surface list has a single definition site (the generated pin lives in a .tsv and is checked byte-for-byte)"
	# The property is source level, because it is the one this host can actually drive: a vtable
	# symbol is emitted only when a class is used polymorphically, and the synthetic cases the
	# compiler devirtualises emit none at all, so an nm scan could not be exercised (recorded in
	# the commit that introduced the ledger). What can be exercised is the rule: virtual appears
	# only where the ruling allows it, the access interface and its 43499 implementation. Tests
	# are exempt, and the window headers are prose-only, their comments stripped first.
	@files=$$(grep -rlE "^[[:space:]]*virtual " $$(find src -name "*.hpp" -o -name "*.cpp" | grep -v "^src/tests/") || true); \
	stray=$$(echo "$$files" | grep -vE "^src/(contract/kernel_access\.hpp|backend/cve_2026_43499/kernel_access\.hpp|backend/cve_2026_43499/seam/multicast_seam\.hpp|backend/cve_2026_43284/seam/pagecache_seam\.hpp)$$" | grep -v "^$$" || true); \
	if [ -n "$$stray" ]; then echo "VIRTUAL-OUTSIDE-ACCESS: $$stray"; echo "  virtual is allowed only in the access interface and its 43499 implementation"; exit 1; fi; \
	echo "check: virtual is confined to the access interface (tests exempt)"
	@code=$$(sed -e 's://.*::' src/backend/cve_2026_43499/race/pi_window.hpp src/backend/cve_2026_43499/race/pi_race.hpp | grep -vE '^[[:space:]]*\*'); \
	dirty=$$(echo "$$code" | grep -cE '\bnew\b|malloc|calloc|realloc|printf|fprintf|std::string|std::vector|std::function|virtual' || true); \
	if [ "$$dirty" -ne 0 ]; then echo "WINDOW-DIRTY: $$dirty"; echo "$$code" | grep -nE '\bnew\b|malloc|calloc|realloc|printf|fprintf|std::string|std::vector|std::function|virtual'; exit 1; fi; \
	echo "check: race window carries no allocation, logging or indirect dispatch"
	@scode=$$(sed -e 's://.*::' $$(find src/support -name '*.hpp' -o -name '*.cpp') | grep -vE '^[[:space:]]*\*'); \
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
SCRIPT_SRCS := src/script/sandbox.cpp

.PHONY: lua-core lua-core-size

$(BUILD_DIR)/prepare_page_test: src/tests/prepare_page_test.cpp \
  src/backend/cve_2026_43499/memory/prepare_page.hpp \
  src/backend/cve_2026_43499/memory/prepare_plan.cpp \
  src/backend/cve_2026_43499/memory/shaping_plan.cpp \
  src/backend/cve_2026_43499/memory/reclaim_plan.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/prepare_page_test.cpp src/backend/cve_2026_43499/memory/prepare_plan.cpp src/backend/cve_2026_43499/memory/shaping_plan.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp -o $@

$(BUILD_DIR)/heap_geometry_test: src/tests/heap_geometry_test.cpp \
  src/backend/cve_2026_43499/memory/heap_geometry.hpp \
  src/backend/cve_2026_43499/memory/page_geometry.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/heap_geometry_test.cpp -o $@

$(BUILD_DIR)/shaping_plan_test: src/tests/shaping_plan_test.cpp \
  src/backend/cve_2026_43499/memory/shaping_plan.hpp src/backend/cve_2026_43499/memory/shaping_plan.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/shaping_plan_test.cpp src/backend/cve_2026_43499/memory/shaping_plan.cpp -o $@

$(BUILD_DIR)/prepare_plan_test: src/tests/prepare_plan_test.cpp \
  src/backend/cve_2026_43499/memory/prepare_plan.hpp src/backend/cve_2026_43499/memory/prepare_plan.cpp \
  src/backend/cve_2026_43499/memory/prepare_steps.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/prepare_plan_test.cpp src/backend/cve_2026_43499/memory/prepare_plan.cpp -o $@

$(BUILD_DIR)/page_geometry_test: src/tests/page_geometry_test.cpp \
  src/backend/cve_2026_43499/memory/page_geometry.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/page_geometry_test.cpp -o $@

$(BUILD_DIR)/perf_ring_test: src/tests/perf_ring_test.cpp \
  src/backend/cve_2026_43499/perf_ring.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/perf_ring_test.cpp -o $@

$(BUILD_DIR)/perf_vote_test: src/tests/perf_vote_test.cpp \
  src/backend/cve_2026_43499/perf_vote.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/perf_vote_test.cpp -o $@

$(BUILD_DIR)/multicast_policy_test: src/tests/multicast_policy_test.cpp \
  src/backend/cve_2026_43499/route/multicast_policy.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/multicast_policy_test.cpp -o $@

$(BUILD_DIR)/route_policy_test: src/tests/route_policy_test.cpp \
  src/backend/cve_2026_43499/route/route_policy.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/route_policy_test.cpp -o $@

$(BUILD_DIR)/route_status_test: src/tests/route_status_test.cpp \
  src/backend/cve_2026_43499/route/route_status.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/route_status_test.cpp -o $@

$(BUILD_DIR)/collision_count_test: src/tests/collision_count_test.cpp \
  src/backend/cve_2026_43499/leak/collision_count.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/collision_count_test.cpp -o $@

$(BUILD_DIR)/collision_buckets_test: src/tests/collision_buckets_test.cpp \
  src/backend/cve_2026_43499/leak/collision_buckets.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/collision_buckets_test.cpp -o $@

$(BUILD_DIR)/discovery_result_test: src/tests/discovery_result_test.cpp \
  src/backend/cve_2026_43499/leak/discovery_result.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/discovery_result_test.cpp -o $@

$(BUILD_DIR)/snitch_discovery_test: src/tests/snitch_discovery_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_discovery.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_discovery_test.cpp -o $@

$(BUILD_DIR)/snitch_policy_test: src/tests/snitch_policy_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_policy.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_policy_test.cpp -o $@

$(BUILD_DIR)/snitch_tuning_test: src/tests/snitch_tuning_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_tuning.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_tuning_test.cpp -o $@

$(BUILD_DIR)/snitch_child_test: src/tests/snitch_child_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_child.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_child_test.cpp -o $@

$(BUILD_DIR)/snitch_run_test: src/tests/snitch_run_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_run.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_run_test.cpp -o $@

$(BUILD_DIR)/leak_search_test: src/tests/leak_search_test.cpp \
  src/backend/cve_2026_43499/leak/leak_search.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/leak_search_test.cpp -o $@

$(BUILD_DIR)/snitch_scan_test: src/tests/snitch_scan_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_scan.hpp src/backend/cve_2026_43499/leak/snitch_setup.cpp \
  src/backend/cve_2026_43499/leak/futex_hash.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_scan_test.cpp src/backend/cve_2026_43499/leak/snitch_setup.cpp -o $@

$(BUILD_DIR)/snitch_seam_test: src/tests/snitch_seam_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_seam.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_seam_test.cpp -o $@

$(BUILD_DIR)/collision_wait_test: src/tests/collision_wait_test.cpp \
  src/backend/cve_2026_43499/leak/collision_wait.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/collision_wait_test.cpp -o $@

$(BUILD_DIR)/snitch_region_test: src/tests/snitch_region_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_region.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_region_test.cpp -o $@

$(BUILD_DIR)/snitch_setup_test: src/tests/snitch_setup_test.cpp \
  src/backend/cve_2026_43499/leak/snitch_setup.hpp src/backend/cve_2026_43499/leak/snitch_setup.cpp \
  src/backend/cve_2026_43499/leak/futex_hash.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/snitch_setup_test.cpp src/backend/cve_2026_43499/leak/snitch_setup.cpp -o $@

$(BUILD_DIR)/leak_walk_test: src/tests/leak_walk_test.cpp \
  src/backend/cve_2026_43499/leak/leak_walk.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/leak_walk_test.cpp -o $@

$(BUILD_DIR)/futex_hash_test: src/tests/futex_hash_test.cpp \
  src/backend/cve_2026_43499/leak/futex_hash.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/futex_hash_test.cpp -o $@

$(BUILD_DIR)/candidate_match_test: src/tests/candidate_match_test.cpp \
  src/backend/cve_2026_43499/leak/candidate_match.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/candidate_match_test.cpp -o $@

$(BUILD_DIR)/scan_geometry_test: src/tests/scan_geometry_test.cpp \
  src/backend/cve_2026_43499/leak/scan_geometry.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/scan_geometry_test.cpp -o $@

$(BUILD_DIR)/scan_bounds_test: src/tests/scan_bounds_test.cpp \
  src/backend/cve_2026_43499/leak/scan_bounds.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/scan_bounds_test.cpp -o $@

$(BUILD_DIR)/address_discovery_test: src/tests/address_discovery_test.cpp \
  src/contract/address_discovery.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/address_discovery_test.cpp -o $@

$(BUILD_DIR)/prepare_steps_test: src/tests/prepare_steps_test.cpp \
  src/backend/cve_2026_43499/memory/prepare_steps.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/prepare_steps_test.cpp -o $@

$(BUILD_DIR)/reclaim_plan_test: src/tests/reclaim_plan_test.cpp \
  src/backend/cve_2026_43499/memory/reclaim_plan.hpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/reclaim_plan_test.cpp src/backend/cve_2026_43499/memory/reclaim_plan.cpp -o $@

$(BUILD_DIR)/plan_load_test: src/tests/plan_load_test.cpp src/script/plan_load.cpp \
  $(SCRIPT_SRCS) src/script/plan_load.hpp src/script/sandbox.hpp \
  src/script/staged_plan.hpp src/contract/script_surface.hpp $(LUA_CORE_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(LUA_DIR) src/tests/plan_load_test.cpp src/script/plan_load.cpp $(SCRIPT_SRCS) $(LUA_CORE_LIB) -o $@

$(BUILD_DIR)/surface_sandbox_test: src/tests/surface_sandbox_test.cpp $(SCRIPT_SRCS) \
  src/script/sandbox.hpp src/script/call_sink.hpp src/script/lua_whitelist.hpp \
  src/contract/script_surface.hpp $(LUA_CORE_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(LUA_DIR) src/tests/surface_sandbox_test.cpp $(SCRIPT_SRCS) $(LUA_CORE_LIB) -o $@

$(BUILD_DIR)/handle_generation_test: src/tests/handle_generation_test.cpp \
  src/script/handle_table.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/handle_generation_test.cpp -o $@

$(BUILD_DIR)/terminal_payload_test: src/tests/terminal_payload_test.cpp \
  src/terminal/payload.hpp src/support/sha256.hpp src/support/sha256.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/terminal_payload_test.cpp src/support/sha256.cpp -o $@

$(BUILD_DIR)/terminal_store_test: src/tests/terminal_store_test.cpp \
  src/terminal/payload_store.hpp src/terminal/payload_store.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/terminal_store_test.cpp src/terminal/payload_store.cpp -o $@

$(BUILD_DIR)/document_reader_test: src/tests/document_reader_test.cpp \
  src/profile/glkv3_document.hpp src/profile/glkv3_document.cpp \
  src/profile/payload_keys.hpp src/profile/payload_keys.cpp \
  src/support/sha256.hpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/document_reader_test.cpp src/profile/glkv3_document.cpp src/profile/payload_keys.cpp src/support/sha256.cpp $(BUILD_DIR)/mpack.o -o $@

$(BUILD_DIR)/payload_keys_test: src/tests/payload_keys_test.cpp \
  src/profile/payload_keys.hpp src/profile/payload_keys.cpp src/contract/kind.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/payload_keys_test.cpp src/profile/payload_keys.cpp -o $@

$(BUILD_DIR)/terminal_execution_test: src/tests/terminal_execution_test.cpp \
  src/terminal/execution.hpp src/terminal/execution.cpp \
  src/terminal/payload_store.hpp src/terminal/payload_store.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) src/tests/terminal_execution_test.cpp src/terminal/execution.cpp src/terminal/payload_store.cpp -o $@

$(BUILD_DIR)/surface_stage_test: src/tests/surface_stage_test.cpp $(SCRIPT_SRCS) \
  src/script/staged_plan.hpp src/script/sandbox.hpp src/script/call_sink.hpp \
  src/contract/script_surface.hpp $(LUA_CORE_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(LUA_DIR) src/tests/surface_stage_test.cpp $(SCRIPT_SRCS) $(LUA_CORE_LIB) -o $@

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
