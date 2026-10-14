#pragma once
/** @file jni/session_object.hpp
 *  One session: a backend, a phase, and the ops that may run inside it.
 *
 *  The boundary's shape in one file: Kotlin creates a session (this object plus an entry in the
 *  handle table), calls ops on it, and shuts it down. Everything the session owns -- the backend
 *  access -- it takes in open() and gives back in shutdown(), in that order and no other.
 *
 *  Honesty about what is wired: at this stage only Snapshot is implemented for real, and
 *  Initialize is wired generically (the backend is asked with the empty parameter set, which a real
 *  backend refuses by name until its parameter set arrives in its own batch). Every other op
 *  answers Unsupported with a "op-not-wired:<name>" refusal, so a caller can never mistake silence
 *  for success -- which is exactly the state the plan's batches J3/J4 fill in.
 *
 *  JNI-free on purpose: the JNI entry point (session_api.cpp) is a thin adapter, and the whole
 *  state machine is exercised on the host with a fake backend.
 */
#include <cstdint>
#include <memory>
#include <string_view>

#include "contract/kernel_access.hpp"
#include "jni/session_protocol.hpp"

namespace ghostlock::jni {
    /** Where a session is. Named because a caller acts differently on each. */
    enum class SessionPhase : std::uint8_t {
        Declared = 0,     /**< created; the backend exists but is not up. */
        Ready = 1,        /**< the backend came up and serves ops. */
        Failed = 2,       /**< creation, initialize or shutdown failed; the mask says how far. */
        Closed = 3,       /**< shut down deliberately; nothing is touched after this. */
    };

    [[nodiscard]] constexpr const char *session_phase_name(SessionPhase phase) noexcept {
        switch (phase) {
            case SessionPhase::Declared: return "declared";
            case SessionPhase::Ready: return "ready";
            case SessionPhase::Failed: return "failed";
            case SessionPhase::Closed: return "closed";
        }
        return "unknown";
    }

    /** The capability bits Snapshot publishes in its Flags entry. */
    inline constexpr std::uint64_t kCapPiWindow = 1U << 0U;
    inline constexpr std::uint64_t kCapKernelModule = 1U << 1U;
    inline constexpr std::uint64_t kCapPageCache = 1U << 2U;
    inline constexpr std::uint64_t kCapReadable = 1U << 3U;
    inline constexpr std::uint64_t kWidthBitShift = 8U;  /**< bit 8 = width 8, bit 16 = width 16. */

    /**
     *  What a backend own ops are: **not** an interface. A function pointer and an opaque
     *  self, because this tree confines virtual dispatch to the access interface
     *  (tools/window_body_guard.py + the structural check): a new abstract class here would be
     *  refused by the gate, and rightly so.
     */
    struct BackendOps final {
        /** Handle one op that belongs to this backend. @return the status of the reply. */
        FrameStatus (*handle)(void *self, FrameOp op, const FrameReader &request,
                              FrameWriter &reply) noexcept = nullptr;
        /** Destroy the ops object: the session does not know its type, so it is told how. */
        void (*destroy)(void *self) noexcept = nullptr;
        void *self = nullptr;

        [[nodiscard]] bool wired() const noexcept { return handle != nullptr; }
    };

    /** How a session gets a backend and its ops. Injected so the host can run on a fake. */
    struct BackendSeam final {
        /** @return the backend, or null when this build has none for that id. */
        std::unique_ptr<contract::KernelAccess> (*create)(std::uint64_t backend_id,
                                                          void *context) noexcept = nullptr;
        void *context = nullptr;
        /** @return the ops of this backend (an empty bundle when they are not wired yet). */
        BackendOps (*create_ops)(std::uint64_t backend_id, void *context) noexcept = nullptr;
    };

    /** One session's state machine and op dispatch. */
    class Session final {
      public:
        Session() noexcept = default;
        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;
        ~Session() noexcept;

        /**
         *  Create the backend and hold it. Nothing is brought up yet: that is Initialize.
         *
         *  @param backend_id the BackendId the caller asked for.
         *  @param seam       the factory; a null seam or a null result leaves the session Failed.
         *  @param generation the handle generation this session was minted with (for Snapshot).
         *  @return true when a backend was taken; false when the session is already open or the
         *          build has no such backend.
         */
        [[nodiscard]] bool open(std::uint64_t backend_id, const BackendSeam &seam,
                                std::uint32_t generation) noexcept;

        /**
         *  Run one op. Serialised by the caller; a call that arrives while one is running is Busy.
         *
         *  @param op      the op the adapter read out of the frame (FrameReader::open).
         *  @param request the validated request.
         *  @param reply   the answer frame under construction.
         */
        [[nodiscard]] FrameStatus invoke(FrameOp op, const FrameReader &request,
                                        FrameWriter &reply) noexcept;

        /** Publish the phase and the capability flags as an answer frame. */
        [[nodiscard]] FrameStatus snapshot(FrameWriter &reply) noexcept;

        /**
         *  Take the backend down. Idempotent: the second call answers with the first call's status
         *  and does not touch the backend again. A shutdown that failed leaves the session Failed,
         *  where a later call may retry the teardown (that is the remaining disarm, and nothing
         *  else is allowed in that phase).
         */
        [[nodiscard]] FrameStatus shutdown() noexcept;

        [[nodiscard]] SessionPhase phase() const noexcept { return phase_; }
        [[nodiscard]] bool inside() const noexcept { return inside_; }
        [[nodiscard]] std::uint64_t backend_id() const noexcept { return backend_id_; }

      private:
        [[nodiscard]] FrameStatus dispatch(FrameOp op, const FrameReader &request,
                                           FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus reflect_init_status(contract::KernelAccess::Status status,
                                                      FrameWriter &reply) noexcept;
        void refuse(FrameWriter &reply, const char *name) noexcept;

        std::unique_ptr<contract::KernelAccess> access_{};
        BackendOps ops_{};
        SessionPhase phase_ = SessionPhase::Declared;
        std::uint64_t backend_id_ = 0U;
        std::uint32_t generation_ = 0U;
        bool inside_ = false;
        bool opened_ = false;
        /** The status the first shutdown answered; later calls repeat it. */
        FrameStatus shutdown_status_ = FrameStatus::NotReady;
        bool shutdown_done_ = false;
    };
}  // namespace ghostlock::jni
