#ifndef GHOSTLOCK_HOST_SCHED_H
#define GHOSTLOCK_HOST_SCHED_H

/*
 * Host shim: macOS <sched.h> lacks the Linux CPU-set API that common.h pulls in
 * through kernelsnitch/utils.h and runtime_config.cpp. Include the real
 * <sched.h> first (glibc already provides the API, so the block below is
 * skipped there) and add a portable cpu_set_t on platforms that need it.
 */

#include_next <sched.h>

#include <stddef.h>
#include <sys/types.h>

#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024
typedef struct host_cpu_set {
    unsigned long bits[(CPU_SETSIZE + 63) / 64];
} cpu_set_t;

#define CPU_ZERO(set)                                                        \
    do {                                                                     \
        for (unsigned long i = 0; i < sizeof((set)->bits) / sizeof((set)->bits[0]); ++i) \
            (set)->bits[i] = 0;                                              \
    } while (0)

#define CPU_SET(cpu, set)                                                    \
    do {                                                                     \
        if ((cpu) >= 0 && (cpu) < CPU_SETSIZE)                               \
            (set)->bits[(cpu) / 64] |= (1UL << ((cpu) % 64));                \
    } while (0)

#define CPU_ISSET(cpu, set)                                                  \
    (((cpu) >= 0 && (cpu) < CPU_SETSIZE) &&                                  \
     (((set)->bits[(cpu) / 64] & (1UL << ((cpu) % 64))) != 0))

#ifdef __cplusplus
extern "C" {
#endif

int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask);
int sched_getcpu(void);

#ifdef __cplusplus
}
#endif

#endif // CPU_SETSIZE

#endif
