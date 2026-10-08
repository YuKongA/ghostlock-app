#ifndef GHOSTLOCK_PLUGIN_REGISTRY_HPP
#define GHOSTLOCK_PLUGIN_REGISTRY_HPP

/* CM-3: runtime registry for out-of-tree countermeasure hooks.
 *
 * //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
 * Authority: docs/archive/20261007-2237-countermeasure-plugin-plan.md sections 5, 6 and 9.
 *
 * The registry borrows hook tables from modules owned by the caller (the
 * plugin::Loader owns the dlopen handle; this layer never
 * sees it). It flattens the declared hooks, sorts them by
 * (stage, priority, registration order), exposes a stage-filtered traversal to
 * the controller, disables a module after a stage failure and accumulates the
 * fields of the diagnostics channel.
 *
 * R1 (ADR-0004): plugin may depend on contract/memory/support only. This
 * header therefore includes contract/countermeasure.hpp for the neutral C++
 * vocabulary and contract/abi/glk_contract_abi.h for the POD hook table, and never
 * plugin/loader.hpp. The caller supplies the host capability
 * and trigger sets (contract::kHostImplementedCaps / kHostImplementedTriggers);
 * the mechanism never enables a module on its own.
 *
 * Mechanism only: no file, no dlopen, no I/O and no allocation on the default
 * attack path (which loads no module). The diagnostics are rendered into a
 * pure string; the caller decides where to emit them. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contract/countermeasure.hpp"
#include "contract/abi/glk_contract_abi.h"

namespace ghostlock::plugin {

    /* Mirrors the loader's fail-closed bounds (plugin/loader.hpp
     * kMaxHooks / kMaxModuleNameLength / kMaxHookNameLength). Kept separate
     * because plugin must not include platform; the loader test and this
     * registry test pin the same numbers. */
    inline constexpr std::uint32_t kMaxExternalHooks = 16u;
    inline constexpr std::size_t kMaxExternalNameLength = 64u;

    /* Adjudicated v1 stages, mirrored from the loader's kHostImplementedStages.
     * PRE_SPAWN/POST_SPAWN/PRE_TERMINAL are the classic chain points; delta-4
     * adds POST_TERMINAL, the only point inside the 43284 LKM residency window
     * (contract-design.md 3.13), so a kernel-state countermeasure can be
     * registered there. PRE_ROUTE stays reserved and must reject. */
    [[nodiscard]] constexpr std::uint32_t external_stage_bit(
            contract::CountermeasureStage stage) noexcept {
        return std::uint32_t{1} << static_cast<std::uint32_t>(stage);
    }

    inline constexpr std::uint32_t kImplementedExternalStages =
            external_stage_bit(contract::CountermeasureStage::PreSpawn) |
            external_stage_bit(contract::CountermeasureStage::PostSpawn) |
            external_stage_bit(contract::CountermeasureStage::PreTerminal) |
            external_stage_bit(contract::CountermeasureStage::PostTerminal);
    static_assert((kImplementedExternalStages &
                   external_stage_bit(contract::CountermeasureStage::PreRoute)) == 0u);

    /* Caller-built neutral view of one loaded module. All pointers are borrowed
     * from storage the caller owns (the loader's static hook table) and must
     * outlive the registry. */
    struct ExternalModuleBinding final {
        const char *name = nullptr;
        const char *version = nullptr;
        contract::Capability required_caps = contract::Capability::None;
        const glk_hook *hooks = nullptr;
        std::uint32_t hook_count = 0u;
    };

    /* One flattened, borrowed hook. registration is the monotonic index that
     * breaks a (stage, priority) tie in declaration order; module_slot indexes
     * the module records so one failure can disable the whole module. */
    struct RegistryHook final {
        contract::CountermeasureStage stage = contract::CountermeasureStage::PreSpawn;
        std::uint32_t priority = 0u;
        glk_stage_fn fn = nullptr;
        void *user = nullptr;
        const char *module_name = nullptr;
        const char *hook_name = nullptr;
        std::uint32_t registration = 0u;
        std::uint32_t module_slot = 0u;
    };

    enum class RegistryRejectReason : std::uint8_t {
        None = 0,
        InvalidModule,
        UnknownCapability,
        MissingCapability,
        TooManyHooks,
        InvalidHook,
        ReservedTrigger,
        ReservedStage,
    };

    [[nodiscard]] constexpr const char *registry_reject_reason_name(
            RegistryRejectReason reason) noexcept {
        switch (reason) {
        case RegistryRejectReason::None: return "none";
        case RegistryRejectReason::InvalidModule: return "invalid_module";
        case RegistryRejectReason::UnknownCapability: return "unknown_capability";
        case RegistryRejectReason::MissingCapability: return "missing_capability";
        case RegistryRejectReason::TooManyHooks: return "too_many_hooks";
        case RegistryRejectReason::InvalidHook: return "invalid_hook";
        case RegistryRejectReason::ReservedTrigger: return "reserved_trigger";
        case RegistryRejectReason::ReservedStage: return "reserved_stage";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr const char *countermeasure_stage_name(
            contract::CountermeasureStage stage) noexcept {
        switch (stage) {
        case contract::CountermeasureStage::PreSpawn: return "pre_spawn";
        case contract::CountermeasureStage::PostSpawn: return "post_spawn";
        case contract::CountermeasureStage::PreTerminal: return "pre_terminal";
        case contract::CountermeasureStage::PreRoute: return "pre_route";
        case contract::CountermeasureStage::PostTerminal: return "post_terminal";
        }
        return "unknown";
    }

    struct RegistryModuleRecord final {
        const char *name = nullptr;
        const char *version = nullptr;
        std::uint32_t hook_count = 0u;
        bool accepted = false;
        RegistryRejectReason reason = RegistryRejectReason::None;
    };

    struct RegistryFailureRecord final {
        const char *module_name = nullptr;
        const char *hook_name = nullptr;
        contract::CountermeasureStage stage = contract::CountermeasureStage::PreSpawn;
        std::int32_t code = 0;
    };

    class RuntimeRegistry final {
    public:
        RuntimeRegistry() noexcept = default;
        RuntimeRegistry(contract::Capability host_caps,
                        std::uint32_t host_triggers) noexcept
            : host_caps_(host_caps), host_triggers_(host_triggers) {}

        void reset(contract::Capability host_caps,
                   std::uint32_t host_triggers) noexcept {
            host_caps_ = host_caps;
            host_triggers_ = host_triggers;
            hooks_.clear();
            failed_.clear();
            modules_.clear();
            failures_.clear();
            next_registration_ = 0u;
            registered_ = 0u;
            rejected_ = 0u;
            skipped_ = 0u;
        }

        /* Register one module. Returns true when every hook was accepted and
         * installed; on any rejected invariant the whole module is refused
         * (fail-closed, no partial registration) and both a module record and a
         * rejection reason are retained for the diagnostics. */
        [[nodiscard]] bool register_module(const ExternalModuleBinding &module);

        /* Visit the hooks of one stage in (priority, registration) order. Hooks
         * of a module disabled by an earlier stage failure are skipped. The
         * callback receives a borrowed const RegistryHook reference. */
        template <class Fn>
        void for_each(contract::CountermeasureStage stage, Fn &&fn) const {
            for (const RegistryHook &hook : hooks_) {
                if (hook.stage != stage) {
                    continue;
                }
                if (module_failed_slot(hook.module_slot)) {
                    continue;
                }
                fn(hook);
            }
        }

        /* Diagnostics: remember that the hook returned the given code. */
        void record_stage_failure(const RegistryHook &hook, std::int32_t code);

        /* Disable every later stage of the module owning slot. Idempotent. */
        void mark_module_failed(std::uint32_t module_slot) noexcept;

        [[nodiscard]] std::uint32_t modules_offered() const noexcept {
            return static_cast<std::uint32_t>(modules_.size());
        }
        [[nodiscard]] std::uint32_t modules_registered() const noexcept {
            return registered_;
        }
        [[nodiscard]] std::uint32_t modules_rejected() const noexcept {
            return rejected_;
        }
        [[nodiscard]] std::uint32_t hooks_registered() const noexcept {
            return static_cast<std::uint32_t>(hooks_.size());
        }
        [[nodiscard]] std::uint32_t stage_failures() const noexcept {
            return static_cast<std::uint32_t>(failures_.size());
        }
        [[nodiscard]] std::uint32_t modules_skipped() const noexcept {
            return skipped_;
        }

        [[nodiscard]] const std::vector<RegistryModuleRecord> &module_records()
                const noexcept {
            return modules_;
        }
        [[nodiscard]] const std::vector<RegistryFailureRecord> &failures()
                const noexcept {
            return failures_;
        }

    private:
        [[nodiscard]] bool module_failed_slot(std::uint32_t slot) const noexcept {
            return slot < failed_.size() && failed_[slot] != 0u;
        }

        contract::Capability host_caps_ = contract::Capability::None;
        std::uint32_t host_triggers_ = 0u;
        std::vector<RegistryHook> hooks_{};
        std::vector<std::uint8_t> failed_{};
        std::vector<RegistryModuleRecord> modules_{};
        std::vector<RegistryFailureRecord> failures_{};
        std::uint32_t next_registration_ = 0u;
        std::uint32_t registered_ = 0u;
        std::uint32_t rejected_ = 0u;
        std::uint32_t skipped_ = 0u;
    };

    namespace detail {
        /* Length of a NUL-terminated string capped at max. Returns max + 1 when
         * no NUL is found within max bytes, which the caller treats as invalid.
         * Same contract as the loader's bounded_length, duplicated here because
         * plugin must not include platform. */
        [[nodiscard]] inline std::size_t external_bounded_length(
                const char *text, std::size_t max) noexcept {
            if (text == nullptr) {
                return 0u;
            }
            std::size_t len = 0u;
            while (len <= max && text[len] != '\0') {
                ++len;
            }
            return len;
        }
    } // namespace detail

    inline bool RuntimeRegistry::register_module(const ExternalModuleBinding &module) {
        const std::uint32_t slot = static_cast<std::uint32_t>(modules_.size());
        RegistryModuleRecord record{};
        record.name = module.name;
        record.version = module.version;
        record.hook_count = module.hook_count;

        const auto reject = [&](RegistryRejectReason reason) {
            record.accepted = false;
            record.reason = reason;
            modules_.push_back(record);
            failed_.push_back(0u);
            ++rejected_;
            return false;
        };

        const std::size_t name_len =
                detail::external_bounded_length(module.name, kMaxExternalNameLength);
        if (name_len == 0u || name_len > kMaxExternalNameLength) {
            return reject(RegistryRejectReason::InvalidModule);
        }
        const std::uint32_t required = static_cast<std::uint32_t>(module.required_caps);
        const std::uint32_t known = static_cast<std::uint32_t>(contract::kAllCapabilities);
        if ((required & ~known) != 0u) {
            return reject(RegistryRejectReason::UnknownCapability);
        }
        if ((required & ~static_cast<std::uint32_t>(host_caps_)) != 0u) {
            return reject(RegistryRejectReason::MissingCapability);
        }
        if (module.hook_count == 0u || module.hooks == nullptr) {
            return reject(RegistryRejectReason::InvalidHook);
        }
        if (module.hook_count > kMaxExternalHooks) {
            return reject(RegistryRejectReason::TooManyHooks);
        }
        for (std::uint32_t i = 0u; i < module.hook_count; ++i) {
            const glk_hook &hook = module.hooks[i];
            if (hook.fn == nullptr) {
                return reject(RegistryRejectReason::InvalidHook);
            }
            const std::size_t hook_name_len =
                    detail::external_bounded_length(hook.name, kMaxExternalNameLength);
            if (hook_name_len == 0u || hook_name_len > kMaxExternalNameLength) {
                return reject(RegistryRejectReason::InvalidHook);
            }
            const std::uint32_t trigger = static_cast<std::uint32_t>(hook.trigger);
            const std::uint32_t trigger_mask =
                    trigger < 32u ? (std::uint32_t{1} << trigger) : 0u;
            if (trigger_mask == 0u || (host_triggers_ & trigger_mask) == 0u) {
                return reject(RegistryRejectReason::ReservedTrigger);
            }
            if (static_cast<std::uint32_t>(hook.trigger) == GLK_TRIGGER_ON_STAGE) {
                const std::uint32_t stage = static_cast<std::uint32_t>(hook.stage);
                const std::uint32_t stage_mask =
                        stage < 32u ? (std::uint32_t{1} << stage) : 0u;
                if (stage_mask == 0u ||
                    (kImplementedExternalStages & stage_mask) == 0u) {
                    return reject(RegistryRejectReason::ReservedStage);
                }
            }
        }

        hooks_.reserve(hooks_.size() + module.hook_count);
        for (std::uint32_t i = 0u; i < module.hook_count; ++i) {
            const glk_hook &hook = module.hooks[i];
            RegistryHook entry{};
            entry.stage = static_cast<contract::CountermeasureStage>(
                    static_cast<std::uint32_t>(hook.stage));
            entry.priority = hook.priority;
            entry.fn = hook.fn;
            entry.user = hook.user;
            entry.module_name = module.name;
            entry.hook_name = hook.name;
            entry.registration = next_registration_;
            entry.module_slot = slot;
            ++next_registration_;
            hooks_.push_back(entry);
        }
        /* Stable by construction: the globally unique registration index is the
         * final key, so equal (stage, priority) keep declaration order. */
        std::sort(hooks_.begin(), hooks_.end(),
                  [](const RegistryHook &lhs, const RegistryHook &rhs) {
                      const std::uint32_t lhs_stage =
                              static_cast<std::uint32_t>(lhs.stage);
                      const std::uint32_t rhs_stage =
                              static_cast<std::uint32_t>(rhs.stage);
                      if (lhs_stage != rhs_stage) {
                          return lhs_stage < rhs_stage;
                      }
                      if (lhs.priority != rhs.priority) {
                          return lhs.priority < rhs.priority;
                      }
                      return lhs.registration < rhs.registration;
                  });

        record.accepted = true;
        record.reason = RegistryRejectReason::None;
        modules_.push_back(record);
        failed_.push_back(0u);
        ++registered_;
        return true;
    }

    inline void RuntimeRegistry::record_stage_failure(const RegistryHook &hook,
                                                      std::int32_t code) {
        RegistryFailureRecord failure{};
        failure.module_name = hook.module_name;
        failure.hook_name = hook.hook_name;
        failure.stage = hook.stage;
        failure.code = code;
        failures_.push_back(failure);
    }

    inline void RuntimeRegistry::mark_module_failed(std::uint32_t module_slot) noexcept {
        if (module_slot >= failed_.size() || failed_[module_slot] != 0u) {
            return;
        }
        failed_[module_slot] = 1u;
        ++skipped_;
    }

    /* Outcome of one synchronous external dispatch. called counts the hooks
     * actually invoked; failed counts non-zero returns. ok() is false when any
     * hook failed, mirroring PluginController's external dispatch: the stage,
     * not this mechanism, decides whether to abort. */
    struct RegistryRunOutcome final {
        std::uint32_t called = 0U;
        std::uint32_t failed = 0U;

        [[nodiscard]] bool ok() const noexcept { return failed == 0U; }
    };

    /* Reusable plugin-side traversal: invoke every accepted hook of one stage in
     * (priority, registration) order, synchronously and one at a time, passing
     * the host contract ops. A non-zero return is recorded and disables the
     * owning module for its later stages; dispatch continues to the next hook
     * (for_each then skips the disabled module's remaining hooks). Keeping this
     * in the plugin layer lets backend window owners call it without any
     * plugin -> backend dependency. */
    [[nodiscard]] inline RegistryRunOutcome run_registry_stage(
            RuntimeRegistry &registry, contract::CountermeasureStage stage,
            const glk_contract_ops *host) {
        RegistryRunOutcome outcome{};
        registry.for_each(stage, [&](const RegistryHook &hook) {
            ++outcome.called;
            const std::int32_t rc = hook.fn(
                    hook.user,
                    static_cast<glk_stage>(static_cast<std::uint32_t>(hook.stage)),
                    host);
            if (rc != 0) {
                ++outcome.failed;
                registry.record_stage_failure(hook, rc);
                registry.mark_module_failed(hook.module_slot);
            }
        });
        return outcome;
    }

    /* Pure, field-structured rendering of the registry diagnostics. One
     * grep-able record per line on the run.countermeasure channel; the caller
     * decides where to emit it (this layer never writes). */
    [[nodiscard]] inline std::string format_registry_diagnostics(
            const RuntimeRegistry &registry) {
        std::string out;
        out.reserve(256u);
        out += "run.countermeasure registry offered=";
        out += std::to_string(registry.modules_offered());
        out += " registered=";
        out += std::to_string(registry.modules_registered());
        out += " rejected=";
        out += std::to_string(registry.modules_rejected());
        out += " hooks=";
        out += std::to_string(registry.hooks_registered());
        out += " failures=";
        out += std::to_string(registry.stage_failures());
        out += " skipped=";
        out += std::to_string(registry.modules_skipped());
        out += '\n';
        for (const RegistryModuleRecord &module : registry.module_records()) {
            out += "run.countermeasure module name=";
            out += (module.name != nullptr ? module.name : "-");
            out += " version=";
            out += (module.version != nullptr ? module.version : "-");
            out += " accepted=";
            out += (module.accepted ? "1" : "0");
            out += " hooks=";
            out += std::to_string(module.hook_count);
            if (!module.accepted) {
                out += " reason=";
                out += registry_reject_reason_name(module.reason);
            }
            out += '\n';
        }
        for (const RegistryFailureRecord &failure : registry.failures()) {
            out += "run.countermeasure failure module=";
            out += (failure.module_name != nullptr ? failure.module_name : "-");
            out += " hook=";
            out += (failure.hook_name != nullptr ? failure.hook_name : "-");
            out += " stage=";
            out += countermeasure_stage_name(failure.stage);
            out += " rc=";
            out += std::to_string(failure.code);
            out += '\n';
        }
        return out;
    }

} // namespace ghostlock::plugin

#endif
