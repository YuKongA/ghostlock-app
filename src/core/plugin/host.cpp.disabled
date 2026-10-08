/* P1 step 2 plugin host implementation. See plugin/host.hpp for the ordered
 * rules and their authority (design sections 4, 5, 7, 11.5, 12, 13.2;
 * contract-design 3.14.7.8). Nothing here writes to a log or decides control
 * flow: the host counts, and the caller reads diagnostics(). */

#include "plugin/host.hpp"

#include "plugin/host_ops.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <utility>

namespace ghostlock::plugin {
    namespace {

        /* Reverse of to_countermeasure_stage for diagnostics only: a hook whose
         * stage is outside the four host boundaries (the loader already rejected
         * PRE_ROUTE) is reported as "no stage" rather than guessed. */
        [[nodiscard]] std::uint8_t host_slot_of(
                contract::CountermeasureStage stage) noexcept {
            switch (stage) {
            case contract::CountermeasureStage::PreSpawn:
                return host_stage_index(HostStage::PreSpawn);
            case contract::CountermeasureStage::PostSpawn:
                return host_stage_index(HostStage::PostSpawn);
            case contract::CountermeasureStage::PreTerminal:
                return host_stage_index(HostStage::PreTerminal);
            case contract::CountermeasureStage::PostTerminal:
                return host_stage_index(HostStage::PostTerminal);
            case contract::CountermeasureStage::PreRoute:
                break;
            }
            return kNoHostStage;
        }

        /* Slot -> token for the diagnostic rendering. It must not be a cast:
         * the ABI skips PRE_ROUTE (3), so host slot 3 is POST_TERMINAL while
         * CountermeasureStage{3} is PRE_ROUTE. */
        [[nodiscard]] std::string_view stage_token_of_slot(std::uint8_t slot) noexcept {
            switch (slot) {
            case host_stage_index(HostStage::PreSpawn):
                return host_stage_token(HostStage::PreSpawn);
            case host_stage_index(HostStage::PostSpawn):
                return host_stage_token(HostStage::PostSpawn);
            case host_stage_index(HostStage::PreTerminal):
                return host_stage_token(HostStage::PreTerminal);
            case host_stage_index(HostStage::PostTerminal):
                return host_stage_token(HostStage::PostTerminal);
            default:
                break;
            }
            return "unknown";
        }

    } // namespace

    PluginHost::~PluginHost() { close(); }

    PluginHost PluginHost::from_document(const profile::Document &document,
                                         RuntimeBackend backend,
                                         const LoaderOps &ops) {
        PluginHost host;
        host.backend_ = backend;
        host.ops_ = ops;
        /* The P1 root convention (contract-design 3.14.7.4): relative
         * module_path values resolve inside <GHOSTLOCK_HOME>/countermeasures.
         * An unset/empty home yields an empty root, which the loader rejects
         * fail-soft (every plugin is counted as load_failed, never loaded). */
        host.root_ = default_countermeasure_dir(std::getenv("GHOSTLOCK_HOME"));
        host.register_document(document);
        return host;
    }

    void PluginHost::register_document(const profile::Document &document) {
        std::array<PluginWireEntry, kMaxPluginsPerDocument> wire{};
        const PluginWireResult result =
                validate_plugin_wire(document, wire.data(), wire.size());
        if (result.error != PluginWireError::None) {
            /* Fail-soft here: the production path already aborts on this before
             * the host exists (main.cpp), so reaching this branch means the
             * caller skipped the P1 gate. Record it and register nothing. */
            ++diagnostics_.rejected;
            record(HostReason::WireRejected, kNoHostEntry, kNoHostStage,
                   static_cast<std::int32_t>(result.error));
            return;
        }
        entries_.reserve(result.accepted);
        for (std::size_t i = 0u; i < result.accepted; ++i) {
            HostStage stage = HostStage::PreSpawn;
            if (!host_stage_from_token(wire[i].stage, stage)) {
                /* The P1 validator only accepts the four tokens, so this is
                 * unreachable by construction; refuse rather than guess. */
                ++diagnostics_.rejected;
                record(HostReason::WireRejected, kNoHostEntry, kNoHostStage,
                       static_cast<std::int32_t>(PluginWireError::StageUnknown));
                continue;
            }
            Entry entry{};
            entry.id.assign(wire[i].id);
            entry.module_path.assign(wire[i].module_path);
            entry.module_hash.assign(wire[i].module_hash);
            entry.stage = stage;
            entries_.push_back(std::move(entry));
        }
    }

    void PluginHost::record(HostReason reason, std::size_t entry,
                            std::uint8_t stage, std::int32_t status) noexcept {
        if (record_count_ >= kMaxHostRecords) {
            return; /* Counters keep the evidence; the record list is bounded. */
        }
        HostRecord &slot = records_[record_count_];
        slot.reason = reason;
        slot.status = status;
        slot.entry = static_cast<std::uint16_t>(entry);
        slot.stage = stage;
        ++record_count_;
    }

    bool PluginHost::open(WindowState window) noexcept {
        if (closed_) {
            return false; /* Terminal: a closed host never reopens. */
        }
        if (open_) {
            return true; /* Idempotent: never reload a module. */
        }
        /* R1: no plugin mapping while the PI waiter is alive. The refused call
         * is NOT terminal -- the caller may retry once the window closed. */
        if (window != WindowState::WaiterClosed) {
            ++diagnostics_.open_rejected;
            record(HostReason::WindowNotClosed, kNoHostEntry, kNoHostStage, 0);
            return false;
        }

        /* The loader is the only dlopen path; it lives for the load loop and
         * every LoadedModule it returns owns its own handle afterwards. */
        const Loader loader(root_, ops_);
        for (std::size_t index = 0u; index < entries_.size(); ++index) {
            Entry &entry = entries_[index];
            const std::uint8_t doc_stage = host_stage_index(entry.stage);
            /* The declared stage must be usable on THIS backend. The matrix has
             * exactly one authority (stage_available_on) and is never copied. */
            if (!stage_available_on(backend_, to_countermeasure_stage(entry.stage))) {
                ++diagnostics_.rejected;
                record(HostReason::StageUnavailableOnBackend, index, doc_stage, 0);
                continue;
            }

            LoadResult result = loader.load(
                    entry.module_path.c_str(), entry.module_hash.c_str(),
                    contract::kHostImplementedCaps, contract::kHostImplementedTriggers);
            if (result.status != LoadStatus::Ok) {
                /* Fail-soft (design section 5): the attack chain continues. */
                ++diagnostics_.load_failed;
                record(HostReason::LoadFailed, index, doc_stage,
                       static_cast<std::int32_t>(result.status));
                continue;
            }

            /* Hook-level matrix (design section 13.2): a hook whose stage is not
             * available on this backend is REJECTED AS A HOOK and accounted --
             * the module survives for its other hooks. The filtered table is a
             * stack copy; the registry only borrows fn/user/name, and name
             * points into the module image that stays mapped below. */
            std::array<glk_hook, kMaxHooks> usable{};
            std::uint32_t usable_count = 0u;
            for (std::uint32_t hook_index = 0u; hook_index < result.hook_count;
                 ++hook_index) {
                const glk_hook &hook = result.hooks[hook_index];
                const auto hook_stage = static_cast<contract::CountermeasureStage>(
                        static_cast<std::uint32_t>(hook.stage));
                if (!stage_available_on(backend_, hook_stage)) {
                    ++diagnostics_.stage_unavailable;
                    record(HostReason::StageUnavailableOnBackend, index,
                           host_slot_of(hook_stage), 0);
                    continue;
                }
                usable[usable_count] = hook;
                ++usable_count;
            }
            if (usable_count == 0u) {
                /* Every hook was refused: no reason to keep the mapping. */
                ++diagnostics_.rejected;
                record(HostReason::NoUsableHooks, index, doc_stage, 0);
                continue; /* result owns the handle and closes it here */
            }

            entry.registry.reset(contract::kHostImplementedCaps,
                                 contract::kHostImplementedTriggers);
            const ExternalModuleBinding binding{result.module.name(),
                                                result.module.version(),
                                                result.required_caps, usable.data(),
                                                usable_count};
            if (!entry.registry.register_module(binding)) {
                ++diagnostics_.rejected;
                std::int32_t reason = 0;
                const auto &modules = entry.registry.module_records();
                if (!modules.empty()) {
                    reason = static_cast<std::int32_t>(modules.back().reason);
                }
                record(HostReason::RegistrationRejected, index, doc_stage, reason);
                continue;
            }

            entry.module = std::move(result.module);
            entry.armed = true;
            ++loaded_;
            ++diagnostics_.loaded;
        }
        open_ = true;
        return true;
    }

    void PluginHost::dispatch(HostStage stage,
                              const PluginCallContext &context) noexcept {
        const std::uint8_t index = host_stage_index(stage);
        for (std::size_t i = 0u; i < entries_.size(); ++i) {
            Entry &entry = entries_[i];
            /* Only the plugins registered for this stage take part; a stage the
             * backend does not expose is never dispatched at all. */
            if (entry.stage != stage) {
                continue;
            }
            if (!stage_available_on(backend_, to_countermeasure_stage(entry.stage))) {
                continue;
            }
            /* Not open (never opened, or closed) and not armed (load failed or
             * refused): skip and account, never load implicitly. */
            if (!open_ || closed_ || !entry.armed) {
                ++diagnostics_.skipped;
                continue;
            }
            /* Strictly increasing stages, at most once per plugin per stage. */
            if (entry.last_stage != kNoHostStage && entry.last_stage >= index) {
                ++diagnostics_.skipped;
                continue;
            }
            entry.last_stage = index;
            /* S4 logging batch B: this module is dispatched with its OWN ops
             * table -- a copy of the caller's whose log() is host-owned and
             * attributed to entry.id -- so a module can neither spoof another
             * id nor exceed its per-run budget. All other entries forward to the
             * caller's ctx unchanged. */
            ModuleLogRouter router{};
            router.module_id = entry.id.c_str();
            const glk_contract_ops module_ops = make_module_ops(context.host, router);
            const RegistryRunOutcome outcome = run_registry_stage(
                    entry.registry, to_countermeasure_stage(stage), &module_ops);
            diagnostics_.called += outcome.called;
            diagnostics_.log_calls += router.emitted;
            diagnostics_.log_dropped += router.dropped;
            if (outcome.failed != 0u) {
                /* run_registry_stage already disabled this module's later hooks
                 * and recorded the failure; other modules are untouched. */
                diagnostics_.hook_failed += outcome.failed;
                const auto &failures = entry.registry.failures();
                const std::int32_t code = failures.empty() ? 0 : failures.back().code;
                record(HostReason::HookFailed, i, index, code);
            }
        }
    }

    void PluginHost::close() noexcept {
        if (closed_) {
            return;
        }
        closed_ = true;
        open_ = false;
        /* Reverse registration order. Each registry is cleared BEFORE its handle
         * is released: the installed hooks borrow pointers into the module
         * image, so no traversal may be possible after dlclose. */
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            it->armed = false;
            it->registry.reset(contract::kHostImplementedCaps,
                               contract::kHostImplementedTriggers);
            it->module.close();
        }
    }

    std::string PluginHost::format_diagnostics() const {
        std::string out;
        out.reserve(512u);
        out += "run.plugin host loaded=";
        out += std::to_string(diagnostics_.loaded);
        out += " load_failed=";
        out += std::to_string(diagnostics_.load_failed);
        out += " rejected=";
        out += std::to_string(diagnostics_.rejected);
        out += " called=";
        out += std::to_string(diagnostics_.called);
        out += " hook_failed=";
        out += std::to_string(diagnostics_.hook_failed);
        out += " skipped=";
        out += std::to_string(diagnostics_.skipped);
        out += " open_rejected=";
        out += std::to_string(diagnostics_.open_rejected);
        out += " stage_unavailable=";
        out += std::to_string(diagnostics_.stage_unavailable);
        out += " log_calls=";
        out += std::to_string(diagnostics_.log_calls);
        out += " log_dropped=";
        out += std::to_string(diagnostics_.log_dropped);
        out += '\n';
        for (std::size_t i = 0u; i < record_count_; ++i) {
            const HostRecord &record = records_[i];
            out += "run.plugin record reason=";
            out += host_reason_name(record.reason);
            out += " id=";
            out += record.entry < entries_.size() ? entries_[record.entry].id : "-";
            if (record.stage != kNoHostStage) {
                out += " stage=";
                out += stage_token_of_slot(record.stage);
            }
            switch (record.reason) {
            case HostReason::LoadFailed:
                out += " status=";
                out += load_status_name(static_cast<LoadStatus>(record.status));
                break;
            case HostReason::RegistrationRejected:
                out += " status=";
                out += registry_reject_reason_name(
                        static_cast<RegistryRejectReason>(record.status));
                break;
            case HostReason::WireRejected:
                out += " error=";
                out += plugin_wire_error_name(static_cast<PluginWireError>(record.status));
                break;
            case HostReason::HookFailed:
                out += " rc=";
                out += std::to_string(record.status);
                break;
            case HostReason::None:
            case HostReason::WindowNotClosed:
            case HostReason::StageUnavailableOnBackend:
            case HostReason::NoUsableHooks:
                break;
            }
            out += '\n';
        }
        return out;
    }

} // namespace ghostlock::plugin
