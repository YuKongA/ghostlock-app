/**
 * @file page.c
 * @brief 43499 Landing 与 Page 的物理实现：把 Kotlin 的最终字节喷入候选页并交接闭包。
 *
 * @note 移植自 2.0.8 `support.cpp:land_with` 的物理段（socketpair reclaim +
 *       SO_SNDBUF + O_NONBLOCK、pcp shaping socketpair sendmsg、pin/yield、
 *       按 stride 关闭各 ctx memfd、关闭 leak memfd、N 次 reclaim sendmsg、
 *       关闭 prepare memfd 并回收 child），改写为 C11；不 include 也不链接
 *       `legacy` 各子目录。旧 route 状态机/Flow 层不引入。
 * @note Native 不解释 payload 业务含义：`bytes->data` 由 JNI 深拷贝后传入，
 *       landing 只把它复制进 Placement 的 staging 并按物理顺序发送。
 * @note 提交点（commit point）：pcp shaping `sendmsg` 成功返回。提交前失败为
 *       NOT_APPLIED 且 `placement_consumed=0`（Placement 仍归 Kotlin）；提交后
 *       无论成功/失败 `placement_consumed=1`，无法安全释放的闭包保留到进程退出。
 * @note Page 独占内核访问所需的 reclaim socketpair 闭包；Actor 接管后由 Actor 释放。
 * @note 并发：wrapper 保证同一 Page/Placement 串行调用；本文件无锁、无全局。
 */

#include "page_internal.h"

#include "placement_internal.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief SO_SNDBUF 的物理上限：旧实现固定 1 MiB，这里只做上界校验。 */
#define GLK43499_PAGE_MAX_SNDBUF (UINT64_C(1) << 22)

/** @brief reclaim sendmsg 次数的物理上限；旧实现固定 4 次。 */
#define GLK43499_PAGE_MAX_RECLAIM_SENDS UINT64_C(16)

/** @brief landing 超时的物理上限（1 小时）；landing 本身不等待，只做边界校验。 */
#define GLK43499_PAGE_MAX_TIMEOUT_NS UINT64_C(3600000000000)

/**
 * @brief 校验 PageBytes 顶层 ABI 与保留字段。
 * @param bytes 调用期借用；可为 NULL。
 * @return 1 接受；0 拒绝。
 * @note 只校验 ABI/保留字段；data/size 的物理校验在 land_page 内完成。
 */
static int32_t glk43499_page_valid_bytes(
    const struct glk43499_page_bytes *bytes) {
    return bytes != NULL &&
           glk43499_internal_valid_header(&bytes->abi,
                                          (uint32_t) sizeof(*bytes)) &&
           glk43499_internal_zero_u64(
               bytes->reserved,
               sizeof(bytes->reserved) / sizeof(bytes->reserved[0]));
}

/**
 * @brief 校验 LandingRequest 顶层 ABI、保留字段与物理边界。
 * @param request 已通过 ABI 头校验的请求。
 * @param placement Placement 的已验证事实（几何必须一致）。
 * @return 1 接受；0 拒绝（INVALID_ARGUMENT，Placement 不消费）。
 * @note Native 不填默认值：所有几何与计数都必须显式给出并与 Placement 一致。
 */
static int32_t glk43499_page_valid_landing_request(
    const struct glk43499_landing_request *request,
    const struct glk43499_placement_impl *placement) {
    if (request == NULL ||
        !glk43499_internal_valid_header(&request->abi,
                                        (uint32_t) sizeof(*request)) ||
        !glk43499_internal_zero_u64(
            request->reserved,
            sizeof(request->reserved) / sizeof(request->reserved[0]))) {
        return 0;
    }
    if (request->expected_page_base != placement->candidate_page_base ||
        request->page_size != placement->page_size ||
        request->allocation_size != placement->allocation_size ||
        request->required_alignment != placement->required_alignment) {
        return 0;
    }
    if (request->reclaim_payload_size != GLK43499_PHYS_SKB_SEND_SIZE) {
        return 0;
    }
    if (request->socket_send_buffer_size < 4096 ||
        request->socket_send_buffer_size > GLK43499_PAGE_MAX_SNDBUF) {
        return 0;
    }
    if (request->reclaim_send_limit == 0 ||
        request->reclaim_send_limit > GLK43499_PAGE_MAX_RECLAIM_SENDS) {
        return 0;
    }
    if (request->timeout_ns == 0 ||
        request->timeout_ns > GLK43499_PAGE_MAX_TIMEOUT_NS) {
        return 0;
    }
    return 1;
}

/**
 * @brief 关掉一组 Page-token 之前建好的 socketpair。
 * @param fds 长度 2 的数组；-1 表示已关闭。
 * @note 只用于失败路径与 Page 终结；成功路径把 fd 交给 Page 对象。
 */
static int32_t glk43499_page_close_pair(int32_t fds[2]) {
    int32_t clean = 1;
    for (size_t index = 0; index < 2; ++index) {
        if (fds[index] >= 0) {
            if (close(fds[index]) != 0) {
                clean = 0; /* 失败槽位保留身份，禁止假 released */
                continue;
            }
            fds[index] = -1;
        }
    }
    return clean;
}

/**
 * @brief 把 Landing 收尾时 Placement 残留的上下文组全部释放。
 * @param placement 非空 Placement；landing 已提交并拥有其全部资源。
 * @note 顺序：先 kill/reap 仍存活的 child（stop→close→reap），再释放用户态数组。
 *       这是 landing 成功后的唯一 Placement 终结路径。
 */
static int32_t glk43499_page_finish_placement(
    struct glk43499_placement_impl *placement) {
    int32_t clean = 1;
    struct glk43499_ctx_set *sets[4] = {
        &placement->prepare, &placement->spray, &placement->pre,
        &placement->post,
    };
    for (size_t index = 0; index < sizeof(sets) / sizeof(sets[0]); ++index) {
        (void) glk43499_placement_kill_children(sets[index], &clean);
        if (clean != 0) glk43499_placement_free_ctx(sets[index]);
    }
    if (placement->leak_child > 0) {
        if (kill(placement->leak_child, SIGKILL) != 0 && errno != ESRCH) {
            clean = 0;
        }
        if (clean != 0) {
            for (;;) {
                const pid_t reaped = waitpid(placement->leak_child, NULL, 0);
                if (reaped == placement->leak_child) break;
                if (reaped < 0 && errno == EINTR) continue;
                if (reaped < 0 && errno == ECHILD) break;
                clean = 0;
                break;
            }
        }
        if (clean != 0) placement->leak_child = -1;
    }
    if (placement->leak_memfd >= 0) {
        if (close(placement->leak_memfd) == 0) {
            placement->leak_memfd = -1;
        } else {
            clean = 0;
        }
    }
    if (clean != 0) {
        free(placement);
    } else {
        placement->retained = 1; /* 身份保留到进程退出 */
    }
    return clean;
}

/**
 * @brief 提交前失败的统一回滚：关掉本次新建的 socketpair，只在全部成功时释放候选对象。
 * @param page 候选 Page 对象（可能从未对外暴露）。
 * @param first 第一对 socketpair（可为 NULL）。
 * @param second 第二对 socketpair（可为 NULL）。
 * @param result 输出回执。
 * @param status 回执状态（未接受时用 SYSTEM_ERROR；不得为 OK）。
 * @param system_error 错误码。
 * @note clean 时 effect=NOT_APPLIED、Placement 未消费；清理失败时改为 DIRTY/QUARANTINED
 *       并保留候选对象身份（不 free、不 close 残余 fd）。
 * @note 阶段：landing 的提交前失败路径。
 */
static void glk43499_page_abort_precommit(
    struct glk43499_page_impl *page, int32_t first[2], int32_t second[2],
    struct glk43499_landing_result *result, glk43499_call_status status,
    int32_t system_error) {
    int32_t clean = 1;
    if (first != NULL && glk43499_page_close_pair(first) == 0) clean = 0;
    if (second != NULL && glk43499_page_close_pair(second) == 0) clean = 0;
    if (clean != 0) {
        free(page);
        glk43499_internal_set_call(&result->call, status,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, system_error, 0);
        return;
    }
    page->retained = 1;
    glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                               GLK43499_EFFECT_DIRTY,
                               GLK43499_RESOURCE_QUARANTINED, system_error, 1);
}

/**
 * @brief 把 Kotlin 构造的最终字节落入 Placement 指定的候选位置。
 * @param placement 有效且未被并发使用的 Placement handle。
 * @param bytes 调用期借用；data 非空且 size 必须等于 reclaim_payload_size。
 * @param request 调用期借用的最终几何。
 * @param result 必填输出。
 * @return 成功时返回新的 Page handle；其余返回 0。
 * @note 入口顺序：输出 ABI → 输入 ABI → token 非零 → 几何/物理边界。
 * @note commit 前失败：effect=NOT_APPLIED、placement_consumed=0、Placement 仍有效。
 *       commit 后失败：effect=DIRTY/INDETERMINATE、placement_consumed=1，闭包保留。
 * @note 阶段：G4L-2；landing 无 PI 窗口。
 */
glk43499_handle glk43499_land_page(
    glk43499_handle placement, const struct glk43499_page_bytes *bytes,
    const struct glk43499_landing_request *request,
    struct glk43499_landing_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    memset(&result->receipt, 0, sizeof(result->receipt));
    if (!glk43499_page_valid_bytes(bytes) ||
        (request != NULL &&
         !glk43499_internal_valid_header(&request->abi,
                                         (uint32_t) sizeof(*request)))) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_BAD_ABI,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    struct glk43499_placement_impl *holder =
        glk43499_placement_from_handle(placement);
    if (!glk43499_page_valid_landing_request(request, holder)) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }
    if (bytes->data == NULL ||
        bytes->size != request->reclaim_payload_size ||
        bytes->size != GLK43499_PHYS_SKB_SEND_SIZE) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    struct glk43499_page_impl *page =
        (struct glk43499_page_impl *) calloc(
            1, sizeof(struct glk43499_page_impl));
    if (page == NULL) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, ENOMEM, 0);
        return GLK43499_INVALID_HANDLE;
    }
    int32_t reclaim_pair[2] = {-1, -1};
    int32_t shaping_pair[2] = {-1, -1};
    page->reclaim_read = -1;
    page->reclaim_write = -1;

    /* ---------- preflight：全部可失败资源在此建立，失败可完全回滚 ---------- */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_pair) != 0) {
        const int32_t saved = errno;
        glk43499_page_abort_precommit(page, NULL, NULL, result,
                                      GLK43499_CALL_SYSTEM_ERROR, saved);
        return GLK43499_INVALID_HANDLE;
    }
    {
        const int32_t sndbuf = (int32_t) request->socket_send_buffer_size;
        if (setsockopt(reclaim_pair[0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
                       (socklen_t) sizeof(sndbuf)) != 0) {
            const int32_t saved = errno;
            glk43499_page_abort_precommit(page, reclaim_pair, NULL, result,
                                          GLK43499_CALL_SYSTEM_ERROR, saved);
            return GLK43499_INVALID_HANDLE;
        }
        const int32_t flags = fcntl(reclaim_pair[0], F_GETFL, 0);
        if (flags < 0 ||
            fcntl(reclaim_pair[0], F_SETFL, flags | O_NONBLOCK) != 0) {
            const int32_t saved = errno;
            glk43499_page_abort_precommit(page, reclaim_pair, NULL, result,
                                          GLK43499_CALL_SYSTEM_ERROR, saved);
            return GLK43499_INVALID_HANDLE;
        }
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, shaping_pair) != 0) {
        const int32_t saved = errno;
        glk43499_page_abort_precommit(page, reclaim_pair, NULL, result,
                                      GLK43499_CALL_SYSTEM_ERROR, saved);
        return GLK43499_INVALID_HANDLE;
    }

    /* Kotlin 的最终字节深拷贝进 Placement staging（不保留调用方指针）。 */
    memcpy(holder->staging, bytes->data, (size_t) bytes->size);

    page->reclaim_read = reclaim_pair[0];
    page->reclaim_write = reclaim_pair[1];
    page->base = holder->candidate_page_base;
    page->page_size = holder->page_size;
    page->allocation_size = holder->allocation_size;
    page->required_alignment = holder->required_alignment;
    page->content_generation = bytes->content_generation;
    page->state = 0;

    struct iovec iov;
    memset(&iov, 0, sizeof(iov));
    iov.iov_base = holder->staging;
    iov.iov_len = (size_t) GLK43499_PHYS_SKB_SEND_SIZE;
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    /* ---------- 提交点：pcp shaping skb 一旦成功入队即不可回滚 ---------- */
    errno = 0;
    const ssize_t shaped = sendmsg(shaping_pair[0], &message, 0);
    if (shaped <= 0) {
        const int32_t saved = errno != 0 ? errno : EIO;
        glk43499_page_abort_precommit(page, shaping_pair, reclaim_pair, result,
                                      GLK43499_CALL_SYSTEM_ERROR, saved);
        return GLK43499_INVALID_HANDLE;
    }
    result->receipt.commit_reached = 1;
    result->receipt.placement_consumed = 1;
    result->receipt.actual_page_base = page->base;
    result->receipt.content_generation = page->content_generation;

    /* ---------- 提交后：物理顺序固定，任何失败都按 dirty 保留闭包 ---------- */
    int32_t dirty = 0;
    if (glk43499_internal_pin_cpu(holder->placement_cpu) != 0) dirty = 1;
    for (int32_t spin = 0; spin < 4; ++spin) (void) sched_yield();

    int32_t cleanup_clean = 1;
    if (!dirty) {
        result->receipt.closed_pre_count =
            glk43499_placement_close_memfds(&holder->pre, &cleanup_clean);
        result->receipt.closed_post_count =
            holder->post.count > 0
                ? glk43499_placement_close_memfds_except_tail(&holder->post, 1,
                                                              &cleanup_clean)
                : 0;
        result->receipt.closed_spray_count =
            glk43499_placement_close_memfds_stride(&holder->spray,
                                                   holder->objects_per_slab,
                                                   &cleanup_clean);
        if (glk43499_page_close_pair(shaping_pair) == 0) cleanup_clean = 0;
        for (int32_t spin = 0; spin < 4; ++spin) (void) sched_yield();
        if (holder->leak_memfd >= 0) {
            if (close(holder->leak_memfd) == 0) {
                holder->leak_memfd = -1;
            } else {
                cleanup_clean = 0;
            }
        }
        for (uint64_t send_index = 0; send_index < request->reclaim_send_limit;
             ++send_index) {
            errno = 0;
            const ssize_t sent =
                sendmsg(page->reclaim_write, &message, MSG_DONTWAIT);
            if (sent <= 0) break;
            ++result->receipt.reclaim_send_count;
        }
    } else {
        glk43499_page_close_pair(shaping_pair);
    }

    if (!dirty && result->receipt.reclaim_send_count == 0) {
        /* 页面回收没有发生：物理结果不确定，按 dirty 保留。 */
        dirty = 1;
    }
    if (!dirty && cleanup_clean == 0) {
        /* 清理记账失败：不得报告 released；整链 retained。 */
        dirty = 1;
    }

    if (dirty) {
        /* 无法证明可安全释放：Placement 与 Page 闭包整体保留到进程退出。 */
        page->retained = 1;
        holder->retained = 1;
        result->receipt.reclaim_retained = 1;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, 0, 1);
        return GLK43499_INVALID_HANDLE;
    }

    /* 收尾：关闭并回收 Placement 剩余上下文；只有全部成功才释放它（已消费）。 */
    if (glk43499_page_finish_placement(holder) == 0) {
        page->retained = 1;
        result->receipt.reclaim_retained = 1;
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_DIRTY,
                                   GLK43499_RESOURCE_QUARANTINED, 0, 1);
        return GLK43499_INVALID_HANDLE;
    }
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_LANDED, 0, 0);
    return glk43499_internal_ptr_handle(page);
}

/**
 * @brief 关闭尚未转移给 Actor 的 Page。
 * @param page 仍由调用方唯一持有的 Page token。
 * @param result 必填输出。
 * @return glk43499_call_status；输出 ABI 无效或 token 为 0 时未接受（调用方
 *         恢复原 token）；一旦接受，无论结果都消费 token。
 * @note 顺序：关闭 reclaim 闭包 → 释放对象；没有在飞 worker。
 * @note 阶段：G4L-2 终结。
 */
int32_t glk43499_close_page(glk43499_handle page,
                            struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (page == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result, GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_page_impl *impl = glk43499_page_from_handle(page);
    impl->state = 1;
    if (glk43499_page_release_checked(impl) != 0) {
        glk43499_internal_set_close(result, GLK43499_CALL_OK,
                                    GLK43499_EFFECT_CLEAN_SUCCESS,
                                    GLK43499_RESOURCE_CLOSED, 0, 1, 0, 1, 0);
        return GLK43499_CALL_OK;
    }
    /* 无法证明完整释放：Page 身份保留到进程退出，回执必须报 retained。 */
    glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                GLK43499_EFFECT_DIRTY,
                                GLK43499_RESOURCE_QUARANTINED, 0, 1, 0, 0, 1);
    return GLK43499_CALL_SYSTEM_ERROR;
}

int32_t glk43499_page_release_checked(struct glk43499_page_impl *page) {
    if (page == NULL) return 1;
    int32_t clean = 1;
    if (page->reclaim_read >= 0) {
        if (close(page->reclaim_read) == 0) {
            page->reclaim_read = -1;
        } else {
            clean = 0;
        }
    }
    if (page->reclaim_write >= 0) {
        if (close(page->reclaim_write) == 0) {
            page->reclaim_write = -1;
        } else {
            clean = 0;
        }
    }
    if (clean == 0) {
        page->retained = 1; /* 身份保留到进程退出；不 free、不归零 */
        return 0;
    }
    page->state = 2;
    free(page);
    return 1;
}

/**
 * @brief 释放 Page 的 reclaim 闭包（Actor 终结用的 void 入口）。
 * @param page 非空对象；已由 Actor 唯一拥有。
 * @note 与 [glk43499_page_release_checked] 同形；失败时对象保留身份（不 free），
 *       不返回错误（actor.c 不在本批写范围，见交接简报的已知缺口）。
 */
void glk43499_page_release(struct glk43499_page_impl *page) {
    (void) glk43499_page_release_checked(page);
}
