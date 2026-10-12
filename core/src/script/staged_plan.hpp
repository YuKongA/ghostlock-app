#pragma once
/** @file script/staged_plan.hpp
 *  A6-2: the staged plan -- what one script run recorded, before anything runs.
 *
 *  Boundary: the sandbox forwards each call through script::CallSink; this file
 *  is the sink that keeps them. Two axes coexist and stay apart, because their
 *  carriers differ (plan_gate's PlanOp holds u64 parameters only):
 *    - surface entries carry what contract::kSurface declares, including string
 *      arguments and the {kind, value} target pairs, verbatim;
 *    - op entries carry an op token from contract::op_registry, whose hash and
 *      numeric parameters feed pipeline::plan_gate as-is (filled by the next
 *      increment; the field is present so callers do not invent a second type).
 *
 *  Bounds are mechanism capacities, not tuning: one plan holds at most
 *  kMaxStagedEntries entries, one entry holds at most kMaxStagedArgs arguments,
 *  and every string lives in one arena of kStagedArenaBytes. Exceeding a bound
 *  is a named refusal; nothing is ever truncated silently.
 *
 *  Determinism: serialize() writes a canonical little-endian image of the staged
 *  content, so two runs of the same script compare byte for byte.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/call_sink.hpp"

namespace ghostlock::script {
    /** One plan holds at most this many entries (mechanism capacity). */
    inline constexpr std::size_t kMaxStagedEntries = 64U;
    /** One entry holds at most this many arguments; copyop is the widest row. */
    inline constexpr std::size_t kMaxStagedArgs = 8U;
    /** All strings of one plan share this arena (mechanism capacity). */
    inline constexpr std::size_t kStagedArenaBytes = 4096U;
    /** A refusal name is copied into the plan, bounded. */
    inline constexpr std::size_t kRefusalNameBytes = 32U;

    /** One recorded argument: a type tag plus the value or a string span. */
    struct StagedArg final {
        contract::ArgKind kind = contract::ArgKind::U64;
        std::uint64_t u64 = 0U;
        bool boolean = false;
        std::uint32_t str_offset = 0U;
        std::uint32_t str_length = 0U;
        Handle handle{};
    };

    /** One recorded call. The op axis is not mirrored here: op entries are the
     *  host's plan declaration, carried as pipeline::PlanOp by the gate itself,
     *  so this container holds exactly what the script produced. */
    struct StagedEntry final {
        std::size_t surface_index = 0U;
        contract::SurfaceForm form = contract::SurfaceForm::Call;
        std::size_t arg_count = 0U;
        StagedArg args[kMaxStagedArgs];
    };

    /** The recording of one script run. Trivially copyable, no allocation. */
    class StagedPlan final {
      public:
        StagedPlan() noexcept = default;

        /** Drop everything, including any refusal. */
        inline void reset() noexcept {
            count_ = 0U;
            arena_used_ = 0U;
            refusal_[0] = '\0';
        }

        /** Record one forwarded call. False when a bound was hit (named). */
        [[nodiscard]] inline bool record(const CallView &view) noexcept {
            if (!ok() || count_ >= kMaxStagedEntries || view.arg_count > kMaxStagedArgs) {
                return refuse("budget-exceeded");
            }
            StagedEntry &entry = entries_[count_];
            entry = StagedEntry{};
            entry.surface_index = view.surface_index;
            entry.form = view.form;
            for (std::size_t i = 0U; i < view.arg_count; ++i) {
                const ArgView &arg = view.args[i];
                StagedArg &slot = entry.args[i];
                slot.kind = arg.kind;
                if (arg.kind == contract::ArgKind::Str) {
                    if (arena_used_ + arg.str.size() > kStagedArenaBytes) {
                        return refuse("budget-exceeded");
                    }
                    slot.str_offset = static_cast<std::uint32_t>(arena_used_);
                    slot.str_length = static_cast<std::uint32_t>(arg.str.size());
                    for (std::size_t c = 0U; c < arg.str.size(); ++c) {
                        arena_[arena_used_ + c] = arg.str[c];
                    }
                    arena_used_ += arg.str.size();
                } else if (arg.kind == contract::ArgKind::Bool) {
                    slot.boolean = arg.boolean;
                } else if (arg.kind == contract::ArgKind::Handle) {
                    slot.handle = arg.handle;
                } else {
                    slot.u64 = arg.u64;
                }
                entry.arg_count = i + 1U;
            }
            ++count_;
            return true;
        }

        /** Record a named refusal (from the sandbox or from this container). */
        [[nodiscard]] inline bool refuse(std::string_view named_code) noexcept {
            if (ok()) {
                const std::size_t n = named_code.size() < kRefusalNameBytes - 1U
                                              ? named_code.size()
                                              : kRefusalNameBytes - 1U;
                for (std::size_t i = 0U; i < n; ++i) {
                    refusal_[i] = named_code[i];
                }
                refusal_[n] = '\0';
            }
            return false;
        }

        [[nodiscard]] inline bool ok() const noexcept { return refusal_[0] == '\0'; }
        [[nodiscard]] inline const char *refusal_name() const noexcept { return refusal_; }
        [[nodiscard]] inline std::size_t size() const noexcept { return count_; }
        [[nodiscard]] inline std::size_t arena_size() const noexcept { return arena_used_; }
        [[nodiscard]] inline const StagedEntry &at(std::size_t index) const noexcept {
            return entries_[index];
        }

        /** The string an argument points at, or empty for non-string kinds. */
        [[nodiscard]] inline std::string_view string_of(const StagedArg &arg) const noexcept {
            if (arg.kind != contract::ArgKind::Str) {
                return {};
            }
            return {&arena_[arg.str_offset], static_cast<std::size_t>(arg.str_length)};
        }

        /** Canonical byte image. Returns bytes written, or 0 when too small. */
        [[nodiscard]] inline std::size_t serialize(std::uint8_t *out,
                                                   std::size_t capacity) const noexcept {
            std::size_t used = 0U;
            for (std::size_t i = 0U; i < count_; ++i) {
                const StagedEntry &entry = entries_[i];
                if (!put_u64(out, capacity, used, entry.surface_index) ||
                    !put(out, capacity, used, static_cast<std::uint8_t>(entry.form)) ||
                    !put_u64(out, capacity, used, entry.arg_count)) {
                    return 0U;
                }
                for (std::size_t a = 0U; a < entry.arg_count; ++a) {
                    const StagedArg &arg = entry.args[a];
                    if (!put(out, capacity, used, static_cast<std::uint8_t>(arg.kind))) {
                        return 0U;
                    }
                    if (arg.kind == contract::ArgKind::Str) {
                        const std::string_view text = string_of(arg);
                        if (!put_u64(out, capacity, used, text.size())) {
                            return 0U;
                        }
                        for (std::size_t c = 0U; c < text.size(); ++c) {
                            if (!put(out, capacity, used, static_cast<std::uint8_t>(text[c]))) {
                                return 0U;
                            }
                        }
                    } else if (arg.kind == contract::ArgKind::Bool) {
                        if (!put(out, capacity, used, arg.boolean ? 1U : 0U)) {
                            return 0U;
                        }
                    } else if (arg.kind == contract::ArgKind::Handle) {
                        if (!put_u64(out, capacity, used, arg.handle.kind) ||
                            !put_u64(out, capacity, used, arg.handle.slot) ||
                            !put_u64(out, capacity, used, arg.handle.generation)) {
                            return 0U;
                        }
                    } else if (!put_u64(out, capacity, used, arg.u64)) {
                        return 0U;
                    }
                }
            }
            return used;
        }

      private:
        [[nodiscard]] static inline bool put(std::uint8_t *out, std::size_t capacity,
                                             std::size_t &used, std::uint8_t byte) noexcept {
            if (used >= capacity) {
                return false;
            }
            out[used] = byte;
            ++used;
            return true;
        }

        [[nodiscard]] static inline bool put_u64(std::uint8_t *out, std::size_t capacity,
                                                 std::size_t &used,
                                                 std::uint64_t value) noexcept {
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                if (!put(out, capacity, used, static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
                    return false;
                }
            }
            return true;
        }

        StagedEntry entries_[kMaxStagedEntries];
        std::size_t count_ = 0U;
        char arena_[kStagedArenaBytes];
        std::size_t arena_used_ = 0U;
        /* Zero-initialised: ok() reads the first byte before any refusal exists
         * (a defaulted array would make the first record() see garbage). */
        char refusal_[kRefusalNameBytes]{};
    };

    /** Bind a plan as the sink the sandbox forwards to. */
    [[nodiscard]] inline CallSink staged_sink(StagedPlan &plan) noexcept {
        CallSink sink{};
        sink.context = &plan;
        /* The sink contract has no return channel: a bound hit stays inside the
         * plan as its refusal, which the caller reads after the run. */
        sink.on_op = [](void *context, const CallView *view) {
            (void)static_cast<StagedPlan *>(context)->record(*view);
        };
        sink.on_error = [](void *context, const char *named_code) {
            (void)static_cast<StagedPlan *>(context)->refuse(named_code == nullptr ? "" : named_code);
        };
        return sink;
    }
}  // namespace ghostlock::script
