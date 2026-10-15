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

    bool seam_resolve(const Init43284Values &, const ResolveTarget43284Values &, std::uint64_t &token,
                      void *) noexcept {
        token = 4242U;
        return true;
    }

    bool seam_write(const Init43284Values &, const WriteBlock43284Values &, const std::uint64_t,
                    std::uint64_t &sequence, void *) noexcept {
        ++spy().writes;
        sequence += 1U;
        return true;
    }

    bool seam_read(const Init43284Values &, const ReadBlock43284Values &, const std::uint64_t,
                   const std::span<std::byte> out, void *) noexcept {
        for (std::size_t index = 0U; index < out.size(); ++index) {
            out[index] = std::byte{0x5A};
        }
        return true;
    }

    Ops43284Seams runtime_seams() noexcept {
        Ops43284Seams seams{};
        seams.resolve = &seam_resolve;
        seams.write_block = &seam_write;
        seams.read_block = &seam_read;
        return seams;
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
        Backend43284 backend{runtime_seams()};
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
        expect(spy().writes == 1U, "the seam saw it");

        ReadBlock43284Values read{};
        read.ticket = ticket;
        read.offset = 0x1000;
        ReadBlockOut43284 out{};
        expect(backend.read_block(read, out) == CallCode::Ok, "read back");
        expect(out.matches_last_write, "matches the write");
        expect(out.state == BlockState::Applied, "and becomes applied");
    }

    void the_remote_bridge_needs_a_verified_patch1() {
        Backend43284 backend{runtime_seams()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::RemoteBridge), ticket) ==
                   CallCode::NotReady,
               "remote bridge before patch1");
        expect(same(backend.refusal(), "not-ready:patch1-verified"), "named");
    }

    void the_block_width_is_enforced() {
        Backend43284 backend{runtime_seams()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::Carrier), ticket) == CallCode::Ok,
               "resolve");
        WriteBlock43284Values short_block = write_request(ticket);
        short_block.block.resize(8U);
        expect(backend.write_block(short_block) == CallCode::BadParameters, "eight bytes");
        expect(same(backend.refusal(), "bad-width"), "named");
    }

    void the_data_seams_serve_the_images_and_the_hook_plan() {
        Backend43284 backend{device_seams_43284()};
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

    void the_device_seams_are_named_when_absent() {
        Backend43284 backend{device_seams_43284()};
        expect(backend.initialize(params()) == CallCode::Ok, "initialize");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(resolve_request(TargetRole::Patch1), ticket) ==
                   CallCode::Unsupported,
               "resolve is not wired");
        expect(same(backend.refusal(), "op-not-wired:resolve_target"), "named");
        expect(backend.trigger(Trigger43284Values{}) == CallCode::Unsupported, "trigger");
        expect(same(backend.refusal(), "op-not-wired:trigger"), "named");
    }
}  // namespace

int main() {
    std::printf("api 43284: shape and refusals\n");
    the_shape_and_the_named_refusals();
    std::printf("api 43284: tickets and the three states\n");
    a_ticket_is_opaque_and_the_write_stops_at_dispatched();
    std::printf("api 43284: remote bridge dependency\n");
    the_remote_bridge_needs_a_verified_patch1();
    std::printf("api 43284: block width\n");
    the_block_width_is_enforced();
    std::printf("api 43284: data seams\n");
    the_data_seams_serve_the_images_and_the_hook_plan();
    std::printf("api 43284: absent device seams\n");
    the_device_seams_are_named_when_absent();
    if (g_failures != 0U) {
        std::printf("api 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("api 43284: PASS (tickets, dispatched/applied, remote dependency, data seams)\n");
    return 0;
}
