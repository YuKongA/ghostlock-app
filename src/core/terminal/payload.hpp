#pragma once
/** @file terminal/payload.hpp
 *  A6-4: the finishing payload (document segment 3) and what to check first.
 *
 *  The document carries a payload -- an execution script or a kernel module,
 *  as bytes plus the digest the document declares for them. Nothing on this
 *  path trusts the document: a payload whose digest or size does not match what
 *  was declared, or which was declared with nothing behind it, is refused by
 *  name before anything is written or run.
 *
 *  The terminal phase is the only place this applies: the window and the stage
 *  never touch the payload, and the kernel fallback stays in C++ as the
 *  baseline records.
 */
#include <cstddef>
#include <cstdint>

#include "support/sha256.hpp"

namespace ghostlock::terminal {
    /** One payload as the document declares it. */
    struct Payload final {
        const std::uint8_t *bytes = nullptr;
        std::size_t size = 0U;
        /** Text form of the declared digest, or nullptr when absent. */
        const char *declared_sha256 = nullptr;
        /** Declared size; -1 means "the document declared none". */
        std::int64_t declared_size = -1;
    };

    /** Why a payload was refused. Names come from the single table (UML 3.2). */
    enum class PayloadVerdict : unsigned char {
        Ok = 0,
        Missing = 1,
        SizeMismatch = 2,
        DigestMismatch = 3,
    };

    [[nodiscard]] constexpr const char *payload_verdict_name(PayloadVerdict verdict) noexcept {
        switch (verdict) {
            case PayloadVerdict::Ok: return "ok";
            case PayloadVerdict::Missing: return "payload-missing";
            case PayloadVerdict::SizeMismatch: return "payload-missing";
            case PayloadVerdict::DigestMismatch: return "payload-missing";
        }
        return "unknown";
    }

    /** Check a declared payload against its own bytes. All three refusals share
     *  one name on purpose: from the plan's point of view a payload that is not
     *  what was declared is a missing payload, and the reason text is not a
     *  second vocabulary. */
    [[nodiscard]] inline PayloadVerdict check_payload(const Payload &payload) noexcept {
        if (payload.bytes == nullptr || payload.size == 0U) {
            return PayloadVerdict::Missing;
        }
        if (payload.declared_size >= 0 &&
            static_cast<std::uint64_t>(payload.declared_size) != payload.size) {
            return PayloadVerdict::SizeMismatch;
        }
        if (payload.declared_sha256 == nullptr) {
            return PayloadVerdict::Missing;
        }
        const support::Sha256Digest digest = support::sha256(payload.bytes, payload.size);
        if (!support::sha256_matches_text(digest, payload.declared_sha256)) {
            return PayloadVerdict::DigestMismatch;
        }
        return PayloadVerdict::Ok;
    }
}  // namespace ghostlock::terminal
