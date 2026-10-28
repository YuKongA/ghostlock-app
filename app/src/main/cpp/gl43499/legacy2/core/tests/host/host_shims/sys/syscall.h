#pragma once
#include_next <sys/syscall.h>
#ifndef SYS_futex
#define SYS_futex 202
#endif
#ifndef SYS_gettid
#define SYS_gettid 186
#endif
#ifndef SYS_memfd_create
#define SYS_memfd_create 319
#endif
#ifndef SYS_exit_group
#define SYS_exit_group 231
#endif
#ifndef SYS_clone
#define SYS_clone 56
#endif
#ifndef SYS_timerfd_create
#define SYS_timerfd_create 283
#endif
