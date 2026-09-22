#include "routes/tcp_zerocopy_route.h"

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
using namespace ghostlock::profile;
using namespace ghostlock::route;
using namespace ghostlock::route::tcp_zerocopy;
using namespace ghostlock::support;
using namespace ghostlock::race;
using namespace ghostlock::memory;


static int mmap_zero_page(void **address, size_t size) {
    char name[64];
    snprintf(name, sizeof(name), "/ghostlock_tcp_test_%d", (int) getpid());
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
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

int main(void) {
    PiRace race;
    race.reset(0, 0, 1);
    WriteRequest request{};
    const struct execution_settings *execution =
            (const struct execution_settings *) (uintptr_t) 0x1234;

    TcpZerocopyRoute context(&race, &request, execution, 16 * 1024 * 1024);
    assert(context.race == &race);
    assert(context.request == &request);
    assert(context.execution == execution);
    assert(!context.client_fd.valid());
    assert(!context.server_fd.valid());
    assert(!context.punch_fd.valid());
    assert(!context.mapping.valid());
    assert(context.mapping_length == 16 * 1024 * 1024);
    assert(context.punch_worker.state() == PthreadOwner::State::Empty);
    assert(context.punch_go.load() == 0);
    assert(context.punch_stop.load() == 0);
    assert(context.punch_phase.load() == 0);
    assert(context.punch_failed.load() == 0);
    assert(context.status.code == ROUTE_RETRYABLE);
    assert(context.status.userspace_clean == 0);
    assert(context.status.kernel_disarmed == 0);

    /* Move-only: no copy, resources transfer with the move. */
    static_assert(!std::is_copy_constructible_v<TcpZerocopyRoute>);
    static_assert(!std::is_copy_assignable_v<TcpZerocopyRoute>);
    static_assert(std::is_move_constructible_v<TcpZerocopyRoute>);
    {
        TcpZerocopyRoute source(&race, &request, execution, 8192);
        void *mapped = nullptr;
        int fd = mmap_zero_page(&mapped, 4096);
        assert(fd >= 0);
        source.punch_fd.reset(fd);
        source.mapping = MappedRegion(mapped, 4096);
        source.punch_phase.store(3);
        TcpZerocopyRoute moved(std::move(source));
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
    assert(context.status.code == ROUTE_FALLBACK_SAFE);

    /* fail() records step and errno for the caller's log. */
    assert(context.fail(59, 5) == -1);
    assert(context.status.step == 59);
    assert(context.status.error_number == 5);

    puts("tcp_zerocopy_route_test: ok");
    return 0;
}
