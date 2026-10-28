#pragma once
#include <stdarg.h>
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_SET_PDEATHSIG 1
static inline int prctl(int option, ...) { (void)option; return 0; }
