/** @file jni/ops_43284.cpp
 *  43284's op bodies: read the request, check the width and the token, call the landing, name the
 *  answer (see the header for what this unit deliberately leaves to the device).
 */
#include "jni/ops_43284.hpp"

namespace ghostlock::jni {
    namespace {
        void refuse(FrameWriter &reply, std::string_view text) noexcept {
            static_cast<void>(reply.put_bytes(
                FrameKey::Text,
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()),
                                           text.size())));
        }

        void refuse_not_wired(FrameWriter &reply, std::string_view op_name) noexcept {
            char buffer[64] = {};
            std::size_t at = 0U;
            for (const char c : std::string_view("op-not-wired:")) {
                buffer[at++] = c;
            }
            for (const char c : op_name) {
                if (at + 1U >= sizeof(buffer)) {
                    break;
                }
                buffer[at++] = c;
            }
            refuse(reply, std::string_view(buffer, at));
        }

        [[nodiscard]] bool want_u64(const FrameReader &request, FrameKey key, const char *name,
                                    std::uint64_t &out, FrameWriter &reply) noexcept {
            if (request.find_u64(key, out)) {
                return true;
            }
            char buffer[64] = {};
            std::size_t at = 0U;
            for (const char c : std::string_view("key-missing:")) {
                buffer[at++] = c;
            }
            for (const char *p = name; *p != '\0' && at + 1U < sizeof(buffer); ++p) {
                buffer[at++] = *p;
            }
            refuse(reply, std::string_view(buffer, at));
            return false;
        }
    }  // namespace

    FrameStatus Ops43284::on_resolve(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.resolve == nullptr) {
            refuse_not_wired(reply, "resolve_target");
            return FrameStatus::Unsupported;
        }
        std::uint64_t raw_role = 0U;
        if (!want_u64(request, FrameKey::Mode, "mode", raw_role, reply)) {
            return FrameStatus::KeyMissing;
        }
        const TargetRole role = target_role_of(raw_role);
        if (role == TargetRole::Count) {
            refuse(reply, "role-unknown");
            return FrameStatus::Unsupported;
        }
        std::span<const std::byte> path{};
        if (!request.find_bytes(FrameKey::Path, path)) {
            refuse(reply, "key-missing:path");
            return FrameStatus::KeyMissing;
        }
        const std::string_view path_text(reinterpret_cast<const char *>(path.data()), path.size());
        std::uint64_t token = 0U;
        if (!ops_.resolve(raw_role, path_text, token, ops_.context) || token == 0U) {
            refuse(reply, "door-refused");
            return FrameStatus::Failed;
        }
        facts_.token = token;
        facts_.role = raw_role;
        facts_.resolved = true;
        static_cast<void>(reply.put_u64(FrameKey::Token, token));
        static_cast<void>(reply.put_u64(FrameKey::Mode, raw_role));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::on_write_block(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.write_block == nullptr) {
            refuse_not_wired(reply, "write_block");
            return FrameStatus::Unsupported;
        }
        std::uint64_t token = 0U;
        if (!want_u64(request, FrameKey::Token, "token", token, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::uint64_t offset = 0U;
        if (!want_u64(request, FrameKey::Offset, "offset", offset, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::span<const std::byte> block{};
        if (!request.find_bytes(FrameKey::Bytes, block)) {
            refuse(reply, "key-missing:bytes");
            return FrameStatus::KeyMissing;
        }
        if (block.size() != kBlockWidth) {
            /* The unit of this backend is one block: a caller asking for another width is refused
             *  rather than served a partial write. */
            refuse(reply, "bad-width");
            return FrameStatus::BadWidth;
        }
        if (const char *const why = facts_.ready_for_block(); why != nullptr) {
            refuse(reply, why);
            return FrameStatus::NotReady;
        }
        if (token != facts_.token) {
            /* A token this session never handed out (or one from another session): refused, never
             *  used as if it were the resolved target. */
            refuse(reply, "stale-target");
            return FrameStatus::StaleTarget;
        }
        std::uint64_t sequence = facts_.sequence;
        if (!ops_.write_block(token, offset, block, sequence, ops_.context)) {
            refuse(reply, "block-refused");
            return FrameStatus::Failed;
        }
        facts_.sequence = sequence;
        static_cast<void>(reply.put_u64(FrameKey::Sequence, sequence));
        static_cast<void>(reply.put_u64(FrameKey::Width, static_cast<std::uint64_t>(kBlockWidth)));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::on_read_block(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.read_block == nullptr) {
            refuse_not_wired(reply, "read_block");
            return FrameStatus::Unsupported;
        }
        std::uint64_t token = 0U;
        if (!want_u64(request, FrameKey::Token, "token", token, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::uint64_t offset = 0U;
        if (!want_u64(request, FrameKey::Offset, "offset", offset, reply)) {
            return FrameStatus::KeyMissing;
        }
        if (const char *const why = facts_.ready_for_block(); why != nullptr) {
            refuse(reply, why);
            return FrameStatus::NotReady;
        }
        if (token != facts_.token) {
            refuse(reply, "stale-target");
            return FrameStatus::StaleTarget;
        }
        std::byte block[kBlockWidth] = {};
        if (!ops_.read_block(token, offset, std::span<std::byte>(block), ops_.context)) {
            refuse(reply, "read-refused");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_bytes(FrameKey::Bytes, std::span<const std::byte>(block)));
        static_cast<void>(reply.put_u64(FrameKey::Width, static_cast<std::uint64_t>(kBlockWidth)));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::on_trigger(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.trigger == nullptr) {
            refuse_not_wired(reply, "trigger");
            return FrameStatus::Unsupported;
        }
        std::uint64_t delay_ms = 0U;
        if (!want_u64(request, FrameKey::DelayMs, "delay_ms", delay_ms, reply)) {
            return FrameStatus::KeyMissing;
        }
        if (!ops_.trigger(delay_ms, ops_.context)) {
            refuse(reply, "trigger-refused");
            return FrameStatus::Failed;
        }
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::on_wait_terminal(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.wait_terminal == nullptr) {
            refuse_not_wired(reply, "wait_terminal");
            return FrameStatus::Unsupported;
        }
        std::uint64_t budget_ms = 0U;
        if (!want_u64(request, FrameKey::Mode, "mode", budget_ms, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::uint64_t poll_ms = 0U;
        if (!want_u64(request, FrameKey::PollMs, "poll_ms", poll_ms, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::span<const std::byte> module{};
        static_cast<void>(request.find_bytes(FrameKey::Text, module));
        const std::string_view module_name(reinterpret_cast<const char *>(module.data()), module.size());
        TerminalState terminal = TerminalState::InFlight;
        if (!ops_.wait_terminal(budget_ms, poll_ms, module_name, terminal, ops_.context)) {
            refuse(reply, "wait-failed");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_u64(FrameKey::Mode, static_cast<std::uint64_t>(terminal)));
        const std::string_view name(terminal_state_name(terminal));
        static_cast<void>(reply.put_bytes(
            FrameKey::Text,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(name.data()), name.size())));
        switch (terminal) {
            case TerminalState::Success:
                return FrameStatus::Ok;
            case TerminalState::Failure:
                return FrameStatus::Failed;
            case TerminalState::InFlight:
                /* The run has not concluded: claiming either way would be a guess, and this layer
                 *  does not guess. The caller freezes on Indeterminate. */
                return FrameStatus::Indeterminate;
        }
        return FrameStatus::Failed;
    }

    FrameStatus Ops43284::on_embedded_image(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.embedded_image == nullptr) {
            refuse_not_wired(reply, "embedded_image");
            return FrameStatus::Unsupported;
        }
        std::uint64_t role = 0U;
        if (!want_u64(request, FrameKey::Mode, "mode", role, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::span<const std::byte> image{};
        if (!ops_.embedded_image(role, image, ops_.context) || image.empty()) {
            refuse(reply, "image-unknown");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_bytes(FrameKey::Bytes, image));
        static_cast<void>(reply.put_u64(FrameKey::Length, static_cast<std::uint64_t>(image.size())));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::on_hook_words(const FrameReader &request, FrameWriter &reply) noexcept {
        static_cast<void>(request);
        if (ops_.hook_words == nullptr) {
            refuse_not_wired(reply, "hook_words");
            return FrameStatus::Unsupported;
        }
        std::uint64_t branch_word = 0U;
        std::uint64_t displaced_word = 0U;
        std::uint64_t site_offset = 0U;
        std::uint64_t landing_offset = 0U;
        std::span<const std::byte> payload{};
        if (!ops_.hook_words(branch_word, displaced_word, site_offset, landing_offset, payload,
                               ops_.context)) {
            refuse(reply, "plan-refused");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_u64(FrameKey::BranchWord, branch_word));
        static_cast<void>(reply.put_u64(FrameKey::DisplacedWord, displaced_word));
        static_cast<void>(reply.put_u64(FrameKey::SiteOffset, site_offset));
        static_cast<void>(reply.put_u64(FrameKey::LandingOffset, landing_offset));
        static_cast<void>(reply.put_bytes(FrameKey::Bytes, payload));
        static_cast<void>(reply.put_u64(FrameKey::Length, static_cast<std::uint64_t>(payload.size())));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43284::handle(FrameOp op, const FrameReader &request, FrameWriter &reply) noexcept {
        switch (op) {
            case FrameOp::ResolveTarget: return on_resolve(request, reply);
            case FrameOp::WriteBlock: return on_write_block(request, reply);
            case FrameOp::ReadBlock: return on_read_block(request, reply);
            case FrameOp::Trigger: return on_trigger(request, reply);
            case FrameOp::WaitTerminal: return on_wait_terminal(request, reply);
            case FrameOp::EmbeddedImage: return on_embedded_image(request, reply);
            case FrameOp::HookWords: return on_hook_words(request, reply);
            default: break;
        }
        refuse(reply, "op-unknown-for-backend");
        return FrameStatus::Unsupported;
    }

    namespace {
        FrameStatus bundle_handle(void *self, FrameOp op, const FrameReader &request,
                                  FrameWriter &reply) noexcept {
            return static_cast<Ops43284 *>(self)->handle(op, request, reply);
        }
        void bundle_destroy(void *self) noexcept { delete static_cast<Ops43284 *>(self); }
    }  // namespace

    BackendOps make_ops_43284(Ops43284Seams device_ops) noexcept {
        auto *const ops = new Ops43284(device_ops);
        return BackendOps{&bundle_handle, &bundle_destroy, ops};
    }
}  // namespace ghostlock::jni
