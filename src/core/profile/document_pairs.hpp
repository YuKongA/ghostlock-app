#pragma once
/** @file profile/document_pairs.hpp
 *  The bridge from a document segment to the pairs the memory layer reads.
 *
 *  The memory layer's read path takes a flat view of resolved pairs and refuses
 *  a key that is not there by name; this is where that view comes from. The keys
 *  are asked for in a fixed order and answered in the same order, so a caller can
 *  check the list once and then read by slot -- and a key that the document does
 *  not carry, or carries as something other than a number, is reported with the
 *  key that failed rather than with a default.
 *
 *  A duplicate key inside one segment is not addressable through this path: the
 *  wire is a map, so the document yields one value per key. The read path keeps
 *  its duplicate case for callers that assemble their own lists.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "memory/target_profile.hpp"
#include "profile/glkv3_document.hpp"

namespace ghostlock::profile {
    /** Why the pairs could not be filled. Names come from the single table. */
    enum class PairsVerdict : unsigned char {
        Ok = 0,
        /** The document is not one this build reads. */
        DocumentInvalid = 1,
        /** A requested key is not in the segment, or is not a number. */
        KeyUnavailable = 2,
        /** The caller's output buffer cannot hold the requested keys. */
        TooManyKeys = 3,
    };

    [[nodiscard]] constexpr const char *pairs_verdict_name(PairsVerdict verdict) noexcept {
        switch (verdict) {
            case PairsVerdict::Ok: return "ok";
            case PairsVerdict::DocumentInvalid: return "document-invalid";
            case PairsVerdict::KeyUnavailable: return "tuning-missing";
            case PairsVerdict::TooManyKeys: return "budget-exceeded";
        }
        return "unknown";
    }

    /** The outcome: which key failed, and how many pairs are usable. On any
     *  failure filled is zero, so a caller cannot read a half-filled list. */
    struct PairsResult final {
        PairsVerdict verdict = PairsVerdict::DocumentInvalid;
        const char *missing_key = nullptr;
        std::size_t filled = 0U;
    };

    /** Fill out[0..key_count) with the segment's value for each key, in order. */
    [[nodiscard]] PairsResult read_section_pairs(const Document &document,

                                                 std::string_view segment,

                                                 const char *const *keys, std::size_t key_count,
                                                 memory::KeyValue *out,

                                                 std::size_t capacity) noexcept;
}  // namespace ghostlock::profile
