#ifndef GHOSTLOCK_PROFILE_MACROS_H
#define GHOSTLOCK_PROFILE_MACROS_H

/* The home dir and root script path are read from the session snapshot at
 * every syscall/exec boundary. Keeping the reads inline means the call
 * sites pass stable c_str() pointers exactly where the kernel or exec
 * needs them and nowhere else, and no process-global path state is
 * exposed outside the session (CPP12/SESSION-01). */

#include "common.h"
#include "kernel/runtime_struct_offsets.h"

/* runtime_struct_offsets.h resolves every symbol/task offset from the loaded
 * profile and falls back to the compile-time target.h defaults. No macro here
 * re-reads the profile; consumers use the TargetProfile accessors. */

/* VR.ko anti-root fallback defines */
#ifndef VR_TAG_A_OFF
#define VR_TAG_A_OFF           0x06
#endif
#ifndef VR_TAG_B_OFF
#define VR_TAG_B_OFF           0x2c
#endif
#ifndef VR_SYSCALL_TP_FLAG
#define VR_SYSCALL_TP_FLAG     0x400ULL
#endif

#endif
