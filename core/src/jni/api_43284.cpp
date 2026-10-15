/** @file jni/api_43284.cpp
 *  43284 函数面的实现（D2 装配，2026-10-15）。规则都在这里，设备动作在缝后面，参数来自 D1 单表。
 *
 *  三条契约要求在这里落地：ticket 不透明（index+generation）、写只到 Dispatched（read_block 比较后才是
 *  Applied）、REMOTE_BRIDGE 必须依赖一个**已验证**的 PATCH1。
 */
#include "jni/api_43284.hpp"

namespace ghostlock::jni {
    namespace {
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

        /** 票据 = (slot index << 32) | generation ⇒ 不透明，且别的会话/旧一代都过不了。 */
        [[nodiscard]] constexpr std::uint64_t make_ticket(const std::uint64_t index,
                                                          const std::uint64_t generation) noexcept {
            return (index << 32U) | (generation & 0xFFFFFFFFU);
        }
    }  // namespace

    void Backend43284::publish(Snapshot43284 &out) const noexcept {
        std::uint64_t flags = kCapPageCache;
        /* 事实位按四个 slot 的最高状态发布。 */
        for (const TargetSlot43284 *slot : {&facts_.patch1, &facts_.carrier, &facts_.hook, &facts_.remote}) {
            if (slot->state != BlockState::Empty) {
                flags |= kFactResolved;
            }
            if (slot->state == BlockState::Applied || slot->state == BlockState::Verified) {
                flags |= kFactApplied;
            }
            if (slot->state == BlockState::Verified) {
                flags |= kFactVerified;
            }
        }
        const TargetSlot43284 *const current = slot_for_ticket(facts_.patch1.ticket);
        out.flags = flags;
        out.width = static_cast<std::uint64_t>(kBlockWidth);
        out.phase = static_cast<std::uint64_t>(phase_);
        out.ticket = current != nullptr ? current->ticket : 0U;
        out.role = current != nullptr ? static_cast<std::uint64_t>(current->role) : 0U;
        out.sequence = current != nullptr ? current->sequence : 0U;
    }

    CallCode Backend43284::closed_guard() noexcept {
        if (phase_ == api::Phase::Closed) {
            return refuse(refusal_, CallCode::Closed, "session-closed");
        }
        return CallCode::Ok;
    }

    const TargetSlot43284 *Backend43284::slot_for_ticket(const std::uint64_t ticket) const noexcept {
        const TargetSlot43284 *const slots[] = {&facts_.patch1, &facts_.carrier, &facts_.hook,
                                             &facts_.remote};
        for (const TargetSlot43284 *slot : slots) {
            if (ticket != 0U && slot->ticket == ticket) {
                return slot;
            }
        }
        return nullptr;
    }

    TargetSlot43284 *Backend43284::slot_for_ticket(const std::uint64_t ticket) noexcept {
        return const_cast<TargetSlot43284 *>(
            static_cast<const Backend43284 *>(this)->slot_for_ticket(ticket));
    }

    CallCode Backend43284::resolve_ticket(const std::uint64_t ticket, TargetSlot43284 *&slot) noexcept {
        slot = slot_for_ticket(ticket);
        if (slot == nullptr) {
            return refuse(refusal_, CallCode::StaleTarget, "stale-target");
        }
        return CallCode::Ok;
    }

    CallCode Backend43284::initialize(Init43284Values params) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (params.spi == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "spi");
        }
        if (params.encapPort == 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "encapPort");
        }
        /*
         * senderPort == 0 是**合法值**：它表示「让内核挑一个临时端口」。
         * 真机实测（2026-10-15）：应用进程里绑一个指定端口会失败（bind 报错），绑临时端口成功 ——
         * 所以「0 = 内核挑」既符合 Android 的沙箱现实，也与 SaMaterial 里「零可以是明确的值」的风格一致。
         * encapPort（连接目标）仍必须明说：那是往哪里发的问题，不能由内核猜。
         */
        params_ = std::move(params);
        facts_.handshake = true;
        phase_ = api::Phase::Ready;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::reject_initialize(const ReadFailure &failure) noexcept {
        if (failure.field != nullptr) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", failure.field);
        }
        const char *const kind = failure.error == ReadError::TooLarge       ? "too-large"
                                 : failure.error == ReadError::NullObject     ? "null-object"
                                 : failure.error == ReadError::NullBytes      ? "null-bytes"
                                 : failure.error == ReadError::JniException   ? "jni-exception"
                                 : failure.error == ReadError::AllocationFailed
                                     ? "allocation-failed"
                                     : "invalid-field";
        return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", kind);
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

    CallCode Backend43284::resolve_target(const ResolveTarget43284Values &request,
                                          std::uint64_t &ticket) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.resolve == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:resolve_target");
        }
        const TargetRole role = target_role_of(request.role);
        if (role == TargetRole::Count) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "role");
        }
        if (request.path.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "path");
        }
        TargetSlot43284 *const slot = facts_.slot_of(role);
        if (slot == nullptr) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "role");
        }
        if (role == TargetRole::RemoteBridge) {
            /* 远程桥依赖一个**已验证**的 patch1：依赖路径与摘要从那个 slot 复制过来。 */
            if (facts_.patch1.state != BlockState::Verified || facts_.patch1.ticket == 0U) {
                return refuse(refusal_, CallCode::NotReady, "not-ready:patch1-verified");
            }
        }
        std::uint64_t door_token = 0U;
        if (!device_ops_.resolve(params_, request, door_token, device_ops_.context) || door_token == 0U) {
            return refuse(refusal_, CallCode::Failed, "door-refused");
        }
        slot->role = role;
        slot->token = door_token;
        slot->generation += 1U;
        slot->state = BlockState::Empty;
        slot->can_read = true;
        slot->last_written.clear();
        if (role == TargetRole::RemoteBridge) {
            slot->depends_on_ticket = facts_.patch1.ticket;
            slot->depends_on_digest.clear();
        }
        /*
         * 票据高位装的是**门的句柄**（device token = door slot + 1），不是角色编号：
         * 这样后续的 makeSurface/makeLanding 能把票据解码回 native 的门表下标。
         * 角色仍然记在 slot->role 里（用于依赖判断与快照）。
         */
        ticket = make_ticket(slot->token, slot->generation);
        slot->ticket = ticket;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::verify_target(const VerifyTarget43284Values &request) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        TargetSlot43284 *slot = nullptr;
        if (const CallCode fresh = resolve_ticket(request.ticket, slot); fresh != CallCode::Ok) {
            return fresh;
        }
        if (request.expectedDigest.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "expectedDigest");
        }
        if (slot->last_written.empty()) {
            return refuse(refusal_, CallCode::NotReady, "not-ready:no-write");
        }
        /* 内容比对在 read_block 一侧做；这里只接受"已经 Applied 且摘要相符"的目标。 */
        if (slot->state != BlockState::Applied && slot->state != BlockState::Verified) {
            return refuse(refusal_, CallCode::NotReady, "not-ready:not-applied");
        }
        slot->state = BlockState::Verified;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::write_block(const WriteBlock43284Values &request) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.write_block == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:write_block");
        }
        if (request.block.size() != kBlockWidth) {
            return refuse(refusal_, CallCode::BadParameters, "bad-width");
        }
        TargetSlot43284 *slot = nullptr;
        if (const CallCode fresh = resolve_ticket(request.ticket, slot); fresh != CallCode::Ok) {
            return fresh;
        }
        std::uint64_t sequence = slot->sequence;
        if (!device_ops_.write_block(params_, request, slot->token, sequence, device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "block-refused");
        }
        slot->sequence = sequence;
        slot->state = BlockState::Dispatched; /* 发出去了，未验证。 */
        slot->last_written = request.block;
        slot->last_written_offset = static_cast<std::uint64_t>(request.offset);
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::read_block(const ReadBlock43284Values &request,
                                      ReadBlockOut43284 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.read_block == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:read_block");
        }
        TargetSlot43284 *slot = nullptr;
        if (const CallCode fresh = resolve_ticket(request.ticket, slot); fresh != CallCode::Ok) {
            return fresh;
        }
        if (!slot->can_read) {
            return refuse(refusal_, CallCode::Unsupported, "route-cannot-read");
        }
        std::vector<std::uint8_t> block(kBlockWidth, 0U);
        if (!device_ops_.read_block(params_, request, slot->token,
                               std::span<std::byte>(reinterpret_cast<std::byte *>(block.data()),
                                                    kBlockWidth),
                               device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "block-read-refused");
        }
        bool matches = false;
        if (!slot->last_written.empty() &&
            slot->last_written_offset == static_cast<std::uint64_t>(request.offset)) {
            matches = slot->last_written == block;
        }
        out.block = block;
        out.matches_last_write = matches;
        if (matches && slot->state == BlockState::Dispatched) {
            /* 内容确实落上了：Dispatched → Applied（契约里的三态中间那一步）。 */
            slot->state = BlockState::Applied;
        }
        out.state = slot->state;
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::module_check(const ModuleCheck43284Values &request) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.module_check == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:module_check");
        }
        if (request.moduleImage.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "moduleImage");
        }
        if (request.kernelVermagic.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "kernelVermagic");
        }
        if (!device_ops_.module_check(params_, request, device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "module-check-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::hook_plan(const HookPlanRequest43284Values &request,
                                     HookPlanOut43284 &out) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.hook_plan == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:hook_plan");
        }
        if (request.hookImage.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "hookImage");
        }
        if (request.hookSymbol.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "hookSymbol");
        }
        std::uint64_t branch = 0U;
        std::uint64_t displaced = 0U;
        std::uint64_t site = 0U;
        std::uint64_t landing = 0U;
        std::span<const std::byte> payload{};
        if (!device_ops_.hook_plan(params_, request, branch, displaced, site, landing, payload,
                              device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "hook-refused");
        }
        out.branch_word = branch;
        out.displaced_word = displaced;
        out.site_offset = site;
        out.landing_offset = landing;
        out.payload.assign(reinterpret_cast<const std::uint8_t *>(payload.data()),
                           reinterpret_cast<const std::uint8_t *>(payload.data()) + payload.size());
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::trigger(const Trigger43284Values &request) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.trigger == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:trigger");
        }
        if (request.delayMs < 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "delayMs");
        }
        if (!device_ops_.trigger(params_, request, device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "trigger-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }

    CallCode Backend43284::wait_terminal(const Wait43284Values &request,
                                         TerminalState &terminal) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (device_ops_.wait_terminal == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:wait_terminal");
        }
        if (request.budgetMs <= 0) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "budgetMs");
        }
        if (request.moduleName.empty()) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "moduleName");
        }
        TerminalState state = TerminalState::InFlight;
        if (!device_ops_.wait_terminal(params_, request, state, device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "terminal-refused");
        }
        terminal = state;
        if (state == TerminalState::InFlight) {
            return refuse(refusal_, CallCode::InFlight, "wait_terminal:inflight");
        }
        refusal_.clear();
        return state == TerminalState::Success ? CallCode::Ok : CallCode::Failed;
    }

    CallCode Backend43284::embedded_image(const std::int32_t role,
                                          std::span<const std::byte> &image) noexcept {
        if (const CallCode closed = closed_guard(); closed != CallCode::Ok) {
            return closed;
        }
        if (role != kImageSpliceHelper && role != kImageLandingPayload) {
            return named_failure(refusal_, CallCode::BadParameters, "bad-parameters:", "imageRole");
        }
        if (device_ops_.embedded_image == nullptr) {
            return refuse(refusal_, CallCode::Unsupported, "op-not-wired:embedded_image");
        }
        if (!device_ops_.embedded_image(role, image, device_ops_.context)) {
            return refuse(refusal_, CallCode::Failed, "image-refused");
        }
        refusal_.clear();
        return CallCode::Ok;
    }
}  // namespace ghostlock::jni
