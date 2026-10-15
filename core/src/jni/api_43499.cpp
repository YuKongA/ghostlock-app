/** @file jni/api_43499.cpp
 *  43499 函数面的实现：规则都在这里，设备动作在缝后面。
 *
 *  这些规则与旧的帧实现逐条相同（形状极性与事实门是同一套判据），差别只在"怎么把请求交进来"：
 *  以前是解码一帧字节，现在是一次具名函数调用。
 */
#include "jni/api_43499.hpp"

namespace ghostlock::jni {
    namespace {
        /** 初始化参数里第一个缺失的字段名（0 表示没给够）。 */
        [[nodiscard]] const char *missing_initialize_field(const Initialize43499 &params) noexcept {
            if (params.route_id == 0U) {
                return "route";
            }
            if (params.buffer_size == 0U) {
                return "buffer_size";
            }
            if (params.task_offset == 0U) {
                return "task_offset";
            }
            if (params.lock_offset == 0U) {
                return "lock_offset";
            }
            return nullptr;
        }
    }  // namespace

    void Backend43499::publish(Snapshot43499 &out) const noexcept {
        out.facts = facts_;
        std::uint64_t flags = kCapPiWindow;
        if (facts_.prepared) {
            flags |= kFactPrepared;
        }
        if (facts_.discovered) {
            flags |= kFactDiscovered;
        }
        if (facts_.victim_spawned) {
            flags |= kFactVictim;
        }
        out.flags = flags;
        out.width = k43499Width;
        out.phase = static_cast<std::uint64_t>(phase_);
    }

    CallCode Backend43499::initialize(const Initialize43499 &params) noexcept {
        if (const char *const absent = missing_initialize_field(params); absent != nullptr) {
            /* 名字要拼出来：前缀 + 字段名（不能用 string_view + const char* 直接相加）。 */
            char named[64] = {};
            std::size_t at = 0U;
            for (const char *cursor = "bad-parameters:"; *cursor != 0 && at + 1U < sizeof(named); ++cursor) {
                named[at++] = *cursor;
            }
            for (const char *cursor = absent; *cursor != 0 && at + 1U < sizeof(named); ++cursor) {
                named[at++] = *cursor;
            }
            return refuse(refusal_, CallCode::BadParameters, std::string_view(named, at));
        }
        params_ = params;
        initialized_ = true;
        phase_ = api::Phase::Ready;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::shutdown() noexcept {
        /* 幂等：再关一次只是回答 Ok（已经是关闭态）。 */
        if (phase_ != api::Phase::Closed) {
            phase_ = api::Phase::Closed;
            initialized_ = false;
            facts_ = Facts43499{};
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::snapshot(Snapshot43499 &out) noexcept {
        publish(out);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::prepare(Snapshot43499 &out) noexcept {
        if (seams_.prepare == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:prepare");
        }
        if (!seams_.prepare(facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "prepare-refused");
        }
        facts_.prepared = true;
        publish(out);
        return CallCode::Ok;
    }

    CallCode Backend43499::discover(Snapshot43499 &out) noexcept {
        if (seams_.discover == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:discover");
        }
        if (!seams_.discover(facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "discovery-refused");
        }
        facts_.discovered = facts_.found != 0U;
        publish(out);
        return facts_.discovered ? CallCode::Ok : CallCode::Failed;
    }

    CallCode Backend43499::spawn_victim(Snapshot43499 &out) noexcept {
        if (seams_.spawn_victim == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:spawn_victim");
        }
        if (!seams_.spawn_victim(facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "victim-spawn-refused");
        }
        facts_.victim_spawned = true;
        publish(out);
        return CallCode::Ok;
    }

    CallCode Backend43499::victim_probe(std::uint64_t kind, std::uint64_t &value) noexcept {
        if (seams_.victim_probe == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:victim_probe");
        }
        if (!seams_.victim_probe(kind, value, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "victim-probe-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::write_word(const WriteWord43499 &request) noexcept {
        if (seams_.land == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:write_word");
        }
        if (request.form == WriteForm::Count) {
            return refuse(refusal_, CallCode::BadParameters, "write-refused:form");
        }
        if (request.form == WriteForm::ValuePreserving && !request.value_given) {
            /* 要写值却没给值：是调用方的错，不是"写零"。 */
            return refuse(refusal_, CallCode::BadParameters, "write-refused:value-missing");
        }
        if (request.form != WriteForm::ValuePreserving && request.value_given && request.value != 0U) {
            /* clear 形状不写值：这里非零说明调用方以为自己在用另一种形状。 */
            return refuse(refusal_, CallCode::BadParameters, "write-refused:value-not-empty");
        }
        if (const char *const why = facts_.ready_for_write(); why != nullptr) {
            return refuse(refusal_, CallCode::NotReady, why);
        }
        if (!seams_.land(request.address, request.value, request.form, request.offset, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "write-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::read_word(std::uint64_t address, std::uint64_t &value) noexcept {
        if (seams_.read == nullptr) {
            /* 今天诚实的答案：这个 backend 的路由不能读，所以"读"是 unsupported 而不是失败 —— */
            /* 调用方不该去重试一个不存在的能力。 */
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:read_word");
        }
        if (!seams_.read(address, value, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "read-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }
}  // namespace ghostlock::jni
