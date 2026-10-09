/** @file main.cpp
 *  The runtime entry: read what the app sends, resolve it, and say what it is.
 *
 *  One document arrives on stdin, framed as the app frames it (a four-byte
 *  big-endian length, then the document, then an optional session frame this
 *  build does not read yet). The document is parsed, resolved into a chain and an
 *  ordered list of steps, and reported: which chain, which steps, and whether the
 *  steps came from the queue the document carried or from the chain default.
 *
 *  What it does not do yet: run them. Every chain resolves to a refusal that says
 *  so -- the mechanisms are not in this tree, and a report that claimed otherwise
 *  would be worse than a refusal. The refusal is the same one the plan gate uses
 *  for an operation no executor is available for.
 *
 *  With no arguments it prints the design family it was built from, which is what
 *  the skeleton did and what a build check can look at.
 *
 *  Build: make -C src host         (host binary, used by the evidence checks)
 *         make -C src android      (arm64 device binary; NDK_CC overrides)
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "contract/layer_scope.hpp"
#include "contract/step_catalog.hpp"
#include "memory/target_profile.hpp"
#include "profile/document_pairs.hpp"
#include "profile/glkv3_document.hpp"
#include "profile/run_plan.hpp"
#include "race/race_runtime.hpp"

namespace {
    constexpr std::size_t kMaxDocumentBytes = 1U << 20U;

    /** Read the framed document from stdin into buf; returns its length. */
    std::size_t read_document(std::uint8_t *buf, std::size_t capacity) {
        std::uint8_t header[4U] = {};
        if (std::fread(header, 1U, sizeof(header), stdin) != sizeof(header)) {
            return 0U;
        }
        const std::size_t length = (static_cast<std::size_t>(header[0U]) << 24U) |
                                   (static_cast<std::size_t>(header[1U]) << 16U) |
                                   (static_cast<std::size_t>(header[2U]) << 8U) |
                                   static_cast<std::size_t>(header[3U]);
        if (length == 0U || length > capacity) {
            return 0U;
        }
        if (std::fread(buf, 1U, length, stdin) != length) {
            return 0U;
        }
        return length;
    }

    int report_identity() {
        std::printf("ghostlock: %zu layers\n", ghostlock::contract::kLayerCount);
        for (std::size_t i = 0U; i < ghostlock::contract::kLayerCount; ++i) {
            std::printf("  %s\n", ghostlock::contract::kLayers[i].name);
        }
        return 0;
    }

    /** Read the race settings the document declares for this chain's backend and
     *  report whether they are coherent. The keys are the document's own; the
     *  rounds are derived from the timeout and the interval it declares. */
    ghostlock::race::SettingsVerdict report_race_settings(
        const ghostlock::profile::Document &document, const char *backend) {
        char section[192];
        std::snprintf(section, sizeof(section), "backend.%s.execution.recommended_cpus", backend);
        const char *cpu_keys[] = {"main", "consumer"};
        ghostlock::memory::KeyValue cpus[2U];
        ghostlock::race::RaceSettings settings;
        if (ghostlock::profile::read_section_pairs(document, std::string_view(section), cpu_keys, 2U,
                                                   cpus, 2U)
                .verdict == ghostlock::profile::PairsVerdict::Ok) {
            settings.main_cpu = static_cast<int>(cpus[0U].value);
            settings.consumer_cpu = static_cast<int>(cpus[1U].value);
        }
        std::snprintf(section, sizeof(section), "backend.%s.execution.race", backend);
        const char *race_keys[] = {"setup_settle_us", "state_poll_interval_us",
                                   "route_done_timeout_ms"};
        ghostlock::memory::KeyValue race[3U];
        if (ghostlock::profile::read_section_pairs(document, std::string_view(section), race_keys, 3U,
                                                   race, 3U)
                .verdict == ghostlock::profile::PairsVerdict::Ok) {
            settings.settle_us = race[0U].value;
            settings.poll_us = race[1U].value;
            if (settings.poll_us != 0U) {
                settings.poll_rounds = (race[2U].value * 1000U) / settings.poll_us;
            }
        }
        const ghostlock::race::SettingsVerdict verdict = ghostlock::race::check_settings(settings);
        std::printf("race main=%d consumer=%d settle=%llu poll=%llu rounds=%llu %s\n",
                    settings.main_cpu, settings.consumer_cpu,
                    static_cast<unsigned long long>(settings.settle_us),
                    static_cast<unsigned long long>(settings.poll_us),
                    static_cast<unsigned long long>(settings.poll_rounds),
                    ghostlock::race::settings_verdict_name(verdict));
        return verdict;
    }

    /** Resolve the document and say what the run would do. Never claims to run. */
    int report_plan() {
        static std::uint8_t document_bytes[kMaxDocumentBytes];
        const std::size_t length = read_document(document_bytes, sizeof(document_bytes));
        if (length == 0U) {
            std::printf("document-invalid\n");
            return 2;
        }
        ghostlock::profile::Document document;
        if (document.parse(document_bytes, length) !=
            ghostlock::profile::DocumentVerdict::Ok) {
            std::printf("document-invalid\n");
            return 2;
        }
        const ghostlock::profile::RunPlanResult resolved =
            ghostlock::profile::resolve_run_plan(document);
        if (resolved.verdict != ghostlock::profile::PlanVerdict::Ok) {
            std::printf("%s\n", ghostlock::profile::plan_verdict_name(resolved.verdict));
            return 3;
        }
        std::printf("plan chain=%s steps=", resolved.plan.chain->token);
        for (std::size_t i = 0U; i < resolved.plan.step_count; ++i) {
            std::printf("%s%s", i == 0U ? "" : ",",
                        ghostlock::contract::step_token(resolved.plan.steps[i]));
        }
        std::printf(" source=%s\n", resolved.plan.from_fallback ? "chain-default" : "queue");
        const ghostlock::race::SettingsVerdict settings =
            report_race_settings(document, resolved.plan.chain->backend);
        /* The chain resolved; running it is what this tree cannot do yet. */
        std::printf("executor-unavailable\n");
        return settings == ghostlock::race::SettingsVerdict::Ok ? 4 : 5;
    }
}  // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ghostlock-app-call") == 0) {
            return report_plan();
        }
    }
    return report_identity();
}
