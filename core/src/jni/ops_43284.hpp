#pragma once
/** @file jni/ops_43284.hpp
 *  43284's ops: the page-cache chain's boundary half, over seams.
 *
 *  Same split as the other backend (J3a/J3b in docs/analysis/jni-two-backend-plan.md): the rules
 *  that can be decided and tested on the host live here -- the sixteen-byte width, the token a
 *  resolved target hands out, which refusal each failure gets, and what the device seams are called
 *  with -- while the device work (the door, the splice surface, the ESP frame, the double fork, the
 *  terminal wait) arrives as function pointers and is implemented on a device.
 *
 *  Two things this unit deliberately does NOT own:
 *   - the sequence. It belongs to the security association, and a window that restarted at one had
 *     every datagram dropped by the kernel's inbound anti-replay window. The boundary keeps the
 *     value the seam returned and hands it back on the next call; who advances it is the seam.
 *   - the crypto. The keys are the credentials' business; this layer never sees them.
 */
#include <cstdint>
#include <span>
#include <string_view>

#include "jni/session_object.hpp"
#include "jni/session_protocol.hpp"

namespace ghostlock::jni {
    /** The block width this backend transfers: one AES-CBC block, sixteen bytes. */
    inline constexpr std::size_t kBlockWidth = 16U;

    /** What a step's own role names. The names travel in the frame; the seam maps them to paths. */
    enum class TargetRole : std::uint64_t {
        Patch1 = 1,   /**< the helper image's target (the bridge's entry). */
        Carrier = 2,  /**< the file the module image is written into. */
        Hook = 3,     /**< the image the sentry hook patches. */
        Count = 4,
    };

    [[nodiscard]] constexpr const char *target_role_name(TargetRole role) noexcept {
        switch (role) {
            case TargetRole::Patch1: return "patch1";
            case TargetRole::Carrier: return "carrier";
            case TargetRole::Hook: return "hook";
            case TargetRole::Count: break;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr TargetRole target_role_of(std::uint64_t raw) noexcept {
        return raw >= 1U && raw < static_cast<std::uint64_t>(TargetRole::Count)
                   ? static_cast<TargetRole>(raw)
                   : TargetRole::Count;
    }

    /** Which terminal state a wait ended in. The names are the wire's, spelled once. */
    enum class TerminalState : std::uint64_t {
        Failure = 0,
        Success = 1,
        InFlight = 2,
    };

    [[nodiscard]] constexpr const char *terminal_state_name(TerminalState state) noexcept {
        switch (state) {
            case TerminalState::Failure: return "failure";
            case TerminalState::Success: return "success";
            case TerminalState::InFlight: return "inflight";
        }
        return "unknown";
    }

    /** What the device side published for this session. Zero means "not resolved yet". */
    struct Facts43284 final {
        std::uint64_t token = 0U;      /**< the target the last ResolveTarget bound. */
        std::uint64_t role = 0U;       /**< which role that token stands for. */
        std::uint64_t sequence = 0U;   /**< the sequence the security association has reached. */
        bool resolved = false;
        bool can_read = false;         /**< whether the door/carrier can be read back at all. */

        /** @return nullptr when a block op may run, or the name of the fact that is absent. */
        [[nodiscard]] const char *ready_for_block() const noexcept {
            if (!resolved || token == 0U) {
                return "not-ready:resolve-target";
            }
            return nullptr;
        }
    };

    /** The flags Snapshot's Flags entry publishes for this backend's facts. */
    inline constexpr std::uint64_t kFactResolved = 1U << 19U;

    /** The device work, as function pointers. A null seam makes its op "not wired" by name. */
    struct Ops43284Seams final {
        /** Bind one door to one role's path and answer a token. */
        bool (*resolve)(std::uint64_t role, std::string_view path, std::uint64_t &token,
                        void *context) noexcept = nullptr;
        /** Write one sixteen-byte block at @p offset through @p token; the seam advances sequence. */
        bool (*write_block)(std::uint64_t token, std::uint64_t offset, std::span<const std::byte> block,
                            std::uint64_t &sequence, void *context) noexcept = nullptr;
        /** Read one sixteen-byte block back. False means this route cannot read (or failed). */
        bool (*read_block)(std::uint64_t token, std::uint64_t offset, std::span<std::byte> block,
                           void *context) noexcept = nullptr;
        /** Arm the hooked path: one double fork, so init reaps the orphan and runs the hook. */
        bool (*trigger)(std::uint64_t delay_ms, void *context) noexcept = nullptr;
        /** Wait for the run's own end state. @param terminal receives which one it was. */
        bool (*wait_terminal)(std::uint64_t budget_ms, std::uint64_t poll_ms, std::string_view module_name,
                              TerminalState &terminal, void *context) noexcept = nullptr;
        /** The bytes this build carries: role 0 = the splice helper, 1 = the sentry landing payload. */
        bool (*embedded_image)(std::uint64_t role, std::span<const std::byte> &image,
                               void *context) noexcept = nullptr;
        /** The hook's numbers: the branch word, the word it displaces, where both and the payload go. */
        bool (*hook_words)(std::uint64_t &branch_word, std::uint64_t &displaced_word,
                           std::uint64_t &site_offset, std::uint64_t &landing_offset,
                           std::span<const std::byte> &payload, void *context) noexcept = nullptr;
        void *context = nullptr;
    };

    /** 43284's ops, over the seams. One instance per session; the session owns it. */
    class Ops43284 final {
      public:
        Ops43284() noexcept = default;
        explicit Ops43284(Ops43284Seams seams) noexcept : seams_(seams) {}

        [[nodiscard]] FrameStatus handle(FrameOp op, const FrameReader &request,
                                         FrameWriter &reply) noexcept;

        [[nodiscard]] const Facts43284 &facts() const noexcept { return facts_; }

      private:
        [[nodiscard]] FrameStatus on_resolve(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_write_block(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_read_block(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_trigger(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_wait_terminal(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_embedded_image(const FrameReader &request, FrameWriter &reply) noexcept;
        [[nodiscard]] FrameStatus on_hook_words(const FrameReader &request, FrameWriter &reply) noexcept;

        Ops43284Seams seams_{};
        Facts43284 facts_{};
    };

    /**
     *  Build this backend's ops as the session's bundle.
     *
     *  @param seams the device work; an empty seam makes its op answer "op-not-wired".
     */
    [[nodiscard]] BackendOps make_ops_43284(Ops43284Seams seams) noexcept;

    /**
     *  The seams this build serves without a device: the bytes it carries and the hook plan it
     *  was built for. Everything else (door, splice, ESP, fork, wait) stays null and answers
     *  op-not-wired until J4b implements it against a device.
     */
    [[nodiscard]] Ops43284Seams device_seams_43284() noexcept;
}  // namespace ghostlock::jni
