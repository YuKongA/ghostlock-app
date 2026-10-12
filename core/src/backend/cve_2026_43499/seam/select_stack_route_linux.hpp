#pragma once
/** @file backend/cve_2026_43499/seam/select_stack_route_linux.hpp
 *  `select_stack` route 的**系统调用面**（设备侧 ✓；主机侧不编译 ✗）。
 *
 *  与 `multicast_route_linux.hpp` 同形（纯静态方法 ✓），供 route 模板以 `Ops` 传入 ✓。
 *  生命周期契约见 `docs/analysis/43499-select-stack-resource-plan.md` ✓：
 *  自有 fd 三处（pipe 两端 + timerfd ✓）、低编号 fd 的"先备份后改" ✓、还原顺序固定 ✓。
 */
#if defined(__ANDROID__)
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/futex.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43499::seam {
    /** `select_stack` 的 syscall 集合（全部 noexcept ✓，失败只回值不改全局 ✗）。 */
    struct SelectStackRouteLinux final {
        /** `pselect` 一次（三组 fd_set + 半个超时 = 永不超时 ✓）。
         *  @param nfds   fd 数（= 文档的 fd_count ✓）
         *  @return 内核返回值（-1 时 errno 已设 ✓）。 */
        static int pselect_now(int nfds, fd_set &in_set, fd_set &out_set, fd_set &ex_set,
                               std::int64_t timeout_us) noexcept {
            /* **有界**等待 ✓（规格的风险条款 ✓）：无限等会在收尾时挂住 ✗ ⇒ 给一个上限 ✓，
             *  内核在这段时间内 park 该线程即可 ✓；超时返回 0 由调用方按轮处理 ✓。 */
            struct timespec ts{};
            ts.tv_sec = static_cast<time_t>(timeout_us / 1000000);
            ts.tv_nsec = static_cast<long>((timeout_us % 1000000) * 1000);
            return ::pselect(nfds, &in_set, &out_set, &ex_set, &ts, nullptr);
        }

        /** 建一对管道（读端随范围传出 ✓）。 @return 0 成功 ✓，否则 errno ✓。 */
        static int make_pipe(int &read_end, int &write_end) noexcept {
            int fds[2] = {-1, -1};
            if (::pipe(fds) != 0) {
                return errno;
            }
            read_end = fds[0];
            write_end = fds[1];
            return 0;
        }

        /** 永不就绪的定时器 fd（`CLOCK_MONOTONIC` + `TFD_CLOEXEC` ✓）。 @return fd 或 -1 ✓。 */
        static int make_idle_timer() noexcept {
            return static_cast<int>(::syscall(SYS_timerfd_create, CLOCK_MONOTONIC, TFD_CLOEXEC));
        }

        /** 给某个 fd 造一个**高位副本**（用于备份原值 ✓）。 @return 副本 fd 或 -1 ✓。 */
        static int duplicate_above(int fd, int floor_value) noexcept {
            return ::fcntl(fd, F_DUPFD, floor_value);
        }

        /** 把 `src` 装到 `dst`（低编号借用 ✓）。 @return 0 成功 ✓，否则 errno ✓。 */
        static int install_at(int src, int dst) noexcept {
            if (::dup2(src, dst) < 0) {
                return errno;
            }
            return 0;
        }

        /** 关一个 fd（幂等 ✓：负数直接回 ✓）。 */
        static void close_fd(int fd) noexcept {
            if (fd >= 0) {
                (void) ::close(fd);
            }
        }

        /** 让出 CPU（不产生系统调用的自旋 ✓，与退役的 `yield` 一致 ✓）。 */
        static void yield() noexcept {
            __asm__ volatile("yield" ::: "memory");
        }

        /** errno（紧随调用之后取 ✓）。 */
        static std::int32_t last_errno() noexcept { return static_cast<std::int32_t>(errno); }

        /* ---- 演员需要的 futex/sched 面（与 LinuxRouteOps 同形 ✓，本 route 自持 ✗ 不共用） ---- */

        /** 当前线程 id（tid ✓）。 */
        static std::int32_t thread_id() noexcept {
            return static_cast<std::int32_t>(::syscall(SYS_gettid));
        }

        /** 取 PI 锁。 @return 0 成功 ✓。 */
        static std::int32_t lock_pi(std::uint32_t *word) noexcept {
            return static_cast<std::int32_t>(
                    ::syscall(SYS_futex, word, FUTEX_LOCK_PI, 0, nullptr, nullptr, 0));
        }

        /** 放 PI 锁。 @return 0 成功 ✓。 */
        static std::int32_t unlock_pi(std::uint32_t *word) noexcept {
            return static_cast<std::int32_t>(
                    ::syscall(SYS_futex, word, FUTEX_UNLOCK_PI, 0, nullptr, nullptr, 0));
        }

        /** `CMP_REQUEUE_PI`（快照 :243-245 ✓：val2 = **1**、val3 = **0** ✓）。
         *  @param chain  park 所在的字（= `wait_futex` ✓）
         *  @param target 目标字 ✓
         *  @return 内核返回值（`>=1` = 真搬动了等待者 ✓）。 */
        static std::int32_t cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            /* `val = 1`（= 快照 :243-245 的值 ✓）。读数史（round 226 ✓）：val=1/wait=0 ⇒ EDEADLK(35) ✗；
             *  val=1/wait=1 ⇒ EAGAIN(11) ✗；val=0/wait=0 ⇒ EINVAL(22) ✗ ⇒ 失败**不在 val** ✗ ⇒ 已复原。 */
            return static_cast<std::int32_t>(::syscall(SYS_futex, chain, FUTEX_CMP_REQUEUE_PI, 1, 1,
                                                       target, 0));
        }

        /** 在 chain 上 park 并请求被 requeue 到 target（带超时 ✓，防挂 ✓）。
         *  @return 内核返回值（负数 = -errno ✓）。 */
        static std::int32_t wait_requeue_pi_until(std::uint32_t *chain, std::uint32_t *target,
                                                  std::int64_t timeout_us) noexcept {
            struct timespec ts{};
            ts.tv_sec = static_cast<time_t>(timeout_us / 1000000);
            ts.tv_nsec = static_cast<long>((timeout_us % 1000000) * 1000);
            /* ★ `errno = 0`（round 223 ✓）：**成功返回 0 时 errno 不会被改写** ✗ ⇒ 上一次的 110
             *  会被误读成"超时" ✓ ⇒ 必须清零再调用 ✓（这条也解释了此前 `park=-1/110` 的读数可能失真 ✗）。 */
            errno = 0;
            return static_cast<std::int32_t>(::syscall(SYS_futex, chain, FUTEX_WAIT_REQUEUE_PI, 0,
                                                       &ts, target, 0));
        }

        /** 轮转 nice 并设到某个 tid 上（PI 链的"消费者动作" ✓）。
         *  @return 0 成功 ✓（被拒时由调用方兜底 ✓）。 */
        static std::int32_t setattr(std::int32_t tid, std::int32_t nice,
                                    std::int32_t policy) noexcept {
            struct local_sched_attr {
                std::uint32_t size;
                std::uint32_t sched_policy;
                std::uint64_t sched_flags;
                std::int32_t sched_nice;
                std::uint32_t sched_priority;
                std::uint64_t sched_runtime;
                std::uint64_t sched_deadline;
                std::uint64_t sched_period;
            } attr{};
            attr.size = sizeof(attr);
            attr.sched_policy = static_cast<std::uint32_t>(policy);
            attr.sched_nice = nice;
            return static_cast<std::int32_t>(::syscall(SYS_sched_setattr, tid, &attr, 0U));
        }

        /** 睡一小段（微秒 ✓）。 */
        static void sleep_micros(std::int64_t us) noexcept {
            struct timespec ts{};
            ts.tv_sec = static_cast<time_t>(us / 1000000);
            ts.tv_nsec = static_cast<long>((us % 1000000) * 1000);
            (void) ::nanosleep(&ts, nullptr);
        }
    };
}  // namespace ghostlock::backend::cve_2026_43499::seam
#endif
