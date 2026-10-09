#pragma once
/** @file memory/write_request.hpp
 *  What one write asks for, and what makes it a well-formed request.
 *
 *  A write names a kernel address, says what goes there, and says whether the
 *  child that carries it is preserved. The shape is fixed and trivially
 *  copyable, because a request is passed through the race window where nothing
 *  may allocate; the sizes and offsets that fill a request come from the
 *  profile, never from here.
 *
 *  Validation is deliberately about the request itself: what a write targets is
 *  the caller's business, but a request that asks for nothing, or for a mode
 *  this build does not know, is refused by name before any mechanism runs.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ghostlock::backend::cve_2026_43499::memory {
    /** What goes at the target. Disabled never reaches memory. */
    enum class WriteMode : std::int32_t {
        Disabled = 0,
        /** Zero the fragment: clearing a field rather than replacing it. */
        Zero = 1,
        /** Write a credential fragment: uid/gid and the capability set. */
        Credential = 2,
    };

    struct WriteRequest final {
        std::uintptr_t target = 0U;
        WriteMode mode = WriteMode::Disabled;
        bool preserve_child = false;
    };

    /* The window passes a request by value through a path that must not allocate
     * or dispatch indirectly, so the shape is pinned here rather than assumed. */
    static_assert(std::is_standard_layout_v<WriteRequest>);
    static_assert(std::is_trivially_copyable_v<WriteRequest>);
    static_assert(std::is_trivially_destructible_v<WriteRequest>);
    static_assert(offsetof(WriteRequest, target) == 0U);
    static_assert(offsetof(WriteRequest, mode) == sizeof(std::uintptr_t));
    static_assert(sizeof(WriteRequest) <= 3U * sizeof(std::uintptr_t));

    /** Why a request was refused. Names come from the single table (UML 3.2). */
    enum class RequestVerdict : unsigned char {
        Ok = 0,
        NoTarget = 1,
        NoMode = 2,
    };

    [[nodiscard]] constexpr const char *request_verdict_name(RequestVerdict verdict) noexcept {
        switch (verdict) {
            case RequestVerdict::Ok: return "ok";
            case RequestVerdict::NoTarget: return "param-invalid";
            case RequestVerdict::NoMode: return "param-invalid";
        }
        return "unknown";
    }

    /** A request that would not write anything is not a request. Both refusals
     *  share one name on purpose: from the plan's point of view a write with no
     *  target and a write with nothing to put there are the same bad argument. */
    [[nodiscard]] constexpr RequestVerdict check_request(const WriteRequest &request) noexcept {
        if (request.target == 0U) {
            return RequestVerdict::NoTarget;
        }
        if (request.mode == WriteMode::Disabled) {
            return RequestVerdict::NoMode;
        }
        return RequestVerdict::Ok;
    }

    /** The mode a step's write uses, from its declared name; Disabled when the
     *  name is not one this build knows, which check_request then refuses. */
    [[nodiscard]] constexpr WriteMode write_mode_of(std::string_view name) noexcept {
        if (name == "zero") {
            return WriteMode::Zero;
        }
        if (name == "credential") {
            return WriteMode::Credential;
        }
        return WriteMode::Disabled;
    }

    [[nodiscard]] constexpr const char *write_mode_token(WriteMode mode) noexcept {
        switch (mode) {
            case WriteMode::Disabled: return "disabled";
            case WriteMode::Zero: return "zero";
            case WriteMode::Credential: return "credential";
        }
        return "unknown";
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
