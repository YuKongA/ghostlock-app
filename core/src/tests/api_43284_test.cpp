/** @file tests/api_43284_test.cpp
 *  43284 的函数面（用户裁定 2026-10-14：不用帧，用类型化函数）。
 *
 *  钉住的判据：十六字节块宽、token 必须是本会话解析出来的、三种终态各自有名、未接线的缝具名拒绝。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "jni/api_43284.hpp"

namespace {
    using ghostlock::jni::api::Phase;
    using ghostlock::jni::Backend43284;
    using ghostlock::jni::CallCode;
    using ghostlock::jni::HookWords43284;
    using ghostlock::jni::Initialize43284;
    using ghostlock::jni::kBlockWidth;
    using ghostlock::jni::kFactResolved;
    using ghostlock::jni::Ops43284Seams;
    using ghostlock::jni::Snapshot43284;
    using ghostlock::jni::TargetRole;
    using ghostlock::jni::TerminalState;
    using ghostlock::jni::WaitTerminal43284;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool same(const char *lhs, const char *rhs) { return std::strcmp(lhs, rhs) == 0; }

    std::uint64_t g_last_token = 0U;
    std::uint64_t g_last_offset = 0U;
    std::size_t g_last_block = 0U;
    std::uint64_t g_last_delay = 0U;
    TerminalState g_next_terminal = TerminalState::Success;

    bool seam_resolve(std::uint64_t role, std::string_view path, std::uint64_t &token,
                      void *) noexcept {
        if (path.empty() || role == 0U) {
            return false;
        }
        token = 42U;
        return true;
    }

    bool seam_write(std::uint64_t token, std::uint64_t offset, std::span<const std::byte> block,
                    std::uint64_t &sequence, void *) noexcept {
        g_last_token = token;
        g_last_offset = offset;
        g_last_block = block.size();
        sequence += 1U;
        return true;
    }

    bool seam_read(std::uint64_t, std::uint64_t, std::span<std::byte> block, void *) noexcept {
        for (std::size_t index = 0U; index < block.size(); ++index) {
            block[index] = std::byte{0x5A};
        }
        return true;
    }

    bool seam_trigger(std::uint64_t delay_ms, void *) noexcept {
        g_last_delay = delay_ms;
        return true;
    }

    bool seam_wait(std::uint64_t, std::uint64_t, std::string_view, TerminalState &terminal,
                   void *) noexcept {
        terminal = g_next_terminal;
        return true;
    }

    bool seam_image(std::uint64_t role, std::span<const std::byte> &image, void *) noexcept {
        static std::byte bytes[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        if (role != 0U) {
            return false;
        }
        image = std::span<const std::byte>(bytes, sizeof(bytes));
        return true;
    }

    bool seam_hook(std::uint64_t &branch, std::uint64_t &displaced, std::uint64_t &site,
                   std::uint64_t &landing, std::span<const std::byte> &payload, void *) noexcept {
        static std::byte bytes[16] = {};
        branch = 0x14012d83U;
        displaced = 0xf9400028U;
        site = 733356U;
        landing = 1041960U;
        payload = std::span<const std::byte>(bytes, sizeof(bytes));
        return true;
    }

    Ops43284Seams full_seams() noexcept {
        Ops43284Seams seams{};
        seams.resolve = &seam_resolve;
        seams.write_block = &seam_write;
        seams.read_block = &seam_read;
        seams.trigger = &seam_trigger;
        seams.wait_terminal = &seam_wait;
        seams.embedded_image = &seam_image;
        seams.hook_words = &seam_hook;
        return seams;
    }

    Initialize43284 full_params() noexcept {
        Initialize43284 params{};
        params.trigger_delay_ms = 150U;
        params.wait_budget_ms = 800U;
        params.wait_poll_ms = 10U;
        return params;
    }

    void a_fresh_session_reports_the_page_cache_shape() {
        Backend43284 backend{};
        Snapshot43284 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot answers");
        expect(snapshot.phase == static_cast<std::uint64_t>(Phase::Declared), "declared phase");
        expect(snapshot.width == kBlockWidth, "the block width is sixteen");
        expect((snapshot.flags & ghostlock::jni::kCapPageCache) != 0U, "the page-cache capability bit");
        expect((snapshot.flags & kFactResolved) == 0U, "nothing is resolved yet");
    }

    void initialize_names_the_field_it_is_missing() {
        Backend43284 backend{};
        expect(backend.initialize(Initialize43284{}) == CallCode::BadParameters, "an empty set is refused");
        expect(same(backend.refusal(), "bad-parameters:trigger_delay_ms"), "first field named");
        expect(backend.initialize(full_params()) == CallCode::Ok, "a full set comes up");
        expect(backend.phase() == Phase::Ready, "and the phase is ready");
    }

    void every_device_function_is_named_when_its_seam_is_absent() {
        Backend43284 backend{};
        std::uint64_t token = 0U;
        expect(backend.resolve_target(TargetRole::Patch1, "/system/lib64/x.so", token) ==
                   CallCode::Unsupported,
               "resolve is unsupported");
        expect(same(backend.refusal(), "op-not-wired:resolve_target"), "resolve says why");
        std::byte block[kBlockWidth] = {};
        expect(backend.write_block(1U, 0U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::Unsupported,
               "write_block is unsupported");
        expect(same(backend.refusal(), "op-not-wired:write_block"), "write_block says why");
        expect(backend.read_block(1U, 0U, std::span<std::byte>(block, kBlockWidth)) ==
                   CallCode::Unsupported,
               "read_block is unsupported");
        expect(same(backend.refusal(), "op-not-wired:read_block"), "read_block says why");
        expect(backend.trigger(150U) == CallCode::Unsupported, "trigger is unsupported");
        expect(same(backend.refusal(), "op-not-wired:trigger"), "trigger says why");
        WaitTerminal43284 terminal{};
        expect(backend.wait_terminal(800U, 10U, "ghostlock", terminal) == CallCode::Unsupported,
               "wait_terminal is unsupported");
        expect(same(backend.refusal(), "op-not-wired:wait_terminal"), "wait_terminal says why");
        std::span<const std::byte> image{};
        expect(backend.embedded_image(0U, image) == CallCode::Unsupported, "image is unsupported");
        expect(same(backend.refusal(), "op-not-wired:embedded_image"), "image says why");
        HookWords43284 words{};
        expect(backend.hook_words(words) == CallCode::Unsupported, "hook_words is unsupported");
        expect(same(backend.refusal(), "op-not-wired:hook_words"), "hook_words says why");
    }

    void the_block_width_and_the_token_are_both_checked() {
        Backend43284 backend{full_seams()};
        std::byte short_block[8] = {};
        expect(backend.write_block(1U, 0U, std::span<const std::byte>(short_block, 8U)) ==
                   CallCode::BadParameters,
               "a short block is refused");
        expect(same(backend.refusal(), "bad-width"), "by name");

        std::byte block[kBlockWidth] = {};
        expect(backend.write_block(1U, 0U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::NotReady,
               "no target resolved yet");
        expect(same(backend.refusal(), "not-ready:resolve-target"), "which is named");

        std::uint64_t token = 0U;
        expect(backend.resolve_target(TargetRole::Hook, "/system/lib64/libc++.so", token) == CallCode::Ok,
               "the door resolves");
        expect((token & 0xFFFFFFFFU) == 42U, "and answers a ticket carrying the door token");
        expect((token >> 32U) != 0U, "whose high half is this session salt");

        expect(backend.write_block(7U, 0U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::StaleTarget,
               "a ticket this session never handed out is refused");

        /* 跨会话：另一个会话即使拿到同样的门 token，它的票据在这里也过不了。 */
        Backend43284 other{full_seams()};
        std::uint64_t other_ticket = 0U;
        expect(other.resolve_target(TargetRole::Hook, "/system/lib64/libc++.so", other_ticket) ==
                   CallCode::Ok,
               "the second session resolves too");
        expect((other_ticket & 0xFFFFFFFFU) == (token & 0xFFFFFFFFU),
               "both doors answer the same raw token");
        expect(other_ticket != token, "but their tickets differ");
        expect(backend.write_block(other_ticket, 0U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::StaleTarget,
               "and the other session ticket is refused here");
        expect(same(backend.refusal(), "stale-target"), "by name");

        expect(backend.write_block(token, 0x100U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::Ok,
               "the right token writes");
        expect(g_last_token == 42U && g_last_offset == 0x100U && g_last_block == kBlockWidth,
               "and the seam saw all three");
    }

    void the_three_terminal_states_are_distinguished() {
        Backend43284 backend{full_seams()};
        WaitTerminal43284 out{};
        g_next_terminal = TerminalState::InFlight;
        expect(backend.wait_terminal(800U, 10U, "ghostlock", out) == CallCode::InFlight,
               "still running is not a failure");
        expect(same(backend.refusal(), "wait_terminal:inflight"), "and says so by name");
        g_next_terminal = TerminalState::Success;
        expect(backend.wait_terminal(800U, 10U, "ghostlock", out) == CallCode::Ok, "success is ok");
        expect(out.terminal == TerminalState::Success, "and the state is published");
        g_next_terminal = TerminalState::Failure;
        expect(backend.wait_terminal(800U, 10U, "ghostlock", out) == CallCode::Failed,
               "failure is a failure");
        expect(out.terminal == TerminalState::Failure, "with its own name");
    }

    void the_images_and_the_hook_numbers_come_back_typed() {
        Backend43284 backend{full_seams()};
        std::span<const std::byte> image{};
        expect(backend.embedded_image(2U, image) == CallCode::BadParameters, "an unknown role");
        expect(same(backend.refusal(), "image-role-unknown"), "is named");
        expect(backend.embedded_image(0U, image) == CallCode::Ok, "role 0 is served");
        expect(image.size() == 4U, "with its bytes");

        HookWords43284 words{};
        expect(backend.hook_words(words) == CallCode::Ok, "the hook plan answers");
        expect(words.branch_word == 0x14012d83U, "branch word");
        expect(words.displaced_word == 0xf9400028U, "displaced word");
        expect(words.site_offset == 733356U && words.landing_offset == 1041960U, "both offsets");
        expect(words.payload.size() == 16U, "and the payload");
    }

    void a_closed_session_refuses_everything_but_the_lifecycle_calls() {
        Backend43284 backend{full_seams()};
        expect(backend.shutdown() == CallCode::Ok, "close");
        Snapshot43284 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot still answers after close");
        std::uint64_t ticket = 0U;
        expect(backend.resolve_target(TargetRole::Hook, "/system/lib64/libc++.so", ticket) ==
                   CallCode::Closed,
               "resolve after close is refused");
        expect(same(backend.refusal(), "session-closed"), "by name");
        std::byte block[kBlockWidth] = {};
        expect(backend.write_block(1U, 0U, std::span<const std::byte>(block, kBlockWidth)) ==
                   CallCode::Closed,
               "write after close is refused");
        expect(backend.trigger(150U) == CallCode::Closed, "trigger after close is refused");
    }

    void shutdown_is_idempotent() {
        Backend43284 backend{};
        expect(backend.shutdown() == CallCode::Ok, "first shutdown");
        expect(backend.shutdown() == CallCode::Ok, "second shutdown");
        Snapshot43284 snapshot{};
        expect(backend.snapshot(snapshot) == CallCode::Ok, "snapshot answers");
        expect(snapshot.phase == static_cast<std::uint64_t>(Phase::Closed), "phase is closed");
    }
}  // namespace

int main() {
    std::printf("api 43284: fresh session\n");
    a_fresh_session_reports_the_page_cache_shape();
    std::printf("api 43284: initialize\n");
    initialize_names_the_field_it_is_missing();
    std::printf("api 43284: absent seams\n");
    every_device_function_is_named_when_its_seam_is_absent();
    std::printf("api 43284: block width and token\n");
    the_block_width_and_the_token_are_both_checked();
    std::printf("api 43284: terminal states\n");
    the_three_terminal_states_are_distinguished();
    std::printf("api 43284: images and hook\n");
    the_images_and_the_hook_numbers_come_back_typed();
    std::printf("api 43284: closed session\n");
    a_closed_session_refuses_everything_but_the_lifecycle_calls();
    std::printf("api 43284: shutdown\n");
    shutdown_is_idempotent();
    if (g_failures != 0U) {
        std::printf("api 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("api 43284: PASS (typed functions, named refusals, three terminal states)\n");
    return 0;
}
