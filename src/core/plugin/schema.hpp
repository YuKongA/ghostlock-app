#ifndef GHOSTLOCK_PLUGIN_SCHEMA_HPP
#define GHOSTLOCK_PLUGIN_SCHEMA_HPP

/* S4 P1 plugin wire schema (contract-design 3.14.7.5).
 *
 * plugin.<id>.<field> is a THIRD top-level owner, neither backend nor platform:
 * one countermeasure can serve several backends, so its configuration must stay
 * orthogonal to them. The frozen path shape is declared here as PATTERNS (the
 * <id> placeholder is part of the declared path), because plugins are imported
 * at runtime and cannot be enumerated at compile time:
 *
 *   plugin.<id>.enabled      bool   default false; only enabled=true is emitted
 *   plugin.<id>.stage        str    one of the host stage tokens
 *   plugin.<id>.module_path  str    RELATIVE to <GHOSTLOCK_HOME>/countermeasures
 *   plugin.<id>.module_hash  str    64 lowercase hex digits
 *
 * The per-plugin parameter values (plugin.<id>.params.<key>) are typed by the
 * plugin descriptor itself: the probe TSV param/extract rows carry the same
 * uint|int|bool|str literals as GLKv3 WireKind, so the type has ONE authority
 * (the .so) and is validated against it at import/runtime instead of being
 * frozen into a static table. The extract.* contents are produced by the P2
 * extractor projection; P1 only validates their shape.
 *
 * Defaults live here (the FieldSpec default), not in HOCON: a plugin writes
 * only overrides, matching the R1 schema-authority rule. */

#include "profile/glkv3.hpp"
#include "profile/schema.hpp"

#include <cstdint>
#include <string_view>

namespace ghostlock::plugin {
    /* Typed view of the static plugin policy fields (one plugin instance). */
    struct PluginProfile final {
        bool enabled = false;
        std::string_view stage{};
        std::string_view module_path{};
        std::string_view module_hash{};
    };

    inline constexpr std::string_view kPluginSection = "plugin";

    using PluginField = profile::FieldSpec<PluginProfile>;

    struct Schema final {
        using View = PluginProfile;

        static constexpr PluginField kFields[] = {
            {kPluginSection, "<id>.enabled", 1, false, false,
             [](PluginProfile &view, std::uint64_t raw) {
                 view.enabled = raw != 0u;
             },
             profile::DefaultValue::literal(0), profile::FieldSource::Profile,
             profile::WireKind::Bool,
             "Plugin enabled; default false, and only enabled=true is emitted."},
            {kPluginSection, "<id>.stage", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Hook stage token (pre_spawn|post_spawn|pre_terminal|post_terminal).",
             [](PluginProfile &view, std::string_view text) {
                 view.stage = text;
             }},
            {kPluginSection, "<id>.module_path", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Module path RELATIVE to <GHOSTLOCK_HOME>/countermeasures.",
             [](PluginProfile &view, std::string_view text) {
                 view.module_path = text;
             }},
            {kPluginSection, "<id>.module_hash", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Lowercase hex SHA-256 of the module file.",
             [](PluginProfile &view, std::string_view text) {
                 view.module_hash = text;
             }},
            /* Dynamic wildcards. They carry no setter and never bind a concrete
             * key (the concrete keys are validated by plugin/wire.cpp against
             * the plugin descriptor); the entries exist so the manifest carries
             * the union type and the doc text of each dynamic path. */
            {kPluginSection, "<id>.params.*", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Dynamic: type comes from the plugin descriptor (probe TSV param rows)."},
            {kPluginSection, "<id>.extract.*", 0, false, false, nullptr,
             profile::DefaultValue::none(), profile::FieldSource::Profile,
             profile::WireKind::String,
             "Dynamic: P2 extractor writes it; P1 validates shape only."},
        };
    };

    /* GLKv3 path -> type mirror of Schema::kFields, generated into
     * profile-manifest-v3.tsv (the manifest is built from these lists). */
    inline constexpr profile::glkv3::FieldSpec kPluginGlkv3Fields[] = {
        {kPluginSection, "<id>.enabled", profile::glkv3::WireType::Bool, false},
        {kPluginSection, "<id>.stage", profile::glkv3::WireType::Str, false},
        {kPluginSection, "<id>.module_path", profile::glkv3::WireType::Str, false},
        {kPluginSection, "<id>.module_hash", profile::glkv3::WireType::Str, false},
        /* Dynamic wildcard rows: the manifest carries them, so both sides can
         * tell a declared dynamic prefix from an ordinary key. Their type is the
         * scalar union; the concrete type is fixed by the plugin descriptor. */
        {kPluginSection, "<id>.params.*", profile::glkv3::WireType::Union, false},
        {kPluginSection, "<id>.extract.*", profile::glkv3::WireType::Union, false},
    };

    /* The owner Schema mirrors the GLKv3 list one-for-one; the two wildcard
     * entries carry no setter and never bind a concrete key. */
    static_assert(std::size(Schema::kFields) == std::size(kPluginGlkv3Fields));
    /* ---- Dynamic keys and fail-closed validation (S4 P1) ---- */

    enum class DynamicKind : std::uint8_t { None = 0, Param, Extract };

    /* The ONE matcher for the declared wildcards: only plugin.<id>.params.<key>
     * and plugin.<id>.extract.<key> with a non-empty id and key are dynamic.
     * Callers must not invent implicit prefix rules. */
    [[nodiscard]] constexpr DynamicKind plugin_dynamic_key(
            std::string_view section, std::string_view key) noexcept {
        if (section != kPluginSection) return DynamicKind::None;
        const auto split = [key](std::string_view marker,
                                 DynamicKind kind) constexpr -> DynamicKind {
            const std::size_t at = key.rfind(marker);
            if (at == std::string_view::npos || at == 0u) return DynamicKind::None;
            if (key.size() <= at + marker.size()) return DynamicKind::None;
            return kind;
        };
        if (const DynamicKind param = split(".params.", DynamicKind::Param);
            param != DynamicKind::None) {
            return param;
        }
        return split(".extract.", DynamicKind::Extract);
    }

    /* The four static plugin keys, spelled as the document carries them. */
    [[nodiscard]] constexpr bool plugin_static_key(std::string_view key) noexcept {
        return key.ends_with(".enabled") || key.ends_with(".stage") ||
               key.ends_with(".module_path") || key.ends_with(".module_hash");
    }

    /* Host stage vocabulary (frozen 3.14.7.2); PRE_ROUTE is reserved. */
    [[nodiscard]] constexpr bool plugin_stage_token_valid(
            std::string_view token) noexcept {
        return token == "pre_spawn" || token == "post_spawn" ||
               token == "pre_terminal" || token == "post_terminal";
    }

    /* module_path is RELATIVE to <GHOSTLOCK_HOME>/countermeasures: no absolute
     * path, no parent traversal, no backslash, no NUL, bounded length. */
    [[nodiscard]] constexpr bool plugin_module_path_valid(
            std::string_view path) noexcept {
        if (path.empty() || path.size() > profile::glkv3::kMaxStringBytes) return false;
        if (path.front() == '/') return false;
        if (path.find("..") != std::string_view::npos) return false;
        if (path.find('\\') != std::string_view::npos) return false;
        return path.find('\0') == std::string_view::npos;
    }

    [[nodiscard]] constexpr bool plugin_module_hash_valid(
            std::string_view hash) noexcept {
        if (hash.size() != 64u) return false;
        for (const char ch : hash) {
            const bool digit = ch >= '0' && ch <= '9';
            const bool lower = ch >= 'a' && ch <= 'f';
            if (!digit && !lower) return false;
        }
        return true;
    }

} // namespace ghostlock::plugin

#endif
