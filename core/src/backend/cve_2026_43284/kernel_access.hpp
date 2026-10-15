#pragma once
/** @file backend/cve_2026_43284/kernel_access.hpp
 *  The 43284 backend's kernel access, over the page cache write.
 *
 *  Same interface as the other backend and a different mechanism behind it: this one writes a
 *  sixteen-byte block through the page cache rather than a word through the race window, so its
 *  width answer is sixteen and its refusals name the same codes for different reasons. Credentials
 *  are the condition for coming up at all, because its write cannot be made without the key
 *  material, and the material is held by the access so it can be cleared when the route goes down.
 *  Both faces are served: a block read travels the same door the write opened, which is what a patch
 *  verification reads, and a word read is that block projected onto its first eight bytes.
 *
 *  The route axis: empty. This backend is selected by its id and nothing inside it chooses between
 *  routes, so the route id in the parameters is not consulted; what the parameters carry is the key
 *  material and a position.
 */
#include <cstddef>
#include <cstdint>
#include <span>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/landing/pagecache_landing.hpp"
#include "contract/kernel_access.hpp"


namespace ghostlock::backend::cve_2026_43284 {
    class Access final : public contract::KernelAccess {
    public:
        using AccessState = contract::KernelAccess::AccessState;
        using AccessStatus = contract::KernelAccess::Status;
        using BoolResult = contract::KernelAccess::AccessResult<bool>;
        using CountResult = contract::KernelAccess::AccessResult<std::size_t>;
        using BlockResult = contract::KernelAccess::AccessResult<std::span<const std::byte>>;
        using AccessMiscParameters = contract::KernelAccess::AccessMiscParameters;
        using AccessCarrier = contract::AccessCarrier;
        using AccessChannel = contract::AccessChannel;

        /** Blank: nothing is taken or decided here, as the ruling requires. The landing is borrowed
         *  and must outlive this object. */
        //  reference becomes a nullable pointer. Delete the commented form above once the factory
        //  and its tests pass, as the commenting rule requires.
        Access() noexcept = default;
        explicit Access(landing::PageCacheLanding *landing) noexcept : ops_(landing) {}
        /** Hand over the landing a factory could not know. */
        void adopt(landing::PageCacheLanding *landing) noexcept { ops_ = landing; }

        [[nodiscard]] AccessState state(AccessChannel channel) const noexcept override {
            if (channel != AccessChannel::PageCache) {
                return AccessState::Unsupported;
            }
            if (ops_ != nullptr && ops_->up()) {
                return AccessState::Ready;
            }
            return ever_up_ ? AccessState::Closed : AccessState::Declared;
        }
        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            return channel == AccessChannel::PageCache;
        }
        [[nodiscard]] bool supports(AccessCarrier carrier) const noexcept override {
            return carrier == AccessCarrier::FilePage;
        }
        /** A block, not a word: sixteen bytes, which is what the page cache writes. */
        [[nodiscard]] bool supports_width(std::size_t bytes) const noexcept override {
            return bytes == landing::kWriteWidth;
        }

        [[nodiscard]] BoolResult initialize(const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (params == nullptr || !params->usable_for_initialize()) {
                return BoolResult::failed(AccessStatus::BadParameters);
            }
            if (ops_ == nullptr || !ops_->bring_up_with(params->sa)) {
                return BoolResult::failed(AccessStatus::Failed);
            }
            /* After bring-up, never before: opening the doors starts a fresh sequence, and a session
             *  that has already written (patch #1, or another window) must continue its own. */
            if (params->has_sequence()) {
                ops_->adopt_sequence(params->sequence);
            }
            ever_up_ = true;
            return BoolResult::ok(true);
        }

        [[nodiscard]] BoolResult shutdown() noexcept override {
            if (ops_ == nullptr || !ops_->up()) {
                return BoolResult::failed(AccessStatus::NotInitialized);
            }
            return (ops_ == nullptr || ops_->tear_down()) ? BoolResult::ok(true)
                                                                : BoolResult::failed(AccessStatus::Failed);
        }

        /** Write one block. The bytes are the value; the position comes from the parameters. */
        [[nodiscard]] CountResult write(std::uint64_t, std::span<const std::byte> in,
                                        AccessChannel channel,
                                        const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (channel != AccessChannel::PageCache) {
                return CountResult::failed(AccessStatus::Unsupported);
            }
            if (params == nullptr || !params->usable_for_access()) {
                return CountResult::failed(AccessStatus::BadParameters);
            }
            if (ops_ == nullptr || !ops_->up()) {
                return CountResult::failed(AccessStatus::NotInitialized);
            }
            if (in.size() != landing::kWriteWidth) {
                return CountResult::failed(AccessStatus::BadWidth);
            }
            std::uint8_t block[landing::kWriteWidth] = {};
            for (std::size_t i = 0U; i < landing::kWriteWidth; ++i) {
                block[i] = std::to_integer<std::uint8_t>(in[i]);
            }
            if (ops_ == nullptr || !ops_->land_block(params->offset, block)) {
                return CountResult::failed(AccessStatus::Failed);
            }
            return CountResult::ok(landing::kWriteWidth);
        }

        /** Writing zeros is the same path with a zero block, as on the other backend. */
        [[nodiscard]] CountResult write_zero(std::uint64_t address, AccessChannel channel,
                                             const AccessMiscParameters &parameters) noexcept override {
            const std::byte zero[landing::kWriteWidth] = {};
            return write(address, std::span<const std::byte>(zero), channel, parameters);
        }

        /** Read one block back. The unit is the same sixteen bytes the write deals in, and the
         *  position comes from the parameters for the same reason the write takes it there: this
         *  backend's addresses are carrier positions, and one authority for the position means a
         *  read and the write it verifies cannot disagree about where they are. A caller wanting a
         *  single word has read64, which is this same block projected onto its first eight bytes. */
        [[nodiscard]] BlockResult read(std::uint64_t, std::size_t bytes, AccessChannel channel,
                                       const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (channel != AccessChannel::PageCache) {
                return BlockResult::failed(AccessStatus::Unsupported);
            }
            if (params == nullptr || !params->usable_for_access()) {
                return BlockResult::failed(AccessStatus::BadParameters);
            }
            if (ops_ == nullptr || !ops_->up()) {
                return BlockResult::failed(AccessStatus::NotInitialized);
            }
            if (bytes != landing::kWriteWidth) {
                return BlockResult::failed(AccessStatus::BadWidth);
            }
            /* A route that cannot read and a read that failed are different answers, and the landing
             *  is asked which one this is before any block is attempted: the first is a capability
             *  this route does not have, the second is a call that did not come back. */
            if (!ops_->can_read()) {
                return BlockResult::failed(AccessStatus::Unsupported);
            }
            std::uint8_t block[landing::kWriteWidth] = {};
            if (!ops_->read_block(params->offset, block)) {
                return BlockResult::failed(AccessStatus::Failed);
            }
            for (std::size_t i = 0U; i < landing::kWriteWidth; ++i) {
                block_out_[i] = static_cast<std::byte>(block[i]);
            }
            return BlockResult::ok(std::span<const std::byte>(block_out_, landing::kWriteWidth));
        }

        /** One word, as the first eight bytes of the block that holds it, little-endian. Reading
         *  the whole block and projecting is the honest answer: this backend's unit is sixteen
         *  bytes, and a word read that skipped the block read would be a second read path. */
        [[nodiscard]] contract::KernelAccess::AccessResult<std::uint64_t> read64(
            std::uint64_t address, AccessChannel channel,
            const AccessMiscParameters &parameters) noexcept override {
            const BlockResult block = read(address, landing::kWriteWidth, channel, parameters);
            contract::KernelAccess::AccessResult<std::uint64_t> answer{};
            answer.status = block.status;
            if (!block.ok()) {
                return answer;
            }
            std::uint64_t value = 0U;
            for (std::size_t i = 0U; i < sizeof(std::uint64_t); ++i) {
                value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(block.value[i]))
                         << (8U * i);
            }
            answer.status = AccessStatus::Ok;
            answer.value = value;
            return answer;
        }

    private:
        [[nodiscard]] static const AccessParams *as_params(const AccessMiscParameters &p) noexcept {
            if (p.name() != std::string_view("cve_2026_43284")) {
                return nullptr;
            }
            return static_cast<const AccessParams *>(&p);
        }

        landing::PageCacheLanding *ops_ = nullptr;
        /** Whether the route has ever been up, so a closed route is distinguishable from one that
         *  never came up. */
        bool ever_up_ = false;
        /** The buffer a read answer points into: owned by the access so the span a caller gets stays
         *  valid until the next call, which is what the interface promises. */
        std::byte block_out_[landing::kWriteWidth] = {};
    };
}  // namespace ghostlock::backend::cve_2026_43284
