#ifndef GHOSTLOCK_RUN_HANDOFF_H
#define GHOSTLOCK_RUN_HANDOFF_H

/**
 * @file common/run_handoff.h
 * @brief Execute the fixed GhostLock root-script handoff in the caller's process context.
 * @note The helper owns only directory/script descriptors and the child it spawns. It borrows the current
 *       native log export fd without closing, retaining, or writing it directly.
 */

#include <stdint.h>

#define GL_HANDOFF_CHILD_EXIT_127_AMBIGUOUS INT32_C(127)

#define GL_HANDOFF_HOME_MISSING INT32_C(-1)
#define GL_HANDOFF_HOME_INVALID INT32_C(-2)
#define GL_HANDOFF_OPEN_HOME INT32_C(-3)
#define GL_HANDOFF_OPEN_DIRECTORY INT32_C(-4)
#define GL_HANDOFF_OPEN_SCRIPT INT32_C(-5)
#define GL_HANDOFF_SCRIPT_TYPE INT32_C(-6)
#define GL_HANDOFF_SCRIPT_MAGIC INT32_C(-7)
#define GL_HANDOFF_SPAWN_CALL INT32_C(-8)
#define GL_HANDOFF_WAIT INT32_C(-9)
#define GL_HANDOFF_CHILD_SIGNALED INT32_C(-10)
#define GL_HANDOFF_INTERNAL INT32_C(-11)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Locate, validate, spawn, and synchronously reap the fixed root handoff script.
 * @return Child exit 0..255, or one negative GL_HANDOFF_* infrastructure status.
 * @note Reads only `GHOSTLOCK_HOME/root-script/root-script.sh`; performs no uid check, scan, or fallback.
 *       The child naturally inherits the caller's uid, gid, capabilities, and SELinux context.
 */
int32_t run_handoff(void);

#if defined(__cplusplus)
}
#endif

#endif /* GHOSTLOCK_RUN_HANDOFF_H */
