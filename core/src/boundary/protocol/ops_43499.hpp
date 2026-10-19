#pragma once
/** @file boundary/protocol/ops_43499.hpp
 *  43499's ops: what a request means for this backend, and what its refusal is called.
 *
 *  This unit is the boundary's backend-facing half, and it is written against **ops** on purpose:
 *  the device work (the heap preparation, the search, the fork, the PI window) arrives as function
 *  pointers, so the rules below -- which form maps to which landing shape, what has to be in place
 *  before a write may run, and what each refusal is called -- are decided and tested on the host.
 *  The device implementation of those ops is its own batch (recorded as J3b in
 *  docs/analysis/jni-two-backend-plan.md), because it needs a device to be verified.
 *
 *  The one thing this unit deliberately does NOT do: retry. A call is one attempt; how many
 *  attempts a step makes, and how long it waits between them, is the Kotlin step engine's business
 *  (its inputs are profile values). That keeps "how patient the run is" out of the boundary.
 */
#include <cstdint>
#include <string_view>

#include "boundary/protocol/session_object.hpp"
#include "boundary/protocol/session_protocol.hpp"

namespace ghostlock::jni {
    /**
     *  Which shape a 43499 word write takes.
     *
     *  A closed set, because the landing shape is not derivable from (address, value): W1 and W3
     *  both write a zero, and they differ only in whether the bytes already in the target are kept.
     *  The mapping to the backend's own (mode, preserve) pair lives next to this enumeration and
     *  nowhere else.
     */
    enum class WriteForm : std::uint64_t {
        ClearPreserving = 1, /**< W1: clear the field, keep what the target already holds. */
        ClearLeaf = 2,       /**< W3: clear the field through the leaf arm (nothing preserved). */
        ValuePreserving = 3, /**< W2: write a value, keeping what the target already holds. */
        Count = 4,
    };

    [[nodiscard]] constexpr const char *write_form_name(WriteForm form) noexcept {
        switch (form) {
            case WriteForm::ClearPreserving: return "clear-preserving";
            case WriteForm::ClearLeaf: return "clear-leaf";
            case WriteForm::ValuePreserving: return "value-preserving";
            case WriteForm::Count: break;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr WriteForm write_form_of(std::uint64_t raw) noexcept {
        return raw >= 1U && raw < static_cast<std::uint64_t>(WriteForm::Count)
                   ? static_cast<WriteForm>(raw)
                   : WriteForm::Count;
    }

    /** The backend's own landing shape: the value the memory layer calls mode, and preserve. */
    struct FormMapping final {
        std::int32_t mode; /**< 0 disabled, 1 zero, 2 credential (memory::WriteMode). */
        bool preserve;     /**< whether the bytes already in the target are kept. */
    };

    [[nodiscard]] constexpr FormMapping map_write_form(WriteForm form) noexcept {
        switch (form) {
            case WriteForm::ClearPreserving: return FormMapping{1, true};
            case WriteForm::ClearLeaf: return FormMapping{1, false};
            case WriteForm::ValuePreserving: return FormMapping{2, true};
            case WriteForm::Count: break;
        }
        return FormMapping{0, false};
    }

    /** What the device side published for this session. Zero means "not known yet". */
    struct Facts43499 final {
        std::uint64_t page_base = 0U;
        std::uint64_t word0 = 0U;
        std::uint64_t word1 = 0U;
        std::uint64_t payload_page = 0U;
        std::uint64_t payload_base = 0U;
        std::uint64_t alias_delta = 0U;
        std::uint64_t found = 0U;
        std::uint64_t mm_struct = 0U;
        std::uint64_t victim_task = 0U;
        std::uint64_t victim_pid = 0U;
        bool prepared = false;
        bool discovered = false;
        bool victim_spawned = false;

        /** @return nullptr when a word write may run, or the name of the first fact that is absent. */
        [[nodiscard]] const char *ready_for_write() const noexcept {
            if (!prepared || page_base == 0U) {
                return "not-ready:prepare";
            }
            if (!discovered || found == 0U) {
                return "not-ready:discover";
            }
            if (!victim_spawned || victim_task == 0U) {
                return "not-ready:victim";
            }
            return nullptr;
        }
    };

    /** The flags Snapshot's Flags entry publishes for this backend's facts. */
    inline constexpr std::uint64_t kFactPrepared = 1U << 16U;
    inline constexpr std::uint64_t kFactDiscovered = 1U << 17U;
    inline constexpr std::uint64_t kFactVictim = 1U << 18U;

    /** The device work, as function pointers. A null landing makes its op "not wired" by name. */
    struct Ops43499Bindings final {
        /** Run the preparation and fill the facts it produced. */
        bool (*prepare)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** Run the search and fill what it found. */
        bool (*discover)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** Fork the victim and read its task address back. */
        bool (*spawn_victim)(Facts43499 &facts, void *context) noexcept = nullptr;
        /** Ask the victim what it is: kind 0 = uid, 1 = comm. */
        bool (*victim_probe)(std::uint64_t kind, std::uint64_t &value, void *context) noexcept = nullptr;
        /** Land one word through the window: one attempt, the whole transaction. */
        bool (*land)(std::uint64_t address, std::uint64_t value, WriteForm form, std::uint64_t offset,
                     void *context) noexcept = nullptr;
        /** Read one word back, when the route can read. */
        bool (*read)(std::uint64_t address, std::uint64_t &value, void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /** 43499's ops, over the ops. One instance per session; the session owns it. */
    class Ops43499 final {
      public:
        Ops43499() noexcept = default;
        explicit Ops43499(Ops43499Bindings ops) noexcept : ops_(ops) {}

        [[nodiscard]] FrameStatus handle(FrameOp op, const FrameReader &request,
                                         FrameWriter &reply) noexcept;

        [[nodiscard]] const Facts43499 &facts() const noexcept { return facts_; }

      private:
        [[nodiscard]] FrameStatus on_prepare(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_discover(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_spawn_victim(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_victim_probe(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_write_word(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_read_word(const FrameReader &request, FrameWriter &reply) noexcept;

        void publish_facts(FrameWriter &reply) noexcept;

        Ops43499Bindings ops_{};
        Facts43499 facts_{};
    };

    /**
     *  Build this backend's ops as the session's bundle: the object is heap-allocated here, and
     *  the bundle carries the deleter, because the session does not know the type.
     *
     *  @param ops the device work; an empty landing makes its op answer "op-not-wired".
     */
    [[nodiscard]] BackendOps make_ops_43499(Ops43499Bindings ops) noexcept;
}  // namespace ghostlock::jni
