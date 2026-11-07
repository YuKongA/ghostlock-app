/**
 * @file backends/diagnose/diagnose.cpp
 * @brief Implement one bounded Diagnose native/log/common-handoff operation.
 * @note Validation and attachment failures leave the caller result unchanged; no exception crosses the C ABI.
 */

#include "backends/diagnose/api/diagnose_api.h"

#include "common/gl_log.h"
#include "common/run_handoff.h"

#include <cstdint>
#include <limits>
#include <sys/types.h>
#include <unistd.h>

namespace {
    /** Detaches the DSO-owned duplicate at the end of one Native or Shell attachment window. */
    class LogAttachment final {
    public:
        /** @param fd Borrowed caller descriptor duplicated into the DSO logger. */
        explicit LogAttachment(const int fd) noexcept : attached_(gl_log_attach_fd(fd, 0, GL_LOG_INFO) >= 0) {}

        /** Closes only the logger-owned duplicate; the caller retains its original descriptor. */
        ~LogAttachment() noexcept {
            if (attached_) {
                (void)gl_log_attach_fd(-1, 0, GL_LOG_INFO);
            }
        }

        /** @return True only when the logger owns a valid duplicate. */
        [[nodiscard]] bool attached() const noexcept { return attached_; }

        LogAttachment(const LogAttachment &) = delete;
        LogAttachment &operator=(const LogAttachment &) = delete;
        LogAttachment(LogAttachment &&) = delete;
        LogAttachment &operator=(LogAttachment &&) = delete;

    private:
        bool attached_;
    };

    /**
     * @brief Convert the current uid to the stable signed ABI field.
     * @param output Receives the converted uid.
     * @return True when the host value fits exactly.
     */
    bool current_uid(std::int32_t *const output) noexcept {
        if (output == nullptr) {
            return false;
        }
        const uid_t value = getuid();
        const std::uintmax_t wide = static_cast<std::uintmax_t>(value);
        if (wide > static_cast<std::uintmax_t>(std::numeric_limits<std::int32_t>::max())) {
            return false;
        }
        *output = static_cast<std::int32_t>(wide);
        return true;
    }

    /**
     * @brief Convert the current pid to the stable signed ABI field.
     * @param output Receives the converted pid.
     * @return True when the host value is nonnegative and fits exactly.
     */
    bool current_pid(std::int32_t *const output) noexcept {
        if (output == nullptr) {
            return false;
        }
        const pid_t value = getpid();
        if (value < 0) {
            return false;
        }
        const std::uintmax_t wide = static_cast<std::uintmax_t>(value);
        if (wide > static_cast<std::uintmax_t>(std::numeric_limits<std::int32_t>::max())) {
            return false;
        }
        *output = static_cast<std::int32_t>(wide);
        return true;
    }
}

extern "C" GL_DIAGNOSE_EXPORT int32_t gl_diagnose_run(
    const gl_diagnose_request *const request,
    gl_diagnose_result *const result
) noexcept {
    (void)gl_log_attach_fd(-1, 0, GL_LOG_INFO);
    try {
        if (request == nullptr) {
            return GL_DIAGNOSE_STATUS_NULL_REQUEST;
        }
        if (result == nullptr) {
            return GL_DIAGNOSE_STATUS_NULL_RESULT;
        }
        if (result->handoff_status != GL_DIAGNOSE_HANDOFF_NOT_RUN) {
            return GL_DIAGNOSE_STATUS_RESULT_SENTINEL;
        }

        std::int32_t uid = 0;
        std::int32_t pid = 0;
        if (!current_uid(&uid) || !current_pid(&pid)) {
            return GL_DIAGNOSE_STATUS_INTERNAL_EXCEPTION;
        }

        {
            const LogAttachment native_attachment(request->native_log_fd);
            if (!native_attachment.attached()) {
                return GL_DIAGNOSE_STATUS_LOG_ATTACH;
            }
            info("diagnose:native:roundtrip-ok");
        }

        std::int32_t handoff_status = GL_DIAGNOSE_HANDOFF_NOT_RUN;
        {
            const LogAttachment shell_attachment(request->shell_log_fd);
            if (!shell_attachment.attached()) {
                return GL_DIAGNOSE_STATUS_LOG_ATTACH;
            }
            handoff_status = run_handoff();
        }

        result->uid = uid;
        result->pid = pid;
        result->echoed_nonce = request->nonce;
        result->handoff_status = handoff_status;
        return GL_DIAGNOSE_STATUS_OK;
    } catch (...) {
        (void)gl_log_attach_fd(-1, 0, GL_LOG_INFO);
        return GL_DIAGNOSE_STATUS_INTERNAL_EXCEPTION;
    }
}
