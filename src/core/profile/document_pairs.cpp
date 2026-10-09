/** @file profile/document_pairs.cpp
 *  Asking a document segment for the keys a caller declared.
 */
#include "profile/document_pairs.hpp"

namespace ghostlock::profile {
    PairsResult read_section_pairs(const Document &document, std::string_view segment,

                                   const char *const *keys, std::size_t key_count,

                                   memory::KeyValue *out, std::size_t capacity) noexcept {
        PairsResult result{};
        if (!document.is_open()) {
            result.verdict = PairsVerdict::DocumentInvalid;
            return result;
        }
        if (keys == nullptr || out == nullptr) {
            result.verdict = PairsVerdict::TooManyKeys;
            return result;
        }
        if (key_count > capacity) {
            result.verdict = PairsVerdict::TooManyKeys;
            return result;
        }
        /* Every key is read before any of them is handed over: a caller that
         * gets Ok gets the whole list, and one that gets a refusal gets a name. */
        for (std::size_t i = 0U; i < key_count; ++i) {
            if (keys[i] == nullptr) {
                result.verdict = PairsVerdict::KeyUnavailable;
                result.missing_key = nullptr;
                return result;
            }
            std::uint64_t value = 0U;
            const std::string_view key(keys[i]);
            if (document.uint_key(segment, key, value) != DocumentVerdict::Ok) {
                result.verdict = PairsVerdict::KeyUnavailable;
                result.missing_key = keys[i];
                return result;
            }
            out[i].key = key;
            out[i].value = value;
        }
        result.verdict = PairsVerdict::Ok;
        result.filled = key_count;
        return result;
    }
}  // namespace ghostlock::profile
