/** @file boundary/protocol/session_object.cpp
 *  The session state machine: open, invoke, snapshot, shutdown (see the header for the rules).
 */
#include "boundary/protocol/session_object.hpp"

namespace ghostlock::jni {
    namespace {
        /** Put a named refusal into the reply and answer the status it belongs to. */
        [[nodiscard]] const char *op_refusal_prefix() noexcept { return "op-not-wired:"; }
    }  // namespace

    Session::~Session() noexcept {
        if (ops_.destroy != nullptr) {
            ops_.destroy(ops_.self);
            ops_ = BackendOps{};
        }
        /* A session that was never shut down still gives its backend back: the destructor is the
         *  last chance, and skipping it would leak whatever the backend took. */
        if (access_ != nullptr && !shutdown_done_) {
            static_cast<void>(access_->shutdown());
        }
    }

    bool Session::open(std::uint64_t backend_id, const BackendFactory &landing,
                       std::uint32_t generation) noexcept {
        if (opened_) {
            return false;
        }
        opened_ = true;
        backend_id_ = backend_id;
        generation_ = generation;
        if (landing.create == nullptr) {
            phase_ = SessionPhase::Failed;
            return false;
        }
        access_ = landing.create(backend_id, landing.context);
        if (landing.create_ops != nullptr) {
            ops_ = landing.create_ops(backend_id, landing.context);
        }
        if (access_ == nullptr) {
            /* Asked for a backend this build does not have. Not a crash and not a stand-in. */
            phase_ = SessionPhase::Failed;
            return false;
        }
        phase_ = SessionPhase::Declared;
        return true;
    }

    void Session::refuse(FrameWriter &reply, const char *name) noexcept {
        const std::string_view text(name);
        static_cast<void>(reply.put_bytes(FrameKey::Text,
                                          std::span<const std::byte>(
                                              reinterpret_cast<const std::byte *>(text.data()),
                                              text.size())));
    }

    FrameStatus Session::reflect_init_status(contract::KernelAccess::Status status,
                                             FrameWriter &reply) noexcept {
        using Status = contract::KernelAccess::Status;
        switch (status) {
            case Status::Ok:
                return FrameStatus::Ok;
            case Status::OutOfRange:
                refuse(reply, "out-of-range");
                return FrameStatus::OutOfRange;
            case Status::NotInitialized:
                refuse(reply, "not-ready");
                return FrameStatus::NotReady;
            case Status::Unsupported:
                refuse(reply, "unsupported");
                return FrameStatus::Unsupported;
            case Status::BadWidth:
                refuse(reply, "bad-width");
                return FrameStatus::BadWidth;
            case Status::BadParameters:
                /* The backend refused the parameter set it was handed: named, because the caller
                 *  has to know that the parameters -- not the backend -- are what is missing. */
                refuse(reply, "bad-parameters");
                return FrameStatus::Failed;
            case Status::Failed:
                refuse(reply, "call-failed");
                return FrameStatus::Failed;
        }
        refuse(reply, "unknown");
        return FrameStatus::Failed;
    }

    FrameStatus Session::snapshot(FrameWriter &reply) noexcept {
        static_cast<void>(reply.put_u64(FrameKey::Phase, static_cast<std::uint64_t>(phase_)));
        static_cast<void>(reply.put_u64(FrameKey::Backend, backend_id_));
        static_cast<void>(reply.put_u64(FrameKey::Generation, static_cast<std::uint64_t>(generation_)));
        std::uint64_t flags = 0U;
        std::uint64_t width = 0U;
        if (access_ != nullptr) {
            using Channel = contract::AccessChannel;
            if (access_->supports(Channel::PiWindow)) {
                flags |= kCapPiWindow;
            }
            if (access_->supports(Channel::KernelModule)) {
                flags |= kCapKernelModule;
            }
            if (access_->supports(Channel::PageCache)) {
                flags |= kCapPageCache;
            }
            if (access_->supports_width(8U)) {
                flags |= (8U << kWidthBitShift);
                width = 8U;
            }
            if (access_->supports_width(16U)) {
                flags |= (16U << kWidthBitShift);
                width = 16U;
            }
            if (access_->state(access_->supports(Channel::PageCache) ? Channel::PageCache
                                                                     : Channel::PiWindow) ==
                contract::KernelAccess::AccessState::Ready) {
                flags |= kCapReadable;
            }
        }
        static_cast<void>(reply.put_u64(FrameKey::Flags, flags));
        static_cast<void>(reply.put_u64(FrameKey::Width, width));
        return FrameStatus::Ok;
    }

    FrameStatus Session::dispatch(FrameOp op, const FrameReader &request,
                                  FrameWriter &reply) noexcept {
        if (op == FrameOp::Snapshot) {
            return snapshot(reply);
        }
        if (access_ == nullptr) {
            /* No backend at all: every op but Snapshot answers "not ready", never a status that
             *  a caller could read as "the backend refused the work". */
            refuse(reply, "no-backend");
            return FrameStatus::NotReady;
        }
        if (op == FrameOp::Initialize) {
            if (phase_ == SessionPhase::Ready) {
                refuse(reply, "already-initialized");
                return FrameStatus::Failed;
            }
            if (phase_ == SessionPhase::Closed) {
                refuse(reply, "closed");
                return FrameStatus::Closed;
            }
            /* The empty parameter set on purpose: a real backend refuses it by name until its own
             *  batch wires the parameters the frame will carry. */
            const contract::KernelAccess::NoMiscParameters none{};
            const auto answer = access_->initialize(none);
            const FrameStatus status = reflect_init_status(answer.status, reply);
            phase_ = answer.ok() ? SessionPhase::Ready : SessionPhase::Failed;
            if (phase_ == SessionPhase::Ready) {
                static_cast<void>(reply.put_u64(FrameKey::Phase,
                                               static_cast<std::uint64_t>(phase_)));
            }
            return status;
        }
        if (ops_.wired()) {
            /* This backend has its own ops: the request goes there, and its answer is the answer. */
            return ops_.handle(ops_.self, op, request, reply);
        }
        static_cast<void>(request);
        /* Every op that a later batch owns answers by name: silence would read as success. */
        const std::string_view name(frame_op_name(op));
        if (name.size() + 16U < 64U) {
            char buffer[64] = {};
            std::size_t at = 0U;
            for (const char *cursor = op_refusal_prefix(); *cursor != '\0' && at < sizeof(buffer); ++cursor) {
                buffer[at++] = *cursor;
            }
            for (const char c : name) {
                if (at + 1U < sizeof(buffer)) {
                    buffer[at++] = c;
                }
            }
            buffer[at] = '\0';
            refuse(reply, buffer);
        } else {
            refuse(reply, "op-not-wired");
        }
        return FrameStatus::Unsupported;
    }

    FrameStatus Session::invoke(FrameOp op, const FrameReader &request, FrameWriter &reply) noexcept {
        if (inside_) {
            refuse(reply, "busy");
            return FrameStatus::Busy;
        }
        inside_ = true;
        FrameStatus status = FrameStatus::Ok;
        if (phase_ == SessionPhase::Closed) {
            refuse(reply, "closed");
            status = FrameStatus::Closed;
        } else {
            status = dispatch(op, request, reply);
        }
        inside_ = false;
        return status;
    }

    FrameStatus Session::shutdown() noexcept {
        if (access_ == nullptr) {
            phase_ = SessionPhase::Closed;
            shutdown_done_ = true;
            shutdown_status_ = FrameStatus::NotReady;
            return shutdown_status_;
        }
        if (shutdown_done_ && phase_ != SessionPhase::Failed) {
            /* Idempotent: the same answer, and the backend is not touched a second time. */
            return shutdown_status_;
        }
        const auto answer = access_->shutdown();
        shutdown_done_ = true;
        if (answer.ok()) {
            phase_ = SessionPhase::Closed;
            shutdown_status_ = FrameStatus::Ok;
        } else {
            /* A shutdown that did not come back cleanly is not a closed session: the mask may still
             *  be armed, and claiming Closed would be the lie this layer exists to avoid. */
            phase_ = SessionPhase::Failed;
            shutdown_status_ = FrameStatus::Failed;
        }
        return shutdown_status_;
    }
}  // namespace ghostlock::jni
