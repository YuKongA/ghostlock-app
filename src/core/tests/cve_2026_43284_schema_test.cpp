/* Host test for the cve_2026_43284 owner Schema and its private wire section
 * (A2-3c / S3 B4).
 *
 * Covers: the declared (section, key) ownership is unique and all inside the
 * 43284 private section; a Production bind accepts every declared optional key
 * and preserves presence; Production rejects an unknown key/width; and the
 * binary transport accepts backend.cve_2026_43284 only when the header backend
 * id is 6, rejects it for every other backend, and still parses the 43499
 * section exactly as before. */

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "profile/binary.h"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using ghostlock::backend::Cve2026_43284Profile;
using ghostlock::backend::Cve2026_43284Schema;
using ghostlock::profile::BindCode;
using ghostlock::profile::DecodeMode;
using ghostlock::profile::Document;

namespace {
    void put_u16(std::string &out, uint16_t value) {
        out.push_back(static_cast<char>(value & 0xff));
        out.push_back(static_cast<char>((value >> 8) & 0xff));
    }

    void put_u32(std::string &out, uint32_t value) {
        for (int i = 0; i < 4; i++) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
        }
    }

    void put_u64(std::string &out, uint64_t value) {
        for (int i = 0; i < 8; i++) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
        }
    }

    struct Entry {
        std::string key;
        uint64_t value;
    };

    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };

    std::string build_doc(uint8_t route, const std::string &release,
                          const std::vector<Section> &sections,
                          uint16_t backend =
                                  ghostlock::binary_profile::kBackendCve202643284) {
        std::string out;
        put_u32(out, ghostlock::binary_profile::kMagic);
        put_u16(out, ghostlock::binary_profile::kVersion);
        put_u16(out, ghostlock::binary_profile::kTerminalRootChild);
        put_u16(out, backend);
        put_u16(out, route);
        put_u16(out, static_cast<uint16_t>(release.size()));
        put_u16(out, 0);
        out += release;
        put_u16(out, static_cast<uint16_t>(sections.size()));
        for (const Section &section : sections) {
            out.push_back(static_cast<char>(section.name.size()));
            out += section.name;
            put_u32(out, static_cast<uint32_t>(section.entries.size()));
            for (const Entry &entry : section.entries) {
                out.push_back(static_cast<char>(entry.key.size()));
                out += entry.key;
                put_u64(out, entry.value);
            }
        }
        return out;
    }

    void add(Document &doc, std::string_view section, std::string_view key,
             uint64_t raw) {
        ghostlock::profile::Section *found = doc.find_section(section);
        if (!found) found = &doc.append_section(section);
        found->add(key, raw);
    }

    Document full_document() {
        Document doc;
        doc.release = "6.6.77-43284-schema";
        doc.backend = 6;
        doc.middleware = ghostlock::profile::kRouteAuto;
        for (const auto &field : Cve2026_43284Schema::kFields) {
            add(doc, field.section, field.key, 1ULL);
        }
        return doc;
    }

    int32_t parse_43284(const std::string &doc, Cve2026_43284Profile *out,
                        char *release, size_t release_cap) {
        ghostlock::profile::kernel_offsets values = {};
        return ghostlock::binary_profile::parse(
                std::string_view(doc.data(), doc.size()), &values, release,
                release_cap, nullptr, nullptr, out);
    }
} // namespace

int main() {
    constexpr size_t kFieldCount = std::size(Cve2026_43284Schema::kFields);

    /* ---- Ownership: unique (section, key), all in the 43284 section. ---- */
    for (size_t i = 0; i < kFieldCount; i++) {
        assert(Cve2026_43284Schema::kFields[i].section ==
               ghostlock::backend::kCve2026_43284Section);
        for (size_t j = i + 1; j < kFieldCount; j++) {
            const bool same =
                    Cve2026_43284Schema::kFields[i].section ==
                            Cve2026_43284Schema::kFields[j].section &&
                    Cve2026_43284Schema::kFields[i].key ==
                            Cve2026_43284Schema::kFields[j].key;
            assert(!same);
        }
    }
    /* Every field is optional: presence is the only requiredness. */
    for (size_t i = 0; i < kFieldCount; i++) {
        assert(Cve2026_43284Schema::kFields[i].required == false);
    }

    /* ---- Production accepts every declared key and preserves presence. ---- */
    {
        Document doc = full_document();
        Cve2026_43284Profile view{};
        const auto status = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(status.ok());
        assert(view.carrier_path == 1);
        assert(view.lkm_path == 1);
        assert(view.kmi == 1);
        assert(view.selinux_exec_context == 1);
        assert(view.late_load_args == 1);
        assert(view.defex_symbol == 1);
        assert(view.steps == 1);

        /* An empty document binds to all-absent (no required field). */
        Document empty;
        Cve2026_43284Profile none{};
        assert(ghostlock::profile::bind<Cve2026_43284Schema>(
                       empty, none, DecodeMode::Production)
                       .ok());
        assert(!none.carrier_path.has_value());
        assert(!none.steps.has_value());
    }

    /* ---- Production rejects an unknown key in the owned section. ---- */
    {
        Document doc = full_document();
        doc.find_section(ghostlock::backend::kCve2026_43284Section)
                ->add("bogus", 1ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownKey);
        assert(blocked.section == ghostlock::backend::kCve2026_43284Section);
        assert(blocked.key == "bogus");
    }

    /* ---- Production rejects a section this owner does not declare. ---- */
    {
        Document doc = full_document();
        doc.append_section("not_a_section").add("x", 1ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownSection);
    }

    /* ---- Width mismatch fails closed. ---- */
    {
        Document doc;
        doc.append_section(ghostlock::backend::kCve2026_43284Section)
                .add("kmi", 0x10000ULL);
        Cve2026_43284Profile view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43284Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::WidthMismatch);
    }

    /* ---- Transport: backend id 6 accepts the private section. ---- */
    {
        const std::string doc = build_doc(
                ghostlock::profile::kRouteSelectStack, "b4-43284",
                {{std::string(ghostlock::backend::kCve2026_43284Section),
                  {{"carrier_path", 0x11},
                   {"lkm_path", 0x22},
                   {"kmi", 515},
                   {"selinux_exec_context", 0x33},
                   {"late_load_args", 0x44},
                   {"defex_symbol", 0x55},
                   {"steps", 3}}}});
        char release[64] = {0};
        Cve2026_43284Profile profile{};
        assert(parse_43284(doc, &profile, release, sizeof(release)) == 0);
        assert(std::string_view(release) == "b4-43284");
        assert(profile.carrier_path == 0x11);
        assert(profile.lkm_path == 0x22);
        assert(profile.kmi == 515);
        assert(profile.selinux_exec_context == 0x33);
        assert(profile.late_load_args == 0x44);
        assert(profile.defex_symbol == 0x55);
        assert(profile.steps == 3);
    }

    /* ---- Transport: a route-less 43284 document is legal. ---- */
    {
        const std::string doc = build_doc(
                ghostlock::profile::kRouteAuto, "b4-route-less",
                {{std::string(ghostlock::backend::kCve2026_43284Section),
                  {{"kmi", 610}}}});
        char release[64] = {0};
        Cve2026_43284Profile profile{};
        assert(parse_43284(doc, &profile, release, sizeof(release)) == 0);
        assert(profile.kmi == 610);
    }

    /* ---- Transport: every other backend still rejects the section. ---- */
    {
        const std::string doc = build_doc(
                ghostlock::profile::kRouteSelectStack, "b4-43499",
                {{std::string(ghostlock::backend::kCve2026_43284Section),
                  {{"kmi", 515}}}},
                ghostlock::binary_profile::kBackendCve202643499);
        char release[64] = {0};
        Cve2026_43284Profile profile{};
        assert(parse_43284(doc, &profile, release, sizeof(release)) == -1);
    }

    /* ---- Transport: an unknown key in the private section is rejected. ---- */
    {
        const std::string doc = build_doc(
                ghostlock::profile::kRouteSelectStack, "b4-bad-key",
                {{std::string(ghostlock::backend::kCve2026_43284Section),
                  {{"bogus", 1}}}});
        char release[64] = {0};
        Cve2026_43284Profile profile{};
        assert(parse_43284(doc, &profile, release, sizeof(release)) == -1);
    }

    /* ---- Transport: 43499 steps still decode unchanged. ---- */
    {
        const std::string doc = build_doc(
                ghostlock::profile::kRouteSelectStack, "b4-43499-steps",
                {{"backend.cve_2026_43499", {{"steps", 2}}}},
                ghostlock::binary_profile::kBackendCve202643499);
        char release[64] = {0};
        ghostlock::profile::kernel_offsets parsed = {};
        ghostlock::binary_profile::component_ids ids{};
        Cve2026_43284Profile profile{};
        assert(ghostlock::binary_profile::parse(
                       std::string_view(doc.data(), doc.size()), &parsed, release,
                       sizeof(release), &ids, nullptr, &profile) == 0);
        assert(ids.steps == 2);
        assert(!profile.steps.has_value());
    }

    std::printf("cve_2026_43284_schema_test: ok (%zu fields)\n", kFieldCount);
    return 0;
}
