#ifndef GHOSTLOCK_HOST_LINUX_FUTEX_H
#define GHOSTLOCK_HOST_LINUX_FUTEX_H

/*
 * Host shim: macOS has no <linux/futex.h>. The host attack data-flow test
 * never issues a futex syscall (the race/route unit is stubbed), so no
 * constants are needed; this only satisfies common.h's include.
 */

#endif
