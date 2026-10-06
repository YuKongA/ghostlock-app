/* Host test for the platform::abi owner schema/view (ADR-0003 / A2-4-3).
 *
 * Covers: the platform Schema owns exactly the 31 platform keys with the
 * expected widths and no duplicate (section, key); a Production bind accepts
 * every declared key; Production rejects an unknown section/key; and the
 * mechanical platform-View -> kernel_offsets merge copies every field. The
 * runtime offset accessors are the neutral profile surface (covered by the
 * attack data-flow harness), not this header. */

#include "platform/abi.hpp"

#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

using ghostlock::platform::abi::Schema;
using ghostlock::platform::abi::View;
using ghostlock::profile::BindCode;
using ghostlock::profile::DecodeMode;
using ghostlock::profile::Document;
using ghostlock::profile::Section;

namespace {
    void add(Document &doc, std::string_view section, std::string_view key,
             uint64_t raw) {
        Section *found = doc.find_section(section);
        if (!found) found = &doc.append_section(section);
        found->add(key, raw);
    }
} // namespace

int main() {
    constexpr size_t kCount = std::size(Schema::kFields);
    assert(kCount == 31);
    for (size_t i = 0; i < kCount; i++) {
        const auto &field = Schema::kFields[i];
        assert(!field.required);
        assert(field.width == 4 || field.width == 8);
        for (size_t j = i + 1; j < kCount; j++) {
            const auto &other = Schema::kFields[j];
            assert(!(field.section == other.section && field.key == other.key));
        }
    }

    Document doc;
    doc.release = "6.6.77-platform-abi";
    for (const auto &field : Schema::kFields) add(doc, field.section, field.key, 0x1234u);
    View view{};
    const auto status =
            ghostlock::profile::bind<Schema>(doc, view, DecodeMode::Production);
    assert(status.ok());
    assert(view.task.prio == 0x1234u);
    assert(view.cred.usage_offset == 0x1234u);
    assert(view.offset.init_task == 0x1234u);
    assert(view.kernel.kernel_phys_load.value_or(0) == 0x1234u);

    ghostlock::profile::kernel_offsets out{};
    ghostlock::platform::abi::apply_to(view, out);
    assert(out.task.prio == 0x1234u);
    assert(out.task.seccomp == 0x1234u);
    assert(out.credential.usage_offset == 0x1234u);
    assert(out.credential.caps_offset == 0x1234u);
    assert(out.credential.ref_count == 0x1234u);
    assert(out.credential.ref0_offset == 0x1234u);
    assert(out.credential.ref3_offset == 0x1234u);
    assert(out.offsets.init_task == 0x1234u);
    assert(out.offsets.security_hook_heads == 0x1234u);
    assert(out.misc.kernel_phys_load.value_or(0) == 0x1234u);
    assert(out.misc.kernel_phys_offset.value_or(0) == 0x1234u);

    /* Exhaustive merge coverage: binding ONE declared platform field at a time
     * must move the frozen transport. A field added to both declaration tables
     * but forgotten in platform::abi::apply_to would otherwise leave its
     * transport member silently 0. The loop is generated from Schema::kFields,
     * so it cannot go stale; kCount == 31 above is the tripwire that forces
     * this header to be reviewed whenever a field is added. */
    static_assert(std::is_trivially_copyable_v<ghostlock::profile::kernel_offsets>);
    for (const auto &field : Schema::kFields) {
        Document single_doc;
        single_doc.release = "6.6.77-platform-abi-one";
        add(single_doc, field.section, field.key, 0x1234u);
        View single_view{};
        assert(ghostlock::profile::bind<Schema>(single_doc, single_view,
                                               DecodeMode::Production)
                       .ok());
        ghostlock::profile::kernel_offsets moved{};
        ghostlock::profile::kernel_offsets untouched{};
        /* Byte-zero both sides first: value-initialisation leaves the padding
         * bytes unspecified, and a comparison that read them would always report
         * a difference -- a vacuous guard. kernel_offsets is trivially copyable
         * (static_assert above) and all-zero is a valid empty value. */
        std::memset(&moved, 0, sizeof(moved));
        std::memset(&untouched, 0, sizeof(untouched));
        ghostlock::platform::abi::apply_to(single_view, moved);
        /* Any byte difference now means the bound field reached the merge. */
        assert(std::memcmp(&moved, &untouched, sizeof(moved)) != 0);
    }

    /* An absent optional stays absent after the merge. */
    {
        View partial{};
        ghostlock::profile::kernel_offsets merged{};
        ghostlock::platform::abi::apply_to(partial, merged);
        assert(!merged.misc.kernel_phys_load.has_value());
        assert(!merged.misc.kernel_phys_offset.has_value());
    }

    /* Production rejects an unknown section. */
    {
        Document bad = doc;
        bad.append_section("not_a_section").add("x", 1ULL);
        View rejected{};
        const auto blocked =
                ghostlock::profile::bind<Schema>(bad, rejected, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownSection);
    }

    std::printf("platform_abi_test: ok (%zu fields)\n", kCount);
    return 0;
}
