#include "route/tcp_zerocopy_route.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

#include <type_traits>
#include <utility>

using namespace ghostlock;


static int32_t mmap_zero_page(void **address, size_t size) {
    char name[64];
    snprintf(name, sizeof(name), "/ghostlock_tcp_test_%d", (int32_t) getpid());
    int32_t fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    shm_unlink(name);
    if (ftruncate(fd, (off_t) size) != 0) {
        close(fd);
        return -1;
    }
    void *mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return -1;
    }
    *address = mapped;
    return fd;
}

int32_t main(void) {
    ghostlock::race::PiRace race;
    race.reset(0, 0, 1);
    ghostlock::memory::WriteRequest request{};
    const profile::TargetProfile profile{};

    ghostlock::route::tcp_zerocopy::TcpZerocopyRoute context(&race, &request, profile, 16 * 1024 * 1024);
    assert(context.race == &race);
    assert(context.request == &request);
    assert(&context.profile == &profile);
    assert(!context.client_fd.valid());
    assert(!context.server_fd.valid());
    assert(!context.punch_fd.valid());
    assert(!context.mapping.valid());
    assert(context.mapping_length == 16 * 1024 * 1024);
    assert(context.punch_worker.state() == ghostlock::support::PthreadOwner::State::Empty);
    assert(context.punch_go.load() == 0);
    assert(context.punch_stop.load() == 0);
    assert(context.punch_phase.load() == 0);
    assert(context.punch_failed.load() == 0);
    assert(context.status.code == ghostlock::route::ROUTE_RETRYABLE);
    assert(context.status.userspace_clean == 0);
    assert(context.status.kernel_disarmed == 0);

    /* Move-only: no copy, resources transfer with the move. */
    static_assert(!std::is_copy_constructible_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);
    static_assert(!std::is_copy_assignable_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);
    static_assert(std::is_move_constructible_v<ghostlock::route::tcp_zerocopy::TcpZerocopyRoute>);
    {
        ghostlock::route::tcp_zerocopy::TcpZerocopyRoute source(&race, &request, profile, 8192);
        void *mapped = nullptr;
        int32_t fd = mmap_zero_page(&mapped, 4096);
        assert(fd >= 0);
        source.punch_fd.reset(fd);
        source.mapping = ghostlock::support::MappedRegion(mapped, 4096);
        source.punch_phase.store(3);
        ghostlock::route::tcp_zerocopy::TcpZerocopyRoute moved(std::move(source));
        assert(moved.punch_fd.get() == fd);
        assert(!source.punch_fd.valid());
        assert(moved.mapping.valid());
        assert(!source.mapping.valid());
        assert(moved.mapping_length == 8192);
        assert(moved.punch_phase.load() == 3);
        moved.disarm();
        moved.destroy();
        assert(!moved.punch_fd.valid());
        assert(!moved.mapping.valid());
        assert(fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    }

    /* disarm is idempotent and stops the shared consumer trigger. */
    context.disarm();
    context.disarm();
    assert(context.status.kernel_disarmed == 1);
    assert(race.consumer_go.load() == 0);

    /* destroy without resources is idempotent and reports a clean fallback. */
    context.destroy();
    context.destroy();
    assert(context.status.userspace_clean == 1);
    assert(context.status.code == ghostlock::route::ROUTE_FALLBACK_SAFE);

    /* fail() records step and errno for the caller's log. */
    assert(context.fail(59, 5) == -1);
    assert(context.status.step == 59);
    assert(context.status.error_number == 5);

    puts("tcp_zerocopy_route_test: ok");
    return 0;
}
