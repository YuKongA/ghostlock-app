#pragma once
/** @file script/call_sink.hpp
 *  The sink a script call is forwarded to.
 *
 *  Boundary (Lead clarification, A6-1): the sandbox does NOT understand plans.
 *  Each of the seven host-surface bindings forwards the call as a READ-ONLY
 *  argument view to the injected sink; filling a staged plan (PlanOp[]) is
 *  A6-2's job and is not modelled here.
 *
 *  Shape: a function-pointer table, not a virtual class -- the tree's window
 *  work keeps indirect dispatch out of hot paths and the gate scans built
 *  binaries for _ZTV symbols, so no vtable is introduced.
 *
 *  Arguments carry a type tag plus either a u64, a string span or a bool; A6-1
 *  does not parse their semantics.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/handle_table.hpp"

namespace ghostlock::script {
    /** Type tag of one forwarded argument. */
    enum class ArgKind : unsigned char {
        U64 = 0,
        Str = 1,
        Bool = 2,
        /** A typed handle: the script passes it back, never interprets it. */
        Handle = 3,
    };

    /** Read-only view of one forwarded argument. */
    struct ArgView final {
        ArgKind kind = ArgKind::U64;
        std::uint64_t u64 = 0;
        std::string_view str{};
        bool boolean = false;
        Handle handle{};
    };

    /** One forwarded call: which surface row, how it is reached, with what. */
    struct CallView final {
        std::size_t surface_index = 0;
        contract::SurfaceForm form = contract::SurfaceForm::Call;
        const char *name = nullptr;
        const ArgView *args = nullptr;
        std::size_t arg_count = 0;
    };

    /** The injected sink. A6-1 forwards calls; A6-2 records them. */
    struct CallSink final {
        void *context = nullptr;
        void (*on_op)(void *context, const CallView *view) = nullptr;
        void (*on_error)(void *context, const char *named_code) = nullptr;
    };
}  // namespace ghostlock::script
