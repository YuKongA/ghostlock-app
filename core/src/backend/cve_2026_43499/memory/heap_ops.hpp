#pragma once
/** @file backend/cve_2026_43499/memory/heap_ops.hpp
 *  The operations a page preparation needs from the platform, written as a concept.
 *
 *  The orchestration is written against this shape; the device supplies an
 *  implementation and a host test supplies a fake, exactly as the race does with its
 *  futex calls. A landing decides nothing: no sizes, no order, no retries live here --
 *  the order is prepare_steps, the sizes are prepare_plan, and what to do when a call
 *  fails is the orchestration. What the landing owns is the thin edge where the
 *  mechanism meets the kernel, and its only judgement is whether a call returned what
 *  it was supposed to.
 *
 *  A concept rather than a base class or a list of declarations: a policy type either
 *  answers all of these or does not compile, and the same check applies to the fake a
 *  test hands in, so a fake that drifts from the device is caught where it is
 *  written.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** What a platform call said: the value it returned and the errno beside it. */
    struct OpsResult final {
        std::int64_t value = -1;
        std::int32_t error = 0;
    };

    /** The page size this device boots with, which is measured rather than assumed. */
    [[nodiscard]] constexpr bool operator==(const OpsResult &left, const OpsResult &right) noexcept {
        return left.value == right.value && left.error == right.error;
    }

    /** Every call a preparation makes on the platform. */
    template <class Landing>
    concept HeapOps = requires(std::int32_t fd, std::int32_t *pair, std::int32_t bytes,
                                std::size_t count, std::uint64_t pid, std::int32_t cpu,
                                const void *data) {
        { Landing::page_size() } -> std::same_as<OpsResult>;
        { Landing::open_memfd(pid) } -> std::same_as<OpsResult>;
        { Landing::close_fd(fd) } -> std::same_as<OpsResult>;
        { Landing::socket_pair(pair) } -> std::same_as<OpsResult>;
        { Landing::set_socket_buffer(fd, bytes) } -> std::same_as<OpsResult>;
        { Landing::set_nonblocking(fd) } -> std::same_as<OpsResult>;
        { Landing::send_buffer(fd, count) } -> std::same_as<OpsResult>;
        /* 带内容发送：页面必须走这条路进内核（退役的 sendmsg(skb_buf) ✓），
         *  否则内核里的 skb 是零、页面等于没放（本树此前正是如此 ✗）。 */
        { Landing::send_bytes(fd, data, count) } -> std::same_as<OpsResult>;
        { Landing::fork_child() } -> std::same_as<OpsResult>;
        { Landing::stop_child(pid) } -> std::same_as<OpsResult>;
        { Landing::yield_now() } -> std::same_as<OpsResult>;
        { Landing::pin_to_processor(cpu) } -> std::same_as<OpsResult>;
        /* A forked child has to be able to end itself: returning up the call stack in a
         *  child would run the caller code a second time. */
        { Landing::exit_child() };
    };
}  // namespace ghostlock::backend::cve_2026_43499::memory
