#pragma once
/** @file backend/cve_2026_43499/kernel_access.hpp
 *  The 43499 backend's kernel access: what a script reaches, by address.
 *
 *  Shape, per the user's rulings. One object per backend, not per route: the route is chosen in
 *  the parameters, brought up inside initialize and reclaimed inside shutdown, and construction
 *  does none of that work. The route, its carrier and the landing gear live from initialize to
 *  shutdown and serve every access in between; the window inside one access is scoped to that
 *  access because it is a race scope and reclaims on every exit path.
 *
 *  Honesty about what this backend can do at which point, which is why a capability is a state
 *  and not a boolean: a window write is the bootstrap and works as soon as a route is up, while
 *  arbitrary reads need the object found and a page prepared, so they are declared and refused
 *  by name until the readiness answer says otherwise. Width is a capability for the same reason:
 *  what the window writes is one whole word.
 *
 *  The landing is where the route implementation plugs in. Everything this class does is decide --
 *  which route, which width, whether the facts are in place -- and hand the decision to the landing,
 *  which lands the write through the window. That split is why this class is testable on the host
 *  today and why the route code can be ported one route at a time without touching it.
 */
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "backend/cve_2026_43499/access_params.hpp"
#include "backend/cve_2026_43499/session.hpp"
#include "contract/kernel_access.hpp"
#include "platform/route_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** What a route implementation provides to the access. One word at a time, because that is
     *  what the window does; a caller wanting fewer bytes is refused by the width rule above it
     *  rather than served by a partial write. */
    class WordLanding {
    public:
        WordLanding() noexcept = default;
        WordLanding(const WordLanding &) = delete;
        WordLanding &operator=(const WordLanding &) = delete;
        virtual ~WordLanding() noexcept = default;

        /** Bring the route up: prepare what it needs and leave it ready to write. */
        [[nodiscard]] virtual bool bring_up(profile::RouteId route, const TargetState &target) noexcept = 0;
        /** Take the route down and release what bring_up prepared. */
        [[nodiscard]] virtual bool tear_down() noexcept = 0;
        /** Land one word through the window. The whole point of the backend. */
        [[nodiscard]] virtual bool land_word(std::uint64_t address, std::uint64_t value,
                                             std::uint64_t offset) noexcept = 0;
        /** Whether this route can also read, and by what means. False until a route says
         *  otherwise, which is the honest answer for a window that only writes. */
        [[nodiscard]] virtual bool can_read() const noexcept { return false; }
        /** Read one word, if can_read. */
        [[nodiscard]] virtual bool read_word(std::uint64_t address, std::uint64_t offset,
                                             std::uint64_t &out) noexcept {
            static_cast<void>(address);
            static_cast<void>(offset);
            static_cast<void>(out);
            return false;
        }
    };

    /** The backend's access. Owns nothing at construction; everything it owns it took in
     *  initialize and gives back in shutdown. */
    /* 旧码（原文保留 ✓）：
     *     class Access final : public contract::KernelAccess {
     *  TODO(access-split): 2026-10-12 裁定「三个 route 各建子类」⇒ 本类升为**基类**：
     *  ① 改名 `KernelAccess43499`；② 去掉 `final`（子类须继承）。
     *  机械改动（AGENTS §3 例外）：仅改名与去 final，逻辑未动。
     *  完成 = 三子类 ＋ 工厂落地、门禁全绿、用户确认 ⇒ 删本注释块。 */
    class KernelAccess43499 : public contract::KernelAccess {
    public:
        using AccessState = contract::KernelAccess::AccessState;
        using AccessStatus = contract::KernelAccess::Status;
        using Result = contract::KernelAccess::AccessResult<std::size_t>;
        using WordResult = contract::KernelAccess::AccessResult<std::uint64_t>;
        using ReadResult = contract::KernelAccess::AccessResult<std::span<const std::byte>>;
        using AccessMiscParameters = contract::KernelAccess::AccessMiscParameters;
        using AccessCarrier = contract::AccessCarrier;
        using AccessChannel = contract::AccessChannel;

        /* Construction decides nothing and takes nothing. An instance without either argument is
         *  not usable, and every use below refuses by name rather than dereferencing, which is the
         *  same rule write_readiness follows for a missing fact. */
        /** Blank: construction decides nothing and takes nothing. An instance without either is not
         *  usable, and every use below refuses by name rather than dereferencing, which is the same
         *  rule write_readiness follows for a missing fact. */
        KernelAccess43499() noexcept = default;
        KernelAccess43499(WordLanding *landing, const TargetState *target) noexcept
            : ops_(landing), target_(target) {}
        /** Hand over the two things a factory could not know. */
        void adopt(WordLanding *landing, const TargetState *target) noexcept {
            ops_ = landing;
            target_ = target;
        }

        [[nodiscard]] AccessState state(AccessChannel channel) const noexcept override {
            if (channel != AccessChannel::PiWindow) {
                return AccessState::Unsupported;
            }
            switch (phase_) {
                case Phase::NeverUp: return AccessState::Declared;
                case Phase::ComingUp: return AccessState::Initializing;
                case Phase::Up: return AccessState::Ready;
                case Phase::Down: return AccessState::Closed;
                case Phase::Broken: return AccessState::Failed;
            }
            return AccessState::Unsupported;
        }
        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            return channel == AccessChannel::PiWindow;
        }
        [[nodiscard]] bool supports(AccessCarrier carrier) const noexcept override {
            return carrier == AccessCarrier::KernelPage;
        }
        /** One whole word, and nothing narrower: that is what the window writes. */
        [[nodiscard]] bool supports_width(std::size_t bytes) const noexcept override {
            return bytes == sizeof(std::uint64_t);
        }

        [[nodiscard]] contract::KernelAccess::AccessResult<bool> initialize(
            const AccessMiscParameters &parameters) noexcept override {
            using BoolResult = contract::KernelAccess::AccessResult<bool>;
            const auto *params = as_params(parameters);
            if (params == nullptr || !params->usable_for_initialize()) {
                return BoolResult::failed(AccessStatus::BadParameters);
            }
            if (phase_ == Phase::Up) {
                return BoolResult::failed(AccessStatus::Failed);
            }
            phase_ = Phase::ComingUp;
            route_ = params->route;
            if (ops_ == nullptr || target_ == nullptr || !ops_->bring_up(route_, *target_)) {
                phase_ = Phase::Broken;
                return BoolResult::failed(AccessStatus::Failed);
            }
            phase_ = Phase::Up;
            return BoolResult::ok(true);
        }

        [[nodiscard]] contract::KernelAccess::AccessResult<bool> shutdown() noexcept override {
            using BoolResult = contract::KernelAccess::AccessResult<bool>;
            /* 旧码（原文保留 ✓）：
             *     if (phase_ != Phase::Up) { return BoolResult::failed(AccessStatus::NotInitialized); }
             *  TODO(access-split): 与 ref 冲突 —— `ghostlock-app-ref/src/core/route/route_lifecycle.hpp`
             *  逐字：「prepare -> execute ... -> **disarm** -> **destroy**」，后两者**无条件**；
             *  旧的 `!= Up` 门槛会让 `bring_up` 中途失败（`Broken`）**无人回收** ✗ ——
             *  那正是旧 C「dirty failure」的同一个洞 ⇒ 改为只拒 `NeverUp`/`Down`。
             *  完成 = 门禁全绿 ＋ 用户确认 ⇒ 删本注释块。 */
            if (phase_ == Phase::NeverUp || phase_ == Phase::Down) {
                return BoolResult::failed(AccessStatus::NotInitialized);
            }
            /* Nothing borrowed means nothing to give back, which is a clean teardown. */
            const bool down = ops_ == nullptr || ops_->tear_down();
            phase_ = down ? Phase::Down : Phase::Broken;
            return down ? BoolResult::ok(true) : BoolResult::failed(AccessStatus::Failed);
        }

        [[nodiscard]] Result write(std::uint64_t address, std::span<const std::byte> in,
                                   AccessChannel channel,
                                   const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (channel != AccessChannel::PiWindow) {
                return Result::failed(AccessStatus::Unsupported);
            }
            if (params == nullptr || !params->usable_for_access()) {
                return Result::failed(AccessStatus::BadParameters);
            }
            if (phase_ != Phase::Up) {
                return Result::failed(AccessStatus::NotInitialized);
            }
            if (!supports_width(in.size())) {
                return Result::failed(AccessStatus::BadWidth);
            }
            /* The facts the write lands on have to be in place before the window opens. Readiness
             *  is asked here rather than assumed, and its answer is a named refusal. */
            if (target_ == nullptr || write_readiness(*target_) != WriteReadiness::Ready) {
                return Result::failed(AccessStatus::NotInitialized);
            }
            std::uint64_t value = 0U;
            for (std::size_t i = 0U; i < in.size(); ++i) {
                value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(in[i])) << (8U * i);
            }
            if (ops_ == nullptr || !ops_->land_word(address, value, params->offset)) {
                return Result::failed(AccessStatus::Failed);
            }
            return Result::ok(in.size());
        }

        [[nodiscard]] ReadResult read(std::uint64_t address, std::size_t bytes,
                                      AccessChannel channel,
                                      const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (channel != AccessChannel::PiWindow) {
                return ReadResult::failed(AccessStatus::Unsupported);
            }
            if (params == nullptr || !params->usable_for_access()) {
                return ReadResult::failed(AccessStatus::BadParameters);
            }
            if (phase_ != Phase::Up) {
                return ReadResult::failed(AccessStatus::NotInitialized);
            }
            if (!supports_width(bytes)) {
                return ReadResult::failed(AccessStatus::BadWidth);
            }
            /* Arbitrary reads need the object found and a page prepared, and this backend does
             *  them later than it writes; until then the refusal is named, not silent. */
            if (ops_ == nullptr || target_ == nullptr || !ops_->can_read() ||
                write_readiness(*target_) != WriteReadiness::Ready) {
                return ReadResult::failed(AccessStatus::Unsupported);
            }
            std::uint64_t word = 0U;
            if (ops_ == nullptr || !ops_->read_word(address, params->offset, word)) {
                return ReadResult::failed(AccessStatus::Failed);
            }
            for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
                word_out_[i] = static_cast<std::byte>((word >> (8U * i)) & 0xffU);
            }
            return ReadResult::ok(std::span<const std::byte>(word_out_, sizeof(std::uint64_t)));
        }

        [[nodiscard]] Result write_zero(std::uint64_t address, AccessChannel channel,
                                        const AccessMiscParameters &parameters) noexcept override {
            const std::byte zero[sizeof(std::uint64_t)] = {};
            return write(address, std::span<const std::byte>(zero), channel, parameters);
        }

        /** The route currently up, for diagnostics. Unset when none is. */
        [[nodiscard]] profile::RouteId route() const noexcept { return route_; }

    private:
        enum class Phase : unsigned char { NeverUp = 0, ComingUp = 1, Up = 2, Down = 3, Broken = 4 };

        /** The parameters as this backend's set, or null when they are someone else's. Checked
         *  by name, so a set from another backend is refused rather than reinterpreted. */
        [[nodiscard]] static const Access43499Parameters *as_params(const AccessMiscParameters &p) noexcept {
            if (p.name() != std::string_view("cve_2026_43499")) {
                return nullptr;
            }
            return static_cast<const Access43499Parameters *>(&p);
        }

        /* Borrowed, and nullable: a factory builds an instance before either is known. */
        WordLanding *ops_ = nullptr;
        const TargetState *target_ = nullptr;
        Phase phase_ = Phase::NeverUp;
        profile::RouteId route_ = profile::RouteId::Unset;
        /** The buffer a read answer points into: owned by the access so the span a caller gets
         *  stays valid until the next call, which is what the interface promises. */
        std::byte word_out_[sizeof(std::uint64_t)] = {};
    };
}  // namespace ghostlock::backend::cve_2026_43499
