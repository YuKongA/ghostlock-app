#pragma once
/** @file jni/api_43284.hpp
 *  43284 的函数面：一个库、一组具名函数、参数类型化（用户裁定 2026-10-14；D2 装配 2026-10-15）。
 *
 *  与 43499 那份同构：参数类一律来自 D1 单表生成，缝回调第一个参数是完整的会话参数、第二个是本次
 *  请求组。这一份多三条契约要求：
 *  1. **ticket 是不透明的 index+generation**：调用方拿到票据，不是门自己的 token；
 *  2. **写成功只到 Dispatched**：只有同 ticket/offset 的 read_block 由 native 比较过内容之后才 Applied；
 *  3. **REMOTE_BRIDGE 依赖已验证的 PATCH1**：依赖路径与摘要从已验证的 slot 复制过去。
 */
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "jni/attack_contract_values.hpp"
#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    using contract::HookPlanRequest43284Values;
    using contract::Init43284Values;
    using contract::ModuleCheck43284Values;
    using contract::ReadBlock43284Values;
    using contract::ReadError;
    using contract::ReadFailure;
    using contract::ResolveTarget43284Values;
    using contract::Trigger43284Values;
    using contract::VerifyTarget43284Values;
    using contract::Wait43284Values;
    using contract::WriteBlock43284Values;

    /** 一个块十六字节（AES-CBC 的一块）。 */
    inline constexpr std::size_t kBlockWidth = 16U;

    /** 目标角色（与契约里 ResolveTarget43284.role 的取值一致）。 */
    enum class TargetRole : std::int32_t {
        Patch1 = 1,
        Carrier = 2,
        Hook = 3,
        RemoteBridge = 4,
        Count = 5,
    };

    [[nodiscard]] constexpr const char *target_role_name(TargetRole role) noexcept {
        switch (role) {
            case TargetRole::Patch1: return "patch1";
            case TargetRole::Carrier: return "carrier";
            case TargetRole::Hook: return "hook";
            case TargetRole::RemoteBridge: return "remote-bridge";
            case TargetRole::Count: break;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr TargetRole target_role_of(std::int32_t raw) noexcept {
        return raw >= 1 && raw < static_cast<std::int32_t>(TargetRole::Count)
                   ? static_cast<TargetRole>(raw)
                   : TargetRole::Count;
    }

    /** 一次写落到哪一步：写成功不是改过了。 */
    enum class BlockState : std::uint64_t {
        Empty = 0,
        Dispatched = 1,
        Applied = 2,
        Verified = 3,
    };

    [[nodiscard]] constexpr const char *block_state_name(BlockState state) noexcept {
        switch (state) {
            case BlockState::Empty: return "empty";
            case BlockState::Dispatched: return "dispatched";
            case BlockState::Applied: return "applied";
            case BlockState::Verified: return "verified";
        }
        return "unknown";
    }

    /** 一次等待落在哪个终态（名字即线上拼写）。 */
    enum class TerminalState : std::uint64_t {
        Failure = 0,
        Success = 1,
        InFlight = 2,
    };

    [[nodiscard]] constexpr const char *terminal_state_name(TerminalState state) noexcept {
        switch (state) {
            case TerminalState::Failure: return "failure";
            case TerminalState::Success: return "success";
            case TerminalState::InFlight: return "inflight";
        }
        return "unknown";
    }

    /** 本构建自带的字节：角色 0 = splice helper，1 = sentry 落地载荷。 */
    inline constexpr std::int32_t kImageSpliceHelper = 0;
    inline constexpr std::int32_t kImageLandingPayload = 1;

    /** 一个 target slot：门给的 token、它属于谁、写到哪一步、以及它的依赖。 */
    struct TargetSlot43284 final {
        TargetRole role = TargetRole::Count;
        std::uint64_t token = 0U;
        std::uint64_t ticket = 0U;
        std::uint64_t generation = 0U;
        std::uint64_t sequence = 0U;
        BlockState state = BlockState::Empty;
        bool can_read = false;
        /** REMOTE_BRIDGE 用：依赖的 patch1 票据与其摘要（从已验证的 slot 复制过来）。 */
        std::uint64_t depends_on_ticket = 0U;
        std::vector<std::uint8_t> depends_on_digest{};
        /** 最近一次写出去的块与偏移，等 read_block 回来比较。 */
        std::vector<std::uint8_t> last_written{};
        std::uint64_t last_written_offset = 0U;
    };

    /** 设备侧为这个会话发布的事实。 */
    struct Facts43284 final {
        TargetSlot43284 patch1{};
        TargetSlot43284 carrier{};
        TargetSlot43284 hook{};
        TargetSlot43284 remote{};
        std::uint64_t ticket_counter = 0U;
        bool handshake = false;

        [[nodiscard]] TargetSlot43284 *slot_of(TargetRole role) noexcept {
            switch (role) {
                case TargetRole::Patch1: return &patch1;
                case TargetRole::Carrier: return &carrier;
                case TargetRole::Hook: return &hook;
                case TargetRole::RemoteBridge: return &remote;
                case TargetRole::Count: break;
            }
            return nullptr;
        }

        [[nodiscard]] const TargetSlot43284 *slot_of(TargetRole role) const noexcept {
            return const_cast<Facts43284 *>(this)->slot_of(role);
        }
    };

    inline constexpr std::uint64_t kCapPageCache = 1U << 2U;
    inline constexpr std::uint64_t kFactResolved = 1U << 19U;
    inline constexpr std::uint64_t kFactApplied = 1U << 20U;
    inline constexpr std::uint64_t kFactVerified = 1U << 21U;

    /** 钩子计划的结果：前四个是本构建的四个字（写层用），其余是同一张计划的整份字段
     *  （恢复层要把 plan 原样送回 native，见 Backend43284::hook_plan / Native43284::restoreHook）。 */
    struct HookPlanOut43284 final {
        std::uint64_t branch_word = 0U;
        std::uint64_t displaced_word = 0U;
        std::uint64_t site_offset = 0U;
        std::uint64_t landing_offset = 0U;
        /* 写层要的 SentryPlan 其余字段：由设备层从它持有的完整计划里原样带出（只装结构，不判断）。 */
        std::uint64_t hook_vaddr = 0U;
        std::uint64_t guard_instruction = 0U;
        std::uint64_t payload_file_offset = 0U;
        std::uint64_t payload_vaddr = 0U;
        std::uint64_t payload_max_bytes = 0U;
        std::uint64_t entry_offset = 0U;
        std::uint64_t displaced_offset = 0U;
        std::vector<std::uint8_t> payload{};
    };

    /** 设备工作，用函数指针给；每个回调都收到完整的会话参数与本次请求组。 */
    struct DeviceOps43284 final {
        bool (*resolve)(const Init43284Values &params, const ResolveTarget43284Values &request,
                        std::uint64_t &token, void *context) noexcept = nullptr;
        bool (*write_block)(const Init43284Values &params, const WriteBlock43284Values &request,
                            std::uint64_t door_token, std::uint64_t &sequence,
                            void *context) noexcept = nullptr;
        bool (*read_block)(const Init43284Values &params, const ReadBlock43284Values &request,
                           std::uint64_t door_token, std::span<std::byte> out,
                           void *context) noexcept = nullptr;
        bool (*module_check)(const Init43284Values &params, const ModuleCheck43284Values &request,
                             void *context) noexcept = nullptr;
        /** 计划整份填进 out（含载荷）：四个字与恢复要的 guard/displaced 同源，调用方不再各问一次。 */
        bool (*hook_plan)(const Init43284Values &params, const HookPlanRequest43284Values &request,
                          HookPlanOut43284 &out, void *context) noexcept = nullptr;
        bool (*trigger)(const Init43284Values &params, const Trigger43284Values &request,
                        void *context) noexcept = nullptr;
        bool (*wait_terminal)(const Init43284Values &params, const Wait43284Values &request,
                              TerminalState &terminal, void *context) noexcept = nullptr;
        bool (*embedded_image)(std::int32_t role, std::span<const std::byte> &image,
                               void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /** Snapshot 的结果。 */
    struct Snapshot43284 final {
        std::uint64_t phase = 0U;
        std::uint64_t flags = kCapPageCache;
        std::uint64_t width = static_cast<std::uint64_t>(kBlockWidth);
        std::uint64_t ticket = 0U;
        std::uint64_t role = 0U;
        std::uint64_t sequence = 0U;
    };

    /** read_block 的结果。 */
    struct ReadBlockOut43284 final {
        std::vector<std::uint8_t> block{};
        BlockState state = BlockState::Empty;
        bool matches_last_write = false;
    };

    /** 43284 的会话：一个对象一份状态，函数就是它的方法（并发由句柄表负责）。 */
    class Backend43284 final {
      public:
        Backend43284() noexcept = default;
        explicit Backend43284(DeviceOps43284 device_ops) noexcept : device_ops_(device_ops) {}

        /** 初始化：只建立安全关联；门由 resolve_target 创建。 */
        /** 参数按值收：Values 里的密钥是不可拷贝的，进来之后 move 进会话。 */
        [[nodiscard]] CallCode initialize(Init43284Values params) noexcept;

        /** 读取器失败时用它：名字就是出错的字段。 */
        [[nodiscard]] CallCode reject_initialize(const ReadFailure &failure) noexcept;

        [[nodiscard]] CallCode shutdown() noexcept;

        [[nodiscard]] CallCode snapshot(Snapshot43284 &out) noexcept;

        /** 绑定一个角色到一条路径，回答票据（不透明 index+generation）。 */
        [[nodiscard]] CallCode resolve_target(const ResolveTarget43284Values &request,
                                              std::uint64_t &ticket) noexcept;

        /** 校验目标内容（摘要与大小）⇒ Verified。 */
        [[nodiscard]] CallCode verify_target(const VerifyTarget43284Values &request) noexcept;

        /** 写一个十六字节块 ⇒ Dispatched（不是改过了）。 */
        [[nodiscard]] CallCode write_block(const WriteBlock43284Values &request) noexcept;

        /** 读回一个十六字节块：与最近一次写比较，一致则把该 slot 推到 Applied。 */
        [[nodiscard]] CallCode read_block(const ReadBlock43284Values &request,
                                          ReadBlockOut43284 &out) noexcept;

        /** 模块校验：通过则把 slot 推到 Verified。 */
        [[nodiscard]] CallCode module_check(const ModuleCheck43284Values &request) noexcept;

        /** 动态钩子计划（符号 + guard 策略 + 镜像）⇒ 摘要绑定的载荷。 */
        [[nodiscard]] CallCode hook_plan(const HookPlanRequest43284Values &request,
                                         HookPlanOut43284 &out) noexcept;

        /** 双 fork 触发。 */
        [[nodiscard]] CallCode trigger(const Trigger43284Values &request) noexcept;

        /** 等终态：InFlight 时返回 InFlight（不是失败）。 */
        [[nodiscard]] CallCode wait_terminal(const Wait43284Values &request,
                                             TerminalState &terminal) noexcept;

        /** 取本构建自带的字节。 */
        [[nodiscard]] CallCode embedded_image(std::int32_t role,
                                              std::span<const std::byte> &image) noexcept;

        [[nodiscard]] const Facts43284 &facts() const noexcept { return facts_; }

        [[nodiscard]] const Init43284Values &params() const noexcept { return params_; }

        [[nodiscard]] api::Phase phase() const noexcept { return phase_; }

        [[nodiscard]] const char *refusal() const noexcept { return refusal_.c_str(); }

      private:
        [[nodiscard]] CallCode closed_guard() noexcept;

        /** @return 票据对应的 slot（本会话发的、代还对），否则 nullptr。 */
        [[nodiscard]] const TargetSlot43284 *slot_for_ticket(std::uint64_t ticket) const noexcept;
        [[nodiscard]] TargetSlot43284 *slot_for_ticket(std::uint64_t ticket) noexcept;

        /** 把请求里的票据解析成 slot；失败时给出具名拒绝。 */
        [[nodiscard]] CallCode resolve_ticket(std::uint64_t ticket, TargetSlot43284 *&slot) noexcept;

        void publish(Snapshot43284 &out) const noexcept;

        DeviceOps43284 device_ops_{};
        Init43284Values params_{};
        Facts43284 facts_{};
        Refusal refusal_{};
        api::Phase phase_ = api::Phase::Declared;
    };
}  // namespace ghostlock::jni
