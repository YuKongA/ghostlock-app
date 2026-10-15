#pragma once
/** @file jni/api_43284.hpp
 *  43284 的函数面：一个库、一组具名函数、参数类型化（用户裁定 2026-10-14，取代帧协议）。
 *
 *  与 43499 那份同样的道理：类型（角色、终态、事实、缝）在这里定义一次，函数就是这些类型的
 *  操作；失败给出**名字**，成功给出结果。
 */
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "jni/backend_api.hpp"

namespace ghostlock::jni {
    /** 这个 backend 一次传输的宽度：一个 AES-CBC 块，十六字节。 */
    inline constexpr std::size_t kBlockWidth = 16U;

    /** 一步自己的角色名。名字走函数参数，缝把它们映射成路径。 */
    enum class TargetRole : std::uint64_t {
        Patch1 = 1,  /**< helper 镜像的目标（桥的入口）。 */
        Carrier = 2, /**< 模块镜像写进去的那个文件。 */
        Hook = 3,    /**< sentry 钩子要改的那个镜像。 */
        Count = 4,
    };

    [[nodiscard]] constexpr const char *target_role_name(TargetRole role) noexcept {
        switch (role) {
            case TargetRole::Patch1: return "patch1";
            case TargetRole::Carrier: return "carrier";
            case TargetRole::Hook: return "hook";
            case TargetRole::Count: break;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr TargetRole target_role_of(std::uint64_t raw) noexcept {
        return raw >= 1U && raw < static_cast<std::uint64_t>(TargetRole::Count)
                   ? static_cast<TargetRole>(raw)
                   : TargetRole::Count;
    }

    /** 一次等待落在哪个终态。名字即线上拼写，只在这里拼一次。 */
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

    /** 这个 backend 的两个镜像角色（EmbeddedImage 的参数）。 */
    inline constexpr std::uint64_t kImageSpliceHelper = 0U;
    inline constexpr std::uint64_t kImageLandingPayload = 1U;

    /** 设备侧为这个会话发布的事实。零表示"还没解析过"。 */
    struct Facts43284 final {
        std::uint64_t token = 0U;    /**< 上一次 ResolveTarget 绑定的目标。 */
        std::uint64_t role = 0U;     /**< 那个 token 代表哪个角色。 */
        std::uint64_t sequence = 0U; /**< 安全关联走到第几个序号。 */
        bool resolved = false;
        bool can_read = false; /**< 门/载体能不能读回来。 */

        /** @return 整块操作可以跑时 nullptr，否则是缺失事实的名字。 */
        [[nodiscard]] const char *ready_for_block() const noexcept {
            if (!resolved || token == 0U) {
                return "not-ready:resolve-target";
            }
            return nullptr;
        }
    };

    /** Snapshot 为此 backend 的事实发布的能力位。 */
    inline constexpr std::uint64_t kFactResolved = 1U << 19U;
    /** Snapshot 的能力位：页缓存。 */
    inline constexpr std::uint64_t kCapPageCache = 1U << 2U;

    /** 设备工作，用函数指针给。给空的缝，它的函数就以 op-not-wired:<名字> 具名拒绝。 */
    struct Ops43284Seams final {
        /** 把一个门绑到一个角色的路径上，并给出 token。 */
        bool (*resolve)(std::uint64_t role, std::string_view path, std::uint64_t &token,
                        void *context) noexcept = nullptr;
        /** 通过 token 在 offset 处写一个十六字节块；缝负责推进序号。 */
        bool (*write_block)(std::uint64_t token, std::uint64_t offset, std::span<const std::byte> block,
                            std::uint64_t &sequence, void *context) noexcept = nullptr;
        /** 读回一个十六字节块。false 表示这条路由读不了（或读失败）。 */
        bool (*read_block)(std::uint64_t token, std::uint64_t offset, std::span<std::byte> block,
                           void *context) noexcept = nullptr;
        /** 武装被钩住的那条路：一次双 fork，让 init 收尸并跑钩子。 */
        bool (*trigger)(std::uint64_t delay_ms, void *context) noexcept = nullptr;
        /** 等这一轮的终态。@param terminal 收到是哪一种。 */
        bool (*wait_terminal)(std::uint64_t budget_ms, std::uint64_t poll_ms, std::string_view module_name,
                              TerminalState &terminal, void *context) noexcept = nullptr;
        /** 本构建自带的字节：角色 0 = splice helper，1 = sentry 落地载荷。 */
        bool (*embedded_image)(std::uint64_t role, std::span<const std::byte> &image,
                               void *context) noexcept = nullptr;
        /** 钩子的数字：分支字、它替换掉的字、两者与载荷各自落哪儿。 */
        bool (*hook_words)(std::uint64_t &branch_word, std::uint64_t &displaced_word,
                           std::uint64_t &site_offset, std::uint64_t &landing_offset,
                           std::span<const std::byte> &payload, void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /** 初始化参数（类型化字段，来自画像）。 */
    struct Initialize43284 final {
        std::uint64_t trigger_delay_ms = 0U;
        std::uint64_t wait_budget_ms = 0U;
        std::uint64_t wait_poll_ms = 0U;
        std::uint64_t carrier_base = 0U;
    };

    /** Snapshot 的结果。 */
    struct Snapshot43284 final {
        std::uint64_t phase = 0U;
        std::uint64_t flags = kCapPageCache;
        std::uint64_t width = static_cast<std::uint64_t>(kBlockWidth);
        Facts43284 facts{};
    };

    /** wait_terminal 的结果。 */
    struct WaitTerminal43284 final {
        TerminalState terminal = TerminalState::InFlight;
        std::uint64_t sequence = 0U;
    };

    /** hook_words 的结果。 */
    struct HookWords43284 final {
        std::uint64_t branch_word = 0U;
        std::uint64_t displaced_word = 0U;
        std::uint64_t site_offset = 0U;
        std::uint64_t landing_offset = 0U;
        std::span<const std::byte> payload{};
    };

    /** 43284 的会话：一个对象一份状态，函数就是它的方法。 */
    class Backend43284 final {
      public:
        Backend43284() noexcept = default;
        explicit Backend43284(Ops43284Seams seams) noexcept : seams_(seams) {}

        /** 带参数初始化。@return Ok，或 bad-parameters:<字段名>。 */
        [[nodiscard]] CallCode initialize(const Initialize43284 &params) noexcept;

        [[nodiscard]] CallCode shutdown() noexcept;

        [[nodiscard]] CallCode snapshot(Snapshot43284 &out) noexcept;

        /** 把角色的路径绑成一个 token。 */
        [[nodiscard]] CallCode resolve_target(TargetRole role, std::string_view path,
                                              std::uint64_t &token) noexcept;

        /** 写一个十六字节块。块宽不是十六一律拒绝（不是"写一部分"）。 */
        [[nodiscard]] CallCode write_block(std::uint64_t token, std::uint64_t offset,
                                           std::span<const std::byte> block) noexcept;

        /** 读一个十六字节块（路由读不了时是 Unsupported）。 */
        [[nodiscard]] CallCode read_block(std::uint64_t token, std::uint64_t offset,
                                          std::span<std::byte> out) noexcept;

        /** 双 fork 触发。 */
        [[nodiscard]] CallCode trigger(std::uint64_t delay_ms) noexcept;

        /** 等终态。InFlight 时返回 InFlight（不是失败）。 */
        [[nodiscard]] CallCode wait_terminal(std::uint64_t budget_ms, std::uint64_t poll_ms,
                                             std::string_view module_name,
                                             WaitTerminal43284 &out) noexcept;

        /** 取本构建自带的字节。 */
        [[nodiscard]] CallCode embedded_image(std::uint64_t role,
                                              std::span<const std::byte> &image) noexcept;

        /** 取钩子的数字与载荷。 */
        [[nodiscard]] CallCode hook_words(HookWords43284 &out) noexcept;

        [[nodiscard]] const Facts43284 &facts() const noexcept { return facts_; }

        [[nodiscard]] api::Phase phase() const noexcept { return phase_; }

        [[nodiscard]] const char *refusal() const noexcept { return refusal_.c_str(); }

      private:
        /** 会话关掉之后，除 snapshot / shutdown / destroy 之外的调用一律具名拒绝。 */
        [[nodiscard]] CallCode closed_guard() noexcept;

        /** 每构造一个会话取一份盐：票据的高 32 位，用来把票据钉死在本会话上。 */
        [[nodiscard]] static std::uint64_t next_salt() noexcept {
            static std::atomic<std::uint64_t> counter{1U};
            return counter.fetch_add(1U);
        }

        /** 交给调用方的票据 = 会话盐（高 32 位）| 门给的 token（低 32 位）。 */
        [[nodiscard]] static constexpr std::uint64_t make_ticket(const std::uint64_t salt,
                                                               const std::uint64_t token) noexcept {
            return (salt << 32U) | (token & 0xFFFFFFFFU);
        }

        /** @return 票据可用时 Ok，否则 stale-target（别的会话的票据永远过不了）。 */
        [[nodiscard]] CallCode accept_ticket(std::uint64_t ticket) noexcept;

        void publish(Snapshot43284 &out) const noexcept;

        std::uint64_t salt_ = next_salt();

        Ops43284Seams seams_{};
        Facts43284 facts_{};
        Initialize43284 params_{};
        Refusal refusal_{};
        api::Phase phase_ = api::Phase::Declared;
    };
}  // namespace ghostlock::jni
