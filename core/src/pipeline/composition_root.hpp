#pragma once
/** @file pipeline/composition_root.hpp
 *  P4 milestone A, item three: the composition root -- instantiate, bind and
 *
 *  Boundary (spec sections 1 and 5/C2): this component does not decide which op
 *  runs when. That authority belongs to the script plan alone, so no comparison
 *  or position constant may appear in this file; a grep for the
 *  forbidden tokens must stay empty (the evidence command is recorded in the
 *  task report).
 *
 *  It holds no window-scoped resource: AttackWindow owns those and is reachable
 *  here only through accessor.
 */
#include <type_traits>

#include "pipeline/attack_window.hpp"

namespace ghostlock::pipeline {
    /** Compiled composition: one window host plus the bound executor handle. */
    template <class Executor>
    class CompositionRoot final {
      public:
        CompositionRoot() noexcept = default;
        CompositionRoot(const CompositionRoot &) = delete;
        CompositionRoot &operator=(const CompositionRoot &) = delete;

        /** Bind the executor implementation for the coming window. */
        void bind(Executor *executor) noexcept {
            executor_ = executor;
            bound_ = executor_ != nullptr;
        }

        /** True once an executor has been bound. */
        [[nodiscard]] bool bound() const noexcept { return bound_; }
        /** The bound executor, or nullptr. */
        [[nodiscard]] Executor *executor() noexcept { return executor_; }
        /** The window host owned by this composition. */
        [[nodiscard]] AttackWindow &window() noexcept { return window_; }
        [[nodiscard]] const AttackWindow &window() const noexcept { return window_; }

      private:
        AttackWindow window_{};
        Executor *executor_ = nullptr;
        bool bound_ = false;
    };

    /* ---- compile-time checks of the composition contract ---- */
    static_assert(!std::is_copy_constructible_v<CompositionRoot<int>>, "the composition root is not copyable");
    static_assert(!std::is_copy_assignable_v<CompositionRoot<int>>, "the composition root is not copy-assignable");
    static_assert(std::is_standard_layout_v<CompositionRoot<int>>, "the composition root stays standard-layout");
}  // namespace ghostlock::pipeline
