/** @file jni/api_43284.cpp
 *  43284 函数面的实现：规则都在这里，设备动作在缝后面。
 *
 *  与旧的帧实现逐条相同的关键判据：块宽必须是十六字节（不是"写一部分"）、token 必须是本会话
 *  解析出来的（stale-target 绝不当作解析结果用）、终态的三种可能各自有名、未接线的缝一律具名。
 */
#include "jni/api_43284.hpp"

namespace ghostlock::jni {
    void Backend43284::publish(Snapshot43284 &out) const noexcept {
        out.facts = facts_;
        std::uint64_t flags = kCapPageCache;
        if (facts_.resolved) {
            flags |= kFactResolved;
        }
        out.flags = flags;
        out.width = static_cast<std::uint64_t>(kBlockWidth);
        out.phase = static_cast<std::uint64_t>(phase_);
    }

    CallCode Backend43284::initialize(const Initialize43284 &params) noexcept {
        if (params.trigger_delay_ms == 0U) {
            return refuse(refusal_, CallCode::BadParameters, "bad-parameters:trigger_delay_ms");
        }
        if (params.wait_budget_ms == 0U) {
            return refuse(refusal_, CallCode::BadParameters, "bad-parameters:wait_budget_ms");
        }
        if (params.wait_poll_ms == 0U) {
            return refuse(refusal_, CallCode::BadParameters, "bad-parameters:wait_poll_ms");
        }
        params_ = params;
        phase_ = api::Phase::Ready;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::shutdown() noexcept {
        if (phase_ != api::Phase::Closed) {
            phase_ = api::Phase::Closed;
            facts_ = Facts43284{};
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::snapshot(Snapshot43284 &out) noexcept {
        publish(out);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::resolve_target(const TargetRole role, const std::string_view path,
                                          std::uint64_t &token) noexcept {
        if (seams_.resolve == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:resolve_target");
        }
        if (role == TargetRole::Count) {
            return refuse(refusal_, CallCode::BadParameters, "role-unknown");
        }
        if (path.empty()) {
            return refuse(refusal_, CallCode::BadParameters, "key-missing:path");
        }
        std::uint64_t resolved = 0U;
        if (!seams_.resolve(static_cast<std::uint64_t>(role), path, resolved, seams_.context) ||
            resolved == 0U) {
            return refuse(refusal_, CallCode::Failed, "door-refused");
        }
        facts_.token = resolved;
        facts_.role = static_cast<std::uint64_t>(role);
        facts_.resolved = true;
        token = resolved;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::write_block(const std::uint64_t token, const std::uint64_t offset,
                                       const std::span<const std::byte> block) noexcept {
        if (seams_.write_block == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:write_block");
        }
        if (block.size() != kBlockWidth) {
            /* 这个 backend 的单位是一个整块：要别的宽度就被拒，而不是给它写一部分。 */
            return refuse(refusal_, CallCode::BadParameters, "bad-width");
        }
        if (const char *const why = facts_.ready_for_block(); why != nullptr) {
            return refuse(refusal_, CallCode::NotReady, why);
        }
        if (token != facts_.token) {
            /* 本会话没发过的 token（或别的会话的）：拒绝，绝不当作解析结果用。 */
            return refuse(refusal_, CallCode::StaleTarget, "stale-target");
        }
        std::uint64_t sequence = facts_.sequence;
        if (!seams_.write_block(token, offset, block, sequence, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "block-refused");
        }
        facts_.sequence = sequence;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::read_block(const std::uint64_t token, const std::uint64_t offset,
                                      const std::span<std::byte> out) noexcept {
        if (seams_.read_block == nullptr) {
            /* 这条路由读不回来：是 unsupported（重试无用），不是失败。 */
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:read_block");
        }
        if (out.size() != kBlockWidth) {
            return refuse(refusal_, CallCode::BadParameters, "bad-width");
        }
        if (const char *const why = facts_.ready_for_block(); why != nullptr) {
            return refuse(refusal_, CallCode::NotReady, why);
        }
        if (token != facts_.token) {
            return refuse(refusal_, CallCode::StaleTarget, "stale-target");
        }
        if (!seams_.read_block(token, offset, out, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "block-read-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::trigger(const std::uint64_t delay_ms) noexcept {
        if (seams_.trigger == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:trigger");
        }
        if (!seams_.trigger(delay_ms, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "trigger-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::wait_terminal(const std::uint64_t budget_ms, const std::uint64_t poll_ms,
                                         const std::string_view module_name,
                                         WaitTerminal43284 &out) noexcept {
        if (seams_.wait_terminal == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:wait_terminal");
        }
        TerminalState terminal = TerminalState::InFlight;
        if (!seams_.wait_terminal(budget_ms, poll_ms, module_name, terminal, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "terminal-refused");
        }
        out.terminal = terminal;
        out.sequence = facts_.sequence;
        if (terminal == TerminalState::InFlight) {
            /* 还在进行中：既不是失败也不是成功，调用方可以再问一次。 */
            return refuse(refusal_, CallCode::InFlight, "wait_terminal:inflight");
        }
        refusal_.clear();
        return terminal == TerminalState::Success ? CallCode::Ok : CallCode::Failed;
    }

    CallCode Backend43284::embedded_image(const std::uint64_t role,
                                          std::span<const std::byte> &image) noexcept {
        if (role != kImageSpliceHelper && role != kImageLandingPayload) {
            return refuse(refusal_, CallCode::BadParameters, "image-role-unknown");
        }
        if (seams_.embedded_image == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:embedded_image");
        }
        if (!seams_.embedded_image(role, image, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "image-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::hook_words(HookWords43284 &out) noexcept {
        if (seams_.hook_words == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:hook_words");
        }
        std::uint64_t branch = 0U;
        std::uint64_t displaced = 0U;
        std::uint64_t site = 0U;
        std::uint64_t landing = 0U;
        std::span<const std::byte> payload{};
        if (!seams_.hook_words(branch, displaced, site, landing, payload, seams_.context)) {
            return refuse(refusal_, CallCode::Failed, "hook-refused");
        }
        out.branch_word = branch;
        out.displaced_word = displaced;
        out.site_offset = site;
        out.landing_offset = landing;
        out.payload = payload;
        refusal_.clear();
        return CallCode::Ok;
    }
}  // namespace ghostlock::jni
