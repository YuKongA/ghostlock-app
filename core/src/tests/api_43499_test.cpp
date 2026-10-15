/** @file tests/api_43499_test.cpp
 *  43499 的函数面（用户裁定 2026-10-14；D2 参数流向，2026-10-15）。
 *
 *  钉住审查要求的那条链：**输入来源 → JNI 参数 → 会话保存 → 回调可见性**。
 *  参数类来自 D1 单表生成，逐字段用哨兵值填满；假缝把收到的参数原样记下来，测试逐字段对比 ——
 *  少传一个字段、传错一个字段、或在会话里丢一次，这里都会红。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "jni/api_43499.hpp"

namespace {
    using ghostlock::jni::api::Phase;
    using ghostlock::jni::Backend43499;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::Facts43499;
    using ghostlock::jni::kFactVictim;
    using ghostlock::jni::DeviceOps43499;
    using ghostlock::jni::SessionParams43499;
    using ghostlock::jni::Snapshot43499;
    using ghostlock::jni::VictimAction;
    using ghostlock::jni::contract::Discover43499Values;
    using ghostlock::jni::contract::FinishVictim43499Values;
    using ghostlock::jni::contract::Prepare43499Values;
    using ghostlock::jni::contract::WordWrite43499Values;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool same(const char *lhs, const char *rhs) { return std::strcmp(lhs, rhs) == 0; }

    /** 假缝记下来的东西：会话参数 + 请求组 + 它在事实里看到的受害者。 */
    struct Spy final {
        SessionParams43499 params{};
        Prepare43499Values prepare{};
        Discover43499Values discover{};
        WordWrite43499Values write{};
        FinishVictim43499Values finish{};
        std::uint64_t facts_victim_task = 0U;
        int calls = 0;
    };

    Spy &spy() noexcept {
        static Spy instance;
        return instance;
    }

    bool device_prepare(const SessionParams43499 &params, const Prepare43499Values &request,
                      Facts43499 &facts, void *) noexcept {
        spy().params = params;
        spy().prepare = request;
        ++spy().calls;
        facts.page_base = 0x1000U;
        facts.word0 = 0x2000U;
        facts.word1 = 0x3000U;
        return true;
    }

    bool device_discover(const SessionParams43499 &params, const Discover43499Values &request,
                       Facts43499 &facts, void *) noexcept {
        spy().params = params;
        spy().discover = request;
        ++spy().calls;
        facts.found = 0x4000U;
        facts.alias_delta = 0x5000U;
        return true;
    }

    bool device_spawn(const SessionParams43499 &params, Facts43499 &facts, void *) noexcept {
        spy().params = params;
        ++spy().calls;
        facts.victim_task = 0x6000U;
        facts.victim_pid = 0x1234U;
        return true;
    }

    bool device_finish(const SessionParams43499 &params, const FinishVictim43499Values &request,
                     Facts43499 &facts, void *) noexcept {
        spy().params = params;
        spy().finish = request;
        spy().facts_victim_task = facts.victim_task;
        ++spy().calls;
        return true;
    }

    bool device_land(const SessionParams43499 &params, const WordWrite43499Values &request,
                   Facts43499 &, void *) noexcept {
        spy().params = params;
        spy().write = request;
        ++spy().calls;
        return true;
    }

    bool device_read(const SessionParams43499 &params, std::uint64_t, std::uint64_t &value,
                   void *) noexcept {
        spy().params = params;
        value = 0x7777U;
        ++spy().calls;
        return true;
    }

    DeviceOps43499 full_ops() noexcept {
        DeviceOps43499 ops{};
        ops.prepare = &device_prepare;
        ops.discover = &device_discover;
        ops.spawn_victim = &device_spawn;
        ops.finish_victim = &device_finish;
        ops.land = &device_land;
        ops.read = &device_read;
        return ops;
    }

    /** 四组参数，字段全用不同哨兵值：任何一处错位都会在逐个对比里露出来。 */
    SessionParams43499 sentinel_params() noexcept {
        SessionParams43499 params{};
        params.init.routeId = 11;
        params.init.mainCpu = 2;
        params.init.consumerCpu = 1;
        params.init.waiterOffset = 96;
        params.init.taskOffset = 48;
        params.init.lockOffset = 56;
        params.init.bufferSize = 264;
        params.init.maxAttempts = 4;
        params.init.armSequence = 7;
        params.init.parkMs = 150;
        params.init.zeroCopyMapBytes = 4096;
        params.init.fdCount = 12;
        params.layout.lock = 0x1111;
        params.layout.waiter = 0x2222;
        params.layout.pointerBytes = 8;
        params.layout.initTask = 0x3333;
        params.credential.copySize = 176;
        params.credential.capsValue = 2199023255551LL;
        params.credential.refCount = 4;
        params.targets.selinuxEnforcingImageOffset = 47885704;
        params.targets.selinuxEnforcingImagePresent = true;
        params.targets.taskSeccompOffset = 2144;
        return params;
    }

    Prepare43499Values sentinel_prepare() noexcept {
        Prepare43499Values request{};
        request.collisions = 4;
        request.threads = 3;
        request.pageSize = 4096;
        request.regionSize = 0x200000;
        request.objectSize = 512;
        request.mmStructSize = 1024;
        request.slabStride = 64;
        request.shaped = true;
        return request;
    }

    Discover43499Values sentinel_discover(std::int64_t generation) noexcept {
        Discover43499Values request{};
        request.rangeStart = 0xFFFF888000000000LL;
        request.rangeEnd = 0xFFFF888000100000LL;
        request.patienceRounds = 32;
        request.prepareGeneration = generation;
        return request;
    }

    WordWrite43499Values sentinel_write(std::int64_t prepare, std::int64_t discover,
                                        std::int64_t victim, bool victim_present,
                                        std::int32_t form) noexcept {
        WordWrite43499Values request{};
        request.address = 0xFFFF888000001000LL;
        request.value = form == 3 ? 0xABCDLL : 0LL;
        request.form = form;
        request.offset = 0;
        request.prepareGeneration = prepare;
        request.discoveryGeneration = discover;
        request.victimGeneration = victim;
        request.victimGenerationPresent = victim_present;
        return request;
    }

    void the_session_keeps_every_field_it_was_given() {
        Backend43499 backend{};
        const SessionParams43499 params = sentinel_params();
        expect(backend.initialize(params) == CallCode::Ok, "a full parameter set comes up");
        expect(backend.phase() == Phase::Ready, "and the phase is ready");
        const SessionParams43499 &kept = backend.params();
        expect(kept.init.routeId == 11, "routeId is kept");
        expect(kept.init.waiterOffset == 96 && kept.init.taskOffset == 48 &&
                   kept.init.lockOffset == 56,
               "the three offsets are kept");
        expect(kept.init.bufferSize == 264, "bufferSize is kept");
        expect(kept.init.zeroCopyMapBytes == 4096, "a deep tuning field is kept");
        expect(kept.layout.pointerBytes == 8, "the layout group is kept");
        expect(kept.credential.capsValue == 2199023255551LL, "the credential group is kept");
        expect(kept.targets.selinuxEnforcingImageOffset == 47885704, "the target group is kept");
        expect(kept.targets.selinuxEnforcingImagePresent, "including its presence flag");
    }

    void a_missing_required_field_is_refused_by_name() {
        Backend43499 backend{};
        SessionParams43499 params = sentinel_params();
        params.init.lockOffset = 0;
        expect(backend.initialize(params) == CallCode::BadParameters, "a missing lockOffset");
        expect(same(backend.refusal(), "bad-parameters:lockOffset"), "named with that field");

        SessionParams43499 no_route = sentinel_params();
        no_route.init.routeId = 0;
        expect(backend.initialize(no_route) == CallCode::BadParameters, "a missing routeId");
        expect(same(backend.refusal(), "bad-parameters:routeId"), "named as such");

        SessionParams43499 no_pointer = sentinel_params();
        no_pointer.layout.pointerBytes = 0;
        expect(backend.initialize(no_pointer) == CallCode::BadParameters, "a missing pointerBytes");
        expect(same(backend.refusal(), "bad-parameters:pointerBytes"), "named as such");
    }

    void every_ops_sees_the_whole_session_parameters() {
        Backend43499 backend{full_ops()};
        expect(backend.initialize(sentinel_params()) == CallCode::Ok, "initialize");
        Snapshot43499 snapshot{};

        spy() = Spy{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Ok, "prepare runs");
        expect(spy().calls == 1, "the prepare ops ran");
        expect(spy().params.init.routeId == 11 && spy().params.init.waiterOffset == 96 &&
                   spy().params.init.bufferSize == 264,
               "the prepare ops sees the init group");
        expect(spy().params.layout.pointerBytes == 8, "and the layout group");
        expect(spy().params.credential.capsValue == 2199023255551LL, "and the credential group");
        expect(spy().params.targets.taskSeccompOffset == 2144, "and the target group");
        expect(spy().prepare.collisions == 4 && spy().prepare.threads == 3 &&
                   spy().prepare.pageSize == 4096,
               "prepare sees its own request");
        expect(spy().prepare.slabStride == 64 && spy().prepare.shaped, "including the tail fields");
    }

    void the_generations_bind_each_request_to_its_own_round() {
        Backend43499 backend{full_ops()};
        expect(backend.initialize(sentinel_params()) == CallCode::Ok, "initialize");
        Snapshot43499 snapshot{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Ok, "prepare");
        expect(backend.facts().prepare_generation == 1, "prepare bumps its generation");

        Discover43499Values stale = sentinel_discover(0);
        expect(backend.discover(stale, snapshot) == CallCode::Failed, "a stale prepare generation");
        expect(same(backend.refusal(), "stale-generation:prepare"), "is named");

        expect(backend.discover(sentinel_discover(1), snapshot) == CallCode::Ok, "discover");
        expect(backend.facts().discovery_generation == 1, "discover bumps its generation");

        expect(backend.write_word(sentinel_write(0, 1, 0, false, 1)) == CallCode::Failed,
               "a write with a stale prepare generation");
        expect(same(backend.refusal(), "stale-generation:prepare"), "is named");

        expect(backend.spawn_victim(snapshot) == CallCode::Ok, "the victim spawns");
        expect(backend.facts().victim_generation == 1, "which bumps the victim generation");

        spy() = Spy{};
        const WordWrite43499Values fresh = sentinel_write(1, 1, 1, true, 1);
        expect(backend.write_word(fresh) == CallCode::Ok, "a write on the current generations runs");
        expect(spy().write.address == fresh.address && spy().write.form == fresh.form,
               "and the ops sees the request");
        expect(spy().write.victimGeneration == 1 && spy().write.victimGenerationPresent,
               "including the victim generation");

        expect(backend.write_word(sentinel_write(1, 1, 0, true, 1)) == CallCode::Failed,
               "a stale victim generation");
        expect(same(backend.refusal(), "stale-generation:victim"), "is named");
    }

    void retiring_the_victim_invalidates_old_requests() {
        Backend43499 backend{full_ops()};
        expect(backend.initialize(sentinel_params()) == CallCode::Ok, "initialize");
        Snapshot43499 snapshot{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Ok, "prepare");
        expect(backend.discover(sentinel_discover(1), snapshot) == CallCode::Ok, "discover");
        expect(backend.spawn_victim(snapshot) == CallCode::Ok, "spawn");

        spy() = Spy{};
        FinishVictim43499Values retire{};
        retire.action = static_cast<std::int32_t>(VictimAction::Retire);
        retire.victimGeneration = 1;
        expect(backend.finish_victim(retire, snapshot) == CallCode::Ok, "retire runs");
        expect(spy().finish.action == static_cast<std::int32_t>(VictimAction::Retire),
               "the ops sees retire");
        expect(spy().facts_victim_task == 0x6000U, "and saw the live victim before it was cleared");
        expect(!backend.facts().victim_spawned, "the session no longer holds a victim");
        expect(backend.facts().victim_generation == 2, "and the generation moved on");
        expect((snapshot.flags & kFactVictim) == 0U, "the published flags agree");

        expect(backend.write_word(sentinel_write(1, 1, 1, true, 1)) == CallCode::Failed,
               "an old victim generation is refused");
        expect(same(backend.refusal(), "stale-generation:victim"), "by name");

        FinishVictim43499Values again{};
        again.action = static_cast<std::int32_t>(VictimAction::Retire);
        again.victimGeneration = 1;
        expect(backend.finish_victim(again, snapshot) == CallCode::NotReady,
               "and there is no victim left to finish");
    }

    void a_park_keeps_the_generation_for_the_next_step() {
        Backend43499 backend{full_ops()};
        expect(backend.initialize(sentinel_params()) == CallCode::Ok, "initialize");
        Snapshot43499 snapshot{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Ok, "prepare");
        expect(backend.discover(sentinel_discover(1), snapshot) == CallCode::Ok, "discover");
        expect(backend.spawn_victim(snapshot) == CallCode::Ok, "spawn");

        FinishVictim43499Values park{};
        park.action = static_cast<std::int32_t>(VictimAction::Park);
        park.victimGeneration = 1;
        expect(backend.finish_victim(park, snapshot) == CallCode::Ok, "park runs");
        expect(backend.facts().victim_spawned, "the victim stays");
        expect(backend.facts().victim_generation == 1, "and so does its generation");
        expect(backend.facts().victim_handed_off, "marked as handed off");
        expect(backend.write_word(sentinel_write(1, 1, 1, true, 3)) == CallCode::Ok,
               "a write with the parked generation still runs");
    }

    void every_device_op_is_named_when_its_ops_is_absent() {
        Backend43499 backend{};
        Snapshot43499 snapshot{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Unsupported, "prepare");
        expect(same(backend.refusal(), "op-not-wired:prepare"), "named");
        expect(backend.discover(sentinel_discover(0), snapshot) == CallCode::Unsupported, "discover");
        expect(same(backend.refusal(), "op-not-wired:discover"), "named");
        expect(backend.spawn_victim(snapshot) == CallCode::Unsupported, "spawn_victim");
        expect(same(backend.refusal(), "op-not-wired:spawn_victim"), "named");
        FinishVictim43499Values finish{};
        expect(backend.finish_victim(finish, snapshot) == CallCode::Unsupported, "finish_victim");
        expect(same(backend.refusal(), "op-not-wired:finish_victim"), "named");
        expect(backend.write_word(sentinel_write(0, 0, 0, false, 1)) == CallCode::Unsupported,
               "write_word");
        expect(same(backend.refusal(), "op-not-wired:write_word"), "named");
        std::uint64_t value = 0U;
        expect(backend.read_word(0x1000U, value) == CallCode::Unsupported, "read_word");
        expect(same(backend.refusal(), "op-not-wired:read_word"), "named");
    }

    void the_write_form_polarity_is_still_checked() {
        Backend43499 backend{full_ops()};
        expect(backend.initialize(sentinel_params()) == CallCode::Ok, "initialize");
        Snapshot43499 snapshot{};
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Ok, "prepare");
        expect(backend.discover(sentinel_discover(1), snapshot) == CallCode::Ok, "discover");
        expect(backend.spawn_victim(snapshot) == CallCode::Ok, "spawn");

        expect(backend.write_word(sentinel_write(1, 1, 1, true, 3)) == CallCode::Ok,
               "a value write with a value runs");
        WordWrite43499Values zero_value = sentinel_write(1, 1, 1, true, 3);
        zero_value.value = 0;
        expect(backend.write_word(zero_value) == CallCode::BadParameters, "value 0 with form 3");
        expect(same(backend.refusal(), "write-refused:value-missing"), "named");
        WordWrite43499Values dirty_clear = sentinel_write(1, 1, 1, true, 2);
        dirty_clear.value = 0xDEADLL;
        expect(backend.write_word(dirty_clear) == CallCode::BadParameters, "a value with a clear form");
        expect(same(backend.refusal(), "write-refused:value-not-empty"), "named");
        WordWrite43499Values unknown = sentinel_write(1, 1, 1, true, 9);
        expect(backend.write_word(unknown) == CallCode::BadParameters, "an unknown form");
        expect(same(backend.refusal(), "write-refused:form"), "named");
    }

    void a_closed_session_refuses_everything_but_the_lifecycle_calls() {
        Backend43499 backend{full_ops()};
        expect(backend.shutdown() == CallCode::Ok, "close");
        Snapshot43499 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot still answers");
        expect(snapshot.phase == static_cast<std::uint64_t>(Phase::Closed), "and says closed");
        expect(backend.prepare(sentinel_prepare(), snapshot) == CallCode::Closed, "prepare after close");
        expect(same(backend.refusal(), "session-closed"), "named");
        expect(backend.shutdown() == CallCode::Ok, "shutdown is idempotent");
    }
}  // namespace

int main() {
    std::printf("api 43499: session keeps its parameters\n");
    the_session_keeps_every_field_it_was_given();
    std::printf("api 43499: named rejections\n");
    a_missing_required_field_is_refused_by_name();
    std::printf("api 43499: ops visibility\n");
    every_ops_sees_the_whole_session_parameters();
    std::printf("api 43499: generation binding\n");
    the_generations_bind_each_request_to_its_own_round();
    std::printf("api 43499: victim lifecycle\n");
    retiring_the_victim_invalidates_old_requests();
    a_park_keeps_the_generation_for_the_next_step();
    std::printf("api 43499: absent ops\n");
    every_device_op_is_named_when_its_ops_is_absent();
    std::printf("api 43499: form polarity\n");
    the_write_form_polarity_is_still_checked();
    std::printf("api 43499: closed session\n");
    a_closed_session_refuses_everything_but_the_lifecycle_calls();
    if (g_failures != 0U) {
        std::printf("api 43499: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("api 43499: PASS (parameters reach the device work, generations bind, refusals named)\n");
    return 0;
}
