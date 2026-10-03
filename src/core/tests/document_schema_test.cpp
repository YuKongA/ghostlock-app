/* Host test for the neutral profile Document and the owner Schema binding
 * framework (ADR-0003 / A2-3c-1).
 *
 * Covers: the Document framing read API and presence by key occurrence; the
 * shadow Document built inside binary_profile::parse_v2; strict (production)
 * rejection of unknown section/key; tooling tolerance; and an example Schema
 * bind with required presence, destination-width validation and fail-closed
 * behaviour (no partially written View). */

#include "profile/binary.h"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

using ghostlock::profile::BindCode;
using ghostlock::profile::DecodeMode;
using ghostlock::profile::Document;
using ghostlock::profile::Section;
using ghostlock::profile::Value;

namespace {
    struct ExampleView {
        uint32_t task_prio = 0;
        uint64_t init_task = 0;
        int32_t waiter_shift = 0;
        bool compact_waiter = false;
    };

    void set_task_prio(ExampleView &view, uint64_t raw) noexcept {
        view.task_prio = static_cast<uint32_t>(raw);
    }

    void set_init_task(ExampleView &view, uint64_t raw) noexcept {
        view.init_task = raw;
    }

    void set_waiter_shift(ExampleView &view, uint64_t raw) noexcept {
        view.waiter_shift = static_cast<int32_t>(static_cast<int64_t>(raw));
    }

    void set_compact_waiter(ExampleView &view, uint64_t raw) noexcept {
        view.compact_waiter = raw != 0;
    }

    /* One owner-shaped example: two required fields and two optional ones. */
    struct ExampleSchema {
        using View = ExampleView;
        static constexpr ghostlock::profile::FieldSpec<ExampleView> kFields[] = {
            {"task_struct", "prio", 4, false, true, &set_task_prio},
            {"offset", "init_task", 8, false, true, &set_init_task},
            {"route.select_stack", "waiter_shift", 4, true, false, &set_waiter_shift},
            {"kernel", "compact_waiter", 1, false, false, &set_compact_waiter},
        };
    };

    Document example_document() {
        Document doc;
        doc.release = "6.6.77-doc-test";
        doc.terminal = 1;
        doc.backend = 1;
        doc.middleware = ghostlock::profile::kRouteSelectStack;
        doc.append_section("task_struct").add("prio", 132ULL);
        doc.append_section("offset").add("init_task", 0x20dc000ULL);
        doc.append_section("route.select_stack")
                .add("waiter_shift", static_cast<uint64_t>(static_cast<int64_t>(-2)));
        return doc;
    }
} // namespace

int main() {
    /* ---- Document read API and presence by key occurrence. ---- */
    {
        Document doc = example_document();
        assert(doc.release == "6.6.77-doc-test");
        assert(doc.terminal == 1);
        assert(doc.backend == 1);
        assert(doc.middleware == ghostlock::profile::kRouteSelectStack);
        assert(!doc.empty());
        assert(doc.sections.size() == 3u);

        const Section *task = doc.find_section("task_struct");
        assert(task != nullptr && task->contains("prio"));
        assert(doc.find_section("no_such_section") == nullptr);

        const Value *prio = doc.find_value("task_struct", "prio");
        assert(prio != nullptr && prio->present);
        assert(prio->raw == 132ULL);
        assert(prio->width == Value::kWireWidth);

        /* A miss is a non-present sentinel, not a stored zero. */
        const Value missing = doc.get("task_struct", "pid");
        assert(!missing.present);
        assert(missing.raw == 0 && missing.width == 0);
        assert(doc.find_value("task_struct", "pid") == nullptr);
    }

    /* ---- Strict rejects an unknown section; tooling tolerates it. ---- */
    {
        Document doc = example_document();
        doc.append_section("not_a_section").add("x", 1ULL);

        ExampleView view{};
        const auto blocked =
                ghostlock::profile::bind<ExampleSchema>(doc, view, DecodeMode::Production);
        assert(!blocked.ok());
        assert(blocked.code == BindCode::UnknownSection);
        assert(blocked.section == "not_a_section");

        assert(ghostlock::profile::bind<ExampleSchema>(doc, view, DecodeMode::Tooling).ok());
        assert(view.task_prio == 132u);
        assert(view.init_task == 0x20dc000ULL);
        assert(view.waiter_shift == -2);
    }

    /* ---- Strict rejects an unknown key in an owned section. ---- */
    {
        Document doc = example_document();
        doc.find_section("task_struct")->add("pid", 7ULL);

        ExampleView view{};
        const auto blocked =
                ghostlock::profile::bind<ExampleSchema>(doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownKey);
        assert(blocked.section == "task_struct");
        assert(blocked.key == "pid");
        assert(ghostlock::profile::bind<ExampleSchema>(doc, view, DecodeMode::Tooling).ok());
    }

    /* ---- A fully owned document binds, including an optional field. ---- */
    {
        Document doc = example_document();
        doc.append_section("kernel").add("compact_waiter", 1ULL);

        ExampleView view{};
        const auto status = ghostlock::profile::bind<ExampleSchema>(doc, view);
        assert(status.ok());
        assert(status.code == BindCode::Ok);
        assert(view.task_prio == 132u);
        assert(view.init_task == 0x20dc000ULL);
        assert(view.waiter_shift == -2);
        assert(view.compact_waiter);
    }

    /* ---- A missing required field fails closed. ---- */
    {
        Document doc;
        doc.append_section("task_struct").add("prio", 132ULL);

        ExampleView view{};
        const auto status = ghostlock::profile::bind<ExampleSchema>(doc, view);
        assert(status.code == BindCode::MissingRequired);
        assert(status.section == "offset");
        assert(status.key == "init_task");
    }

    /* ---- Width mismatch fails closed without a partial write. ---- */
    {
        Document doc;
        doc.append_section("task_struct").add("prio", 0x100000000ULL);
        doc.append_section("offset").add("init_task", 5ULL);

        ExampleView view{};
        view.task_prio = 0xdeadbeefu;
        view.init_task = 0xfeedULL;
        const auto status = ghostlock::profile::bind<ExampleSchema>(doc, view);
        assert(status.code == BindCode::WidthMismatch);
        assert(status.section == "task_struct");
        assert(status.key == "prio");
        assert(view.task_prio == 0xdeadbeefu);
        assert(view.init_task == 0xfeedULL);
    }

    /* ---- Signed values sign-extend at their declared width. ---- */
    {
        Document doc;
        doc.append_section("task_struct").add("prio", 1ULL);
        doc.append_section("offset").add("init_task", 2ULL);
        doc.append_section("route.select_stack").add("waiter_shift", 0xffffffffffffffffULL);

        ExampleView view{};
        assert(ghostlock::profile::bind<ExampleSchema>(doc, view).ok());
        assert(view.waiter_shift == -1);
    }

    /* ---- Shadow Document built by the binary transport decode. ---- */
    {
        ghostlock::profile::kernel_offsets values = {};
        values.uname_r = "6.6.77-shadow";
        values.route = ghostlock::profile::kRouteSelectStack;
        values.offsets.init_task = 0x20dc000;
        values.geometry.pselect_waiter_shift = -2;

        char buffer[8192];
        const int32_t size =
                ghostlock::binary_profile::serialize(&values, buffer, sizeof(buffer));
        assert(size > 0);

        char release[64] = {0};
        ghostlock::profile::kernel_offsets parsed = {};
        ghostlock::binary_profile::component_ids ids{};
        Document shadow;
        assert(ghostlock::binary_profile::parse(
                       std::string_view(buffer, static_cast<size_t>(size)), &parsed, release,
                       sizeof(release), &ids, &shadow) == 0);

        /* The transport result and the shadow framing agree. */
        assert(parsed.offsets.init_task == values.offsets.init_task);
        assert(shadow.release == "6.6.77-shadow");
        assert(shadow.terminal == ids.terminal);
        assert(shadow.backend == ids.backend);
        assert(shadow.middleware == ghostlock::profile::kRouteSelectStack);

        const Value *init_task = shadow.find_value("offset", "init_task");
        assert(init_task != nullptr && init_task->present);
        assert(init_task->raw == 0x20dc000ULL);
        assert(init_task->width == Value::kWireWidth);
        const Value *shift = shadow.find_value("route.select_stack", "waiter_shift");
        assert(shift != nullptr && shift->raw == 0xfffffffffffffffeULL);
        assert(shadow.get("kernel", "not_there").present == false);

        /* Strict binding of the full framed document fails (it carries more
         * sections than the example owner), while tooling ignores them. */
        ExampleView view{};
        assert(!ghostlock::profile::bind<ExampleSchema>(shadow, view, DecodeMode::Production).ok());
        assert(ghostlock::profile::bind<ExampleSchema>(shadow, view, DecodeMode::Tooling).ok());
        assert(view.task_prio == 0u);
        assert(view.init_task == 0x20dc000ULL);
        assert(view.waiter_shift == -2);
    }

    std::printf("document_schema_test: ok\n");
    return 0;
}
