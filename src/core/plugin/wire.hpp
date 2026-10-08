#ifndef GHOSTLOCK_PLUGIN_WIRE_HPP
#define GHOSTLOCK_PLUGIN_WIRE_HPP

/* S4 P1 plugin wire validation (contract-design 3.14.7.5 / 3.14.7.7).
 *
 * The document `plugin` section carries per-plugin entries:
 *   <id>.enabled  <id>.stage  <id>.module_path  <id>.module_hash
 *   <id>.params.<key>  <id>.extract.<key>
 *
 * This unit is the ONE authority for the static shape. The wildcard prefixes
 * exist only because plugin/schema.hpp declares them; nothing else may accept
 * them implicitly. Every rule is fail-closed. The descriptor-driven rules (is
 * this key declared by the plugin? does the value type match its ParamSpec? is
 * a required param present?) belong to instantiation, where the loaded module
 * exposes its ParamSpec table; plugin_descriptor_declares() is that entry.
 *
 * Default-off: a plugin that is not explicitly enabled must not appear in the
 * document at all, so a plugin section with absent/false enabled is rejected. */

#include "contract/abi/glk_contract_abi.h"
#include "plugin/schema.hpp"
#include "profile/document.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::plugin {
    inline constexpr std::size_t kMaxPluginsPerDocument = 16u;

    enum class PluginWireError : std::uint8_t {
        None = 0,
        UnknownField,      /* a key outside the frozen plugin paths */
        EnabledMissing,    /* section present without enabled=true */
        EnabledNotBool,    /* enabled is not a 0/1 numeric scalar */
        DisabledPresent,   /* enabled=false present in the document */
        StageMissing,
        StageUnknown,
        ModulePathRejected,
        ModuleHashRejected,
        ParamKeyRejected,  /* empty <key> */
        ParamTypeRejected, /* value is not one of uint|int|bool|str */
        TooManyPlugins,
    };

    struct PluginWireEntry final {
        std::string_view id{};
        std::string_view stage{};
        std::string_view module_path{};
        std::string_view module_hash{};
        std::size_t param_count = 0u;
        std::size_t extract_count = 0u;
    };

    struct PluginWireResult final {
        PluginWireError error = PluginWireError::None;
        std::string_view id{};
        std::size_t accepted = 0u;
    };

    [[nodiscard]] const char *plugin_wire_error_name(PluginWireError error) noexcept;

    /* Walks the document `plugin` section; on success out[0..accepted) holds one
     * validated entry per enabled plugin. capacity 0 with a non-empty section is
     * a TooManyPlugins error, never a silent drop. */
    [[nodiscard]] PluginWireResult validate_plugin_wire(
            const profile::Document &document, PluginWireEntry *out,
            std::size_t capacity) noexcept;

    /* ---- descriptor-driven (instantiation gate) ---- */

    enum class PluginValueKind : std::uint8_t { UInt = 0, Int, Bool, Str };

    /* True when the loaded module declares name under the matching table, and
     * reports the declared type. Size-gated: a v1 module has no tail and
     * therefore declares nothing. */
    [[nodiscard]] bool plugin_descriptor_declares(const glk_module &module,
                                                  DynamicKind kind,
                                                  std::string_view name,
                                                  PluginValueKind *type_out) noexcept;

    [[nodiscard]] const char *plugin_value_kind_name(PluginValueKind kind) noexcept;
} // namespace ghostlock::plugin

#endif
