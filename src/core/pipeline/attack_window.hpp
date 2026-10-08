#pragma once
/** @file pipeline/attack_window.hpp
 *  P4 step 2: the AttackWindow skeleton.
 *
 *  Authority: docs/analysis/composition-layer-p4-spec.md sections 1, 5 (C1) and 7.
 *
 *  Responsibilities (and nothing else):
 *   - sole owner of the window-scoped resources (contract::kResourceScopes rows
 *     whose scope is Window). No other layer may hold or release them;
 *   - RAII reclaim on EVERY exit path (success / failure / retry / cancel /
 *     early return) -- the historical PI panic came from a missed reclaim;
 *   - the single mount point for window invariants (static_asserts at the
 *     bottom): resource non-overlap, guard size, exit-path completeness.
 *
 *  Non-responsibilities: no script interpretation, no configuration reading,
 *  no strategy choice, no Lua reference (spec section 4.3).
 *
 *  @note The window host is deliberately allocation-free and trivially movable
 *        only by pointer: callers keep the object alive for the waiter window.
 */
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "contract/resource_scope.hpp"

namespace ghostlock::pipeline {
    /** The seven window-scoped resources (names frozen in the scope table). */
    struct WindowResources final {
        void *profile = nullptr;
        void *addresses = nullptr;
        void *heap = nullptr;
        void *race = nullptr;
        void *victim = nullptr;
        void *parked_victim = nullptr;
        void *parked_victim_cmd = nullptr;
        std::uint64_t generation = 1;
    };

    /** RAII reclaim scope: the one guard every exit path must run through. */
    class AttackWindow final {
      public:
        using ExitPath = contract::ExitPath;

        /** The guard hands the reclaim back to the window on scope exit. */
        class Guard final {
          public:
            Guard() noexcept = default;
            explicit Guard(AttackWindow *owner) noexcept : owner_(owner) {}
            Guard(const Guard &) = delete;
            Guard &operator=(const Guard &) = delete;
            Guard(Guard &&other) noexcept : owner_(other.owner_) { other.owner_ = nullptr; }
            Guard &operator=(Guard &&) = delete;
            /** Reclaims unless the caller explicitly cancelled the window. */
            ~Guard() noexcept {
                if (owner_ != nullptr) {
                    owner_->reclaim();
                }
            }
            /** True while this guard is responsible for the reclaim. */
            [[nodiscard]] bool owns() const noexcept { return owner_ != nullptr; }
            /** Cancel path: the window keeps running, the guard stops owning. */
            void dismiss() noexcept { owner_ = nullptr; }
            /** Release the window through this guard (failure path helper). */
            void reclaim_now() noexcept {
                if (owner_ != nullptr) {
                    owner_->reclaim();
                    owner_ = nullptr;
                }
            }

          private:
            AttackWindow *owner_ = nullptr;
        };

        AttackWindow() noexcept = default;
        ~AttackWindow() noexcept { reclaim(); }
        AttackWindow(const AttackWindow &) = delete;
        AttackWindow &operator=(const AttackWindow &) = delete;

        /** Enter the window; the returned guard reclaims on every exit path. */
        [[nodiscard]] Guard open(ExitPath path) noexcept {
            path_ = path;
            open_ = true;
            bump_generation();
            return Guard{this};
        }

        /** Idempotent reclaim: safe from the guard, the dtor and explicit paths. */
        void reclaim() noexcept {
            if (!open_) {
                return;
            }
            open_ = false;
            reclaimed_ = true;
            ++reclaim_count_;
            bump_generation();
        }

        [[nodiscard]] bool open_here() const noexcept { return open_; }
        [[nodiscard]] bool reclaimed() const noexcept { return reclaimed_; }
        [[nodiscard]] std::size_t reclaim_count() const noexcept { return reclaim_count_; }
        [[nodiscard]] ExitPath path() const noexcept { return path_; }
        [[nodiscard]] std::uint64_t generation() const noexcept { return resources_.generation; }
        [[nodiscard]] WindowResources &resources() noexcept { return resources_; }
        [[nodiscard]] const WindowResources &resources() const noexcept { return resources_; }

      private:
        /** Generation counter: never zero, so "handle live" is decidable. */
        void bump_generation() noexcept {
            ++resources_.generation;
            if (resources_.generation == 0) {
                resources_.generation = 1;
            }
        }

        WindowResources resources_{};
        ExitPath path_ = ExitPath::Success;
        bool open_ = false;
        bool reclaimed_ = false;
        std::size_t reclaim_count_ = 0;
    };

    /** Compile-time string equality for the exit-path name table. */
    [[nodiscard]] constexpr bool same_text(const char *a, const char *b) noexcept {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    /** Exit-path completeness: every enumerated path has its own name. */
    [[nodiscard]] constexpr bool exit_paths_are_complete() noexcept {
        for (std::size_t i = 0; i < contract::kExitPathCount; ++i) {
            if (contract::kExitPathNames[i] == nullptr) {
                return false;
            }
            for (std::size_t j = i + 1; j < contract::kExitPathCount; ++j) {
                if (same_text(contract::kExitPathNames[i], contract::kExitPathNames[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    /* ---- window invariants: the ONLY mount point (spec section 1) ---- */
    static_assert(offsetof(WindowResources, profile) + sizeof(void *) <= offsetof(WindowResources, addresses),
                  "window resources must not overlap");
    static_assert(offsetof(WindowResources, addresses) + sizeof(void *) <= offsetof(WindowResources, heap),
                  "window resources must not overlap");
    static_assert(offsetof(WindowResources, heap) + sizeof(void *) <= offsetof(WindowResources, race),
                  "window resources must not overlap");
    static_assert(offsetof(WindowResources, race) + sizeof(void *) <= offsetof(WindowResources, victim),
                  "window resources must not overlap");
    static_assert(offsetof(WindowResources, victim) + sizeof(void *) <= offsetof(WindowResources, parked_victim),
                  "window resources must not overlap");
    static_assert(offsetof(WindowResources, parked_victim) + sizeof(void *) <=
                      offsetof(WindowResources, parked_victim_cmd),
                  "window resources must not overlap");
    static_assert(std::is_standard_layout_v<WindowResources>, "resources must stay standard-layout");
    static_assert(sizeof(AttackWindow) <= 128, "window host must stay small (no hidden allocation)");
    static_assert(contract::kExitPathCount == 5, "exit-path completeness is a compile-time fact");
    static_assert(exit_paths_are_complete(), "every exit path needs a distinct name");
}  // namespace ghostlock::pipeline
