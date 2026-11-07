#ifndef GHOSTLOCK_DIAGNOSE_API_H
#define GHOSTLOCK_DIAGNOSE_API_H

/**
 * @file backends/diagnose/api/diagnose_api.h
 * @brief Ordinary-C mapping for the app-bundled Diagnose native/log/handoff chain.
 * @note Kotlin/JNA and this DSO are released together. The interface has no version, size, reserved, or legacy
 *       compatibility fields; the function return is the sole call-status authority.
 */

#include <stdint.h>

#define GL_DIAGNOSE_HANDOFF_NOT_RUN INT32_MIN

#define GL_DIAGNOSE_STATUS_OK INT32_C(0)
#define GL_DIAGNOSE_STATUS_NULL_REQUEST INT32_C(1)
#define GL_DIAGNOSE_STATUS_NULL_RESULT INT32_C(2)
#define GL_DIAGNOSE_STATUS_RESULT_SENTINEL INT32_C(3)
#define GL_DIAGNOSE_STATUS_LOG_ATTACH INT32_C(4)
#define GL_DIAGNOSE_STATUS_INTERNAL_EXCEPTION INT32_C(5)

#if defined(__GNUC__) || defined(__clang__)
#define GL_DIAGNOSE_EXPORT __attribute__((visibility("default")))
#else
#define GL_DIAGNOSE_EXPORT
#endif

#if defined(__cplusplus)
#define GL_DIAGNOSE_NOEXCEPT noexcept
extern "C" {
#else
#define GL_DIAGNOSE_NOEXCEPT
#endif

/** Caller-owned inputs for one synchronous native identity and script-handoff chain. */
typedef struct gl_diagnose_request {
    uint64_t nonce;
    int32_t native_log_fd;
    int32_t shell_log_fd;
} gl_diagnose_request;

/** Caller-owned outputs committed once after the helper has returned and its shell child has been reaped. */
typedef struct gl_diagnose_result {
    int32_t uid;
    int32_t pid;
    uint64_t echoed_nonce;
    int32_t handoff_status;
} gl_diagnose_result;

/**
 * @brief Execute one bounded identity, Native-log, common-handoff, and Shell-log chain.
 * @param request Inputs containing the nonce and two borrowed log fds.
 * @param result Output initialized with `handoff_status=INT32_MIN`.
 * @return One GL_DIAGNOSE_STATUS_* call status; outputs are trustworthy only when this is OK.
 * @note The implementation duplicates and detaches each log fd in a separate window, calls `run_handoff()`
 *       exactly once, retains no pointer/fd/thread/callback/handle, and commits the result only at the end.
 */
GL_DIAGNOSE_EXPORT int32_t gl_diagnose_run(
    const gl_diagnose_request *request,
    gl_diagnose_result *result
) GL_DIAGNOSE_NOEXCEPT;

#if defined(__cplusplus)
}
#endif

#undef GL_DIAGNOSE_NOEXCEPT

#endif /* GHOSTLOCK_DIAGNOSE_API_H */
