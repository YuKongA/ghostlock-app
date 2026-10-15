/** @file jni/ops_43499.cpp
 *  43499's op bodies: read the request, check what has to be in place, call the landing, name the
 *  answer (see the header for what this unit deliberately does not do).
 */
#include "jni/ops_43499.hpp"

#include <span>

namespace ghostlock::jni {
    namespace {
        /** Write an ASCII refusal into the reply. Kept local so both ends spell it the same way. */
        void refuse(FrameWriter &reply, std::string_view text) noexcept {
            static_cast<void>(reply.put_bytes(
                FrameKey::Text,
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()),
                                           text.size())));
        }

        /** A named "this op is not wired yet" refusal, with the op's own name in it. */
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
            buffer[at] = '\0';
            refuse(reply, std::string_view(buffer, at));
        }

        /** Require a u64 entry, naming the key that is missing. */
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

    void Ops43499::publish_facts(FrameWriter &reply) noexcept {
        std::uint64_t flags = 0U;
        if (facts_.prepared) {
            flags |= kFactPrepared;
            static_cast<void>(reply.put_u64(FrameKey::PageBase, facts_.page_base));
            static_cast<void>(reply.put_u64(FrameKey::Word0, facts_.word0));
            static_cast<void>(reply.put_u64(FrameKey::Word1, facts_.word1));
            static_cast<void>(reply.put_u64(FrameKey::PayloadPage, facts_.payload_page));
            static_cast<void>(reply.put_u64(FrameKey::PayloadBase, facts_.payload_base));
        }
        if (facts_.discovered) {
            flags |= kFactDiscovered;
            static_cast<void>(reply.put_u64(FrameKey::Found, facts_.found));
            static_cast<void>(reply.put_u64(FrameKey::AliasDelta, facts_.alias_delta));
        }
        if (facts_.victim_spawned) {
            flags |= kFactVictim;
            static_cast<void>(reply.put_u64(FrameKey::VictimTask, facts_.victim_task));
            static_cast<void>(reply.put_u64(FrameKey::VictimPid, facts_.victim_pid));
        }
        static_cast<void>(reply.put_u64(FrameKey::Flags, flags));
    }

    FrameStatus Ops43499::on_prepare(const FrameReader &request, FrameWriter &reply) noexcept {
        static_cast<void>(request);
        if (ops_.prepare == nullptr) {
            refuse_not_wired(reply, "prepare");
            return FrameStatus::Unsupported;
        }
        if (!ops_.prepare(facts_, ops_.context)) {
            refuse(reply, "prepare-refused");
            return FrameStatus::Failed;
        }
        facts_.prepared = true;
        publish_facts(reply);
        return FrameStatus::Ok;
    }

    FrameStatus Ops43499::on_discover(const FrameReader &request, FrameWriter &reply) noexcept {
        static_cast<void>(request);
        if (ops_.discover == nullptr) {
            refuse_not_wired(reply, "discover");
            return FrameStatus::Unsupported;
        }
        if (!ops_.discover(facts_, ops_.context)) {
            refuse(reply, "discovery-refused");
            return FrameStatus::Failed;
        }
        facts_.discovered = facts_.found != 0U;
        publish_facts(reply);
        return facts_.discovered ? FrameStatus::Ok : FrameStatus::Failed;
    }

    FrameStatus Ops43499::on_spawn_victim(const FrameReader &request, FrameWriter &reply) noexcept {
        static_cast<void>(request);
        if (ops_.spawn_victim == nullptr) {
            refuse_not_wired(reply, "spawn_victim");
            return FrameStatus::Unsupported;
        }
        if (!ops_.spawn_victim(facts_, ops_.context)) {
            refuse(reply, "victim-spawn-refused");
            return FrameStatus::Failed;
        }
        facts_.victim_spawned = true;
        publish_facts(reply);
        return FrameStatus::Ok;
    }

    FrameStatus Ops43499::on_victim_probe(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.victim_probe == nullptr) {
            refuse_not_wired(reply, "victim_probe");
            return FrameStatus::Unsupported;
        }
        std::uint64_t kind = 0U;
        if (!want_u64(request, FrameKey::Mode, "mode", kind, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::uint64_t value = 0U;
        if (!ops_.victim_probe(kind, value, ops_.context)) {
            refuse(reply, "victim-probe-refused");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_u64(FrameKey::Value, value));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43499::on_write_word(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.land == nullptr) {
            refuse_not_wired(reply, "write_word");
            return FrameStatus::Unsupported;
        }
        std::uint64_t address = 0U;
        std::uint64_t raw_form = 0U;
        if (!want_u64(request, FrameKey::Address, "address", address, reply)) {
            return FrameStatus::KeyMissing;
        }
        if (!want_u64(request, FrameKey::Form, "form", raw_form, reply)) {
            return FrameStatus::KeyMissing;
        }
        const WriteForm form = write_form_of(raw_form);
        if (form == WriteForm::Count) {
            refuse(reply, "write-refused:form");
            return FrameStatus::Unsupported;
        }
        std::uint64_t value = 0U;
        const bool value_given = request.find_u64(FrameKey::Value, value);
        if (form == WriteForm::ValuePreserving && !value_given) {
            /* A value write without a value is a caller mistake, not a zero write. */
            refuse(reply, "write-refused:value-missing");
            return FrameStatus::KeyMissing;
        }
        if (form != WriteForm::ValuePreserving && value_given && value != 0U) {
            /* The clear forms write nothing: a value here means the caller expected another shape. */
            refuse(reply, "write-refused:value-not-empty");
            return FrameStatus::Unsupported;
        }
        std::uint64_t offset = 0U;
        static_cast<void>(request.find_u64(FrameKey::Offset, offset));
        if (const char *const why = facts_.ready_for_write(); why != nullptr) {
            refuse(reply, why);
            return FrameStatus::NotReady;
        }
        if (!ops_.land(address, value, form, offset, ops_.context)) {
            refuse(reply, "write-refused");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_u64(FrameKey::Width, 8U));
        static_cast<void>(reply.put_u64(FrameKey::Value, value));
        return FrameStatus::Ok;
    }

    FrameStatus Ops43499::on_read_word(const FrameReader &request, FrameWriter &reply) noexcept {
        if (ops_.read == nullptr) {
            /* The honest answer today: this backend's route cannot read, so a read is unsupported
             *  rather than a failure -- a caller must not retry something that does not exist. */
            refuse_not_wired(reply, "read_word");
            return FrameStatus::Unsupported;
        }
        std::uint64_t address = 0U;
        if (!want_u64(request, FrameKey::Address, "address", address, reply)) {
            return FrameStatus::KeyMissing;
        }
        std::uint64_t value = 0U;
        if (!ops_.read(address, value, ops_.context)) {
            refuse(reply, "read-refused");
            return FrameStatus::Failed;
        }
        static_cast<void>(reply.put_u64(FrameKey::Value, value));
        return FrameStatus::Ok;
    }

    namespace {
        FrameStatus bundle_handle(void *self, FrameOp op, const FrameReader &request,
                                  FrameWriter &reply) noexcept {
            return static_cast<Ops43499 *>(self)->handle(op, request, reply);
        }
        void bundle_destroy(void *self) noexcept { delete static_cast<Ops43499 *>(self); }
    }  // namespace

    BackendOps make_ops_43499(Ops43499Seams device_ops) noexcept {
        auto *const ops = new Ops43499(device_ops);
        return BackendOps{&bundle_handle, &bundle_destroy, ops};
    }

    FrameStatus Ops43499::handle(FrameOp op, const FrameReader &request, FrameWriter &reply) noexcept {
        switch (op) {
            case FrameOp::Prepare: return on_prepare(request, reply);
            case FrameOp::Discover: return on_discover(request, reply);
            case FrameOp::SpawnVictim: return on_spawn_victim(request, reply);
            case FrameOp::VictimProbe: return on_victim_probe(request, reply);
            case FrameOp::WriteWord: return on_write_word(request, reply);
            case FrameOp::ReadWord: return on_read_word(request, reply);
            default: break;
        }
        /* An op this backend does not have at all: named, so silence is never read as success. */
        refuse(reply, "op-unknown-for-backend");
        return FrameStatus::Unsupported;
    }
}  // namespace ghostlock::jni
