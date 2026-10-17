#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "kernel/offset.h"
#include "kernel/runtime_struct_offsets.h"
#include "memory/address_space.h"
#include "memory/payload_builder.h"
#include "session/runtime_config.h"
#include "support/time.h"
#include "memory/heap_context.h"
#include "race/pi_race.h"
#include "session/exploit_session.hpp"

#include "kernel/constants.hpp"

#include <dirent.h>
#include <cerrno>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <ctime>
#include <unistd.h>

#include "kernelsnitch/utils.h"

/* v11.2: non-fatal step logger for the race window. kernelsnitch/utils.h
 * defines pr_error() as "log + exit(-1)" in app builds; the first v11.1
 * live run showed one missed futex op there kills the whole process
 * (exit 255) and every other PI worker with it. Anything inside the
 * waiter/owner/consumer window must log plainly instead. */
#define pr_step(fmt, ...) printf("[x] " fmt, ##__VA_ARGS__)

#include "profile/accessors.hpp"
#include "support/decls.hpp"
#include "route/route_api.hpp"

#endif
