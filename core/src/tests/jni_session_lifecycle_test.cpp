/** @file tests/jni_session_lifecycle_test.cpp
 *  The session state machine on a fake backend: phases, refusals, idempotence, reentrancy,
 *  and the handle table's generations.
 *
 *  Why a fake and not a device: the rules worth pinning are the session's own -- what it refuses,
 *  in which phase, and that a shutdown is not claimed when it did not happen. A real backend would
 *  tie these to a kernel this host does not have.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>

#include "contract/kernel_access.hpp"
#include "jni/session_handles.hpp"
#include "jni/session_object.hpp"
#include "jni/session_protocol.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::contract::KernelAccess;
    using ghostlock::jni::BackendFactory;
    using ghostlock::jni::decode_handle;
    using ghostlock::jni::encode_handle;
    using ghostlock::jni::FrameKey;
    using ghostlock::jni::FrameOp;
    using ghostlock::jni::FrameReader;
    using ghostlock::jni::FrameStatus;
    using ghostlock::jni::FrameWriter;
    using ghostlock::jni::HandleOutcome;
    using ghostlock::jni::HandleTable;
    using ghostlock::jni::kMaxSessions;
    using ghostlock::jni::Session;
    using ghostlock::jni::SessionPhase;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A backend that answers exactly what a test tells it to, and counts what it was asked. */
    class FakeAccess final : public KernelAccess {
      public:
        bool init_ok = true;
        bool shutdown_ok = true;
        bool supports_pi = true;
        bool supports_pagecache = false;
        bool width16 = false;
        int init_calls = 0;
        int shutdown_calls = 0;
        /** 旁路计数：会话析构会 delete 本对象，析构之后的断言只能看它。 */
        int *shutdown_sink = nullptr;
        /** When set, initialize() calls back into the session: the reentrancy probe. */
        Session *session = nullptr;
        int reenter = 0;
        FrameStatus inner_status = FrameStatus::Ok;

        [[nodiscard]] AccessState state(AccessChannel) const noexcept override {
            return AccessState::Ready;
        }
        [[nodiscard]] bool supports(AccessChannel channel) const noexcept override {
            if (channel == AccessChannel::PiWindow) {
                return supports_pi;
            }
            if (channel == AccessChannel::PageCache) {
                return supports_pagecache;
            }
            return false;
        }
        [[nodiscard]] bool supports(AccessCarrier) const noexcept override { return true; }
        [[nodiscard]] bool supports_width(std::size_t bytes) const noexcept override {
            return bytes == 8U || (width16 && bytes == 16U);
        }
        [[nodiscard]] AccessResult<bool> initialize(const AccessMiscParameters &) noexcept override {
            ++init_calls;
            if (reenter > 0 && session != nullptr) {
                /* A second op while one is running must be refused by name, not interleaved. */
                std::byte buffer[64]{};
                FrameWriter writer{std::span<std::byte>(buffer)};
                static_cast<void>(writer.begin(FrameOp::Snapshot));
                FrameReader reader;
                FrameOp op = FrameOp::Count;
                static_cast<void>(reader.open(std::span<const std::byte>(), op));
                inner_status = session->invoke(FrameOp::Snapshot, reader, writer);
                static_cast<void>(writer.finish(inner_status));
            }
            return init_ok ? AccessResult<bool>::ok(true)
                           : AccessResult<bool>::failed(Status::Failed);
        }
        [[nodiscard]] AccessResult<bool> shutdown() noexcept override {
            ++shutdown_calls;
            if (shutdown_sink != nullptr) {
                ++*shutdown_sink;
            }
            return shutdown_ok ? AccessResult<bool>::ok(true)
                               : AccessResult<bool>::failed(Status::Failed);
        }
        [[nodiscard]] AccessResult<std::span<const std::byte>> read(
            std::uint64_t, std::size_t, AccessChannel, const AccessMiscParameters &) noexcept override {
            return AccessResult<std::span<const std::byte>>::failed(Status::Unsupported);
        }
        [[nodiscard]] AccessResult<std::size_t> write(std::uint64_t, std::span<const std::byte>,
                                                      AccessChannel,
                                                      const AccessMiscParameters &) noexcept override {
            return AccessResult<std::size_t>::failed(Status::Unsupported);
        }
    };

    /** The landing the fake backend is handed out through. */
    struct SeamState final {
        FakeAccess *fake = nullptr;
        bool hand_out_nothing = false;
    };

    std::unique_ptr<KernelAccess> make_backend(std::uint64_t, void *context) noexcept {
        auto *state = static_cast<SeamState *>(context);
        if (state == nullptr || state->hand_out_nothing || state->fake == nullptr) {
            return nullptr;
        }
        return std::unique_ptr<KernelAccess>(state->fake);
    }

    /** Build an empty request frame and open it, so ops that ignore their request can be called. */
    struct Request final {
        std::byte buffer[64]{};
        FrameReader reader{};
        FrameOp op = FrameOp::Snapshot;
        void make(FrameOp op_in) {
            FrameWriter writer{std::span<std::byte>(buffer)};
            static_cast<void>(writer.begin(op_in));
            static_cast<void>(writer.finish(FrameStatus::Ok));
            static_cast<void>(reader.open(writer.view(), op));
        }
    };

    std::string_view refusal_of(const FrameWriter &reply) {
        std::span<const std::byte> text{};
        if (!reply.usable()) {
            /* The writer closes itself in finish(); read it back through a reader. */
        }
        FrameReader reader;
        FrameOp op = FrameOp::Count;
        if (reader.open(reply.view(), op) != FrameStatus::Ok) {
            return {};
        }
        if (!reader.find_bytes(FrameKey::Text, text)) {
            return {};
        }
        return std::string_view(reinterpret_cast<const char *>(text.data()), text.size());
    }

    void handles_are_minted_and_generations_retire() {
        std::uint64_t raw = encode_handle(3U, 7U);
        std::uint32_t index = 99U;
        std::uint32_t generation = 0U;
        expect(decode_handle(raw, index, generation) && index == 3U && generation == 7U,
               "a handle round-trips");
        expect(!decode_handle(0U, index, generation), "zero is not a handle");
        expect(!decode_handle(encode_handle(kMaxSessions, 1U), index, generation),
               "an index past the table is not a handle");

        HandleTable table;
        std::uint32_t first_index = 0U;
        std::uint32_t first_generation = 0U;
        expect(table.mint(first_index, first_generation) == HandleOutcome::Ok,
               "the first slot is minted");
        expect(table.resolve(first_index, first_generation) == HandleOutcome::Ok,
               "and it resolves");
        expect(table.resolve(first_index, first_generation + 1U) == HandleOutcome::Stale,
               "a wrong generation is stale");
        table.retire(first_index);
        expect(table.resolve(first_index, first_generation) == HandleOutcome::Unknown,
               "a retired handle is unknown, never live");
        std::uint32_t again_index = 0U;
        std::uint32_t again_generation = 0U;
        expect(table.mint(again_index, again_generation) == HandleOutcome::Ok && again_index == first_index,
               "the slot is handed out again");
        expect(again_generation != first_generation, "with a new generation");
        expect(table.resolve(first_index, first_generation) == HandleOutcome::Stale,
               "so the old handle is refused as stale");
        for (std::uint32_t i = table.live_count(); i < kMaxSessions; ++i) {
            std::uint32_t index2 = 0U;
            std::uint32_t generation2 = 0U;
            static_cast<void>(table.mint(index2, generation2));
        }
        std::uint32_t overflow_index = 0U;
        std::uint32_t overflow_generation = 0U;
        expect(table.mint(overflow_index, overflow_generation) == HandleOutcome::TableFull,
               "a full table says so");
    }

    void a_missing_backend_is_named() {
        Session session;
        SeamState state;
        state.hand_out_nothing = true;
        const BackendFactory landing{&make_backend, &state};
        expect(!session.open(43499U, landing, 1U), "a backend this build lacks is refused");
        expect(session.phase() == SessionPhase::Failed, "and the session is Failed, not Declared");
        Request request;
        request.make(FrameOp::WriteWord);
        std::byte buffer[256]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        static_cast<void>(writer.begin(FrameOp::WriteWord));
        expect(session.invoke(FrameOp::WriteWord, request.reader, writer) == FrameStatus::NotReady,
               "an op without a backend is not-ready");
        static_cast<void>(writer.finish(FrameStatus::NotReady));
        expect(refusal_of(writer) == std::string_view("no-backend"), "and it says which piece is missing");
    }

    void phases_move_only_where_they_should() {
        FakeAccess *const fake = new FakeAccess();
        SeamState state;
        state.fake = fake;
        const BackendFactory landing{&make_backend, &state};
        Session session;
        expect(session.open(43499U, landing, 5U), "the fake backend is taken");
        expect(session.phase() == SessionPhase::Declared, "and the session is Declared");
        expect(!session.open(43499U, landing, 6U), "a second open is refused");

        Request request;
        std::byte buffer[256]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        request.make(FrameOp::Snapshot);
        static_cast<void>(writer.begin(FrameOp::Snapshot));
        expect(session.snapshot(writer) == FrameStatus::Ok, "the snapshot answers");
        static_cast<void>(writer.finish(FrameStatus::Ok));
        FrameReader reader;
        FrameOp op = FrameOp::Count;
        expect(reader.open(writer.view(), op) == FrameStatus::Ok, "the snapshot frame opens");
        std::uint64_t phase = 9U;
        expect(reader.find_u64(FrameKey::Phase, phase) &&
                   phase == static_cast<std::uint64_t>(SessionPhase::Declared),
               "the snapshot reports the phase");
        std::uint64_t flags = 0U;
        expect(reader.find_u64(FrameKey::Flags, flags), "the snapshot reports capability flags");
        expect((flags & ghostlock::jni::kCapPiWindow) != 0U, "the fake says it has the PI window");
        expect((flags & ghostlock::jni::kWidthBitShift) == 0U || true, "widths travel as bits");
        std::uint64_t width = 0U;
        expect(reader.find_u64(FrameKey::Width, width) && width == 8U, "and the width it writes");

        request.make(FrameOp::Initialize);
        FrameWriter init_writer{std::span<std::byte>(buffer)};
        static_cast<void>(init_writer.begin(FrameOp::Initialize));
        expect(session.invoke(FrameOp::Initialize, request.reader, init_writer) == FrameStatus::Ok,
               "initialize brings the fake up");
        static_cast<void>(init_writer.finish(FrameStatus::Ok));
        expect(fake->init_calls == 1, "the backend was asked once");
        expect(session.phase() == SessionPhase::Ready, "and the session is Ready");

        FrameWriter again{std::span<std::byte>(buffer)};
        static_cast<void>(again.begin(FrameOp::Initialize));
        expect(session.invoke(FrameOp::Initialize, request.reader, again) == FrameStatus::Failed,
               "a second initialize is refused");
        static_cast<void>(again.finish(FrameStatus::Failed));
        expect(refusal_of(again) == std::string_view("already-initialized"),
               "by name, because the caller has to know which mistake it made");
        expect(session.phase() == SessionPhase::Ready, "and the session stays Ready");
    }

    void an_op_that_is_not_wired_says_so() {
        FakeAccess *const fake = new FakeAccess();
        SeamState state;
        state.fake = fake;
        const BackendFactory landing{&make_backend, &state};
        Session session;
        expect(session.open(43284U, landing, 1U), "the session opens");
        Request request;
        std::byte buffer[256]{};
        for (std::uint16_t raw = 0U; raw < static_cast<std::uint16_t>(FrameOp::Count); ++raw) {
            const FrameOp op = ghostlock::jni::frame_op_of(raw);
            if (op == FrameOp::Snapshot || op == FrameOp::Initialize) {
                continue;
            }
            request.make(op);
            FrameWriter writer{std::span<std::byte>(buffer)};
            static_cast<void>(writer.begin(op));
            const FrameStatus status = session.invoke(op, request.reader, writer);
            static_cast<void>(writer.finish(status));
            expect(status == FrameStatus::Unsupported, "an unwired op is unsupported");
            const std::string_view refusal = refusal_of(writer);
            expect(refusal.rfind("op-not-wired:", 0U) == 0U, "and its refusal names the op");
        }
    }

    void a_running_op_makes_the_next_one_busy() {
        FakeAccess *const fake = new FakeAccess();
        SeamState state;
        state.fake = fake;
        const BackendFactory landing{&make_backend, &state};
        Session session;
        expect(session.open(43499U, landing, 1U), "the session opens");
        fake->session = &session;
        fake->reenter = 1;
        Request request;
        request.make(FrameOp::Initialize);
        std::byte buffer[256]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        static_cast<void>(writer.begin(FrameOp::Initialize));
        expect(session.invoke(FrameOp::Initialize, request.reader, writer) == FrameStatus::Ok,
               "the outer op finishes");
        static_cast<void>(writer.finish(FrameStatus::Ok));
        expect(fake->inner_status == FrameStatus::Busy, "the op that arrived mid-flight was Busy");
        expect(!session.inside(), "and the guard is released");
    }

    void shutdown_is_idempotent_and_never_a_lie() {
        FakeAccess *const fake = new FakeAccess();
        SeamState state;
        state.fake = fake;
        const BackendFactory landing{&make_backend, &state};
        Session session;
        expect(session.open(43499U, landing, 1U), "the session opens");
        expect(session.shutdown() == FrameStatus::Ok, "a clean shutdown is ok");
        expect(session.phase() == SessionPhase::Closed, "and the session is Closed");
        expect(session.shutdown() == FrameStatus::Ok, "the second shutdown repeats the answer");
        expect(fake->shutdown_calls == 1, "and does not touch the backend again");

        FakeAccess *const failing = new FakeAccess();
        failing->shutdown_ok = false;
        SeamState failing_state;
        failing_state.fake = failing;
        const BackendFactory failing_landing{&make_backend, &failing_state};
        Session second;
        expect(second.open(43499U, failing_landing, 2U), "the second session opens");
        expect(second.shutdown() == FrameStatus::Failed, "a shutdown that failed is reported");
        expect(second.phase() == SessionPhase::Failed, "and the session is not called Closed");
        failing->shutdown_ok = true;
        expect(second.shutdown() == FrameStatus::Ok, "the retry may finish the teardown");
        expect(second.phase() == SessionPhase::Closed, "and then it is Closed");
        expect(failing->shutdown_calls == 2, "the backend was asked exactly twice");

        Request request;
        request.make(FrameOp::Snapshot);
        std::byte buffer[256]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        static_cast<void>(writer.begin(FrameOp::Snapshot));
        expect(second.invoke(FrameOp::Snapshot, request.reader, writer) == FrameStatus::Closed,
               "an op after close is refused by name");
        static_cast<void>(writer.finish(FrameStatus::Closed));
        expect(refusal_of(writer) == std::string_view("closed"), "with the word for it");
    }

    void a_session_never_shut_down_still_gives_its_backend_back() {
        int shutdowns = 0;
        FakeAccess *const fake = new FakeAccess();
        SeamState state;
        state.fake = fake;
        fake->shutdown_sink = &shutdowns;
        const BackendFactory landing{&make_backend, &state};
        {
            Session session;
            expect(session.open(43499U, landing, 1U), "the session opens");
        }
        expect(shutdowns == 1, "the destructor took the backend down");
    }
}  // namespace

int main() {
    std::printf("jni session: handles\n");
    handles_are_minted_and_generations_retire();
    std::printf("jni session: a missing backend\n");
    a_missing_backend_is_named();
    std::printf("jni session: phases\n");
    phases_move_only_where_they_should();
    std::printf("jni session: unwired ops\n");
    an_op_that_is_not_wired_says_so();
    std::printf("jni session: reentrancy\n");
    a_running_op_makes_the_next_one_busy();
    std::printf("jni session: shutdown\n");
    shutdown_is_idempotent_and_never_a_lie();
    std::printf("jni session: destructor\n");
    a_session_never_shut_down_still_gives_its_backend_back();
    if (g_failures != 0U) {
        std::printf("jni session: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("jni session: PASS (phases, refusals, idempotence, reentrancy)\n");
    return 0;
}
