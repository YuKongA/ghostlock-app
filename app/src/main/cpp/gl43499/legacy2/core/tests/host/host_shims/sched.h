#pragma once
#include_next <sched.h>
#include <string.h>
typedef struct { unsigned char bits[16]; } cpu_set_t;
#define CPU_ZERO(set) memset((set), 0, sizeof(cpu_set_t))
#define CPU_SET(cpu, set) ((set)->bits[(cpu)/8] |= (unsigned char)(1u << ((cpu)%8)))
static inline int sched_setaffinity(int pid, size_t cpusetsize, const cpu_set_t *set) { (void)pid; (void)cpusetsize; (void)set; return 0; }
static inline int sched_getaffinity(int pid, size_t cpusetsize, cpu_set_t *set) { (void)pid; (void)cpusetsize; if (set) CPU_ZERO(set); return 0; }
