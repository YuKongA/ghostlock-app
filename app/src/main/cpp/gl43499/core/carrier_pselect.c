/**
 * @file carrier_pselect.c
 * @brief 43499 Pselect Carrier 的物理实现（park 在伪造 fd_set 窗口上的 select）。
 *
 * @note 移植自 2.0.8 `route_select.cpp:SelectStackRoute::prepare/execute/disarm/
 *       destroy` 与 `open_selected_fds/build_fdsets` 的物理 syscall 序列，改写为
 *       C11；不 include 也不链接 `legacy` 各子目录。旧 route 状态机、retry ladder
 *       与 fallback 均不引入。
 * @note 语义边界：三组 fd_set 的最终字节由 Kotlin 提供（`frame_bytes` 依次为
 *       input/output/exception 三个 set，每个 `(nfds+63)/64` 个 64 位字）；Native
 *       只校验长度/上限、原样装进 fd_set 并 park，不重算 waiter 偏移、不构造 payload。
 * @note **fd 安全（F-B）**：安装前对每一个将被 `dup2` 覆盖的目标 fd（含 0/1/2 与
 *       任意 JVM fd）做 `F_DUPFD_CLOEXEC` 备份并记录映射；安装失败回滚已安装项；
 *       release/stuck 路径先按映射幂等恢复全部备份，再关闭 Carrier 自有 src fd。
 *       被安装覆盖的目标 fd 由 `dup2` 原子替换，恢复后不需要也不允许再 close，
 *       因此 backup/owned 集合不相交、double-close=0；JVM 原 fd 不会被留在被覆盖状态。
 * @note PI 窗口：触发入口由 Actor 在 prepare 阶段绑定；窗口内只调用 pselect 与内联
 *       原子，不 kind 读取、不分配、不加锁、不写日志、不首次触页。
 * @note 阶段：G4L-4P + F-B。
 */

#include "carrier_internal.h"
#include "pselect_timeout_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

/** @brief fd_set 每个字的位宽（64 位平台；sizeof 派生，非目标参数）。 */
#define GLK43499_PSELECT_WORD_BYTES ((uint64_t) sizeof(uint64_t))

/** @brief 备份表头部（数量 + 补齐到 8 字节；ABI 对齐，非目标参数）。 */
#define GLK43499_PSELECT_BACKUP_HEADER 8U

/**
 * @brief 每个 fd_set 需要的 64 位字数。
 * @param nfds 描述符上界。
 * @return `(nfds + 63) / 64`；调用方已校验 nfds 范围。
 */
static uint64_t glk43499_pselect_words_per_set(uint32_t nfds) {
    return ((uint64_t) nfds + 63) / 64;
}

/**
 * @brief 备份表在 frame 分配的尾部：`[uint32 count][uint32 pad][entry...]`。
 * @param window 非空 Carrier。
 * @return 表头指针（8 字节对齐）。
 * @note frame 分配长度 = frame_size + GLK43499_PSELECT_BACKUP_HEADER + 16 * nfds；
 *       frame_size 为 24 的倍数，因此尾部 8 字节对齐；每个条目 16B
 *       （target/backup/flags/pad，task-132 CLOEXEC 快照）。
 */
static uint8_t *glk43499_pselect_backup_table(
    struct glk43499_carrier_pselect *window) {
    return window->frame + window->frame_size;
}

/**
 * @brief 备份表条目视图：每个条目 `{int32 target; int32 backup; int32 flags; int32 pad}`。
 * @param table 表头指针（来自 [glk43499_pselect_backup_table]）。
 * @return 条目数组指针。
 * @note task-132（PR403）：`flags` 保存原 `F_GETFD`（含 FD_CLOEXEC），恢复时
 *       `dup2` 后必须用 `F_SETFD` 还原；`pad` 保持 16B 对齐。
 */
static int32_t (*glk43499_pselect_backup_entries(uint8_t *table))[4] {
    return (int32_t (*)[4]) (table + GLK43499_PSELECT_BACKUP_HEADER);
}

/** @brief 读取备份条目数。 */
static uint32_t glk43499_pselect_backup_count(const uint8_t *table) {
    uint32_t count = 0;
    memcpy(&count, table, sizeof(count));
    return count;
}

/** @brief 写入备份条目数。 */
static void glk43499_pselect_set_backup_count(uint8_t *table, uint32_t count) {
    memcpy(table, &count, sizeof(count));
}

/**
 * @brief 把 frame 的三个 set 字节原样装入 fd_set（每个 attempt 前重载）。
 * @param window 非空 Carrier。
 * @note frame 已在 prepare 深拷贝并触页；窗口内只做 memcpy（无首次触页）。
 */
static void glk43499_pselect_load_sets(
    struct glk43499_carrier_pselect *window) {
    const uint64_t words = glk43499_pselect_words_per_set(window->nfds);
    const size_t set_bytes = (size_t) (words * GLK43499_PSELECT_WORD_BYTES);
    memcpy(&window->input_set, window->frame, set_bytes);
    memcpy(&window->output_set, window->frame + set_bytes, set_bytes);
    memcpy(&window->exception_set, window->frame + 2 * set_bytes, set_bytes);
    /* 窗口 fd：最后一个描述符必须挂在 exception set 上，select 才会停住。 */
    FD_SET((int32_t) window->nfds - 1, &window->exception_set);
}

/**
 * @brief 返回当前用于 park 的“永不就绪”描述符。
 * @param window 非空 Carrier。
 * @return timerfd；timerfd 创建失败时借用 pipe 读端。
 */
static int32_t glk43499_pselect_block_fd(
    const struct glk43499_carrier_pselect *window) {
    return window->block_borrows_pipe ? window->pipe_read : window->block;
}

/**
 * @brief 关闭 Carrier 自有的 src fd（pipe/timerfd），并逐项记账。
 * @param window 非空 Carrier。
 * @param clean 非空清算标志；任一 close 失败置 0（失败槽位保留身份）。
 * @note 只在备份已恢复之后调用。
 */
static void glk43499_pselect_close_sources(
    struct glk43499_carrier_pselect *window, int32_t *clean) {
    if (window->pipe_read >= 0) {
        if (close(window->pipe_read) == 0) {
            window->pipe_read = -1;
        } else {
            *clean = 0;
        }
    }
    if (window->pipe_write >= 0) {
        if (close(window->pipe_write) == 0) {
            window->pipe_write = -1;
        } else {
            *clean = 0;
        }
    }
    if (!window->block_borrows_pipe && window->block >= 0) {
        if (close(window->block) == 0) {
            window->block = -1;
        } else {
            *clean = 0;
        }
    }
}

/**
 * @brief 从当前 fd_set 收集所有将被 dup2 覆盖的目标 fd（含 `nfds-1`）。
 * @param window 非空 Carrier（fd_set 已从 frame 装载）。
 * @param table 非空备份表。
 * @return 目标数量。
 * @note 目标集合按升序去重；数量上界为 nfds。
 */
static uint32_t glk43499_pselect_collect_targets(
    struct glk43499_carrier_pselect *window, uint8_t *table) {
    int32_t (*entries)[4] = glk43499_pselect_backup_entries(table);
    uint32_t count = 0;
    const int32_t nfds = (int32_t) window->nfds;
    for (int32_t fd = 0; fd < nfds; ++fd) {
        const int32_t selected =
            FD_ISSET(fd, &window->input_set) ||
            FD_ISSET(fd, &window->output_set) ||
            FD_ISSET(fd, &window->exception_set);
        if (selected) {
            entries[count][0] = fd;
            entries[count][1] = -1;
            entries[count][2] = -1;
            entries[count][3] = 0;
            ++count;
        }
    }
    if (count == 0 || entries[count - 1][0] != nfds - 1) {
        entries[count][0] = nfds - 1;
        entries[count][1] = -1;
        entries[count][2] = -1;
        entries[count][3] = 0;
        ++count;
    }
    glk43499_pselect_set_backup_count(table, count);
    return count;
}

/**
 * @brief 恢复全部备份（幂等、恰好一次）：把目标 fd 还原为安装前的描述符。
 * @param window 非空 Carrier。
 * @return 1 全部恢复成功；0 有恢复失败（此时对象必须 retained，不得 free）。
 * @note 对原本无效的目标 fd（备份 -1），恢复 = 关闭安装时建立的 dup；
 *       对有效目标 fd，恢复 = dup2(backup, target) + close(backup)；
 *       恢复后条目被清除，重复调用是无副作用 no-op。
 */
static int32_t glk43499_pselect_restore_backups(
    struct glk43499_carrier_pselect *window) {
    uint8_t *table = glk43499_pselect_backup_table(window);
    int32_t (*entries)[4] = glk43499_pselect_backup_entries(table);
    const uint32_t count = glk43499_pselect_backup_count(table);
    int32_t clean = 1;
    for (uint32_t index = 0; index < count; ++index) {
        const int32_t target = entries[index][0];
        const int32_t backup = entries[index][1];
        const int32_t flags = entries[index][2];
        if (target < 0) continue; /* 已恢复 */
        if (backup >= 0) {
            if (dup2(backup, target) < 0) {
                clean = 0;
            } else if (flags >= 0 && fcntl(target, F_SETFD, flags) < 0) {
                /* task-132：dup2 清除了目标 fd 的 FD_CLOEXEC；必须还原原 flags。 */
                clean = 0;
            }
            if (close(backup) != 0) clean = 0;
        } else {
            /* 原本无效/未打开：关闭安装时建立的 dup。 */
            if (close(target) != 0 && errno != EBADF) clean = 0;
        }
        entries[index][0] = -1;
        entries[index][1] = -1;
        entries[index][2] = -1;
        entries[index][3] = 0;
    }
    return clean;
}

/**
 * @brief 释放尚未安装时的备份 fd（不 dup2 回写目标；G19 dispose 兜底）。
 * @param window 非空 Carrier。
 * @return 1 全部关闭；0 有失败（资源身份保留，调用方报 retained）。
 * @note 只在 `selected_fds_installed==0` 的 dispose 路径调用；已恢复条目为 -1，
 *       重复调用是 no-op。
 */
static int32_t glk43499_pselect_close_backups(
    struct glk43499_carrier_pselect *window) {
    uint8_t *table = glk43499_pselect_backup_table(window);
    int32_t (*entries)[4] = glk43499_pselect_backup_entries(table);
    const uint32_t count = glk43499_pselect_backup_count(table);
    int32_t clean = 1;
    for (uint32_t index = 0; index < count; ++index) {
        if (entries[index][1] >= 0) {
            if (close(entries[index][1]) != 0) clean = 0;
            entries[index][1] = -1;
        }
        entries[index][0] = -1;
        entries[index][2] = -1;
        entries[index][3] = 0;
    }
    glk43499_pselect_set_backup_count(table, 0);
    return clean;
}

/**
 * @brief 对全部目标 fd 做 `F_DUPFD_CLOEXEC` 备份。
 * @param window 非空 Carrier。
 * @param table 非空备份表（目标已收集）。
 * @return 1 成功；0 失败（已建立的备份会被回滚关闭）。
 */
static int32_t glk43499_pselect_backup_targets(
    struct glk43499_carrier_pselect *window, uint8_t *table) {
    int32_t (*entries)[4] = glk43499_pselect_backup_entries(table);
    const uint32_t count = glk43499_pselect_backup_count(table);
    for (uint32_t index = 0; index < count; ++index) {
        const int32_t target = entries[index][0];
        errno = 0;
        const int32_t flags = fcntl(target, F_GETFD);
        if (flags < 0) {
            if (errno == EBADF) {
                /* 目标原本无效：无需备份，恢复时关闭安装的 dup。 */
                entries[index][1] = -1;
                entries[index][2] = -1;
                continue;
            }
            /* flags 读取失败不能证明可恢复：回滚已建立备份，fail-closed。 */
            for (uint32_t rollback = 0; rollback < index; ++rollback) {
                if (entries[rollback][1] >= 0) {
                    (void) close(entries[rollback][1]);
                    entries[rollback][1] = -1;
                }
            }
            return 0;
        }
        entries[index][2] = flags;
        const int32_t backup =
            fcntl(target, F_DUPFD_CLOEXEC,
                  (int32_t) window->nfds +
                      (int32_t) window->fd_reserve_stride +
                      (int32_t) index);
        if (backup < 0) {
            /* target 刚经 F_GETFD 证明有效；dup 失败是真实资源失败 ⇒ fail-closed。 */
            for (uint32_t rollback = 0; rollback < index; ++rollback) {
                if (entries[rollback][1] >= 0) {
                    (void) close(entries[rollback][1]);
                    entries[rollback][1] = -1;
                }
            }
            return 0;
        }
        entries[index][1] = backup;
    }
    return 1;
}

/**
 * @brief 把每个目标 fd 覆盖成永不就绪的 block fd 副本（execute 内、consumer 启动前）。
 * @param window 非空 Carrier。
 * @param table 非空备份表（目标已备份）。
 * @param failure_errno 非空；失败时写首个失败 syscall errno（0=未失败）。
 * @return 1 成功；0 失败（已安装项会被恢复；恢复失败则 window->retained=1）。
 * @note G19：安装从 prepare 移到 execute；本函数只在 trigger 调用一次。
 */
static int32_t glk43499_pselect_install_targets(
    struct glk43499_carrier_pselect *window, uint8_t *table,
    int32_t *failure_errno) {
    *failure_errno = 0;
    int32_t (*entries)[4] = glk43499_pselect_backup_entries(table);
    const uint32_t count = glk43499_pselect_backup_count(table);
    errno = 0;
    const int32_t source = fcntl(glk43499_pselect_block_fd(window),
                                 F_DUPFD_CLOEXEC,
                                 (int32_t) window->nfds +
                                     (int32_t) window->fd_reserve_stride +
                                     (int32_t) window->nfds);
    if (source < 0) {
        *failure_errno = errno != 0 ? errno : EIO;
        return 0;
    }
    int32_t clean = 1;
    for (uint32_t index = 0; index < count; ++index) {
        const int32_t target = entries[index][0];
        if (target < 0) continue;
        if (dup2(source, target) < 0) {
            *failure_errno = errno != 0 ? errno : EIO;
            clean = 0;
            break;
        }
    }
    (void) close(source);
    if (clean == 0) {
        /* 已安装项 rollback；rollback 失败 ⇒ 资源身份保留，由 retained 记账。 */
        if (glk43499_pselect_restore_backups(window) == 0) {
            window->retained = 1;
        }
    }
    return clean;
}

/**
 * @brief 释放 Carrier 的全部 fd 与对象内缓冲。
 * @param window 非空 Carrier。
 * @return 1 干净释放（frame 已 free）；0 无法证明干净（frame/身份保留）。
 * @note 顺序：先恢复备份（JVM 原 fd 复位），再关 Carrier 自有 src fd，最后释放 frame。
 * @note stuck 路径只恢复备份、保留 src fd 与 frame，返回 0（retained）。
 */
static int32_t glk43499_pselect_dispose(struct glk43499_carrier_pselect *window) {
    if (window->frame == NULL) return 1; /* prepare 失败且已清理：无资源可释放 */
    if (window->consumer_stuck != 0 || window->retained != 0) {
        /* R3a-2/P5/P11：consumer 可能仍在 walk 这些描述符 / prepare 清理不干净 ——
         * 不得 dup2 回写、不得 close、不得 free；整链保留到进程退出（retained）。 */
        return 0;
    }
    int32_t clean = 1;
    if (window->selected_fds_installed != 0) {
        /* 兜底：只有仍安装时才 restore；trigger 正常返回前已 restore 并置 false。 */
        if (glk43499_pselect_restore_backups(window) == 0) clean = 0;
    } else {
        /* 未安装（prepare 成功但 execute 未跑/安装失败已 rollback）：只关备份。 */
        if (glk43499_pselect_close_backups(window) == 0) clean = 0;
    }
    window->selected_fds_installed = 0;
    if (clean == 0) {
        window->retained = 1;
        return 0;
    }
    glk43499_pselect_close_sources(window, &clean);
    if (clean == 0) return 0;
    free(window->frame);
    window->frame = NULL;
    window->frame_size = 0;
    window->enter_delay_ns = 0;
    window->fd_reserve_stride = 0;
    window->select_use_timeval = 0;
    window->select_result = 0;
    window->select_errno = 0;
    window->retained = 0;
    window->prepared = 0;
    return 1;
}

/**
 * @brief 准备 Pselect Carrier：pipe/timerfd、目标收集与只读备份（**不安装 fd**）。
 * @param carrier 目标 Carrier；`kind` 已由 Actor 设置。
 * @param spec 调用期借用的 Pselect 物理参数（frame 会被深拷贝）。
 * @return 0 成功；负 errno 语义失败（不留下被覆盖的 fd）。
 * @note G19：prepare 不 dup2；安装/恢复必须在 execute 的同一 native 调用内完成。
 * @note 无法安全备份时返回具名失败，绝不硬装。
 */
int32_t glk43499_carrier_pselect_prepare(
    struct glk43499_carrier *carrier,
    const struct glk43499_pselect_spec *spec) {
    struct glk43499_carrier_pselect *window = &carrier->value.pselect;
    memset(window, 0, sizeof(*window));
    window->pipe_read = -1;
    window->pipe_write = -1;
    window->block = -1;
    window->high_read = -1;
    window->stdio_backup[0] = -1;
    window->stdio_backup[1] = -1;
    window->stdio_backup[2] = -1;
    if (spec == NULL || spec->frame_bytes == NULL) return -EINVAL;
    if (spec->addresses.final_waiter_address == 0 ||
        spec->addresses.final_task_address == 0 ||
        spec->addresses.final_lock_address == 0) {
        return -EINVAL;
    }
    /* A 决策（R2b）：nfds 容量上限来自请求字段 max_nfds，且不得超过系统头
     * FD_SETSIZE；结构性下限仍是 fd_set 至少 2 个槽位（窗口 fd 占 nfds-1）。 */
    if (spec->max_nfds < 2 ||
        spec->max_nfds > (uint64_t) FD_SETSIZE ||
        spec->descriptor_capacity < 2 ||
        spec->descriptor_capacity > spec->max_nfds ||
        spec->descriptor_capacity > (uint64_t) FD_SETSIZE) {
        return -EINVAL;
    }
    /* B15：attempt 计数最终收窄到 int32（consumer_go/回执），域上限显式。 */
    if (spec->operation_budget == 0 ||
        spec->operation_budget > (uint64_t) INT32_MAX) {
        return -EINVAL;
    }
    /* 超时非零且 deadline 加法不溢出；每次尝试延时落在超时窗口内。 */
    const uint64_t now_ns = glk43499_internal_now_ns();
    if (spec->timeout_ns == 0 ||
        spec->timeout_ns > UINT64_MAX - now_ns ||
        spec->per_attempt_delay_ns > spec->timeout_ns) {
        return -EINVAL;
    }
    /* R3a-2/P1/P7/P10：enter delay / select 分支 / fd 备份起点步长（全部来自请求）。
     * 步长用于 `F_DUPFD*` 起点 = nfds + stride + index，必须使结果可表示为 int32。 */
    if (spec->select_use_timeval > 1 || spec->fd_reserve_stride == 0) {
        return -EINVAL;
    }
    {
        uint64_t backup_base = 0;
        if (!glk43499_internal_add_u64(
                spec->descriptor_capacity,
                spec->fd_reserve_stride + spec->descriptor_capacity,
                &backup_base) ||
            backup_base > (uint64_t) INT32_MAX) {
            return -EINVAL;
        }
    }
    const uint64_t words =
        glk43499_pselect_words_per_set((uint32_t) spec->descriptor_capacity);
    uint64_t set_bytes = 0;
    uint64_t frame_capacity = 0;
    uint64_t backup_bytes = 0;
    if (!glk43499_internal_mul_u64(words, GLK43499_PSELECT_WORD_BYTES,
                                   &set_bytes) ||
        !glk43499_internal_mul_u64(set_bytes, UINT64_C(3), &set_bytes) ||
        !glk43499_internal_mul_u64((uint64_t) spec->descriptor_capacity,
                                   UINT64_C(16), &backup_bytes) ||
        !glk43499_internal_add_u64(
            backup_bytes + GLK43499_PSELECT_BACKUP_HEADER, set_bytes,
            &frame_capacity)) {
        return -EOVERFLOW;
    }
    if (spec->frame_size != set_bytes) return -EINVAL;
    window->frame = (uint8_t *) calloc(1, (size_t) frame_capacity);
    if (window->frame == NULL) return -ENOMEM;
    memcpy(window->frame, spec->frame_bytes, (size_t) set_bytes);
    window->frame_size = set_bytes;
    window->nfds = (uint32_t) spec->descriptor_capacity;
    window->operation_budget = spec->operation_budget;
    window->timeout_ns = spec->timeout_ns;
    /* P0a：尝试间延时（ns）来自请求；R3a-2 起不再当 select 超时。 */
    window->per_attempt_delay_ns = spec->per_attempt_delay_ns;
    window->enter_delay_ns = spec->enter_delay_ns;
    window->fd_reserve_stride = spec->fd_reserve_stride;
    window->select_use_timeval = spec->select_use_timeval;

    int32_t pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        const int32_t saved = errno;
        free(window->frame);
        window->frame = NULL;
        window->frame_size = 0;
        return -saved;
    }
    window->pipe_read = pipe_fds[0];
    window->pipe_write = pipe_fds[1];

    /* 两个路线都 park 在永不就绪的 timerfd 上：waiter 必须留在 pselect 栈上。 */
    window->block = (int32_t) syscall(SYS_timerfd_create, CLOCK_MONOTONIC,
                                       (int32_t) TFD_CLOEXEC);
    if (window->block < 0) {
        window->block = -1;
        window->block_borrows_pipe = 1;
    }

    /* G19：prepare 只做装载/目标收集/只读备份（fcntl）；dup2 安装推迟到
     * trigger（consumer 启动/首轮 select 之前），避免 prepare→execute 跨 JNI
     * 返回期间覆盖 JVM fd。 */
    glk43499_pselect_load_sets(window);
    uint8_t *table = glk43499_pselect_backup_table(window);
    (void) glk43499_pselect_collect_targets(window, table);
    if (glk43499_pselect_backup_targets(window, table) == 0) {
        int32_t clean = 1;
        glk43499_pselect_close_sources(window, &clean);
        if (clean != 0) {
            free(window->frame);
            memset(window, 0, sizeof(*window));
            window->pipe_read = -1;
            window->pipe_write = -1;
            window->block = -1;
            window->high_read = -1;
            /* B16：资源已全清 ⇒ retained 显式清零（不留 seam 残留）。 */
            window->retained = 0;
        } else {
            /* B16：只有资源确实未清理时才 retained=1。 */
            window->retained = 1;
        }
        return -EIO;
    }
    /* G19：prepare 成功只表示备份就绪；execute 才安装。 */
    window->selected_fds_installed = 0;
    window->prepared = 1;
    return 0;
}

/**
 * @brief PI 窗口内执行 Pselect 触发：重载 fd_set 并 park 在 select/pselect 上。
 * @param carrier 已 prepare 的 Carrier；只访问 pselect 分支。
 * @param shared PI 共享状态。
 * @param outcome 非空中性物理结果输出。
 * @note R3a-2/P1/P4/P7/P8：与 legacy2 `route_select.cpp:408-497` 对齐 ——
 *       每次 attempt 重载 fd_set（P4）、先发布 consumer 起跑延迟（P1，
 *       `enter_delay_ns`，由 consumer 线程 usleep）、按请求分支调用
 *       pselect/select（P7），并把 select ret/errno 记入 last_step/last_errno（P8）；
 *       `per_attempt_delay_ns` 是 attempt 间隔，不再被误用为 select 超时。
 * @note G19：进入 attempt/consumer 启动前在同一 native 调用内 dup2 安装；
 *       所有 attempt 结束（成功/超时/失败）立即 restore 后再返回；安装失败
 *       回滚，restore 失败 retained（不静默）。
 *       窗口内不分配、不加锁、不写日志、不首次触页。
 */
void glk43499_carrier_pselect_trigger(
    struct glk43499_carrier *carrier, struct glk43499_pi_shared *shared,
    struct glk43499_carrier_outcome *outcome) {
    struct glk43499_carrier_pselect *window = &carrier->value.pselect;
    int32_t result = 0;
    atomic_store(&shared->consumer_calls, 0);
    atomic_store(&shared->consumer_success, 0);
    atomic_store(&shared->consumer_go, 0);
    atomic_store(&shared->route_delay_usec, 0);
    const uint64_t deadline = glk43499_internal_deadline(window->timeout_ns);
    /* ns → usec（consumer 线程 usleep 的粒度；饱和到 uint32 上界）。 */
    const uint64_t enter_delay_usec64 = window->enter_delay_ns / 1000;
    const uint32_t enter_delay_usec =
        enter_delay_usec64 > UINT32_MAX ? UINT32_MAX
                                        : (uint32_t) enter_delay_usec64;
    /* G02：每次 attempt 的 select 超时 = min(remaining, timeout_ns/max(1,budget))；
     * legacy2 route_select.cpp:457-470 为 4×200ms（800ms/4），一次 select 不得
     * 吃掉整个 route 窗口。 */
    const uint64_t per_attempt_timeout_ns =
        glk43499_pselect_per_attempt_timeout_ns(window->timeout_ns,
                                                window->operation_budget);
    uint64_t operations = 0;
    glk43499_carrier_step last_step = GLK43499_CARRIER_STEP_NONE;
    int32_t last_errno = 0;

    /* G19：先安装（dup2），再启动 consumer/首轮 select；失败已在 install 内
     * rollback，具名 stage=IO+errno 返回，不进入 attempt。 */
    int32_t install_errno = 0;
    if (glk43499_pselect_install_targets(
            window, glk43499_pselect_backup_table(window),
            &install_errno) == 0) {
        window->selected_fds_installed = 0;
        window->select_errno = install_errno;
        if (outcome != NULL) {
            outcome->operations = 0;
            outcome->calls = 0;
            outcome->successes = 0;
            outcome->failed = install_errno;
            outcome->observed_write = 0;
            outcome->last_step = GLK43499_CARRIER_STEP_IO;
            outcome->last_errno = install_errno;
        }
        return;
    }
    window->selected_fds_installed = 1;

    for (uint64_t attempt = 1; attempt <= window->operation_budget;
         ++attempt) {
        if (glk43499_internal_expired(deadline)) break;
        /* P1：attempt 间隔（旧 per_attempt_delay_ns 被误当超时）；vDSO 时钟自旋。 */
        if (attempt > 1 && window->per_attempt_delay_ns != 0) {
            const uint64_t spacing_deadline =
                glk43499_internal_deadline(window->per_attempt_delay_ns);
            while (!glk43499_internal_expired(spacing_deadline) &&
                   !glk43499_internal_expired(deadline)) {
                __asm__ volatile("yield" ::: "memory");
            }
        }
        ++operations;
        /* P4：每次 attempt 重载 fd_set（内核可能已修改上一轮的 set）。 */
        glk43499_pselect_load_sets(window);
        atomic_store(&shared->route_delay_usec, enter_delay_usec);
        atomic_store(&shared->consumer_go, (int32_t) attempt);
        const uint64_t now = glk43499_internal_now_ns();
        const uint64_t remaining = deadline > now ? deadline - now : 0;
        const uint64_t attempt_timeout =
            glk43499_pselect_attempt_timeout_ns(remaining,
                                                per_attempt_timeout_ns);
        last_step = GLK43499_CARRIER_STEP_ARM;
        errno = 0;
        int32_t select_result = 0;
        if (window->select_use_timeval != 0) {
            /* P7：select(timeval) 分支（旧 plain waiter 路线）。 */
            struct timespec remaining_ts;
            glk43499_internal_timespec(attempt_timeout, &remaining_ts);
            struct timeval timeout;
            timeout.tv_sec = remaining_ts.tv_sec;
            timeout.tv_usec = remaining_ts.tv_nsec / 1000L;
            select_result = select((int32_t) window->nfds,
                                   &window->input_set, &window->output_set,
                                   &window->exception_set, &timeout);
        } else {
            struct timespec timeout;
            glk43499_internal_timespec(attempt_timeout, &timeout);
            select_result = pselect((int32_t) window->nfds,
                                    &window->input_set, &window->output_set,
                                    &window->exception_set, &timeout, NULL);
        }
        last_step = GLK43499_CARRIER_STEP_IO;
        last_errno = select_result < 0 ? (errno != 0 ? errno : EIO) : 0;
        window->select_result = select_result;
        window->select_errno = last_errno;
        atomic_store(&shared->consumer_go, 0);
        atomic_store(&shared->route_delay_usec, 0);
        const int32_t calls = atomic_load(&shared->consumer_calls);
        const int32_t successes = atomic_load(&shared->consumer_success);
        if (calls > 0 && successes > 0) {
            result = 1;
            break;
        }
    }
    /* G19：attempt/select 结束立即 restore（成功/超时/失败同路径）；restore 失败
     * 保留身份并 retained，不静默。 */
    errno = 0;
    const int32_t restore_clean = glk43499_pselect_restore_backups(window);
    const int32_t restore_errno = errno != 0 ? errno : EIO;
    window->selected_fds_installed = 0;
    if (restore_clean == 0) {
        window->retained = 1;
        result = 0;
        last_step = GLK43499_CARRIER_STEP_IO;
        last_errno = restore_errno;
    }
    if (outcome != NULL) {
        outcome->operations = operations;
        outcome->calls = (uint64_t) atomic_load(&shared->consumer_calls);
        outcome->successes = (uint64_t) atomic_load(&shared->consumer_success);
        outcome->failed = result != 0 ? 0 : (last_errno != 0 ? last_errno : EIO);
        outcome->observed_write = result;
        outcome->last_step = last_step;
        outcome->last_errno = last_errno;
    }
}

int32_t glk43499_carrier_pselect_disarm(struct glk43499_carrier *carrier,
                                        struct glk43499_pi_shared *shared) {
    struct glk43499_carrier_pselect *window = &carrier->value.pselect;
    atomic_store(&shared->consumer_go, 0);
    for (uint64_t spin = 0; spin < window->disarm_spin_limit; ++spin) {
        if (atomic_load(&shared->consumer_inflight) == 0) return 1;
        __asm__ volatile("yield" ::: "memory");
    }
    if (atomic_load(&shared->consumer_inflight) != 0) {
        /* R3a-2/P5：consumer 可能仍在 walk 这些描述符 —— 此时不得 dup2 回写
         * （会破坏它正在使用的 fd），整链按 dirty retained，等进程退出。 */
        window->consumer_stuck = 1;
        return 0;
    }
    return 1;
}

int32_t glk43499_carrier_pselect_release(
    struct glk43499_carrier_pselect *pselect) {
    struct glk43499_carrier_pselect *window = pselect;
    if (window == NULL) return 1;
    return glk43499_pselect_dispose(window);
}
