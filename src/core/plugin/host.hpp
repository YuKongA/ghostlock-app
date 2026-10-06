#ifndef GHOSTLOCK_PLUGIN_HOST_HPP
#define GHOSTLOCK_PLUGIN_HOST_HPP

/* P1 step 2: the plugin host -- register -> window-gated load -> stage dispatch
 * -> unload (design docs/analysis/plugin-runtime-integration-design.md sections
 * 4, 5, 7, 11.5, 12 and 13.2; frozen contract docs/analysis/contract-design.md
 * section 3.14.7.8).
 *
 * Shape of one run, and where the authority for each rule lives:
 *
 *   from_document()  REGISTERS the enabled plugins of the already-validated
 *                    document. It never dlopens: the P1 validator
 *                    (plugin/wire.hpp) is the one authority for the plugin
 *                    section shape, and plugin_dynamic_key()/Schema own the
 *                    field vocabulary. $GHOSTLOCK_HOME/countermeasures is the
 *                    one root every relative module_path is resolved against
 *                    (contract-design 3.14.7.4, the same convention the probe
 *                    reports as countermeasures_root).
 *   open(WindowState)
 *                    R1 (design section 4, non-negotiable): no plugin mapping
 *                    may exist while the PI waiter is alive. open() therefore
 *                    takes the window state and REFUSES WaiterAlive: it counts
 *                    open_rejected, records WindowNotClosed, does not dlopen
 *                    and returns false. With WaiterClosed it validates each
 *                    registered plugin against the per-backend stage matrix
 *                    (stage_available_on(), the ONE authority -- the matrix is
 *                    never copied here), loads it through plugin/loader (the
 *                    only dlopen path) and installs the surviving hooks into a
 *                    per-plugin RuntimeRegistry. Every failure is fail-soft:
 *                    a missing/tampered/incompatible module, an unavailable
 *                    stage and a hook-less module are counted and recorded,
 *                    never escalated to the attack chain (design section 5).
 *   dispatch(stage, ctx)
 *                    runs the hooks of one stage for the plugins registered for
 *                    that stage. It is inert before open() and after close()
 *                    (skipped accounting, never an implicit load) and it runs a
 *                    plugin's stage at most once, in strictly increasing stage
 *                    order (design section 8, items 2 and 4). A non-zero hook
 *                    return is recorded by the shared registry traversal
 *                    (run_registry_stage) and disables that plugin's later
 *                    hooks; other plugins are unaffected.
 *   close()          unloads every module in REVERSE registration order. Each
 *                    registry is cleared BEFORE its handle is released, so a
 *                    borrowed hook pointer into the module image can never be
 *                    traversed after the image is gone (design section 4, UAF
 *                    checklist). close() is idempotent and terminal; the caller
 *                    that owns a residency window (43284: the LKM window) closes
 *                    that window BEFORE calling this, per the fixed teardown
 *                    order in design section 4.
 *
 * Default-off equivalence: without a plugin section the host has no entries and
 * every operation is a no-op with zero counters, i.e. byte-for-byte today's run.
 *
 * R1 (ADR-0004 include firewall): plugin -> {contract, memory, support}; this
 * header reaches contract/, plugin/ and profile/ only, never backend/, pipeline/,
 * platform/ or terminal/. Nothing here decides whether the attack chain fails:
 * the host only counts. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contract/abi/glk_contract_abi.h"
#include "contract/countermeasure.hpp"
#include "plugin/loader.hpp"
#include "plugin/registry.hpp"
#include "plugin/schema.hpp"
#include "plugin/wire.hpp"
#include "profile/document.hpp"

namespace ghostlock::plugin {

    /* The PI waiter window, as seen by the host. R1: a plugin .so may only be
     * mapped once the waiter is gone, so the caller must state which window it
     * is in; the host never infers it. */
    enum class WindowState : std::uint8_t {
        WaiterAlive = 0,
        WaiterClosed,
    };

    /* The four host stage boundaries, in dispatch order. The values are the
     * ordering key the host enforces (strictly increasing, at most once per
     * plugin), so they are pinned rather than left implicit. */
    enum class HostStage : std::uint8_t {
        PreSpawn = 0,
        PostSpawn = 1,
        PreTerminal = 2,
        PostTerminal = 3,
    };

    static_assert(HostStage::PreSpawn < HostStage::PostSpawn);
    static_assert(HostStage::PostSpawn < HostStage::PreTerminal);
    static_assert(HostStage::PreTerminal < HostStage::PostTerminal);

    /* Sentinel for "no stage yet" / "not attributable" in HostRecord. */
    inline constexpr std::uint8_t kNoHostStage = 0xFFu;

    [[nodiscard]] constexpr std::uint8_t host_stage_index(HostStage stage) noexcept {
        return static_cast<std::uint8_t>(stage);
    }

    /* HostStage -> the ABI stage vocabulary. The C ABI is the wire truth, so the
     * mapping is explicit in both directions and no second token table exists. */
    [[nodiscard]] constexpr contract::CountermeasureStage to_countermeasure_stage(
            HostStage stage) noexcept {
        switch (stage) {
        case HostStage::PreSpawn: return contract::CountermeasureStage::PreSpawn;
        case HostStage::PostSpawn: return contract::CountermeasureStage::PostSpawn;
        case HostStage::PreTerminal: return contract::CountermeasureStage::PreTerminal;
        case HostStage::PostTerminal: return contract::CountermeasureStage::PostTerminal;
        }
        return contract::CountermeasureStage::PreSpawn;
    }

    /* The token spelling comes from contract/countermeasure.hpp, so the probe
     * TSV, the wire vocabulary and the host diagnostics share one table. */
    [[nodiscard]] constexpr std::string_view host_stage_token(HostStage stage) noexcept {
        return contract::stage_token(to_countermeasure_stage(stage));
    }

    /* Parses a validated stage token (plugin_stage_token_valid) back into the
     * host enum; false when the token is not one of the four. */
    [[nodiscard]] constexpr bool host_stage_from_token(
            std::string_view token, HostStage &out) noexcept {
        constexpr HostStage kStages[] = {HostStage::PreSpawn, HostStage::PostSpawn,
                                         HostStage::PreTerminal, HostStage::PostTerminal};
        for (const HostStage candidate : kStages) {
            if (host_stage_token(candidate) == token) {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    /* Counters only; they never change control flow (design section 5). The
     * meanings are fixed here so step 3 and the tests agree on them:
     *   loaded            modules dlopen'd and registered (one per plugin)
     *   load_failed       Loader::load refused the module (fail-soft)
     *   rejected          plugins refused as a whole: a plugin wire failure of
     *                     the document, a declared stage unavailable on this
     *                     backend, a module whose every hook was unavailable,
     *                     or a registry refusal
     *   called            hook invocations
     *   hook_failed       hook invocations that returned non-zero
     *   skipped           stage dispatches for an armed plugin that did not run:
     *                     before open(), after close(), or a repeated/regressed
     *                     stage
     *   open_rejected     open(WaiterAlive) refusals (R1)
     *   stage_unavailable hooks refused because their stage is not available on
     *                     the selected backend (StageUnavailableOnBackend) */
    struct HostDiagnostics final {
        std::uint32_t loaded = 0u;
        std::uint32_t load_failed = 0u;
        std::uint32_t rejected = 0u;
        std::uint32_t called = 0u;
        std::uint32_t hook_failed = 0u;
        std::uint32_t skipped = 0u;
        std::uint32_t open_rejected = 0u;
        std::uint32_t stage_unavailable = 0u;
    };

    /* Per-call values handed to a hook. The ops table is BORROWED: the window
     * owner keeps it alive, and the host passes the pointer through unchanged
     * (it never inspects it). A null pointer is legal and reaches the hook as
     * null -- the same "no capability view" state the caller chose. */
    struct PluginCallContext final {
        const glk_contract_ops *host = nullptr;
    };

    /* Named refusal/failure reasons (design section 13.2 requires the reason to
     * be named, never expressed by silent absence). */
    enum class HostReason : std::uint8_t {
        None = 0,
        WindowNotClosed,           /* open(WaiterAlive) refused: R1 */
        WireRejected,              /* the document plugin section failed P1 validation */
        StageUnavailableOnBackend, /* declared stage or hook stage not in the matrix */
        NoUsableHooks,             /* every declared hook was rejected */
        LoadFailed,                /* Loader::load refused the module (fail-soft) */
        RegistrationRejected,      /* the registry refused the surviving hooks */
        HookFailed,                /* a hook returned non-zero */
    };

    [[nodiscard]] constexpr const char *host_reason_name(HostReason reason) noexcept {
        switch (reason) {
        case HostReason::None: return "None";
        case HostReason::WindowNotClosed: return "WindowNotClosed";
        case HostReason::WireRejected: return "WireRejected";
        case HostReason::StageUnavailableOnBackend: return "StageUnavailableOnBackend";
        case HostReason::NoUsableHooks: return "NoUsableHooks";
        case HostReason::LoadFailed: return "LoadFailed";
        case HostReason::RegistrationRejected: return "RegistrationRejected";
        case HostReason::HookFailed: return "HookFailed";
        }
        return "Unknown";
    }

    /* One bounded, allocation-free diagnostic record. entry indexes the
     * registration order (kNoEntry when the event is not attributable to one
     * plugin); stage is a host_stage_index() or kNoHostStage; status carries the
     * LoadStatus value (LoadFailed) or the hook return code (HookFailed). */
    inline constexpr std::uint16_t kNoHostEntry = 0xFFFFu;

    struct HostRecord final {
        HostReason reason = HostReason::None;
        std::int32_t status = 0;
        std::uint16_t entry = kNoHostEntry;
        std::uint8_t stage = kNoHostStage;
    };

    /* Bounded so recording stays noexcept (no allocation on any path). Counting
     * never stops when the bound is reached; only the record list does. */
    inline constexpr std::size_t kMaxHostRecords = 64u;

    /* RAII owner of one run's plugin state. Move-only; the composition root
     * holds it by value for the run scope and the pipeline borrows a pointer. */
    class PluginHost final {
    public:
        /* Register the enabled plugins of an already-validated document. No
         * dlopen happens here; the file is not even read. */
        [[nodiscard]] static PluginHost from_document(
                const profile::Document &document, RuntimeBackend backend,
                const LoaderOps &ops = default_loader_ops());

        PluginHost(PluginHost &&other) noexcept = default;
        PluginHost &operator=(PluginHost &&other) noexcept = default;
        ~PluginHost();

        PluginHost(const PluginHost &) = delete;
        PluginHost &operator=(const PluginHost &) = delete;

        /* Open the host for the stated waiter window. Returns false ONLY when
         * the window is still alive (R1 refusal, open_rejected++) or when the
         * host is already closed; individual plugin failures are counted and
         * never fail the attack chain. Idempotent while open. */
        [[nodiscard]] bool open(WindowState window) noexcept;

        /* Run every hook of stage for the plugins registered for that stage, in
         * (priority, registration) order. Inert (skipped++) before open() and
         * after close(); never loads implicitly. */
        void dispatch(HostStage stage, const PluginCallContext &context) noexcept;

        /* Idempotent, terminal: clear every registry, then unload every module in
         * reverse registration order. */
        void close() noexcept;

        [[nodiscard]] const HostDiagnostics &diagnostics() const noexcept {
            return diagnostics_;
        }
        [[nodiscard]] std::span<const HostRecord> records() const noexcept {
            return {records_.data(), record_count_};
        }
        [[nodiscard]] std::size_t registered() const noexcept { return entries_.size(); }
        [[nodiscard]] std::size_t loaded() const noexcept { return loaded_; }
        [[nodiscard]] bool is_open() const noexcept { return open_; }
        [[nodiscard]] bool is_closed() const noexcept { return closed_; }
        /* The root every relative module_path is resolved against; empty when
         * $GHOSTLOCK_HOME is unset, in which case every load fails soft. */
        [[nodiscard]] const std::string &countermeasures_root() const noexcept {
            return root_;
        }

        /* One grep-able block, in the run.countermeasure diagnostic style. The
         * caller decides where to emit it (this layer never writes). */
        [[nodiscard]] std::string format_diagnostics() const;

    private:
        /* One registered plugin. Identifiers are OWNED copies: the document may
         * die while the host lives, and a borrowed view is exactly the UAF class
         * design section 4 warns about. */
        struct Entry final {
            std::string id{};
            std::string module_path{};
            std::string module_hash{};
            HostStage stage = HostStage::PreSpawn;
            bool armed = false;             /* loaded and hooks registered */
            bool failed = false;            /* a hook returned non-zero */
            std::uint8_t last_stage = kNoHostStage;
            RuntimeRegistry registry{};
            LoadedModule module{};
        };

        PluginHost() = default;

        void register_document(const profile::Document &document);
        void record(HostReason reason, std::size_t entry, std::uint8_t stage,
                    std::int32_t status) noexcept;

        std::vector<Entry> entries_{};
        std::array<HostRecord, kMaxHostRecords> records_{};
        std::size_t record_count_ = 0u;
        std::size_t loaded_ = 0u;
        HostDiagnostics diagnostics_{};
        RuntimeBackend backend_ = RuntimeBackend::Cve2026_43499;
        LoaderOps ops_{};
        std::string root_{};
        bool open_ = false;
        bool closed_ = false;
    };

} // namespace ghostlock::plugin

#endif
