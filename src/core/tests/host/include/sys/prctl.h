#ifndef GHOSTLOCK_HOST_SYS_PRCTL_H
#define GHOSTLOCK_HOST_SYS_PRCTL_H

/*
 * Host shim: macOS has no <sys/prctl.h>. The attack data-flow test does not
 * call prctl (the route/heap units are stubbed); this only satisfies
 * common.h's include and any inline constant reference.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PR_SET_PDEATHSIG
#define PR_SET_PDEATHSIG 1
#endif
#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4
#endif
#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif
#ifndef PR_GET_NAME
#define PR_GET_NAME 16
#endif
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif
#ifndef PR_SET_SECCOMP
#define PR_SET_SECCOMP 22
#endif

int prctl(int option, ...);

#ifdef __cplusplus
}
#endif

#endif
