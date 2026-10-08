#ifndef GHOSTLOCK_PROFILE_REGISTRY_H
#define GHOSTLOCK_PROFILE_REGISTRY_H

/* S4 R1 schema registry: selection -> owner schemas + default-aware bind.
 *
 * SchemaRegistry<Schemas...> is a compile-time owner set (platform::abi plus
 * backend::<id> for the current triples). bind_all(registry, document, sink,
 * mode) performs one fail-closed pass: it validates the (section, key) union,
 * materialises every owner View, applies the FieldSpec defaults and reports
 * each triggered default through the sink's no-allocation BindLog.
 *
 * The selection -> owner mapping is RegistryForSelection, a customization point
 * specialised by each backend owner: the R1 include firewall forbids profile/
 * from naming backend/platform types, so the concrete specialisations live in
 * backend/cve_2026_X/schema.hpp next to the schema they compose. */

#include "contract/identity.hpp"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cstddef>
#include <tuple>
#include <utility>

namespace ghostlock::profile {
    /* A compile-time set of owner schemas. */
    template<SchemaDefinition... Schemas>
    struct SchemaRegistry final {
        static constexpr std::size_t count = sizeof...(Schemas);
    };

    template<SchemaDefinition... Schemas>
    [[nodiscard]] constexpr SchemaRegistry<Schemas...> make_registry() noexcept {
        return {};
    }

    /* A sink groups the caller-owned Views (same order as the registry) and the
     * optional default diagnostic channel. Views are stored by reference; the
     * sink is passed by value. */
    template<typename... Views>
    struct BindSink final {
        std::tuple<Views &...> views;
        BindLog log{};
    };

    template<typename... Views>
    [[nodiscard]] constexpr BindSink<Views...> make_sink(Views &...views) noexcept {
        return BindSink<Views...>{std::tuple<Views &...>(views...), BindLog{}};
    }

    /* Selection-driven registry composition. The primary template is undefined;
     * each backend owner provides the specialisations for the (backend, steps,
     * terminal) triples it serves. */
    template<contract::ComponentSelection Selection>
    struct RegistryForSelection;

    template<contract::ComponentSelection Selection>
    using RegistryForSelectionT = typename RegistryForSelection<Selection>::type;

    /* R1 bind: validate the union (strict in both modes), then materialise every
     * owner View applying declared defaults and reporting each through the sink's
     * BindLog. Test relaxes required only; unknown section/key still reject. A
     * rejected bind leaves every View untouched. */
    template<SchemaDefinition... Schemas, typename... Views>
    [[nodiscard]] BindStatus bind_all(const SchemaRegistry<Schemas...> &,
                                      const Document &document,
                                      BindSink<Views...> sink, BindMode mode) {
        static_assert(sizeof...(Schemas) == sizeof...(Views),
                      "SchemaRegistry and BindSink must list the same owners");
        for (const Section &section : document.sections) {
            const bool section_known =
                    (schema_detail::declares_section<Schemas>(section.name) || ...);
            if (!section_known) {
                return BindStatus{BindCode::UnknownSection, section.name, {}};
            }
            for (const Entry &entry : section.entries) {
                const bool key_known =
                        (schema_detail::declares_key<Schemas>(section.name, entry.key) ||
                         ...);
                if (!key_known) {
                    return BindStatus{BindCode::UnknownKey, section.name, entry.key};
                }
            }
        }
        BindStatus status{};
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            const auto bind_one = [&]<std::size_t Index>() {
                if (!status.ok()) return;
                using Schema =
                        std::tuple_element_t<Index, std::tuple<Schemas...>>;
                status = schema_detail::materialize_with_defaults<Schema>(
                        document, std::get<Index>(sink.views), mode, &sink.log);
            };
            (bind_one.template operator()<I>(), ...);
        }(std::index_sequence_for<Schemas...>{});
        return status;
    }
} // namespace ghostlock::profile

#endif
