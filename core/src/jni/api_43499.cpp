/** @file jni/api_43499.cpp
 *  43499 函数面的实现：规则都在这里，设备动作在缝后面，参数来自 D1 的单表生成。
 *
 *  这里是"参数流向"的中段：Kotlin DTO 经读取器变成 SessionParams43499 / 请求组 ⇒ 存在会话里 ⇒
 *  每个缝回调按引用收到它们。形状极性、事实门、未接线具名与旧实现逐条相同；新增的是契约要求的
 *  **代绑定**（prepare/discover/victim）与**受害者收尾**（retire / park / handoff）。
 */
#include "jni/api_43499.hpp"

namespace ghostlock::jni {
    namespace {
        /** 拼一条 <前缀><名字>（前缀是 bad-parameters: 或 stale-generation:）。 */
        [[nodiscard]] CallCode named_failure(Refusal &refusal, const CallCode code,
                                            const char *prefix, const char *name) noexcept {
            char named[96] = {};
            std::size_t at = 0U;
            for (const char *cursor = prefix; *cursor != '\0' && at + 1U < sizeof(named); ++cursor) {
                named[at++] = *cursor;
            }
            for (const char *cursor = name; *cursor != '\0' && at + 1U < sizeof(named); ++cursor) {
                named[at++] = *cursor;
            }
            return refuse(refusal, code, std::string_view(named, at));
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

    CallCode Backend43499::closed_guard() noexcept {
        if (phase_ == api::Phase::Closed) {
            return refuse(refusal_, CallCode::Closed, "session-closed");
        }
        return CallCode::Ok;
    }

    CallCode Backend43499::initialize(const SessionParams43499 &params) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        /* 必需字段按名字检查：缺哪个报哪个，不猜默认值。 */
        if (params.init.routeId == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "routeId");
        }
        if (params.init.waiterOffset == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "waiterOffset");
        }
        if (params.init.taskOffset == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "taskOffset");
        }
        if (params.init.lockOffset == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "lockOffset");
        }
        if (params.init.bufferSize == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "bufferSize");
        }
        if (params.layout.pointerBytes == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "pointerBytes");
        }
        params_ = params;
        phase_ = api::Phase::Ready;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::reject_initialize(const ReadFailure &failure) noexcept {
        /* 读取器的失败已经是具名的（分类 + 字段名）；这里把它变成 bad-parameters:<字段>。 */
        if (failure.field != nullptr) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", failure.field);
        }
        const char *const kind = failure.error == ReadError::TooLarge         ? "too-large"
                                 : failure.error == ReadError::NullObject     ? "null-object"
                                 : failure.error == ReadError::NullBytes      ? "null-bytes"
                                 : failure.error == ReadError::JniException   ? "jni-exception"
                                 : failure.error == ReadError::AllocationFailed
                                     ? "allocation-failed"
                                     : "invalid-field";
        return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", kind);
    }

    CallCode Backend43499::shutdown() noexcept {
        if (phase_ != api::Phase::Closed) {
            phase_ = api::Phase::Closed;
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

    CallCode Backend43499::prepare(const Prepare43499Values &request, Snapshot43499 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.prepare == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:prepare");
        }
        if (request.threads == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "threads");
        }
        if (request.pageSize == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "pageSize");
        }
        if (request.regionSize == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "regionSize");
        }
        if (request.objectSize == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "objectSize");
        }
        if (request.mmStructSize == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "mmStructSize");
        }
        if (!seams_.prepare(params_, request, facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "prepare-refused");
        }
        facts_.prepared = true;
        facts_.prepare_generation += 1U;
        publish(out);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::discover(const Discover43499Values &request, Snapshot43499 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.discover == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:discover");
        }
        if (request.rangeStart == 0 && request.rangeEnd == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "rangeStart");
        }
        if (request.patienceRounds == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "patienceRounds");
        }
        if (request.prepareGeneration != facts_.prepare_generation) {
            /* 搜索必须绑在"它要搜的那一次准备"上：拿旧代的参数来搜，描述的是另一个堆状态。 */
            return named_failure(refusal_, CallCode::Failed, "stale-generation:", "prepare");
        }
        if (!seams_.discover(params_, request, facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "discovery-refused");
        }
        facts_.discovered = facts_.found != 0U;
        facts_.discovery_generation += 1U;
        publish(out);
        if (!facts_.discovered) {
            return refuse(refusal_, CallCode::Failed, "not-found");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::spawn_victim(Snapshot43499 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.spawn_victim == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:spawn_victim");
        }
        if (!facts_.prepared || !facts_.discovered) {
            /* 受害者要落在已经准备好的堆里：前置不成立就不 fork。 */
            return refuse(refusal_, CallCode::NotReady, facts_.ready_for_write());
        }
        if (!seams_.spawn_victim(params_, facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "victim-spawn-refused");
        }
        if (facts_.victim_task == 0U) {
            return refuse(refusal_, CallCode::Failed, "victim-task-missing");
        }
        facts_.victim_spawned = true;
        facts_.victim_generation += 1U;
        publish(out);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::finish_victim(const FinishVictim43499Values &request,
                                         Snapshot43499 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.finish_victim == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:finish_victim");
        }
        if (request.action < 0 || request.action > 2) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "action");
        }
        if (!facts_.victim_spawned) {
            return refuse(refusal_, CallCode::NotReady, "not-ready:victim");
        }
        if (request.victimGeneration != facts_.victim_generation) {
            return named_failure(refusal_, CallCode::Failed, "stale-generation:", "victim");
        }
        if (!seams_.finish_victim(params_, request, facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "victim-finish-refused");
        }
        switch (static_cast<VictimAction>(request.action)) {
            case VictimAction::Park:
                /* 驻留：对象留着，代不变（后续步骤继续用同一代）。 */
                facts_.victim_handed_off = true;
                break;
            case VictimAction::Handoff:
                facts_.victim_handed_off = true;
                break;
            case VictimAction::Retire:
                facts_.victim_spawned = false;
                facts_.victim_handed_off = false;
                facts_.victim_task = 0U;
                facts_.victim_pid = 0U;
                /* 代递增：旧的写请求从此过不了代检查。 */
                facts_.victim_generation += 1U;
                break;
        }
        publish(out);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::victim_probe(std::uint64_t kind, std::uint64_t &value) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.victim_probe == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:victim_probe");
        }
        if (kind > 1U) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "kind");
        }
        if (!facts_.victim_spawned) {
            return refuse(refusal_, CallCode::NotReady, "not-ready:victim");
        }
        if (!seams_.victim_probe(params_, kind, value, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "victim-probe-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::check_generations(const WordWrite43499Values &request) noexcept {
        if (request.prepareGeneration != facts_.prepare_generation) {
            return named_failure(refusal_, CallCode::Failed, "stale-generation:", "prepare");
        }
        if (request.discoveryGeneration != facts_.discovery_generation) {
            return named_failure(refusal_, CallCode::Failed, "stale-generation:", "discover");
        }
        if (request.victimGenerationPresent && request.victimGeneration != facts_.victim_generation) {
            return named_failure(refusal_, CallCode::Failed, "stale-generation:", "victim");
        }
        return CallCode::Ok;
    }

    CallCode Backend43499::write_word(const WordWrite43499Values &request) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.land == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:write_word");
        }
        const WriteForm form = write_form_of(request.form);
        if (form == WriteForm::Count) {
            return refuse(refusal_, CallCode::BadParameters, "write-refused:form");
        }
        if (form == WriteForm::ValuePreserving && request.value == 0) {
            /* 要写值却给了零：与清零混淆，拒绝而不是照写。 */
            return refuse(refusal_, CallCode::BadParameters, "write-refused:value-missing");
        }
        if (form != WriteForm::ValuePreserving && request.value != 0) {
            return refuse(refusal_, CallCode::BadParameters, "write-refused:value-not-empty");
        }
        if (const CallCode fresh = check_generations(request); fresh != CallCode::Ok) {
            return fresh;
        }
        if (const char *const why = facts_.ready_for_write(); why != nullptr) {
            return refuse(refusal_, CallCode::NotReady, why);
        }
        if (!seams_.land(params_, request, facts_, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "write-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43499::read_word(std::uint64_t address, std::uint64_t &value) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (seams_.read == nullptr) {
            /* 今天诚实的答案：这个 backend 的路由不能读，所以"读"是 unsupported 而不是失败。 */
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:read_word");
        }
        if (!seams_.read(params_, address, value, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "read-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }
}  // namespace ghostlock::jni
