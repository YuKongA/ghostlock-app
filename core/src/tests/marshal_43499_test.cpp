/** @file tests/marshal_43499_test.cpp
 *  43499 snapshot 结果数组的布局：名字与值的唯一权威。
 *
 *  这里钉住三件事：字段名集合、字段顺序（index 按名字查）、三代 generation 的值。
 *  K1 的 `Facts43499.decode` 按 native 导出的名字表解码；名字表少了/多了/写错，这里先红。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "boundary/protocol/marshal_43499.hpp"

namespace {
    using ghostlock::jni::Snapshot43499;
    using ghostlock::jni::marshal::cve43499::Snapshot43499Layout;
    using ghostlock::jni::marshal::cve43499::fill_snapshot;

    std::size_t g_failures = 0U;

    void expect(const bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool has_name(const char *name) noexcept {
        for (std::size_t index = 0U; index < Snapshot43499Layout::count; ++index) {
            if (std::strcmp(Snapshot43499Layout::names[index], name) == 0) {
                return true;
            }
        }
        return false;
    }
}  // namespace

int main() {
    Snapshot43499 snapshot{};
    snapshot.phase = 1U;
    snapshot.flags = 0x70000U;
    snapshot.width = 8U;
    snapshot.facts.page_base = 0x1000U;
    snapshot.facts.word0 = 0x2000U;
    snapshot.facts.word1 = 0x3000U;
    snapshot.facts.payload_page = 0x4000U;
    snapshot.facts.payload_base = 0x5000U;
    snapshot.facts.alias_delta = 0x6000U;
    snapshot.facts.found = 0x7000U;
    snapshot.facts.mm_struct = 0x8000U;
    snapshot.facts.victim_task = 0x9000U;
    snapshot.facts.victim_pid = 0xA000U;
    snapshot.facts.prepare_generation = 1U;
    snapshot.facts.discovery_generation = 2U;
    snapshot.facts.victim_generation = 3U;

    expect(Snapshot43499Layout::count == 16U, "snapshot layout has sixteen fields");
    expect(has_name("prepare_generation"), "prepare_generation is published");
    expect(has_name("discovery_generation"), "discovery_generation is published");
    expect(has_name("victim_generation"), "victim_generation is published");

    std::int64_t values[Snapshot43499Layout::count] = {};
    fill_snapshot(values, snapshot);
    expect(values[Snapshot43499Layout::index("phase")] == 1, "phase is encoded");
    expect(values[Snapshot43499Layout::index("flags")] == 0x70000, "flags are encoded");
    expect(values[Snapshot43499Layout::index("width")] == 8, "width is encoded");
    expect(values[Snapshot43499Layout::index("page_base")] == 0x1000, "page_base is encoded");
    expect(values[Snapshot43499Layout::index("victim_pid")] == 0xA000, "victim_pid is encoded");
    expect(values[Snapshot43499Layout::index("prepare_generation")] == 1, "prepare generation is encoded");
    expect(values[Snapshot43499Layout::index("discovery_generation")] == 2, "discovery generation is encoded");
    expect(values[Snapshot43499Layout::index("victim_generation")] == 3, "victim generation is encoded");

    if (g_failures != 0U) {
        std::printf("marshal_43499_test: %zu failure(s)\n", g_failures);
        return 1;
    }
    std::printf("marshal_43499_test: ok\n");
    return 0;
}
