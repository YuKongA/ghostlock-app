#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_DIAG_LINE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_DIAG_LINE_HPP

/* Structured, bounded 43284 diagnostic lines (S4 logging batch).
 *
 * One line per milestone on the run's diagnostic channel:
 *
 *   run.43284 <phase> key=value key=0x... fail reason=<EnumName> ...
 *
 * Design rules this type exists to enforce:
 *   - BOUNDED: the visible line is at most kDiagLineMaxBytes (256); a line that
 *     would exceed it is cut and marked " truncated=1" instead of growing;
 *   - NO ALLOCATION: a fixed buffer, so a diagnostic can never fail an attack
 *     step (the chain is noexcept and allocation-free by contract);
 *   - NO FORMAT-STRING SEMANTICS: values are copied verbatim (sanitized), never
 *     interpreted as printf formats;
 *   - ONE LINE: whitespace/control bytes in values become '_', so a hostile or
 *     malformed path cannot inject a second line;
 *   - NEVER KEY MATERIAL: callers pass sizes/ids/tokens only. The session keys
 *     and the ESP SA keys must never reach this type (design A.5).
 *
 * Level/transport policy lives with the caller: the device units print these on
 * stderr (diagnostics), while the human-readable phase lines keep using pr_*. */

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284 {

    inline constexpr std::size_t kDiagLineMaxBytes = 256u;
    /* Room reserved so the truncation marker still fits INSIDE the cap: the
     * visible line (marker included) never exceeds kDiagLineMaxBytes. */
    inline constexpr std::size_t kDiagLineMarkerBytes = 12u; /* " truncated=1" */
    inline constexpr std::size_t kDiagLineContentMaxBytes =
            kDiagLineMaxBytes - kDiagLineMarkerBytes;

    class DiagLine final {
    public:
        explicit DiagLine(std::string_view phase) noexcept {
            append("run.43284 ");
            append_token(phase);
        }

        DiagLine(const DiagLine &) = delete;
        DiagLine &operator=(const DiagLine &) = delete;

        /* Decimal unsigned field. */
        DiagLine &u(std::string_view key, std::uint64_t value) noexcept {
            char buffer[24] = {};
            const int written = std::snprintf(buffer, sizeof(buffer), "%llu",
                                              static_cast<unsigned long long>(value));
            return field(key, std::string_view(buffer, written > 0
                                                           ? static_cast<std::size_t>(written)
                                                           : 0u));
        }

        /* Hexadecimal field with the 0x prefix (addresses/offsets). */
        DiagLine &x(std::string_view key, std::uint64_t value) noexcept {
            char buffer[24] = {};
            const int written = std::snprintf(buffer, sizeof(buffer), "0x%llx",
                                              static_cast<unsigned long long>(value));
            return field(key, std::string_view(buffer, written > 0
                                                           ? static_cast<std::size_t>(written)
                                                           : 0u));
        }

        /* Boolean field as 0/1 (matches the existing diagnostics). */
        DiagLine &b(std::string_view key, bool value) noexcept {
            return field(key, value ? std::string_view("1") : std::string_view("0"));
        }

        /* Enum name / stable token (sanitized). */
        DiagLine &n(std::string_view key, std::string_view name) noexcept {
            append_char(' ');
            append_token(key);
            append("=");
            append_token(name);
            return *this;
        }

        /* Free text (path/label), sanitized to a single line. */
        DiagLine &s(std::string_view key, std::string_view text) noexcept {
            return n(key, text);
        }

        /* Failure marker: " fail reason=<Name>". */
        DiagLine &fail(std::string_view reason) noexcept {
            append(" fail reason=");
            append_token(reason);
            return *this;
        }

        /* Finalizes (once) and returns the NUL-terminated single line. */
        [[nodiscard]] const char *c_str() noexcept {
            if (!finalized_) {
                if (truncated_) {
                    append_forced(" truncated=1");
                }
                line_[used_] = '\n';
                line_[used_ + 1u] = '\0';
                finalized_ = true;
            }
            return line_.data();
        }

        /* Visible length, excluding the final newline (always <= cap). */
        [[nodiscard]] std::size_t size() const noexcept { return used_; }

    private:
        static constexpr std::size_t kCapacity = kDiagLineMaxBytes + 24u;

        DiagLine &field(std::string_view key, std::string_view value) noexcept {
            append_char(' ');
            append_token(key);
            append("=");
            append_token(value);
            return *this;
        }

        void append_char(char raw) noexcept {
            if (used_ >= kDiagLineContentMaxBytes) {
                truncated_ = true;
                return;
            }
            line_[used_++] = raw;
        }

        /* Bounded, sanitized copy. Once the cap is reached the rest is dropped
         * and truncated_ marks the line. */
        void append_token(std::string_view text) noexcept {
            for (const char raw : text) {
                if (used_ >= kDiagLineContentMaxBytes) {
                    truncated_ = true;
                    return;
                }
                const unsigned char byte = static_cast<unsigned char>(raw);
                const bool clean = byte > 0x20u && byte < 0x7fu;
                line_[used_++] = clean ? raw : '_';
            }
        }

        /* Literal append (prefix/markers/keys): copied VERBATIM and bounded.
         * Only values go through append_token(), which sanitizes. */
        void append(std::string_view text) noexcept {
            for (const char raw : text) {
                if (used_ >= kDiagLineContentMaxBytes) {
                    truncated_ = true;
                    return;
                }
                line_[used_++] = raw;
            }
        }

        /* Marker append that is allowed to exceed the visible cap (the buffer
         * reserves room for it); still bounded by kCapacity. */
        void append_forced(std::string_view text) noexcept {
            for (const char raw : text) {
                if (used_ >= kDiagLineMaxBytes) {
                    return;
                }
                line_[used_++] = raw;
            }
        }

        std::array<char, kCapacity> line_{};
        std::size_t used_ = 0u;
        bool truncated_ = false;
        bool finalized_ = false;
    };

} // namespace ghostlock::backend::cve_2026_43284

#endif
