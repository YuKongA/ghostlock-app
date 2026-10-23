#ifndef GLK_HOST_CPU_SET_STUB_H
#define GLK_HOST_CPU_SET_STUB_H

#include <stddef.h>
#include <string.h>

/* Minimal host shim for kernelsnitch/utils.h; test does not call affinity. */
typedef unsigned long cpu_set_t;
#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024
#endif
#define CPU_ZERO(set) memset((set), 0, sizeof(cpu_set_t))
#define CPU_SET(cpu, set) ((void)(cpu), (void)(set))
#define CPU_ISSET(cpu, set) (0)
static inline int sched_setaffinity(int pid, size_t size, const cpu_set_t *set) {
  (void)pid; (void)size; (void)set;
  return 0;
}

#endif
