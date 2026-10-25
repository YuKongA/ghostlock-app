#ifndef GHOSTLOCK_HOST_LINUX_MEMFD_H
#define GHOSTLOCK_HOST_LINUX_MEMFD_H

/*
 * Host shim: macOS has no <linux/memfd.h>. The host attack data-flow test does
 * not create memfds (heap/route units are stubbed); this only satisfies
 * common.h's include.
 */

#endif
