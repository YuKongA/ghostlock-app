#ifndef GHOSTLOCK_43284_GLUED_M0_MARKER_H
#define GHOSTLOCK_43284_GLUED_M0_MARKER_H

#include <stddef.h>

/** Configure the insmod worker argument for one launcher-owned run directory.
 *
 * @param run_directory absolute path ending in one 32-character lowercase hex ID.
 * @param argument writable assembly data beginning with `glk_worker=`.
 * @param capacity complete writable argument-buffer capacity, including prefix and NUL.
 * @return zero when the bounded worker path was stored; nonzero for invalid input.
 * @note The value is one argv token and is not interpreted as shell source.
 */
int m0_prepare_worker_argument(const char *run_directory, char *argument, size_t capacity);

/** Return zero when the identity-bound ROOT_READY marker can be read by the launcher. */
int m0_root_ready_exists(const char *run_directory);

/** Execute the system-root worker branch of the M0 fixture ELF. */
int m0_root_worker_main(const char *run_directory);

/** Create one RELEASE_FINISH test frame and wait until the root worker exits. */
int m0_launcher_release(const char *run_directory);

#endif
