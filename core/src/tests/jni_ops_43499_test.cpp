/** @file tests/jni_ops_43499_test.cpp
 *  43499's op layer on the host: the form table, the facts gate, the refusals, and what the ops
 *  are called with.
 *
 *  What this pins is everything the device cannot be asked about cheaply: which (mode, preserve)
 *  pair each form means, that a write is refused by name when a fact is missing, and that a landed
 *  word carries exactly the address, value, offset and form the caller asked for.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "contract/kernel_access.hpp"
#include "jni/ops_43499.hpp"
#include "jni/session_object.hpp"
#include "jni/session_protocol.hpp"

namespace {
    using ghostlock::contract::AccessCarrier;
    using ghostlock::contract::AccessChannel;
    using ghostlock::jni::BackendOps;
    using ghostlock::jni::BackendFactory;
    using ghostlock::jni::Facts43499;
    using ghostlock::jni::FrameKey;
    using ghostlock::jni::FrameOp;
    using ghostlock::jni::FrameReader;
    using ghostlock::jni::FrameStatus;
    using ghostlock::jni::FrameWriter;
    using ghostlock::jni::kFactDiscovered;
    using ghostlock::jni::kFactPrepared;
    using ghostlock::jni::kFactVictim;
    using ghostlock::jni::map_write_form;
    using ghostlock::jni::Ops43499;
    using ghostlock::jni::Ops43499Seams;
    using ghostlock::jni::Session;
    using ghostlock::jni::WriteForm;
    using ghostlock::jni::write_form_name;
    using ghostlock::jni::write_form_of;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A request frame built from a list of u64 entries. */
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
    };

    /** One answer frame, kept alive long enough to be read back. */
    struct Answer final {
        std::byte buffer[512]{};
        FrameWriter writer{std::span<std::byte>(buffer)};
        FrameStatus status = FrameStatus::Failed;

        Answer() noexcept = default;
        Answer(const Answer &) = delete;
        Answer &operator=(const Answer &) = delete;

        /** 重新开始一帧：**不能**用赋值重置 —— FrameWriter 内部持有指向自身缓冲的 span，
         *  赋值会把 span 指到临时对象的缓冲上（已回收）⇒ 后续写入落到垃圾地址（2026-10-14 实测）。 */
        void reset() noexcept {
            for (std::size_t i = 0U; i < sizeof(buffer); ++i) {
                buffer[i] = std::byte{0};
            }
            writer = FrameWriter{std::span<std::byte>(buffer)};
            status = FrameStatus::Failed;
        }

        [[nodiscard]] std::string_view refusal() const {
            FrameReader reader;
            FrameOp op = FrameOp::Count;
            if (reader.open(writer.view(), op) != FrameStatus::Ok) {
                return {};
            }
            std::span<const std::byte> text{};
            if (!reader.find_bytes(FrameKey::Text, text)) {
                return {};
            }
            return std::string_view(reinterpret_cast<const char *>(text.data()), text.size());
        }

        [[nodiscard]] bool find(FrameKey key, std::uint64_t &out) const {
            FrameReader reader;
            FrameOp op = FrameOp::Count;
            if (reader.open(writer.view(), op) != FrameStatus::Ok) {
                return false;
            }
            return reader.find_u64(key, out);
        }
    };

    /** Run one op and close its answer. */
    FrameStatus run(Ops43499 &ops, FrameOp op, const Request &request, Answer &answer) {
        answer.reset();
        static_cast<void>(answer.writer.begin(op));
        answer.status = ops.handle(op, request.reader, answer.writer);
        static_cast<void>(answer.writer.finish(answer.status));
        return answer.status;
    }

    /** The ops a test fills in. */
    struct Seams final {
        Facts43499 published{};
        bool prepare_ok = true;
        bool discover_ok = true;
        bool spawn_ok = true;
        bool land_ok = true;
        bool read_ok = true;
        int prepare_calls = 0;
        int discover_calls = 0;
        int spawn_calls = 0;
        int land_calls = 0;
        std::uint64_t last_address = 0U;
        std::uint64_t last_value = 0U;
        std::uint64_t last_offset = 0U;
        WriteForm last_form = WriteForm::Count;
    };

    bool landing_prepare(Facts43499 &facts, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ++ops->prepare_calls;
        facts = ops->published;
        facts.prepared = ops->prepare_ok;
        return ops->prepare_ok;
    }
    bool landing_discover(Facts43499 &facts, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ++ops->discover_calls;
        facts.discovered = ops->discover_ok;
        facts.found = ops->discover_ok ? ops->published.found : 0U;
        facts.mm_struct = facts.found;
        facts.alias_delta = ops->published.alias_delta;
        return ops->discover_ok;
    }
    bool landing_spawn(Facts43499 &facts, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ++ops->spawn_calls;
        facts.victim_spawned = ops->spawn_ok;
        facts.victim_task = ops->published.victim_task;
        facts.victim_pid = ops->published.victim_pid;
        return ops->spawn_ok;
    }
    bool landing_probe(std::uint64_t kind, std::uint64_t &value, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        value = kind == 0U ? 0U : 0x1234U;
        return true;
    }
    bool landing_land(std::uint64_t address, std::uint64_t value, WriteForm form, std::uint64_t offset,
                   void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        ++ops->land_calls;
        ops->last_address = address;
        ops->last_value = value;
        ops->last_offset = offset;
        ops->last_form = form;
        return ops->land_ok;
    }
    bool landing_read(std::uint64_t, std::uint64_t &value, void *context) noexcept {
        auto *const ops = static_cast<Seams *>(context);
        value = ops->read_ok ? 0xabcdefULL : 0U;
        return ops->read_ok;
    }

    Ops43499Seams full_ops(Seams &state) {
        Ops43499Seams ops{};
        ops.prepare = &landing_prepare;
        ops.discover = &landing_discover;
        ops.spawn_victim = &landing_spawn;
        ops.victim_probe = &landing_probe;
        ops.land = &landing_land;
        ops.read = &landing_read;
        ops.context = &state;
        return ops;
    }

    void the_form_table_is_the_documented_one() {
        expect(map_write_form(WriteForm::ClearPreserving).mode == 1 &&
                   map_write_form(WriteForm::ClearPreserving).preserve,
               "W1 clears and preserves");
        expect(map_write_form(WriteForm::ClearLeaf).mode == 1 &&
                   !map_write_form(WriteForm::ClearLeaf).preserve,
               "W3 clears through the leaf arm");
        expect(map_write_form(WriteForm::ValuePreserving).mode == 2 &&
                   map_write_form(WriteForm::ValuePreserving).preserve,
               "W2 writes a value and preserves");
        expect(map_write_form(WriteForm::Count).mode == 0 && !map_write_form(WriteForm::Count).preserve,
               "an out-of-table form maps to nothing");
        expect(write_form_of(1U) == WriteForm::ClearPreserving && write_form_of(3U) == WriteForm::ValuePreserving,
               "the numbers round-trip");
        expect(write_form_of(0U) == WriteForm::Count && write_form_of(99U) == WriteForm::Count,
               "a number outside the table is Count");
        expect(std::string_view(write_form_name(WriteForm::ClearLeaf)) == "clear-leaf",
               "and the names are spelled once");
    }

    void an_op_without_its_landing_is_unwired_by_name() {
        Ops43499 ops{Ops43499Seams{}};
        Request request;
        for (const FrameOp op : {FrameOp::Prepare, FrameOp::Discover, FrameOp::SpawnVictim,
                                 FrameOp::VictimProbe, FrameOp::WriteWord, FrameOp::ReadWord}) {
            static_cast<void>(request.open(op, {{FrameKey::Mode, 0U}}));
            Answer answer;
            expect(run(ops, op, request, answer) == FrameStatus::Unsupported,
                   "an op without its landing is unsupported");
            const std::string_view refusal = answer.refusal();
            expect(refusal.rfind("op-not-wired:", 0U) == 0U, "and the refusal names it");
        }
        Answer answer;
        static_cast<void>(request.open(FrameOp::WriteBlock, {}));
        expect(run(ops, FrameOp::WriteBlock, request, answer) == FrameStatus::Unsupported,
               "an op of another backend is unsupported here");
        expect(answer.refusal() == std::string_view("op-unknown-for-backend"),
               "as unknown-for-backend");
    }

    void facts_gate_the_write_and_travel_back() {
        Seams state;
        state.published.page_base = 0xffffff8042a00000ULL;
        state.published.word0 = 0x700000000000ULL;
        state.published.word1 = 0x700000000100ULL;
        state.published.payload_page = 0xffffff8042b00000ULL;
        state.published.payload_base = 0xffffff8042c00000ULL;
        state.published.alias_delta = 0xffffff8000000000ULL;
        state.published.found = 0xffffff8080808080ULL;
        state.published.victim_task = 0xffffff80dead0000ULL;
        state.published.victim_pid = 4242U;
        Ops43499 ops{full_ops(state)};

        Request write;
        static_cast<void>(write.open(FrameOp::WriteWord,
                                     {{FrameKey::Address, 0xffffff8000000000ULL},
                                      {FrameKey::Form, 1U},
                                      {FrameKey::Offset, 3U}}));
        Answer answer;
        expect(run(ops, FrameOp::WriteWord, write, answer) == FrameStatus::NotReady,
               "a write before the facts are in place is not-ready");
        expect(answer.refusal() == std::string_view("not-ready:prepare"),
               "and the first missing fact is named");

        Request prepare;
        static_cast<void>(prepare.open(FrameOp::Prepare, {}));
        expect(run(ops, FrameOp::Prepare, prepare, answer) == FrameStatus::Ok, "prepare runs");
        std::uint64_t flags = 0U;
        expect(answer.find(FrameKey::Flags, flags) && (flags & kFactPrepared) != 0U,
               "and its flags say it is prepared");
        std::uint64_t base = 0U;
        expect(answer.find(FrameKey::PageBase, base) && base == 0xffffff8042a00000ULL,
               "with the page it prepared");

        expect(run(ops, FrameOp::WriteWord, write, answer) == FrameStatus::NotReady &&
                   answer.refusal() == std::string_view("not-ready:discover"),
               "the next missing fact is the search");

        Request discover;
        static_cast<void>(discover.open(FrameOp::Discover, {}));
        expect(run(ops, FrameOp::Discover, discover, answer) == FrameStatus::Ok, "the search runs");
        expect(answer.find(FrameKey::Found, base) && base == 0xffffff8080808080ULL,
               "and its answer is published");

        expect(run(ops, FrameOp::WriteWord, write, answer) == FrameStatus::NotReady &&
                   answer.refusal() == std::string_view("not-ready:victim"),
               "the third missing fact is the victim");

        Request spawn;
        static_cast<void>(spawn.open(FrameOp::SpawnVictim, {}));
        expect(run(ops, FrameOp::SpawnVictim, spawn, answer) == FrameStatus::Ok, "the victim is forked");
        std::uint64_t task = 0U;
        expect(answer.find(FrameKey::VictimTask, task) && task == 0xffffff80dead0000ULL,
               "and its task address comes back");
        std::uint64_t victim_flags = 0U;
        expect(answer.find(FrameKey::Flags, victim_flags) &&
                   (victim_flags & (kFactPrepared | kFactDiscovered | kFactVictim)) ==
                       (kFactPrepared | kFactDiscovered | kFactVictim),
               "all three facts are visible in one answer");

        expect(run(ops, FrameOp::WriteWord, write, answer) == FrameStatus::Ok,
               "now the write may run");
        expect(state.land_calls == 1 && state.last_address == 0xffffff8000000000ULL &&
                   state.last_value == 0U && state.last_offset == 3U &&
                   state.last_form == WriteForm::ClearPreserving,
               "and the landing saw exactly what the caller asked for");
        std::uint64_t width = 0U;
        expect(answer.find(FrameKey::Width, width) && width == 8U, "the answer reports the width it wrote");
    }

    void a_bad_write_request_is_refused_by_name() {
        Seams state;
        Ops43499 ops{full_ops(state)};
        FrameStatus status = FrameStatus::Ok;
        Answer answer;

        Request no_address;
        static_cast<void>(no_address.open(FrameOp::WriteWord, {{FrameKey::Form, 1U}}));
        status = run(ops, FrameOp::WriteWord, no_address, answer);
        expect(status == FrameStatus::KeyMissing &&
                   answer.refusal() == std::string_view("key-missing:address"),
               "an address is required, and its absence is named");

        Request no_form;
        static_cast<void>(no_form.open(FrameOp::WriteWord, {{FrameKey::Address, 0x1000U}}));
        status = run(ops, FrameOp::WriteWord, no_form, answer);
        expect(status == FrameStatus::KeyMissing && answer.refusal() == std::string_view("key-missing:form"),
               "so is the form");

        Request bad_form;
        static_cast<void>(bad_form.open(FrameOp::WriteWord,
                                        {{FrameKey::Address, 0x1000U}, {FrameKey::Form, 9U}}));
        status = run(ops, FrameOp::WriteWord, bad_form, answer);
        expect(status == FrameStatus::Unsupported &&
                   answer.refusal() == std::string_view("write-refused:form"),
               "a form outside the table is refused by name");

        Request value_missing;
        static_cast<void>(value_missing.open(FrameOp::WriteWord,
                                             {{FrameKey::Address, 0x1000U}, {FrameKey::Form, 3U}}));
        status = run(ops, FrameOp::WriteWord, value_missing, answer);
        expect(status == FrameStatus::KeyMissing &&
                   answer.refusal() == std::string_view("write-refused:value-missing"),
               "a value write without a value is not a zero write");

        Request value_where_none_belongs;
        static_cast<void>(value_where_none_belongs.open(
            FrameOp::WriteWord, {{FrameKey::Address, 0x1000U}, {FrameKey::Form, 1U}, {FrameKey::Value, 7U}}));
        status = run(ops, FrameOp::WriteWord, value_where_none_belongs, answer);
        expect(status == FrameStatus::Unsupported &&
                   answer.refusal() == std::string_view("write-refused:value-not-empty"),
               "a clear form carrying a value is a caller mistake");
    }

    void a_landing_that_refuses_is_reported_as_a_refusal() {
        Seams state;
        state.land_ok = false;
        state.published.page_base = 0xffffff8042a00000ULL;
        state.published.found = 0xffffff8080808080ULL;
        state.published.victim_task = 0xffffff80dead0000ULL;
        Ops43499 ops{full_ops(state)};
        // 先把三件事实补齐（用同一组 landing）
        Request prepare;
        static_cast<void>(prepare.open(FrameOp::Prepare, {}));
        Answer answer;
        static_cast<void>(run(ops, FrameOp::Prepare, prepare, answer));
        Request discover;
        static_cast<void>(discover.open(FrameOp::Discover, {}));
        static_cast<void>(run(ops, FrameOp::Discover, discover, answer));
        Request spawn;
        static_cast<void>(spawn.open(FrameOp::SpawnVictim, {}));
        static_cast<void>(run(ops, FrameOp::SpawnVictim, spawn, answer));

        Request write;
        static_cast<void>(write.open(FrameOp::WriteWord,
                                     {{FrameKey::Address, 0x1000U}, {FrameKey::Form, 1U}}));
        const FrameStatus status = run(ops, FrameOp::WriteWord, write, answer);
        expect(status == FrameStatus::Failed && answer.refusal() == std::string_view("write-refused"),
               "a landing that did not happen is a failed write, named");
        expect(state.land_calls == 1, "and it was attempted exactly once (retries are the caller's)");
    }

    void a_probe_and_a_read_answer_their_values() {
        Seams state;
        Ops43499 ops{full_ops(state)};
        Request probe;
        static_cast<void>(probe.open(FrameOp::VictimProbe, {{FrameKey::Mode, 1U}}));
        Answer answer;
        expect(run(ops, FrameOp::VictimProbe, probe, answer) == FrameStatus::Ok, "the probe answers");
        std::uint64_t value = 0U;
        expect(answer.find(FrameKey::Value, value) && value == 0x1234U, "with what it read");

        Request read;
        static_cast<void>(read.open(FrameOp::ReadWord, {{FrameKey::Address, 0x2000U}}));
        expect(run(ops, FrameOp::ReadWord, read, answer) == FrameStatus::Ok, "the read answers");
        expect(answer.find(FrameKey::Value, value) && value == 0xabcdefULL, "with the word it read");
    }

    /** A backend that exists and does nothing: the routing test needs a live session. */
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

    BackendOps create_ops_43499(std::uint64_t, void *context) noexcept {
        return ghostlock::jni::make_ops_43499(*static_cast<Ops43499Seams *>(context));
    }

    /** The session must hand the request to the backend ops, not answer for them. */
    void the_session_routes_its_backend_ops() {
        Seams state;
        Ops43499Seams ops = full_ops(state);
        Session session;
        const BackendFactory landing{&create_quiet, &ops, &create_ops_43499};
        /* ops 的接管发生在 open；把它交给 landing 之前先记下来，会话负责 delete（释放经 ops_）。 */
        state.published.page_base = 0x1000U;
        expect(session.open(43499U, landing, 1U), "the session opens on the quiet backend");
        Request prepare;
        static_cast<void>(prepare.open(FrameOp::Prepare, {}));
        Answer answer;
        static_cast<void>(answer.writer.begin(FrameOp::Prepare));
        const FrameStatus status = session.invoke(FrameOp::Prepare, prepare.reader, answer.writer);
        static_cast<void>(answer.writer.finish(status));
        expect(status == FrameStatus::Ok,
               "the backend ops answered a prepare (the session itself would say unsupported)");
        std::uint64_t base = 0U;
        expect(answer.find(FrameKey::PageBase, base) && base == 0x1000U,
               "and the facts they published came back");
        expect(session.shutdown() == FrameStatus::Ok, "and the session still closes cleanly");
    }
}  // namespace

int main() {
    std::printf("jni ops 43499: forms\n");
    the_form_table_is_the_documented_one();
    std::printf("jni ops 43499: unwired ops\n");
    an_op_without_its_landing_is_unwired_by_name();
    std::printf("jni ops 43499: facts gate\n");
    facts_gate_the_write_and_travel_back();
    std::printf("jni ops 43499: bad requests\n");
    a_bad_write_request_is_refused_by_name();
    std::printf("jni ops 43499: refusing landing\n");
    a_landing_that_refuses_is_reported_as_a_refusal();
    std::printf("jni ops 43499: probe and read\n");
    a_probe_and_a_read_answer_their_values();
    if (g_failures != 0U) {
        std::printf("jni ops 43499: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("jni ops 43499: PASS (forms, gate, refusals, landing arguments)\n");
    return 0;
}
