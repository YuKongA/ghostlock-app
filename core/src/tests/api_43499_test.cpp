/** @file tests/api_43499_test.cpp
 *  43499 的函数面（用户裁定 2026-10-14：不用帧，用类型化函数）。
 *
 *  这里钉的是"规则"：相位与能力位怎么发布、缺参数与缺事实各自叫什么名字、落字形状的极性、
 *  以及关掉之后的行为。设备动作在缝后面，本测试用假的缝把规则单独跑一遍。
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
    using ghostlock::jni::Initialize43499;
    using ghostlock::jni::kFactDiscovered;
    using ghostlock::jni::kFactPrepared;
    using ghostlock::jni::kFactVictim;
    using ghostlock::jni::Ops43499Seams;
    using ghostlock::jni::Snapshot43499;
    using ghostlock::jni::WriteForm;
    using ghostlock::jni::WriteWord43499;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool same(const char *lhs, const char *rhs) { return std::strcmp(lhs, rhs) == 0; }

    /** 一套能把事实填满的假缝。 */
    bool seam_prepare(Facts43499 &facts, void *) noexcept {
        facts.page_base = 0x1000U;
        facts.word0 = 0x2000U;
        facts.word1 = 0x3000U;
        return true;
    }

    bool seam_discover(Facts43499 &facts, void *) noexcept {
        facts.found = 0x4000U;
        facts.alias_delta = 0x5000U;
        return true;
    }

    bool seam_victim(Facts43499 &facts, void *) noexcept {
        facts.victim_task = 0x6000U;
        facts.victim_pid = 0x1234U;
        return true;
    }

    bool seam_land(std::uint64_t address, std::uint64_t value, WriteForm form, std::uint64_t offset,
                   void *context) noexcept {
        auto *seen = static_cast<WriteWord43499 *>(context);
        if (seen != nullptr) {
            seen->address = address;
            seen->value = value;
            seen->form = form;
            seen->offset = offset;
        }
        return true;
    }

    Initialize43499 full_params() noexcept {
        Initialize43499 params{};
        params.route_id = 1U;
        params.waiter_off = 96U;
        params.buffer_size = 264U;
        params.task_offset = 48U;
        params.lock_offset = 56U;
        params.mm_struct_sz = 1024U;
        params.image_selinux_enforcing = 47885704U;
        return params;
    }

    void a_fresh_session_is_declared_and_says_so() {
        Backend43499 backend{};
        Snapshot43499 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot answers");
        expect(snapshot.phase == static_cast<std::uint64_t>(Phase::Declared), "declared phase");
        expect(snapshot.width == 8U, "the word width is eight");
        expect((snapshot.flags & ghostlock::jni::kCapPiWindow) != 0U, "the PI-window capability bit");
        expect((snapshot.flags & kFactPrepared) == 0U, "nothing is prepared yet");
    }

    void initialize_names_the_field_it_is_missing() {
        Backend43499 backend{};
        Initialize43499 empty{};
        expect(backend.initialize(empty) == CallCode::BadParameters, "an empty parameter set is refused");
        expect(same(backend.refusal(), "bad-parameters:route"), "and the refusal names the field");

        Initialize43499 no_lock = full_params();
        no_lock.lock_offset = 0U;
        expect(backend.initialize(no_lock) == CallCode::BadParameters, "a missing lock offset is refused");
        expect(same(backend.refusal(), "bad-parameters:lock_offset"), "with that field's name");

        expect(backend.initialize(full_params()) == CallCode::Ok, "a full set comes up");
        expect(backend.phase() == Phase::Ready, "and the phase is ready");
        expect(backend.refusal()[0] == '\0', "a success clears the refusal");
    }

    void every_device_op_is_named_when_its_seam_is_absent() {
        Backend43499 backend{};
        Snapshot43499 snapshot{};
        expect(backend.prepare(snapshot) == CallCode::Unsupported, "prepare is unsupported");
        expect(same(backend.refusal(), "op-not-wired:prepare"), "prepare says why");
        expect(backend.discover(snapshot) == CallCode::Unsupported, "discover is unsupported");
        expect(same(backend.refusal(), "op-not-wired:discover"), "discover says why");
        expect(backend.spawn_victim(snapshot) == CallCode::Unsupported, "spawn_victim is unsupported");
        expect(same(backend.refusal(), "op-not-wired:spawn_victim"), "spawn_victim says why");
        std::uint64_t value = 0U;
        expect(backend.victim_probe(0U, value) == CallCode::Unsupported, "victim_probe is unsupported");
        expect(same(backend.refusal(), "op-not-wired:victim_probe"), "victim_probe says why");
        WriteWord43499 request{};
        expect(backend.write_word(request) == CallCode::Unsupported, "write_word is unsupported");
        expect(same(backend.refusal(), "op-not-wired:write_word"), "write_word says why");
        expect(backend.read_word(0x1000U, value) == CallCode::Unsupported, "read_word is unsupported");
        expect(same(backend.refusal(), "op-not-wired:read_word"), "read_word says why");
    }

    void a_write_waits_for_the_facts_and_then_runs() {
        /* 缝在构造 backend 之前一次性给全：backend 持有的是拷贝，之后再改缝不会生效。 */
        Ops43499Seams seams{};
        WriteWord43499 request{};
        seams.prepare = &seam_prepare;
        seams.discover = &seam_discover;
        seams.spawn_victim = &seam_victim;
        seams.land = &seam_land;
        seams.context = &request;
        Backend43499 backend{seams};
        Snapshot43499 snapshot{};

        WriteWord43499 write{};
        write.address = 0x7000U;
        write.form = WriteForm::ClearPreserving;
        expect(backend.write_word(write) == CallCode::NotReady, "no facts, no write");
        expect(same(backend.refusal(), "not-ready:prepare"), "and the first missing fact is named");

        expect(backend.prepare(snapshot) == CallCode::Ok, "prepare runs");
        expect((snapshot.flags & kFactPrepared) != 0U, "and publishes the fact");
        expect(backend.write_word(write) == CallCode::NotReady, "the search is still missing");
        expect(same(backend.refusal(), "not-ready:discover"), "which is named");

        expect(backend.discover(snapshot) == CallCode::Ok, "discover runs");
        expect((snapshot.flags & kFactDiscovered) != 0U, "and publishes the fact");
        expect(backend.spawn_victim(snapshot) == CallCode::Ok, "the victim spawns");
        expect((snapshot.flags & kFactVictim) != 0U, "and publishes its fact");

        expect(backend.write_word(write) == CallCode::Ok, "now the write runs");
        expect(request.form == WriteForm::ClearPreserving, "the form reaches the seam unchanged");
        expect(request.address == 0x7000U, "so does the address");
    }

    void the_form_polarity_is_checked_not_guessed() {
        Ops43499Seams seams{};
        WriteWord43499 seen{};
        seams.land = &seam_land;
        seams.context = &seen;
        Backend43499 backend{seams};

        WriteWord43499 value_form{};
        value_form.form = WriteForm::ValuePreserving;
        value_form.value_given = false;
        expect(backend.write_word(value_form) == CallCode::BadParameters, "a value write without a value");
        expect(same(backend.refusal(), "write-refused:value-missing"), "is named");

        WriteWord43499 clear_form{};
        clear_form.form = WriteForm::ClearLeaf;
        clear_form.value = 0xDEADU;
        clear_form.value_given = true;
        expect(backend.write_word(clear_form) == CallCode::BadParameters, "a clear write with a value");
        expect(same(backend.refusal(), "write-refused:value-not-empty"), "is named");

        WriteWord43499 bad_form{};
        bad_form.form = WriteForm::Count;
        expect(backend.write_word(bad_form) == CallCode::BadParameters, "an unknown form");
        expect(same(backend.refusal(), "write-refused:form"), "is named");
    }

    void shutdown_is_idempotent_and_closes_the_phase() {
        Backend43499 backend{};
        expect(backend.shutdown() == CallCode::Ok, "the first shutdown answers ok");
        expect(backend.shutdown() == CallCode::Ok, "the second one too");
        Snapshot43499 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot still answers");
        expect(snapshot.phase == static_cast<std::uint64_t>(Phase::Closed), "the phase is closed");
    }
}  // namespace

int main() {
    std::printf("api 43499: fresh session\n");
    a_fresh_session_is_declared_and_says_so();
    std::printf("api 43499: initialize\n");
    initialize_names_the_field_it_is_missing();
    std::printf("api 43499: absent seams\n");
    every_device_op_is_named_when_its_seam_is_absent();
    std::printf("api 43499: facts gate the write\n");
    a_write_waits_for_the_facts_and_then_runs();
    std::printf("api 43499: form polarity\n");
    the_form_polarity_is_checked_not_guessed();
    std::printf("api 43499: shutdown\n");
    shutdown_is_idempotent_and_closes_the_phase();
    if (g_failures != 0U) {
        std::printf("api 43499: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("api 43499: PASS (typed functions, named refusals, facts gate)\n");
    return 0;
}
