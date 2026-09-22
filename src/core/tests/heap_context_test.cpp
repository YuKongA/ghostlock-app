#include "memory/heap_context.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <unistd.h>

#include <type_traits>
#include <utility>

using namespace ghostlock;

static void test_move_preserves_page_as_one_owner(void) {
    memory::HeapContext context;
    context.init();
    assert(context.current.reclaim.fd[0] == -1);
    assert(context.prebuilt.state == memory::PayloadPageState::Empty);

    int owned[2];
    assert(pipe(owned) == 0);
    context.current.base = 0x12340000;
    context.current.fake_lock = 0x12340100;
    context.current.reclaim.fd[0] = owned[0];
    context.current.reclaim.fd[1] = owned[1];
    context.current.state = memory::PayloadPageState::Current;

    assert(context.current.move_to(context.prebuilt, memory::PayloadPageState::Prebuilt));
    assert(context.current.state == memory::PayloadPageState::Empty);
    assert(context.current.reclaim.fd[0] == -1);
    assert(context.prebuilt.base == 0x12340000);
    assert(context.prebuilt.fake_lock == 0x12340100);
    assert(context.prebuilt.state == memory::PayloadPageState::Prebuilt);

    context.prebuilt.destroy();
    errno = 0;
    assert(fcntl(owned[0], F_GETFD) == -1 && errno == EBADF);
}

static void test_move_rejects_partial_or_occupied_ownership(void) {
    memory::HeapContext context;
    context.init();
    context.current.reclaim.fd[0] = 3;
    context.current.state = memory::PayloadPageState::Current;
    assert(!context.current.move_to(context.prebuilt, memory::PayloadPageState::Prebuilt));

    context.current.reclaim.fd[1] = 4;
    context.prebuilt.state = memory::PayloadPageState::Prebuilt;
    assert(!context.current.move_to(context.prebuilt, memory::PayloadPageState::Prebuilt));
    context.current.reclaim.fd[0] = -1;
    context.current.reclaim.fd[1] = -1;
}

static void test_page_is_move_only_and_released_explicitly(void) {
    static_assert(!std::is_copy_constructible_v<memory::PayloadPage>);
    static_assert(!std::is_copy_assignable_v<memory::PayloadPage>);
    static_assert(std::is_move_constructible_v<memory::PayloadPage>);
    /* Scope exit must not close fds that the kernel may still reference. */
    static_assert(std::is_trivially_destructible_v<memory::PayloadPage>);

    memory::PayloadPage source;
    assert(source.state == memory::PayloadPageState::Empty);
    assert(!source.has_reclaim());

    int owned[2];
    assert(pipe(owned) == 0);
    source.base = 0xabc00000;
    source.reclaim.fd[0] = owned[0];
    source.reclaim.fd[1] = owned[1];
    source.state = memory::PayloadPageState::Current;

    memory::PayloadPage moved(std::move(source));
    assert(moved.has_reclaim());
    assert(moved.base == 0xabc00000);
    assert(moved.state == memory::PayloadPageState::Current);
    assert(source.state == memory::PayloadPageState::Empty);
    assert(!source.has_reclaim());

    moved.destroy();
    errno = 0;
    assert(fcntl(owned[0], F_GETFD) == -1 && errno == EBADF);
    moved.destroy();
    assert(moved.state == memory::PayloadPageState::Empty);
}

static void test_mm_set_releases_descriptors_explicitly(void) {
    MmContextSet set;
    int owned[4];
    assert(pipe(owned) == 0);
    assert(pipe(owned + 2) == 0);

    set.childs.assign(2, 0);
    set.memfds.assign(2, -1);
    assert(set.childs.size() == 2);
    assert(set.memfds.size() == 2);
    set.childs[0] = 1234;
    set.memfds[0] = owned[0];
    set.memfds[1] = owned[2];

    memory::close_ctx_memfds(&set);
    assert(set.memfds[0] == -1);
    assert(set.memfds[1] == -1);
    errno = 0;
    assert(fcntl(owned[0], F_GETFD) == -1 && errno == EBADF);
    errno = 0;
    assert(fcntl(owned[2], F_GETFD) == -1 && errno == EBADF);

    memory::free_ctx_storage(&set);
    assert(set.childs.empty());
    assert(set.memfds.empty());
    /* Repeated cleanup and destroy are idempotent. */
    memory::close_ctx_memfds(&set);
    memory::free_ctx_storage(&set);
    assert(set.childs.empty());

    /* Scope exit alone only frees storage: no descriptor is closed and no pid
   * is signalled, so a forked helper that calls exit() cannot release the
   * parent's children. */
    int survivor[2];
    assert(pipe(survivor) == 0);
    {
        MmContextSet scoped;
        scoped.memfds.assign(1, survivor[0]);
        scoped.childs.assign(1, 4321);
    }
    assert(fcntl(survivor[0], F_GETFD) >= 0);
    close(survivor[0]);
    close(survivor[1]);

    set.memfds.assign(1, owned[1]);
    MmContextSet moved(std::move(set));
    assert(moved.memfds.size() == 1 && moved.memfds[0] == owned[1]);
    close(moved.memfds[0]);
}

static void test_heap_context_init_releases_attempt_state(void) {
    memory::HeapContext context;
    context.init();
    assert(context.skb_buffer == nullptr);
    assert(!context.leak_memfd.valid());
    assert(context.prepare.childs.empty());
    assert(context.prepare.memfds.empty());

    int owned[2];
    assert(pipe(owned) == 0);
    context.skb_buffer = std::make_unique<unsigned char[]>(16);
    context.leak_memfd.reset(owned[0]);
    context.prepare.memfds.assign(1, owned[1]);

    context.init();
    assert(context.skb_buffer == nullptr);
    assert(!context.leak_memfd.valid());
    assert(context.prepare.memfds.empty());
    errno = 0;
    assert(fcntl(owned[0], F_GETFD) == -1 && errno == EBADF);
    /* The mm set only drops storage; a memfd recorded there is never closed
   * implicitly because the kernel may still reference it. */
    assert(fcntl(owned[1], F_GETFD) >= 0);
    context.prepare.memfds.assign(1, owned[1]);
    memory::close_ctx_memfds(&context.prepare);
    errno = 0;
    assert(fcntl(owned[1], F_GETFD) == -1 && errno == EBADF);
}

int main(void) {
    test_move_preserves_page_as_one_owner();
    test_move_rejects_partial_or_occupied_ownership();
    test_page_is_move_only_and_released_explicitly();
    test_mm_set_releases_descriptors_explicitly();
    test_heap_context_init_releases_attempt_state();
    puts("heap_context_test: ok");
    return 0;
}
