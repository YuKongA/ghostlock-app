/* S4 P1 plugin wire tests: the dynamic-key declaration, the fail-closed
 * document validation and the descriptor-driven check (contract-design
 * 3.14.7.5 / 3.14.7.7). */

#include "plugin/schema.hpp"
#include "plugin/wire.hpp"
#include "profile/glkv3.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using ghostlock::plugin::DynamicKind;
    using ghostlock::plugin::PluginValueKind;
    using ghostlock::plugin::PluginWireEntry;
    using ghostlock::plugin::PluginWireError;
    using ghostlock::profile::Document;
    using ghostlock::profile::Entry;
    using ghostlock::profile::Section;
    using ghostlock::profile::Value;

    Value text_value(std::string_view text) {
        Value value{};
        value.is_text = true;
        value.text = text;
        value.present = true;
        return value;
    }

    Value uint_value(std::uint64_t raw, std::uint8_t width = 8u) {
        Value value{};
        value.raw = raw;
        value.width = width;
        value.present = true;
        return value;
    }

    Document make_doc(std::vector<std::pair<std::string, Value>> entries) {
        Document document{};
        Section section{};
        section.name = "plugin";
        for (auto &item : entries) {
            Entry entry{};
            entry.key = std::move(item.first);
            entry.value = item.second;
            section.entries.push_back(std::move(entry));
        }
        document.sections.push_back(std::move(section));
        return document;
    }

    PluginWireError validate(const Document &document, std::size_t *accepted = nullptr) {
        PluginWireEntry entries[ghostlock::plugin::kMaxPluginsPerDocument]{};
        const auto result = ghostlock::plugin::validate_plugin_wire(
                document, entries, ghostlock::plugin::kMaxPluginsPerDocument);
        if (accepted != nullptr) *accepted = result.accepted;
        return result.error;
    }

    const std::string kHash(64u, 'a');
} // namespace

int main() {
    using ghostlock::plugin::kPluginGlkv3Fields;
    using ghostlock::plugin::plugin_dynamic_key;
    using ghostlock::plugin::plugin_descriptor_declares;
    using ghostlock::plugin::plugin_module_hash_valid;
    using ghostlock::plugin::plugin_module_path_valid;
    using ghostlock::plugin::plugin_stage_token_valid;
    using ghostlock::profile::glkv3::kUnionScalarTypes;
    using ghostlock::profile::glkv3::wire_type_name;
    using ghostlock::profile::glkv3::WireType;

    /* ---- the union is EXACTLY {uint,int,bool,str} ---- */
    {
        const std::string expected = "uint|int|bool|str";
        assert(wire_type_name(WireType::Union) == expected);
        assert(std::size(kUnionScalarTypes) == 4u);
        std::string joined;
        for (const WireType member : kUnionScalarTypes) {
            if (!joined.empty()) joined += '|';
            joined += wire_type_name(member);
        }
        assert(joined == expected);
        assert(ghostlock::profile::glkv3::wire_type_is_union_member(WireType::UInt));
        assert(!ghostlock::profile::glkv3::wire_type_is_union_member(WireType::Bin));
    }

    /* ---- the two wildcard rows are declared, the static four are typed ---- */
    {
        std::size_t unions = 0u;
        for (const auto &field : kPluginGlkv3Fields) {
            if (field.type == WireType::Union) {
                ++unions;
                assert(field.key == "<id>.params.*" || field.key == "<id>.extract.*");
            }
        }
        assert(unions == 2u);
        assert(std::size(kPluginGlkv3Fields) == 6u);
    }

    /* ---- wildcard matcher: explicit, no implicit prefixes ---- */
    {
        assert(plugin_dynamic_key("plugin", "vivo.params.arm_delay_us") == DynamicKind::Param);
        assert(plugin_dynamic_key("plugin", "samsung.extract.symbol") == DynamicKind::Extract);
        assert(plugin_dynamic_key("plugin", "a.b.params.x") == DynamicKind::Param);
        assert(plugin_dynamic_key("plugin", "vivo.enabled") == DynamicKind::None);
        assert(plugin_dynamic_key("plugin", "vivo.params.") == DynamicKind::None);
        assert(plugin_dynamic_key("plugin", ".params.x") == DynamicKind::None);
        assert(plugin_dynamic_key("backend.cve_2026_43499", "vivo.params.x") == DynamicKind::None);
    }

    /* ---- value rules ---- */
    {
        assert(plugin_stage_token_valid("post_terminal"));
        assert(!plugin_stage_token_valid("pre_route"));
        assert(!plugin_stage_token_valid("post_setup"));
        assert(plugin_module_path_valid("vivo_vr_guard/1.0.0/vivo.so"));
        assert(!plugin_module_path_valid("/abs/vivo.so"));
        assert(!plugin_module_path_valid("../vivo.so"));
        assert(!plugin_module_path_valid(""));
        assert(plugin_module_hash_valid(kHash));
        assert(!plugin_module_hash_valid("abc"));
        assert(!plugin_module_hash_valid(std::string(64u, 'A')));
    }

    /* ---- document validation ---- */
    {
        const Document none{};
        assert(validate(none) == PluginWireError::None);
    }
    {
        std::size_t accepted = 0u;
        const Document good = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                        {"vivo.stage", text_value("post_terminal")},
                                        {"vivo.module_path", text_value("vivo/1.0.0/v.so")},
                                        {"vivo.module_hash", text_value(kHash)},
                                        {"vivo.params.arm_delay_us", uint_value(200u, 8u)},
                                        {"vivo.params.mode", text_value("auto")},
                                        {"vivo.extract.symbol", text_value("task_defex")}});
        assert(validate(good, &accepted) == PluginWireError::None);
        assert(accepted == 1u);
    }
    {
        const Document missing = make_doc({{"vivo.stage", text_value("post_terminal")}});
        assert(validate(missing) == PluginWireError::EnabledMissing);
    }
    {
        const Document disabled = make_doc({{"vivo.enabled", uint_value(0u, 1u)}});
        assert(validate(disabled) == PluginWireError::DisabledPresent);
    }
    {
        const Document text_enabled = make_doc({{"vivo.enabled", text_value("yes")}});
        assert(validate(text_enabled) == PluginWireError::EnabledNotBool);
    }
    {
        const Document bad_stage = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                             {"vivo.stage", text_value("pre_route")}});
        assert(validate(bad_stage) == PluginWireError::StageUnknown);
    }
    {
        const Document no_stage = make_doc({{"vivo.enabled", uint_value(1u, 1u)}});
        assert(validate(no_stage) == PluginWireError::StageMissing);
    }
    {
        const Document bad_path = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                            {"vivo.stage", text_value("pre_spawn")},
                                            {"vivo.module_path", text_value("/etc/x.so")}});
        assert(validate(bad_path) == PluginWireError::ModulePathRejected);
    }
    {
        const Document bad_hash = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                            {"vivo.stage", text_value("pre_spawn")},
                                            {"vivo.module_path", text_value("a/b.so")},
                                            {"vivo.module_hash", text_value("short")}});
        assert(validate(bad_hash) == PluginWireError::ModuleHashRejected);
    }
    {
        const Document unknown = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                           {"vivo.nope", uint_value(1u, 8u)}});
        assert(validate(unknown) == PluginWireError::UnknownField);
    }
    {
        /* An empty dynamic key is not a wildcard match at all (the matcher is
         * the single authority for the key shape), so it fails as an unknown
         * field rather than as an empty parameter name. */
        const Document empty_key = make_doc({{"vivo.enabled", uint_value(1u, 1u)},
                                             {"vivo.params.", uint_value(1u, 8u)}});
        assert(validate(empty_key) == PluginWireError::UnknownField);
    }

    /* ---- descriptor-driven declarations (instantiation gate) ---- */
    {
        const glk_param params[] = {{"threshold", GLK_PARAM_UINT, 1u, 200u, nullptr, "d"}};
        const glk_param extract[] = {{"symbol", GLK_PARAM_STR, 0u, 0u, "auto", nullptr}};
        glk_module module{};
        module.abi_version = GLK_ABI_VERSION;
        module.size = GLK_MODULE_SIZE_V2;
        module.name = "test.schema";
        module.version = "1";
        module.param_count = 1u;
        module.params = params;
        module.extract_count = 1u;
        module.extract = extract;
        PluginValueKind kind = PluginValueKind::Str;
        assert(plugin_descriptor_declares(module, DynamicKind::Param, "threshold", &kind));
        assert(kind == PluginValueKind::UInt);
        assert(plugin_descriptor_declares(module, DynamicKind::Extract, "symbol", &kind));
        assert(kind == PluginValueKind::Str);
        assert(!plugin_descriptor_declares(module, DynamicKind::Param, "nope", &kind));
        assert(!plugin_descriptor_declares(module, DynamicKind::Param, "symbol", &kind));
        module.size = 40u; /* v1 size: the tail must be ignored */
        assert(!plugin_descriptor_declares(module, DynamicKind::Param, "threshold", &kind));
    }

    std::puts("plugin_wire_test: ok");
    return 0;
}
