#ifndef GLK_HOST_SYS_PRCTL_STUB_H
#define GLK_HOST_SYS_PRCTL_STUB_H

#define PR_SET_NAME 15
#define CLONE_NEWUSER 0x10000000
#define CLONE_NEWNET 0x40000000

static inline int prctl(int option, ...) {
  (void)option;
  return 0;
}

static inline int unshare(int flags) {
  (void)flags;
  return 0;
}

#endif
