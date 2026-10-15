/** @file tests/marshal_test.cpp
 *  JNI 编组层（审查第 5 点）：名字表唯一、按名字取值、往返一致。
 *
 *  跨端传的是数字数组，"第几个是什么"只能有一份权威。这里钉的是：名字表没有重复、每个名字都能取到
 *  范围内的下标、以及填充/解析按名字往返 —— 名字与下标一旦错位，这里就红。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "jni/marshal_43284.hpp"
#include "jni/marshal_43499.hpp"

namespace {
    using ghostlock::jni::field_names_unique;
    using ghostlock::jni::marshal::cve43284::Hook43284Layout;
    using ghostlock::jni::marshal::cve43284::Initialize43284Layout;
    using ghostlock::jni::marshal::cve43499::Initialize43499Layout;
    using ghostlock::jni::marshal::cve43284::Snapshot43284Layout;
    using ghostlock::jni::marshal::cve43499::Snapshot43499Layout;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    void every_table_is_unique_and_resolvable() {
        expect(field_names_unique(Snapshot43499Layout::names, Snapshot43499Layout::count),
               "43499 snapshot names are unique");
        expect(field_names_unique(Initialize43499Layout::names, Initialize43499Layout::count),
               "43499 initialize names are unique");
        expect(field_names_unique(Snapshot43284Layout::names, Snapshot43284Layout::count),
               "43284 snapshot names are unique");
        expect(field_names_unique(Initialize43284Layout::names, Initialize43284Layout::count),
               "43284 initialize names are unique");
        expect(field_names_unique(Hook43284Layout::names, Hook43284Layout::count),
               "43284 hook names are unique");

        expect(Snapshot43499Layout::index("page_base") < Snapshot43499Layout::count,
               "a known name resolves");
        expect(Snapshot43499Layout::index("not-a-field") == Snapshot43499Layout::count,
               "an unknown name resolves to count (never to a valid slot)");
        expect(Initialize43284Layout::index("wait_poll_ms") < Initialize43284Layout::count,
               "43284 initialize resolves by name");
    }

    void the_43499_round_trip_keeps_every_field_in_its_own_slot() {
        ghostlock::jni::Snapshot43499 snapshot{};
        snapshot.phase = 1U;
        snapshot.flags = 0x801U;
        snapshot.width = 8U;
        snapshot.facts.page_base = 0xAAAAU;
        snapshot.facts.word0 = 0xBBBBU;
        snapshot.facts.victim_pid = 0xCCCCU;

        std::int64_t values[Snapshot43499Layout::count] = {};
        ghostlock::jni::marshal::cve43499::fill_snapshot(values, snapshot);
        expect(values[Snapshot43499Layout::index("flags")] == 0x801, "flags land where the name says");
        expect(values[Snapshot43499Layout::index("width")] == 8, "width too");
        expect(values[Snapshot43499Layout::index("page_base")] == 0xAAAA, "page_base too");
        expect(values[Snapshot43499Layout::index("victim_pid")] == 0xCCCC, "victim_pid too");

        std::int64_t params[Initialize43499Layout::count] = {};
        params[Initialize43499Layout::index("route_id")] = 1;
        params[Initialize43499Layout::index("waiter_off")] = 96;
        params[Initialize43499Layout::index("lock_offset")] = 56;
        params[Initialize43499Layout::index("image_selinux_enforcing")] = 47885704;
        const ghostlock::jni::Initialize43499 parsed = ghostlock::jni::marshal::cve43499::parse_initialize(params);
        expect(parsed.route_id == 1U, "route_id round trips");
        expect(parsed.waiter_off == 96U, "waiter_off round trips");
        expect(parsed.lock_offset == 56U, "lock_offset round trips");
        expect(parsed.image_selinux_enforcing == 47885704U, "the image offset round trips");
    }

    void the_43284_round_trip_keeps_every_field_in_its_own_slot() {
        ghostlock::jni::Snapshot43284 snapshot{};
        snapshot.phase = 1U;
        snapshot.flags = 0x1004U;
        snapshot.width = 16U;
        snapshot.facts.token = 0x1234U;
        snapshot.facts.role = 3U;
        snapshot.facts.sequence = 7U;

        std::int64_t values[Snapshot43284Layout::count] = {};
        ghostlock::jni::marshal::cve43284::fill_snapshot(values, snapshot);
        expect(values[Snapshot43284Layout::index("width")] == 16, "the block width lands by name");
        expect(values[Snapshot43284Layout::index("token")] == 0x1234, "the ticket lands by name");
        expect(values[Snapshot43284Layout::index("role")] == 3, "the role lands by name");

        ghostlock::jni::HookWords43284 words{};
        words.branch_word = 0x14012d83U;
        words.displaced_word = 0xf9400028U;
        words.site_offset = 733356U;
        words.landing_offset = 1041960U;
        std::int64_t hook[Hook43284Layout::count] = {};
        ghostlock::jni::marshal::cve43284::fill_hook(hook, words);
        expect(hook[Hook43284Layout::index("branch_word")] == 0x14012d83, "branch by name");
        expect(hook[Hook43284Layout::index("displaced_word")] == 0xf9400028, "displaced by name");
        expect(hook[Hook43284Layout::index("site_offset")] == 733356, "site by name");
        expect(hook[Hook43284Layout::index("landing_offset")] == 1041960, "landing by name");

        std::int64_t params[Initialize43284Layout::count] = {};
        params[Initialize43284Layout::index("trigger_delay_ms")] = 150;
        params[Initialize43284Layout::index("wait_budget_ms")] = 800;
        params[Initialize43284Layout::index("wait_poll_ms")] = 10;
        const ghostlock::jni::Initialize43284 parsed = ghostlock::jni::marshal::cve43284::parse_initialize(params);
        expect(parsed.trigger_delay_ms == 150U, "delay round trips");
        expect(parsed.wait_budget_ms == 800U, "budget round trips");
        expect(parsed.wait_poll_ms == 10U, "poll round trips");
    }
}  // namespace

int main() {
    std::printf("marshal: layouts\n");
    every_table_is_unique_and_resolvable();
    std::printf("marshal: 43499 round trip\n");
    the_43499_round_trip_keeps_every_field_in_its_own_slot();
    std::printf("marshal: 43284 round trip\n");
    the_43284_round_trip_keeps_every_field_in_its_own_slot();
    if (g_failures != 0U) {
        std::printf("marshal: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("marshal: PASS (one authority per layout, verified by name)\n");
    return 0;
}
