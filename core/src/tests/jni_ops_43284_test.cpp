/** @file tests/jni_ops_43284_test.cpp
 *  43284's op layer on the host: the sixteen-byte unit, the token, the terminal's three answers,
 *  and what the device ops are called with.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>

#include "jni/ops_43284.hpp"
#include "jni/session_object.hpp"
#include "jni/session_protocol.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::jni::BackendOps;
    using ghostlock::jni::FrameKey;
    using ghostlock::jni::FrameOp;
    using ghostlock::jni::FrameReader;
    using ghostlock::jni::FrameStatus;
    using ghostlock::jni::FrameWriter;
    using ghostlock::jni::kBlockWidth;
    using ghostlock::jni::Ops43284;
    using ghostlock::jni::Ops43284Seams;
    using ghostlock::jni::TerminalState;
    using ghostlock::jni::terminal_state_name;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A request frame built from u64 entries plus optional byte entries. */
    struct Request final {
        std::byte buffer[512]{};
        std::size_t written = 0U;
        FrameReader reader{};
        FrameOp op = FrameOp::Snapshot;

        bool open(FrameOp wanted, std::initializer_list<std::pair<FrameKey, std::uint64_t>> values) {
            FrameWriter writer{std::span<std::byte>(buffer)};
            if (!writer.begin(wanted)) {
                return false;
            }
            for (const auto &pair : values) {
                if (!writer.put_u64(pair.first, pair.second)) {
                    return false;
                }
            }
            if (!writer.finish(FrameStatus::Ok)) {
                return false;
            }
            written = writer.size();
            return reader.open(std::span<const std::byte>(buffer, written), op) == FrameStatus::Ok;
        }

        bool add_bytes(FrameKey key, std::string_view text) {
            FrameWriter writer{std::span<std::byte>(buffer)};
            if (!writer.begin(op)) {
                return false;
            }
            return writer.put_bytes(key, std::span<const std::byte>(
                                             reinterpret_cast<const std::byte *>(text.data()),
                                             text.size()));
        }
    };

    /** Build a request with both scalars and one byte entry. */
    struct Builder final {
        std::byte buffer[512]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        FrameOp op = FrameOp::Snapshot;
        FrameReader reader{};
        std::size_t written = 0U;

        Builder() noexcept = default;
        Builder(const Builder &) = delete;
        Builder &operator=(const Builder &) = delete;

        bool build(FrameOp wanted, std::initializer_list<std::pair<FrameKey, std::uint64_t>> values,
                   FrameKey bytes_key, std::span<const std::byte> bytes) {
            if (!writer.begin(wanted)) {
                return false;
            }
            for (const auto &pair : values) {
                if (!writer.put_u64(pair.first, pair.second)) {
                    return false;
                }
            }
            if (!bytes.empty() && !writer.put_bytes(bytes_key, bytes)) {
                return false;
            }
            if (!writer.finish(FrameStatus::Ok)) {
                return false;
            }
            written = writer.size();
            op = wanted;
            return reader.open(std::span<const std::byte>(buffer, written), op) == FrameStatus::Ok;
        }
    };

    /** One answer frame: reset in place (a FrameWriter holds a span to its own buffer). */
    struct Answer final {
        std::byte buffer[1024]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        FrameStatus status = FrameStatus::Failed;

        Answer() noexcept = default;
        Answer(const Answer &) = delete;
        Answer &operator=(const Answer &) = delete;

        void reset() noexcept {
            for (std::size_t i = 0U; i < sizeof(buffer); ++i) {
                buffer[i] = std::byte{0};
            }
            writer = FrameWriter{std::span<std::byte>(buffer)};
            status = FrameStatus::Failed;
        }

        [[nodiscard]] FrameReader reopen() const {
            FrameReader reader;
            FrameOp op = FrameOp::Count;
            static_cast<void>(reader.open(writer.view(), op));
            return reader;
        }

        [[nodiscard]] std::string_view refusal() const {
            FrameReader reader = reopen();
            std::span<const std::byte> text{};
            if (!reader.find_bytes(FrameKey::Text, text)) {
                return {};
            }
            return std::string_view(reinterpret_cast<const char *>(text.data()), text.size());
        }

        [[nodiscard]] bool find(FrameKey key, std::uint64_t &out) const {
            return reopen().find_u64(key, out);
        }

        [[nodiscard]] bool find_bytes(FrameKey key, std::span<const std::byte> &out) const {
            return reopen().find_bytes(key, out);
        }
    };

    FrameStatus run(Ops43284 &ops, FrameOp op, const FrameReader &request, Answer &answer) {
        answer.reset();
        static_cast<void>(answer.writer.begin(op));
        answer.status = ops.handle(op, request, answer.writer);
        static_cast<void>(answer.writer.finish(answer.status));
        return answer.status;
    }

    /** The ops a test fills in. */
    struct Seams final {
        bool resolve_ok = true;
        bool write_ok = true;
        bool read_ok = true;
        bool trigger_ok = true;
        bool wait_ok = true;
        bool image_ok = true;
        bool hook_ok = true;
        TerminalState terminal = TerminalState::Success;
        std::uint64_t token_out = 0x77U;
        std::uint64_t sequence_out = 41U;
        std::uint64_t writes = 0U;
        std::uint64_t last_token = 0U;
        std::uint64_t last_offset = 0U;
        std::size_t last_block = 0U;
        std::uint64_t last_delay = 0U;
        std::uint64_t last_budget = 0U;
        std::uint64_t last_poll = 0U;
        std::string_view last_path{};
        const std::byte *payload = nullptr;
        std::size_t payload_size = 0U;
    };

    bool landing_resolve(std::uint64_t, std::string_view path, std::uint64_t &token,
                      void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ops->last_path = path;
        token = ops->resolve_ok ? ops->token_out : 0U;
        return ops->resolve_ok;
    }
    bool landing_write(std::uint64_t token, std::uint64_t offset, std::span<const std::byte> block,
                    std::uint64_t &sequence, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ++ops->writes;
        ops->last_token = token;
        ops->last_offset = offset;
        ops->last_block = block.size();
        if (!ops->write_ok) {
            return false;
        }
        sequence = ops->sequence_out;
        return true;
    }
    bool landing_read(std::uint64_t, std::uint64_t, std::span<std::byte> block, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        if (!ops->read_ok) {
            return false;
        }
        for (std::size_t i = 0U; i < block.size(); ++i) {
            block[i] = static_cast<std::byte>(i + 1U);
        }
        return true;
    }
    bool landing_trigger(std::uint64_t delay_ms, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ops->last_delay = delay_ms;
        return ops->trigger_ok;
    }
    bool landing_wait(std::uint64_t budget, std::uint64_t poll, std::string_view, TerminalState &terminal,
                   void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ops->last_budget = budget;
        ops->last_poll = poll;
        if (!ops->wait_ok) {
            return false;
        }
        terminal = ops->terminal;
        return true;
    }
    bool landing_image(std::uint64_t role, std::span<const std::byte> &image, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        if (!ops->image_ok || role != 0U) {
            return false;
        }
        image = std::span<const std::byte>(ops->payload, ops->payload_size);
        return true;
    }
    bool landing_hook(std::uint64_t &branch, std::uint64_t &displaced, std::uint64_t &site,
                   std::uint64_t &landing, std::span<const std::byte> &payload,
                   void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        if (!ops->hook_ok) {
            return false;
        }
        branch = 0x14000000ULL;
        displaced = 0xd503201fULL;
        site = 0x1234U;
        landing = 0x5678U;
        payload = std::span<const std::byte>(ops->payload, ops->payload_size);
        return true;
    }

    Ops43284Seams full_ops(Seams &state) {
        Ops43284Seams ops{};
        ops.resolve = &landing_resolve;
        ops.write_block = &landing_write;
        ops.read_block = &landing_read;
        ops.trigger = &landing_trigger;
        ops.wait_terminal = &landing_wait;
        ops.embedded_image = &landing_image;
        ops.hook_words = &landing_hook;
        ops.context = &state;
        return ops;
    }

    void an_op_without_its_landing_is_unwired_by_name() {
        Ops43284 ops{Ops43284Seams{}};
        Builder request;
        Answer answer;
        for (const FrameOp op : {FrameOp::ResolveTarget, FrameOp::WriteBlock, FrameOp::ReadBlock,
                                 FrameOp::Trigger, FrameOp::WaitTerminal, FrameOp::EmbeddedImage,
                                 FrameOp::HookWords}) {
            static_cast<void>(request.build(op, {{FrameKey::Mode, 1U}}, FrameKey::Bytes, {}));
            expect(run(ops, op, request.reader, answer) == FrameStatus::Unsupported,
                   "an op without its landing is unsupported");
            expect(answer.refusal().rfind("op-not-wired:", 0U) == 0U, "and the refusal names it");
        }
        static_cast<void>(request.build(FrameOp::WriteWord, {{FrameKey::Mode, 1U}}, FrameKey::Bytes, {}));
        expect(run(ops, FrameOp::WriteWord, request.reader, answer) == FrameStatus::Unsupported,
               "the other backend's op is not this one");
        expect(answer.refusal() == std::string_view("op-unknown-for-backend"),
               "and it says unknown-for-backend");
    }

    void a_target_is_resolved_before_any_block() {
        Seams state;
        Ops43284 ops{full_ops(state)};
        Builder request;
        Answer answer;
        const std::byte block[kBlockWidth] = {};

        static_cast<void>(request.build(FrameOp::WriteBlock,
                                        {{FrameKey::Token, 0x77U}, {FrameKey::Offset, 0U}},
                                        FrameKey::Bytes, std::span<const std::byte>(block)));
        expect(run(ops, FrameOp::WriteBlock, request.reader, answer) == FrameStatus::NotReady &&
                   answer.refusal() == std::string_view("not-ready:resolve-target"),
               "a block write before a target is resolved is not-ready");

        static_cast<void>(request.build(FrameOp::ResolveTarget, {{FrameKey::Mode, 9U}}, FrameKey::Bytes,
                                        {}));
        expect(run(ops, FrameOp::ResolveTarget, request.reader, answer) == FrameStatus::Unsupported &&
                   answer.refusal() == std::string_view("role-unknown"),
               "an unknown role is refused by name");

        static_cast<void>(request.build(FrameOp::ResolveTarget, {{FrameKey::Mode, 2U}}, FrameKey::Bytes,
                                        {}));
        expect(run(ops, FrameOp::ResolveTarget, request.reader, answer) == FrameStatus::KeyMissing &&
                   answer.refusal() == std::string_view("key-missing:path"),
               "a resolve without a path is refused by name");

        const std::string_view path = "/vendor/lib64/libbinderdebug.so";
        static_cast<void>(request.build(FrameOp::ResolveTarget, {{FrameKey::Mode, 2U}}, FrameKey::Path,
                                        std::span<const std::byte>(
                                            reinterpret_cast<const std::byte *>(path.data()),
                                            path.size())));
        expect(run(ops, FrameOp::ResolveTarget, request.reader, answer) == FrameStatus::Ok,
               "the resolve runs");
        std::uint64_t token = 0U;
        expect(answer.find(FrameKey::Token, token) && token == 0x77U, "and its token comes back");
        expect(state.last_path == path, "the landing saw the path the caller named");
    }

    void the_block_unit_is_exactly_sixteen_bytes() {
        Seams state;
        Ops43284 ops{full_ops(state)};
        Builder request;
        Answer answer;
        const std::string_view path = "/dev/x";
        static_cast<void>(request.build(FrameOp::ResolveTarget, {{FrameKey::Mode, 1U}}, FrameKey::Path,
                                        std::span<const std::byte>(
                                            reinterpret_cast<const std::byte *>(path.data()),
                                            path.size())));
        static_cast<void>(run(ops, FrameOp::ResolveTarget, request.reader, answer));

        for (const std::size_t width : {15U, 17U}) {
            std::byte block[32] = {};
            static_cast<void>(request.build(FrameOp::WriteBlock,
                                            {{FrameKey::Token, 0x77U}, {FrameKey::Offset, 0U}},
                                            FrameKey::Bytes,
                                            std::span<const std::byte>(block, width)));
            expect(run(ops, FrameOp::WriteBlock, request.reader, answer) == FrameStatus::BadWidth &&
                       answer.refusal() == std::string_view("bad-width"),
                   "a width that is not one block is refused by width");
        }

        std::byte block[kBlockWidth] = {};
        static_cast<void>(request.build(FrameOp::WriteBlock,
                                        {{FrameKey::Token, 0x1234U}, {FrameKey::Offset, 0U}},
                                        FrameKey::Bytes, std::span<const std::byte>(block)));
        expect(run(ops, FrameOp::WriteBlock, request.reader, answer) == FrameStatus::StaleTarget &&
                   answer.refusal() == std::string_view("stale-target"),
               "a token this session never handed out is refused as stale");

        static_cast<void>(request.build(FrameOp::WriteBlock,
                                        {{FrameKey::Token, 0x77U}, {FrameKey::Offset, 32U}},
                                        FrameKey::Bytes, std::span<const std::byte>(block)));
        expect(run(ops, FrameOp::WriteBlock, request.reader, answer) == FrameStatus::Ok,
               "a sixteen-byte write at a resolved token runs");
        std::uint64_t sequence = 0U;
        expect(answer.find(FrameKey::Sequence, sequence) && sequence == 41U,
               "and the sequence the landing left behind comes back");
        expect(state.writes == 1U && state.last_token == 0x77U && state.last_offset == 32U &&
                   state.last_block == kBlockWidth,
               "with the landing called exactly once, on the right token and offset");

        state.write_ok = false;
        static_cast<void>(request.build(FrameOp::WriteBlock,
                                        {{FrameKey::Token, 0x77U}, {FrameKey::Offset, 64U}},
                                        FrameKey::Bytes, std::span<const std::byte>(block)));
        expect(run(ops, FrameOp::WriteBlock, request.reader, answer) == FrameStatus::Failed &&
                   answer.refusal() == std::string_view("block-refused"),
               "a block that did not go in is a failed write, named");

        state.read_ok = true;
        static_cast<void>(request.build(FrameOp::ReadBlock,
                                        {{FrameKey::Token, 0x77U}, {FrameKey::Offset, 0U}},
                                        FrameKey::Bytes, {}));
        expect(run(ops, FrameOp::ReadBlock, request.reader, answer) == FrameStatus::Ok,
               "a block read answers");
        std::span<const std::byte> read_back{};
        expect(answer.find_bytes(FrameKey::Bytes, read_back) && read_back.size() == kBlockWidth,
               "with one block of bytes");
    }

    void the_terminal_has_three_answers_and_they_are_not_interchangeable() {
        Seams state;
        Ops43284 ops{full_ops(state)};
        Builder request;
        Answer answer;

        static_cast<void>(request.build(FrameOp::Trigger, {}, FrameKey::Bytes, {}));
        expect(run(ops, FrameOp::Trigger, request.reader, answer) == FrameStatus::KeyMissing &&
                   answer.refusal() == std::string_view("key-missing:delay_ms"),
               "a trigger without a delay is refused by name");
        static_cast<void>(request.build(FrameOp::Trigger, {{FrameKey::DelayMs, 150U}}, FrameKey::Bytes,
                                        {}));
        expect(run(ops, FrameOp::Trigger, request.reader, answer) == FrameStatus::Ok &&
                   state.last_delay == 150U,
               "and with one, the landing sees it");

        for (const auto pair : {std::pair<TerminalState, FrameStatus>{TerminalState::Success, FrameStatus::Ok},
                                {TerminalState::Failure, FrameStatus::Failed},
                                {TerminalState::InFlight, FrameStatus::Indeterminate}}) {
            state.terminal = pair.first;
            static_cast<void>(request.build(FrameOp::WaitTerminal,
                                            {{FrameKey::Mode, 800U}, {FrameKey::PollMs, 10U}},
                                            FrameKey::Text, {}));
            const FrameStatus status = run(ops, FrameOp::WaitTerminal, request.reader, answer);
            expect(status == pair.second, "each terminal state has its own status");
            std::uint64_t code = 0U;
            expect(answer.find(FrameKey::Mode, code) &&
                       code == static_cast<std::uint64_t>(pair.first),
                   "and the state travels as a number");
            const std::string_view expected(terminal_state_name(pair.first));
            if (status != FrameStatus::Indeterminate) {
                expect(answer.refusal() == expected, "and as the name the wire uses");
            }
        }
        expect(state.last_budget == 800U && state.last_poll == 10U,
               "the wait landing saw the budget and the interval");
    }

    void images_and_hook_words_come_back_as_bytes() {
        Seams state;
        const std::byte payload[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        state.payload = payload;
        state.payload_size = sizeof(payload);
        Ops43284 ops{full_ops(state)};
        Builder request;
        Answer answer;

        static_cast<void>(request.build(FrameOp::EmbeddedImage, {{FrameKey::Mode, 0U}}, FrameKey::Bytes,
                                        {}));
        expect(run(ops, FrameOp::EmbeddedImage, request.reader, answer) == FrameStatus::Ok,
               "the embedded image is served");
        std::span<const std::byte> image{};
        std::uint64_t length = 0U;
        expect(answer.find_bytes(FrameKey::Bytes, image) && image.size() == sizeof(payload) &&
                   answer.find(FrameKey::Length, length) && length == sizeof(payload),
               "with its bytes and its length");

        static_cast<void>(request.build(FrameOp::HookWords, {}, FrameKey::Bytes, {}));
        expect(run(ops, FrameOp::HookWords, request.reader, answer) == FrameStatus::Ok,
               "the hook words are served");
        std::uint64_t branch = 0U;
        std::uint64_t displaced = 0U;
        std::uint64_t site = 0U;
        std::uint64_t landing = 0U;
        expect(answer.find(FrameKey::BranchWord, branch) && branch == 0x14000000ULL &&
                   answer.find(FrameKey::DisplacedWord, displaced) && displaced == 0xd503201fULL &&
                   answer.find(FrameKey::SiteOffset, site) && site == 0x1234U &&
                   answer.find(FrameKey::LandingOffset, landing) && landing == 0x5678U,
               "with both words and both offsets");
        expect(answer.find_bytes(FrameKey::Bytes, image) && image.size() == sizeof(payload),
               "and the payload");
    }

    /** The session must hand the request to this backend's ops, not answer for them. */
    class QuietAccess final : public ghostlock::contract::KernelAccess {
      public:
        [[nodiscard]] AccessState state(AccessChannel) const noexcept override {
            return AccessState::Ready;
        }
        [[nodiscard]] bool supports(AccessChannel) const noexcept override { return true; }
        [[nodiscard]] bool supports(AccessCarrier) const noexcept override { return true; }
        [[nodiscard]] bool supports_width(std::size_t) const noexcept override { return true; }
        [[nodiscard]] AccessResult<bool> initialize(const AccessMiscParameters &) noexcept override {
            return AccessResult<bool>::ok(true);
        }
        [[nodiscard]] AccessResult<bool> shutdown() noexcept override {
            return AccessResult<bool>::ok(true);
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

    std::unique_ptr<ghostlock::contract::KernelAccess> create_quiet(std::uint64_t, void *) noexcept {
        return std::make_unique<QuietAccess>();
    }
    BackendOps create_ops(std::uint64_t, void *context) noexcept {
        return ghostlock::jni::make_ops_43284(*static_cast<Ops43284Seams *>(context));
    }

    void the_session_routes_this_backends_ops() {
        Seams state;
        const std::byte payload[2] = {std::byte{9}, std::byte{9}};
        state.payload = payload;
        state.payload_size = sizeof(payload);
        Ops43284Seams ops = full_ops(state);
        ghostlock::jni::Session session;
        const ghostlock::jni::BackendFactory backend{&create_quiet, &ops, &create_ops};
        expect(session.open(43284U, backend, 1U), "the session opens on the quiet backend");
        Builder request;
        Answer answer;
        static_cast<void>(request.build(FrameOp::EmbeddedImage, {{FrameKey::Mode, 0U}}, FrameKey::Bytes,
                                        {}));
        static_cast<void>(answer.writer.begin(FrameOp::EmbeddedImage));
        const FrameStatus status =
            session.invoke(FrameOp::EmbeddedImage, request.reader, answer.writer);
        static_cast<void>(answer.writer.finish(status));
        expect(status == FrameStatus::Ok,
               "the backend ops answered (the session itself would say unsupported)");
        std::span<const std::byte> image{};
        expect(answer.find_bytes(FrameKey::Bytes, image) && image.size() == sizeof(payload),
               "and their answer came back through the session");
    }
}  // namespace

int main() {
    std::printf("jni ops 43284: unwired ops\n");
    an_op_without_its_landing_is_unwired_by_name();
    std::printf("jni ops 43284: resolve\n");
    a_target_is_resolved_before_any_block();
    std::printf("jni ops 43284: block unit\n");
    the_block_unit_is_exactly_sixteen_bytes();
    std::printf("jni ops 43284: terminal\n");
    the_terminal_has_three_answers_and_they_are_not_interchangeable();
    std::printf("jni ops 43284: images and hook\n");
    images_and_hook_words_come_back_as_bytes();
    std::printf("jni ops 43284: routing\n");
    the_session_routes_this_backends_ops();
    if (g_failures != 0U) {
        std::printf("jni ops 43284: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("jni ops 43284: PASS (width, token, terminal, landing arguments)\n");
    return 0;
}
