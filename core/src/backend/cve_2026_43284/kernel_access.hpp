#pragma once
/** @file backend/cve_2026_43284/kernel_access.hpp
 *  The 43284 backend's kernel access, over the page cache write.
 *
 *  Same interface as the other backend and a different mechanism behind it: this one writes a
 *  sixteen-byte block through the page cache rather than a word through the race window, so its
 *  width answer is sixteen and its refusals name the same codes for different reasons. Credentials
 *  are the condition for coming up at all, because its write cannot be made without the key
 *  material, and the material is held by the access so it can be cleared when the route goes down.
 *
 *  The route axis: empty. This backend is selected by its id and nothing inside it chooses between
 *  routes, so the route id in the parameters is not consulted; what the parameters carry is the key
 *  material and a position.
 */
#include <cstddef>
#include <cstdint>
#include <span>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"
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

        /** Blank: nothing is taken or decided here, as the ruling requires. The seam is borrowed
         *  and must outlive this object. */
        // The retired form borrowed a reference, which cannot be default constructed.
        // /** Blank: nothing is taken or decided here, as the ruling requires. The seam is borrowed
        //  *  and must outlive this object. */
        // explicit Access(seam::PageCacheSeam &seam) noexcept : seam_(seam) {}
        //TODO(factory-reachability): the factory knows an id and nothing else, so the borrowed
        //  reference becomes a nullable pointer. Delete the commented form above once the factory
        //  and its tests pass, as the commenting rule requires.
        Access() noexcept = default;
        explicit Access(seam::PageCacheSeam *seam) noexcept : seam_(seam) {}
        /** Hand over the seam a factory could not know. */
        void adopt(seam::PageCacheSeam *seam) noexcept { seam_ = seam; }

        [[nodiscard]] AccessState state(AccessChannel channel) const noexcept override {
            if (channel != AccessChannel::PageCache) {
                return AccessState::Unsupported;
            }
            if (seam_ != nullptr && seam_->up()) {
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
            return bytes == seam::kWriteWidth;
        }

        [[nodiscard]] BoolResult initialize(const AccessMiscParameters &parameters) noexcept override {
            const auto *params = as_params(parameters);
            if (params == nullptr || !params->usable_for_initialize()) {
                return BoolResult::failed(AccessStatus::BadParameters);
            }
            if (seam_ == nullptr || !seam_->bring_up_with(params->sa)) {
                return BoolResult::failed(AccessStatus::Failed);
            }
            ever_up_ = true;
            return BoolResult::ok(true);
        }

        [[nodiscard]] BoolResult shutdown() noexcept override {
            if (seam_ == nullptr || !seam_->up()) {
                return BoolResult::failed(AccessStatus::NotInitialized);
            }
            return (seam_ == nullptr || seam_->tear_down()) ? BoolResult::ok(true)
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
            if (seam_ == nullptr || !seam_->up()) {
                return CountResult::failed(AccessStatus::NotInitialized);
            }
            if (in.size() != seam::kWriteWidth) {
                return CountResult::failed(AccessStatus::BadWidth);
            }
            std::uint8_t block[seam::kWriteWidth] = {};
            for (std::size_t i = 0U; i < seam::kWriteWidth; ++i) {
                block[i] = std::to_integer<std::uint8_t>(in[i]);
            }
            if (seam_ == nullptr || !seam_->land_block(params->offset, block)) {
                return CountResult::failed(AccessStatus::Failed);
            }
            return CountResult::ok(seam::kWriteWidth);
        }

        /** Writing zeros is the same path with a zero block, as on the other backend. */
        [[nodiscard]] CountResult write_zero(std::uint64_t address, AccessChannel channel,
                                             const AccessMiscParameters &parameters) noexcept override {
            const std::byte zero[seam::kWriteWidth] = {};
            return write(address, std::span<const std::byte>(zero), channel, parameters);
        }

        /** Reads are not filled in yet: the block read exists beside this write in the old tree
         *  and is not ported, so this says unsupported rather than returning zeros as data. */
        [[nodiscard]] BlockResult read(std::uint64_t, std::size_t, AccessChannel,
                                       const AccessMiscParameters &) noexcept override {
            return BlockResult::failed(AccessStatus::Unsupported);
        }

    private:
        [[nodiscard]] static const AccessParams *as_params(const AccessMiscParameters &p) noexcept {
            if (p.name() != std::string_view("cve_2026_43284")) {
                return nullptr;
            }
            return static_cast<const AccessParams *>(&p);
        }

        // The retired member was a reference.
        // seam::PageCacheSeam &seam_;
        //TODO(factory-reachability): nullable, so a factory-built instance can exist.
        seam::PageCacheSeam *seam_ = nullptr;
        /** Whether the route has ever been up, so a closed route is distinguishable from one that
         *  never came up. */
        bool ever_up_ = false;
    };
}  // namespace ghostlock::backend::cve_2026_43284
