/** @file tests/api_43284_test.cpp
 *  43284 的函数面（D2 装配 2026-10-15）：ticket 不透明、写只到 Dispatched、read 比较后 Applied、
 *  REMOTE_BRIDGE 依赖已验证 PATCH1、块宽十六字节、数据缝（两张图与钩子计划）、未接线具名拒绝。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "jni/api_43284.hpp"
#include "jni/api_43284_device.hpp"

namespace {
    using namespace ghostlock::jni;

    std::size_t g_failures = 0U;

    void expect(const bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool same(const char *lhs, const char *rhs) { return std::strcmp(lhs, rhs) == 0; }

    struct Spy final {
        std::uint64_t writes = 0U;
    };

    Spy &spy() noexcept {
        static Spy instance;
        return instance;
    }

    bool device_resolve(const Init43284Values &, const ResolveTarget43284Values &, std::uint64_t &token,
                      void *) noexcept {
        token = 4242U;
        return true;
    }

    bool device_write(const Init43284Values &, const WriteBlock43284Values &, const std::uint64_t,
                    std::uint64_t &sequence, void *) noexcept {
        ++spy().writes;
        sequence += 1U;
        return true;
    }

    bool device_read(const Init43284Values &, const ReadBlock43284Values &, const std::uint64_t,
                   const std::span<std::byte> out, void *) noexcept {
        for (std::size_t index = 0U; index < out.size(); ++index) {
            out[index] = std::byte{0x5A};
        }
        return true;
    }

    DeviceOps43284 runtime_ops() noexcept {
        DeviceOps43284 ops{};
        ops.resolve = &device_resolve;
        ops.write_block = &device_write;
        ops.read_block = &device_read;
        return ops;
    }

    Init43284Values params() noexcept {
        Init43284Values values{};
        values.spi = 0x1234;
        values.encapPort = 4500;
        values.senderPort = 4501;
        values.icvLength = 16;
        return values;
    }

    ResolveTarget43284Values resolve_request(const TargetRole role) noexcept {
        ResolveTarget43284Values request{};
        request.role = static_cast<std::int32_t>(role);
        const char *const kPath = "/system/lib64/libc++.so";
        request.path.assign(kPath, kPath + 22);
        return request;
    }

    WriteBlock43284Values write_request(const std::uint64_t ticket) noexcept {
        WriteBlock43284Values request{};
        request.ticket = ticket;
        request.offset = 0x1000;
        request.block.assign(kBlockWidth, 0x5AU);
        return request;
    }

    void the_session_parameters_become_door_material() {
        Init43284Values full = params();
        std::uint8_t aes[32] = {};
        std::uint8_t hmac[32] = {};
        for (std::size_t index = 0U; index < 32U; ++index) {
            aes[index] = static_cast<std::uint8_t>(0xA0U + index);
            hmac[index] = static_cast<std::uint8_t>(0x50U + index);
        }
        std::memcpy(full.aesKey.data(), aes, 32U);
        std::memcpy(full.hmacKey.data(), hmac, 32U);
        const ghostlock::backend::cve_2026_43284::SaMaterial sa =
            ghostlock::jni::sa_material_of(full);
        expect(sa.set, "a real key pair makes material");
        expect(sa.spi == 0x1234U, "spi travels");
        expect(sa.encap_port == 4500U && sa.sender_port == 4501U, "both ports travel");
        expect(sa.icv_len == 16U, "the icv length travels");
        expect(sa.aes_key[0] == 0xA0U && sa.aes_key[31] == 0xBFU, "the aes key travels whole");
        expect(sa.hmac_key[0] == 0x50U && sa.hmac_key[31] == 0x6FU, "and so does the hmac key");

        const ghostlock::backend::cve_2026_43284::SaMaterial none =
            ghostlock::jni::sa_material_of(params());
        expect(!none.set, "all-zero keys are not material (no zero-key guessing)");
    }

    void the_shape_and_the_named_refusals() {
        Backend43284 backend{};
        Snapshot43284 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot answers");
        expect(snapshot.width == kBlockWidth, "block width is sixteen");
        expect(backend.initialize(Init43284Values{}) == CallCode::BadParameters, "empty params");
        expect(same(backend.refusal(), "bad-parameters:spi"), "named");
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        expect(backend.facts().handshake, "handshake recorded");
    }

    void a_ticket_is_opaque_and_the_write_stops_at_dispatched() {
        Backend43284 backend{runtime_ops()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::Patch1), ticket) == CallCode::Ok,
               "resolve");
        expect((ticket >> 32U) == static_cast<std::uint64_t>(TargetRole::Patch1),
               "ticket carries the slot index, not the door token");
        expect(backend.write_block(write_request(999U)) == CallCode::StaleTarget, "unknown ticket");
        expect(same(backend.refusal(), "stale-target"), "named");

        spy() = Spy{};
        expect(backend.write_block(write_request(ticket)) == CallCode::Ok, "write");
        expect(backend.facts().patch1.state == BlockState::Dispatched, "only dispatched");
        expect(spy().writes == 1U, "the ops saw it");

        ReadBlock43284Values read{};
        read.ticket = ticket;
        read.offset = 0x1000;
        ReadBlockOut43284 out{};
        expect(backend.read_block(read, out) == CallCode::Ok, "read back");
        expect(out.matches_last_write, "matches the write");
        expect(out.state == BlockState::Applied, "and becomes applied");
    }

    void the_remote_bridge_needs_a_verified_patch1() {
        Backend43284 backend{runtime_ops()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::RemoteBridge), ticket) ==
                   CallCode::NotReady,
               "remote bridge before patch1");
        expect(same(backend.refusal(), "not-ready:patch1-verified"), "named");
    }

    void the_block_width_is_enforced() {
        Backend43284 backend{runtime_ops()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::Carrier), ticket) == CallCode::Ok,
               "resolve");
        WriteBlock43284Values short_block = write_request(ticket);
        short_block.block.resize(8U);
        expect(backend.write_block(short_block) == CallCode::BadParameters, "eight bytes");
        expect(same(backend.refusal(), "bad-width"), "named");
    }

    void the_data_device_ops_serve_the_images_and_the_hook_plan() {
        Backend43284 backend{device_ops_43284()};
        std::span<const std::byte> image{};
        expect(backend.embedded_image(kImageSpliceHelper, image) == CallCode::Ok, "helper image");
        expect(image.size() == 1440U, "1440 bytes padded to the block");
        expect(backend.embedded_image(99, image) == CallCode::BadParameters, "unknown role");
        HookPlanRequest43284Values request{};
        request.hookImage.assign(16U, 0x11U);
        const char *const kSymbol = "splice";
        request.hookSymbol.assign(kSymbol, kSymbol + 6);
        HookPlanOut43284 plan{};
        expect(backend.hook_plan(request, plan) == CallCode::Ok, "hook plan");
        expect(plan.branch_word != 0U && plan.displaced_word != 0U, "both words");
        expect(!plan.payload.empty(), "payload");
    }

    void the_device_device_ops_are_named_when_absent() {
        Backend43284 backend{device_ops_43284()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        /* 门已经接线：这里用一条不存在的路径 ⇒ 开不了就具名拒绝（不是 unsupported）。 */
        ResolveTarget43284Values missing{};
        missing.role = static_cast<std::int32_t>(TargetRole::Patch1);
        missing.path.assign("/no/such/door", "/no/such/door" + 13);
        expect(backend.resolve_target(missing, ticket) == CallCode::Failed, "an unopenable door fails");
        expect(same(backend.refusal(), "door-refused"), "named");
        /* 触发已经接线（走上双 fork）：在没有 helper 的宿主上如实回答 Failed + trigger-refused。 */
        Trigger43284Values trigger{};
        trigger.delayMs = 0;
        /* 触发已接线：宿主上「fork 成功」与「fork 失败」都是诚实的结局，这里只断言它不再答 op-not-wired。 */
        const CallCode code = backend.trigger(trigger);
        expect(code != CallCode::Unsupported, "trigger is wired");
        expect(!same(backend.refusal(), "op-not-wired:trigger"), "and does not answer op-not-wired");
    }
}  // namespace

int main() {
    std::printf("api 43284: door material\n");
    the_session_parameters_become_door_material();
    std::printf("api 43284: shape and refusals\n");
    the_shape_and_the_named_refusals();
    std::printf("api 43284: tickets and the three states\n");
    a_ticket_is_opaque_and_the_write_stops_at_dispatched();
    std::printf("api 43284: remote bridge dependency\n");
    the_remote_bridge_needs_a_verified_patch1();
    std::printf("api 43284: block width\n");
    the_block_width_is_enforced();
    std::printf("api 43284: data ops\n");
    the_data_device_ops_serve_the_images_and_the_hook_plan();
    std::printf("api 43284: absent device ops\n");
    the_device_device_ops_are_named_when_absent();
    if (g_failures != 0U) {
        std::printf("api 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("api 43284: PASS (tickets, dispatched/applied, remote dependency, data ops)\n");
    return 0;
}
