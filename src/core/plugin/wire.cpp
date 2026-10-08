/* S4 P1 plugin wire validation implementation. See wire.hpp. */

#include "plugin/wire.hpp"

#include <cstddef>
#include <cstring>
#include <string_view>

namespace ghostlock::plugin {
    namespace {
        struct FieldSplit final {
            std::string_view id{};
            std::string_view suffix{}; /* ".enabled" / ".params." / ... */
            std::string_view rest{};   /* text after the suffix */
        };

        /* Splits "<id><suffix><rest>" on the LAST occurrence of suffix; the id
         * must be non-empty (plugin ids may themselves contain dots). */
        [[nodiscard]] bool split_on(std::string_view key, std::string_view suffix,
                                    FieldSplit &out) noexcept {
            const std::size_t at = key.rfind(suffix);
            if (at == std::string_view::npos || at == 0u) return false;
            out.id = key.substr(0u, at);
            out.suffix = suffix;
            out.rest = key.substr(at + suffix.size());
            return true;
        }

        [[nodiscard]] bool split_static(std::string_view key,
                                        FieldSplit &out) noexcept {
            for (const std::string_view suffix : {std::string_view(".enabled"),
                                                  std::string_view(".stage"),
                                                  std::string_view(".module_path"),
                                                  std::string_view(".module_hash")}) {
                if (split_on(key, suffix, out)) return true;
            }
            return false;
        }

        struct Group final {
            std::string_view id{};
            PluginWireEntry entry{};
            bool enabled = false;
            bool seen_enabled = false;
            bool seen_stage = false;
            bool seen_path = false;
            bool seen_hash = false;
        };

        PluginWireResult fail(PluginWireError error, std::string_view id,
                              std::size_t accepted) noexcept {
            PluginWireResult result{};
            result.error = error;
            result.id = id;
            result.accepted = accepted;
            return result;
        }

        [[nodiscard]] bool scalar_value(const profile::Value &value) noexcept {
            /* uint | int | bool are numeric (the GLKv3 decoder rejects bin/array
             * for declared fields, and the dynamic prefixes only ever carry
             * scalars); str is text. */
            return value.is_text || value.present;
        }
    } // namespace

    const char *plugin_wire_error_name(PluginWireError error) noexcept {
        switch (error) {
            case PluginWireError::None: return "None";
            case PluginWireError::UnknownField: return "UnknownField";
            case PluginWireError::EnabledMissing: return "EnabledMissing";
            case PluginWireError::EnabledNotBool: return "EnabledNotBool";
            case PluginWireError::DisabledPresent: return "DisabledPresent";
            case PluginWireError::StageMissing: return "StageMissing";
            case PluginWireError::StageUnknown: return "StageUnknown";
            case PluginWireError::ModulePathRejected: return "ModulePathRejected";
            case PluginWireError::ModuleHashRejected: return "ModuleHashRejected";
            case PluginWireError::ParamKeyRejected: return "ParamKeyRejected";
            case PluginWireError::ParamTypeRejected: return "ParamTypeRejected";
            case PluginWireError::TooManyPlugins: return "TooManyPlugins";
        }
        return "Unknown";
    }

    PluginWireResult validate_plugin_wire(const profile::Document &document,
                                          PluginWireEntry *out,
                                          std::size_t capacity) noexcept {
        const profile::Section *section = document.find_section(kPluginSection);
        if (section == nullptr) {
            return PluginWireResult{};
        }
        /* Pass 1: discover the distinct plugin ids and enforce the static
         * shape. Two passes keep the grouping allocation-free. */
        Group groups[kMaxPluginsPerDocument]{};
        std::size_t group_count = 0u;
        for (const profile::Entry &entry : section->entries) {
            const std::string_view key = entry.key;
            const DynamicKind dynamic = plugin_dynamic_key(kPluginSection, key);
            const std::string_view dynamic_marker =
                    dynamic == DynamicKind::Param
                            ? std::string_view(".params.")
                            : std::string_view(".extract.");
            FieldSplit split{};
            std::string_view id{};
            if (dynamic != DynamicKind::None) {
                id = key.substr(0u, key.find(dynamic_marker));
            } else if (split_static(key, split)) {
                id = split.id;
            } else {
                return fail(PluginWireError::UnknownField, {}, 0u);
            }
            if (id.empty()) {
                return fail(PluginWireError::UnknownField, {}, 0u);
            }
            std::size_t index = group_count;
            for (std::size_t i = 0u; i < group_count; ++i) {
                if (groups[i].id == id) {
                    index = i;
                    break;
                }
            }
            if (index == group_count) {
                if (group_count >= kMaxPluginsPerDocument) {
                    return fail(PluginWireError::TooManyPlugins, id, 0u);
                }
                groups[group_count].id = id;
                groups[group_count].entry.id = id;
                ++group_count;
            }
            Group &group = groups[index];
            if (dynamic == DynamicKind::Param || dynamic == DynamicKind::Extract) {
                /* <id>.params.<key> / <id>.extract.<key> */
                const std::string_view name =
                        key.substr(key.find(dynamic_marker) + dynamic_marker.size());
                if (name.empty()) {
                    return fail(PluginWireError::ParamKeyRejected, id, 0u);
                }
                if (!scalar_value(entry.value)) {
                    return fail(PluginWireError::ParamTypeRejected, id, 0u);
                }
                if (dynamic == DynamicKind::Param) {
                    ++group.entry.param_count;
                } else {
                    ++group.entry.extract_count;
                }
                continue;
            }
            if (split.suffix == ".enabled") {
                /* The NEUTRAL Document does not preserve the GLKv3 bool/uint
                 * distinction: a decoded bool arrives as a present numeric with
                 * the default wire width (profile/document.hpp Section::add), so
                 * the rule is "non-text scalar whose value is 0 or 1". A text
                 * value or any other number is still rejected fail-closed. */
                if (entry.value.is_text || !entry.value.present ||
                    entry.value.raw > 1u) {
                    return fail(PluginWireError::EnabledNotBool, id, 0u);
                }
                group.seen_enabled = true;
                group.enabled = entry.value.raw != 0u;
            } else if (split.suffix == ".stage") {
                if (!entry.value.is_text) {
                    return fail(PluginWireError::StageUnknown, id, 0u);
                }
                group.seen_stage = true;
                group.entry.stage = entry.value.text;
            } else if (split.suffix == ".module_path") {
                if (!entry.value.is_text) {
                    return fail(PluginWireError::ModulePathRejected, id, 0u);
                }
                group.seen_path = true;
                group.entry.module_path = entry.value.text;
            } else {
                if (!entry.value.is_text) {
                    return fail(PluginWireError::ModuleHashRejected, id, 0u);
                }
                group.seen_hash = true;
                group.entry.module_hash = entry.value.text;
            }
        }

        if (group_count > capacity) {
            return fail(PluginWireError::TooManyPlugins, {}, 0u);
        }
        /* Pass 2: per-plugin semantic rules. */
        for (std::size_t i = 0u; i < group_count; ++i) {
            const Group &group = groups[i];
            if (!group.seen_enabled) {
                return fail(PluginWireError::EnabledMissing, group.id, 0u);
            }
            if (!group.enabled) {
                return fail(PluginWireError::DisabledPresent, group.id, 0u);
            }
            if (!group.seen_stage) {
                return fail(PluginWireError::StageMissing, group.id, 0u);
            }
            if (!plugin_stage_token_valid(group.entry.stage)) {
                return fail(PluginWireError::StageUnknown, group.id, 0u);
            }
            if (!plugin_module_path_valid(group.entry.module_path)) {
                return fail(PluginWireError::ModulePathRejected, group.id, 0u);
            }
            if (!plugin_module_hash_valid(group.entry.module_hash)) {
                return fail(PluginWireError::ModuleHashRejected, group.id, 0u);
            }
            out[i] = group.entry;
        }
        PluginWireResult result{};
        result.accepted = group_count;
        return result;
    }

    bool plugin_descriptor_declares(const glk_module &module, DynamicKind kind,
                                    std::string_view name,
                                    PluginValueKind *type_out) noexcept {
        if (kind == DynamicKind::None || name.empty()) return false;
        if (module.size < GLK_MODULE_SIZE_V2) return false;
        const glk_param *table = kind == DynamicKind::Param ? module.params
                                                            : module.extract;
        const std::uint32_t count = kind == DynamicKind::Param ? module.param_count
                                                                : module.extract_count;
        if (table == nullptr) return false;
        for (std::uint32_t i = 0u; i < count; ++i) {
            if (table[i].name == nullptr) continue;
            if (name != std::string_view(table[i].name)) continue;
            if (table[i].type > GLK_PARAM_STR) return false;
            if (type_out != nullptr) {
                *type_out = static_cast<PluginValueKind>(table[i].type);
            }
            return true;
        }
        return false;
    }

    const char *plugin_value_kind_name(PluginValueKind kind) noexcept {
        switch (kind) {
            case PluginValueKind::UInt: return "uint";
            case PluginValueKind::Int: return "int";
            case PluginValueKind::Bool: return "bool";
            case PluginValueKind::Str: return "str";
        }
        return "unknown";
    }
} // namespace ghostlock::plugin
