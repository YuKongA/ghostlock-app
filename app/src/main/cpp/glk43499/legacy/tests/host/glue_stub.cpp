#include <cstdint>

#include <sched.h>

/* g_direct_map_end is defined by the linked memory/address_space.cpp; the host
 * in_direct_map stub does not consult it. */

/* macOS libSystem has no Linux CPU-affinity entry points; provide no-op
 * definitions so the inline pin_to_core() and runtime_config's CPU check link.
 * Linux hosts resolve these from libc instead. */
#if defined(__APPLE__)
extern "C" int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask) {
    (void) pid;
    (void) cpusetsize;
    (void) mask;
    return 0;
}

extern "C" int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
    (void) pid;
    (void) cpusetsize;
    if (mask != nullptr) CPU_ZERO(mask);
    return 0;
}

extern "C" int sched_getcpu(void) { return 0; }
#endif
