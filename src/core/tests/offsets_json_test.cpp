/* Host fixed-vector test for the JSON profile decoder.
 *
 * The runtime transport is the typed binary layout (profile_binary_test);
 * this test keeps the JSON decoder honest for imported offsets, legacy flat
 * documents and HOCON-loaded configuration sources. */

#include "memory/address_space.h"
#include "legacy_support/offsets_json.h"
using namespace ghostlock;
#include "profile.h"
#include "target.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <string>

using namespace ghostlock;
using namespace ghostlock::profile;
using namespace ghostlock::legacy;

namespace {
    int g_failures = 0;

    void expect(bool condition, const char *message) {
        if (condition) return;
        fprintf(stderr, "FAIL %s\n", message);
        g_failures++;
    }

    bool decode_text(const char *text, struct kernel_offsets *out, char *release,
                     size_t cap) {
        char path[] = "../build/host-test/offsets_vector_XXXXXX";
        const int fd = mkstemp(path);
        if (fd < 0) return false;
        const size_t length = strlen(text);
        const bool written = write(fd, text, length) == (ssize_t) length;
        close(fd);
        const bool ok = written && load_resolved_profile_json(path, out, release, cap) == 0;
        unlink(path);
        return ok;
    }

    /* Shared geometry for the vectors below. */
    const char *kCommon =
            "\"schema_version\":1,\"kernel_major\":6,"
            "\"kernelsnitch\":{\"collisions\":4,\"mm_struct_sz\":1024},"
            "\"task_struct\":{\"prio\":132,\"cred\":2080,\"pi_lock\":2316,"
            "\"pi_waiters\":2336,\"pi_blocked_on\":2360,\"seccomp\":2280},"
            "\"cred\":{\"copy_size\":136,\"usage_value\":1,\"caps_offset\":48,"
            "\"caps_count\":5,\"caps_value\":-1},"
            "\"offset\":{\"init_task\":34464384,\"init_cred\":34538824,"
            "\"root_task_group\":36521344,\"selinux_enforcing\":36790120}";

    const char *kExecution =
            "\"execution\":{"
            "\"recommended_cpus\":{\"main\":0,\"consumer\":1},"
            "\"heap\":{\"prepare_max_attempts\":4,\"prepare_timeout_ms\":240000,"
            "\"kernelsnitch_timeout_ms\":60000},"
            "\"race\":{\"route_wait_ms\":1000,\"setup_settle_us\":50000,"
            "\"state_poll_interval_us\":1000},"
            "\"stages\":{\"w1_attempts\":15,\"w1_settle_us\":100000,"
            "\"w1_scratch_repair_attempts\":3,\"w2_attempts\":15,"
            "\"w2_settle_us\":100000,\"w3_chain_rounds\":3,\"w3_attempts\":6,"
            "\"w3_settle_us\":50000},"
            "\"handoff\":{\"pre_dispatch_settle_ms\":2000,"
            "\"module_poll_attempts\":30,\"module_poll_interval_ms\":100,"
            "\"enforce_poll_attempts\":200,\"enforce_poll_interval_ms\":100},"
            "\"routes\":{"
            "\"tcp_zerocopy\":{\"attempts\":2000,\"arm_sequence\":16,"
            "\"post_receive_hold_iterations\":20000},"
            "\"select_stack\":{\"enter_delay_us\":50000,\"timeout_us\":200000,"
            "\"consumer_max_calls\":1,\"consumer_burst_calls\":1},"
            "\"multicast_waiter\":{\"ready_timeout_ms\":10000,"
            "\"post_requeue_settle_us\":200000,\"post_adjust_settle_us\":100000}}}";

    void check_documents() {
        struct kernel_offsets v = {};
        char release[64] = {0};

        /* New layout: one route branch plus a fallback declaration. */
        const std::string select_doc = std::string("{\"release\":\"6.6.test\",") +
                                       kCommon + "," + kExecution +
                                       ",\"route\":{\"select_stack\":{\"waiter_shift\":-2}},"
                                       "\"fallback\":{\"to\":\"none\"}}";
        expect(decode_text(select_doc.c_str(), &v, release, sizeof(release)),
               "select document decodes");
        expect(strcmp(release, "6.6.test") == 0, "release decoded");
        expect(v.route == kRouteSelectStack, "select route kind");
        expect(v.fallback_route == kRouteAuto, "fallback none");
        expect(v.pselect_waiter_shift == -2, "waiter shift decoded");
        expect(v.task_prio == 132 && v.cred_copy_size == 136, "namespaced scalars decoded");
        expect(v.off_init_task == 34464384, "offset namespace decoded");
        expect(v.kernelsnitch_collisions == 4 && v.mm_struct_sz == 1024,
               "kernelsnitch namespace decoded");
        expect(v.execution.w1_attempts == 15 && v.execution.tcp_attempts == 2000,
               "execution decoded");

        const std::string tcp_doc = std::string("{\"release\":\"6.1.test\",") +
                                    kCommon +
                                    ",\"route\":{\"tcp_zerocopy\":{\"compact_waiter\":1}},"
                                    "\"fallback\":{\"to\":\"select_stack\","
                                    "\"route\":{\"select_stack\":{\"waiter_shift\":1}}}}";
        expect(decode_text(tcp_doc.c_str(), &v, release, sizeof(release)),
               "tcp document decodes");
        expect(v.route == kRouteTcpZerocopy, "tcp route kind");
        expect(v.fallback_route == kRouteSelectStack, "fallback target decoded");
        expect(v.compact_waiter == 1 && v.pselect_waiter_shift == 1,
               "fallback branch fields decoded");

        const std::string mcast_doc = std::string("{\"release\":\"5.15.test\",") +
                                      kCommon +
                                      ",\"route\":{\"multicast_waiter\":{\"waiter_off\":96,"
                                      "\"buffer_size\":264,\"compact_waiter\":1}},"
                                      "\"fallback\":{\"to\":\"none\"}}";
        expect(decode_text(mcast_doc.c_str(), &v, release, sizeof(release)),
               "multicast document decodes");
        expect(v.route == kRouteMulticastWaiter, "multicast route kind");
        expect(v.mcast_waiter_off == 96 && v.mcast_buffer_size == 264 &&
               v.compact_waiter == 1,
               "multicast branch decoded");

        /* Legacy flat spelling stays decodable for old offsets.json files. */
        const std::string legacy_doc = std::string("{\"release\":\"legacy.test\",") +
                                       kCommon +
                                       ",\"route\":\"tcp_zerocopy\",\"compact_waiter\":1,"
                                       "\"fallback_to\":\"select_stack\",\"pselect_waiter_shift\":1}";
        expect(decode_text(legacy_doc.c_str(), &v, release, sizeof(release)),
               "legacy document decodes");
        expect(v.route == kRouteTcpZerocopy, "legacy route decoded");
        expect(v.fallback_route == kRouteSelectStack, "legacy fallback decoded");
        expect(v.compact_waiter == 1 && v.pselect_waiter_shift == 1,
               "legacy flat fields decoded");
    }

    void check_decoder_rejections() {
        const char *cases[] = {
            "",
            "[]",
            "{}",
            "{\"schema_version\":2,\"release\":\"x\",\"execution\":{}}",
            "{\"schema_version\":1,\"release\":\"x\",\"execution\":{"
            "\"recommended_cpus\":{\"main\":0,\"consumer\":1,"
            "\"heap\":{\"prepare_max_attempts\":1},"
            "\"race\":{},\"stages\":{},\"handoff\":{},\"routes\":{}}}",
            "{\"schema_version\":1,\"release\":\"x\"",
            "{\"schema_version\":1,\"release\":\"x\"} trailing",
            "{\"schema_version\":1,\"execution\":{},\"release\":7}",
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            struct kernel_offsets v = {};
            char release[64] = {0};
            expect(!decode_text(cases[i], &v, release, sizeof(release)),
                   "rejection vector rejected");
        }

        /* A release longer than the caller buffer must be rejected. */
        const char *long_release =
                "{\"schema_version\":1,\"release\":\"0123456789\"}";
        char path[] = "../build/host-test/offsets_short_cap_XXXXXX";
        const int fd = mkstemp(path);
        if (fd < 0) {
            expect(false, "mkstemp for release-cap vector");
            return;
        }
        const size_t length = strlen(long_release);
        expect(write(fd, long_release, length) == (ssize_t) length,
               "write release-cap vector");
        close(fd);
        struct kernel_offsets v = {};
        char release[8] = {0};
        expect(load_resolved_profile_json(path, &v, release, sizeof(release)) == -1,
               "release longer than buffer rejected");
        unlink(path);
    }

    void check_address_paths() {
        struct kernel_offsets decoded = {};
        decoded.uname_r = "6.6.test";
        decoded.kernel_major = 6;
        decoded.off_init_cred = 0x1000;
        decoded.kernel_phys_load = 0x80000000ULL;
        TargetProfile profile = TargetProfile::from(&decoded);
        memory::ResolvedAddresses addresses = {};
        expect(addresses.init_for_soc(&profile,
                                                       memory::SocFamily::Qcom) == 0,
               "address init succeeds");
        expect(addresses.init_cred_image_addr() ==
               (uintptr_t)(KIMAGE_TEXT_BASE + decoded.off_init_cred),
               "init_cred image formula");
        const uint64_t expected_phys = decoded.kernel_phys_load;
        const uintptr_t physical = expected_phys + decoded.off_init_cred;
        expect(addresses.data_alias(addresses.init_cred_image_addr()) ==
               ((physical - P0_PHYS_OFFSET) | P0_PAGE_OFFSET),
               "init_cred direct-map alias");

        /* SoC fallbacks only apply when the profile carries no measured load. */
        struct kernel_offsets zero_load = decoded;
        zero_load.kernel_phys_load = 0;
        TargetProfile zero_profile = TargetProfile::from(&zero_load);
        memory::ResolvedAddresses mtk = {};
        expect(mtk.init_for_soc(&zero_profile,
                                                       memory::SocFamily::Mtk) == 0 &&
               mtk.phys_load() ==
               (uintptr_t)(KIMAGE_TEXT_BASE - MTK_VADDR_BASE),
               "MTK physical load fallback");
    }
} // namespace

int main(void) {
    check_documents();
    check_decoder_rejections();
    check_address_paths();
    if (g_failures != 0) {
        fprintf(stderr, "offsets_json_test: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("offsets_json_test: ok");
    return 0;
}
