#pragma once
/** @file memory/target_profile.hpp
 *  The TargetProfile CONCEPT and its read path -- and no values at all.
 *
 *  Numbers policy (Q2 rule 1): this file defines no geometry, offset, timing or
 *  tuning value. Values arrive from the GLK profile (their parsing is P3's job);
 *  what lives here is the shape a resolved profile must satisfy and the lookup
 *  path that reports a missing key by NAME.
 *
 *  No default fallback: a required key that cannot be read ends in a named
 *  rejection carrying the key, so the caller (and the gate) can see which asset
 *  entry is missing. Silently substituting a default is exactly what this file
 *  must make impossible.
 *
 *  The read path is pure: it walks a caller-provided key/value view, allocates
 *  nothing and takes no branch on anything but the key set.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ghostlock::memory {
    /** One resolved key/value pair, as the profile layer hands it over. */
    struct KeyValue final {
        std::string_view key;
        std::uint64_t value;
    };

    /** Non-owning view of the resolved pairs (owned by the profile layer). */
    struct KeyView final {
        const KeyValue *items = nullptr;
        std::size_t count = 0;
    };

    /** Why a key lookup did not produce a value (each case is named). */
    enum class ProfileRejection : unsigned char {
        None = 0,
        EmptySource = 1,
        MissingKey = 2,
        DuplicateKey = 3,
    };

    /** Lookup outcome: on failure it carries the offending key. */
    struct KeyLookup final {
        bool ok = false;
        ProfileRejection reason = ProfileRejection::EmptySource;
        std::string_view key{};
        std::uint64_t value = 0;
    };

    /** The read path: exactly one definition of each key, or a named failure. */
    [[nodiscard]] constexpr KeyLookup read_key(const KeyView &view, std::string_view key) noexcept {
        KeyLookup result{};
        result.key = key;
        if (view.items == nullptr || view.count == 0) {
            result.reason = ProfileRejection::EmptySource;
            return result;
        }
        bool found = false;
        for (std::size_t i = 0; i < view.count; ++i) {
            if (view.items[i].key != key) {
                continue;
            }
            if (found) {
                result.reason = ProfileRejection::DuplicateKey;
                return result;
            }
            found = true;
            result.value = view.items[i].value;
        }
        if (!found) {
            result.reason = ProfileRejection::MissingKey;
            return result;
        }
        result.ok = true;
        result.reason = ProfileRejection::None;
        return result;
    }

    /** Printable name of a rejection (exhaustive switch). */
    [[nodiscard]] constexpr const char *profile_rejection_name(ProfileRejection reason) noexcept {
        switch (reason) {
            case ProfileRejection::None: return "none";
            case ProfileRejection::EmptySource: return "profile-empty";
            case ProfileRejection::MissingKey: return "profile-key-missing";
            case ProfileRejection::DuplicateKey: return "profile-key-duplicate";
        }
        return "unknown";
    }

    /** What a resolved target profile must offer. P3 supplies the implementation
     *  (the parsed GLK profile); A2 only fixes the shape so the rest of the tree
     *  can depend on it without depending on the parser. */
    template <class T>
    concept TargetProfile = requires(const T &profile, std::string_view key) {
        { profile.value(key) } -> std::convertible_to<std::optional<std::uint64_t>>;
        { profile.kernel_major() } -> std::convertible_to<unsigned>;
        { profile.kernel_minor() } -> std::convertible_to<unsigned>;
    };

    /** A profile that only exposes the raw view above; used by the A2 evidence
     *  test to prove the read path names a missing key instead of defaulting. */
    struct ViewProfile final {
        KeyView view{};

        [[nodiscard]] std::optional<std::uint64_t> value(std::string_view key) const noexcept {
            const KeyLookup lookup = read_key(view, key);
            if (!lookup.ok) {
                return std::nullopt;
            }
            return lookup.value;
        }
        [[nodiscard]] unsigned kernel_major() const noexcept { return 0; }
        [[nodiscard]] unsigned kernel_minor() const noexcept { return 0; }
    };

    static_assert(TargetProfile<ViewProfile>, "the view profile must satisfy the TargetProfile concept");
}  // namespace ghostlock::memory
