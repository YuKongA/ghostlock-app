/**
 * @file perf.c
 * @brief 43499 无状态 perf 采样原语的物理实现（机制-only：syscall + 整块拷贝）。
 *
 * @note 本文件只做五件事：`perf_event_open`、`mmap` 环、`ioctl(ENABLE/DISABLE)`、
 *       把环数据区**整块** memcpy 到调用方缓冲、释放资源。**不解析 record、
 *       不收集候选、不投票、不做地址选择/校验/重试/状态机**；解析与判定全在
 *       Kotlin（`Cve43499PerfSampleParser`/`Cve43499PerfTargetSource`）。
 * @note 无状态句柄（H3-a）：`struct glk43499_perf_impl` 只保存 fd/mmap/几何；
 *       不保存 OPEN/COLLECTED 状态；close 唯一性由 Kotlin owner 契约保证。
 * @note 并发：只被 JNI 串行调用（wrapper 对同一 token 串行）；无锁、无全局。
 * @note 阶段：R3e-1；默认关闭（`perf.enabled=0`），G1 真机门禁后启用。
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "perf.h"

#include "core_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 一次 perf 采样会话独占的机械资源（无状态字段）。
 *
 * @note `enabled` 只记录「当前是否已 enable」，用于 close 时决定是否补一次
 *       DISABLE；它不是业务状态机，也不参与任何调用前置判断。
 */
struct glk43499_perf_impl {
    int32_t fd;
    uint8_t *ring;
    uint64_t mmap_bytes;
    uint64_t page_size;
    uint64_t ring_pages;
    uint64_t data_offset;
    uint64_t data_size;
    int32_t enabled;
};

/**
 * @brief 把 token 还原为私有对象。
 * @param handle 非零 token；调用方已确认非零。
 * @return 对象指针；0 返回 NULL。
 * @note 不校验存活性：线性 owner 契约把误用责任交给调用方（H3-a）。
 */
static struct glk43499_perf_impl *glk43499_perf_from_handle(
    glk43499_handle handle) {
    return (struct glk43499_perf_impl *) glk43499_internal_handle_ptr(handle);
}

/**
 * @brief 释放一次 open 的机械资源（fd/mmap），不释放 impl 对象。
 * @param impl 非空对象；ring 可为 NULL（open 未成功）。
 * @param first_errno 非空错误槽；仅记录第一个失败 errno。
 * @return 1 全部释放成功；0 有释放步骤失败（impl 身份保留）。
 * @note 顺序遵循「先 munmap 再 close fd」；失败槽位不置空，便于保留身份。
 */
static int32_t glk43499_perf_release_resources(struct glk43499_perf_impl *impl,
                                               int32_t *first_errno) {
    int32_t clean = 1;
    if (impl->ring != NULL) {
        if (munmap(impl->ring, (size_t) impl->mmap_bytes) == 0) {
            impl->ring = NULL;
        } else {
            if (*first_errno == 0) *first_errno = errno;
            clean = 0;
        }
    }
    if (impl->fd >= 0) {
        if (close(impl->fd) == 0) {
            impl->fd = -1;
        } else {
            if (*first_errno == 0) *first_errno = errno;
            clean = 0;
        }
    }
    return clean;
}

/**
 * @brief 校验 open 请求的域与几何（只做机制不变量，不做业务判断）。
 * @param request 非空请求；输入 ABI 已校验。
 * @param out_mmap_bytes 非空输出：mmap 长度=(ring_pages+1)*page_size。
 * @return 1 合法；0 拒绝。
 * @note 校验项：cpu 必须为 -1；pid 域；auto_enable∈{0,1}；page_size 与
 *       `sysconf(_SC_PAGESIZE)` 相等；ring_pages≠0；两个乘法/加法不溢出。
 */
static int32_t glk43499_perf_valid_open_request(
    const struct glk43499_perf_open_request *request, uint64_t *out_mmap_bytes) {
    const long host_page_size = sysconf(_SC_PAGESIZE);
    if (host_page_size <= 0) return 0;
    if (request->cpu != UINT64_MAX) return 0;
    if (request->pid > (uint64_t) INT32_MAX) return 0;
    if (request->auto_enable > 1) return 0;
    if (request->page_size != (uint64_t) host_page_size) return 0;
    if (request->ring_pages == 0) return 0;
    if (request->ring_pages == UINT64_MAX) return 0;
    const uint64_t pages_with_meta = request->ring_pages + 1;
    uint64_t mmap_bytes = 0;
    if (!glk43499_internal_mul_u64(pages_with_meta, request->page_size,
                                   &mmap_bytes)) {
        return 0;
    }
    if (mmap_bytes == 0) return 0;
    *out_mmap_bytes = mmap_bytes;
    return 1;
}

/**
 * @brief 在 open 发出后校验 mmap 页头的环几何，并写入 impl 事实。
 * @param impl 非空对象；ring 已 mmap。
 * @return 1 几何合法；0 拒绝。
 * @note 只读几何字段（data_offset/data_size），不读任何 record。
 */
static int32_t glk43499_perf_read_ring_geometry(
    struct glk43499_perf_impl *impl) {
    const struct perf_event_mmap_page *meta =
        (const struct perf_event_mmap_page *) impl->ring;
    const uint64_t data_offset = (uint64_t) meta->data_offset;
    const uint64_t data_size = (uint64_t) meta->data_size;
    uint64_t end = 0;
    if (data_size == 0) return 0;
    if (!glk43499_internal_add_u64(data_offset, data_size, &end)) return 0;
    if (end > impl->mmap_bytes) return 0;
    uint64_t expected = 0;
    if (!glk43499_internal_mul_u64(impl->ring_pages, impl->page_size,
                                   &expected)) {
        return 0;
    }
    if (data_size != expected) return 0;
    impl->data_offset = data_offset;
    impl->data_size = data_size;
    return 1;
}

/**
 * @brief 打开一个 perf 采样事件并完成 mmap（机制入口）。
 * @param request 显式参数；可为 NULL。
 * @param result 必填输出。
 * @return 成功为新 handle；失败为 0。
 * @note 失败路径按「逆序释放已建资源 + 回执 NOT_APPLIED」收口，不产生 token。
 */
glk43499_handle glk43499_perf_open(
    const struct glk43499_perf_open_request *request,
    struct glk43499_perf_open_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    if (request == NULL ||
        !glk43499_internal_valid_header(&request->abi,
                                        (uint32_t) sizeof(*request))) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_BAD_ABI,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }
    uint64_t mmap_bytes = 0;
    if (!glk43499_perf_valid_open_request(request, &mmap_bytes)) {
        glk43499_internal_set_call(&result->call,
                                   GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_INVALID_HANDLE;
    }

    struct glk43499_perf_impl *impl =
        (struct glk43499_perf_impl *) calloc(1, sizeof(*impl));
    if (impl == NULL) {
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, ENOMEM, 0);
        return GLK43499_INVALID_HANDLE;
    }
    impl->fd = -1;

    /* 机制：attr 零初始化后只填 Kotlin 给出的请求字段；其余字段保持 0。 */
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = (uint32_t) request->attr_type;
    attr.size = (uint32_t) sizeof(attr);
    attr.config = request->attr_config;
    attr.sample_period = request->attr_sample_period;
    attr.sample_type = (uint32_t) request->attr_sample_type;
    attr.sample_regs_intr = request->attr_sample_regs_intr;
    attr.disabled = (uint32_t) request->attr_disabled;
    attr.exclude_user = (uint32_t) request->exclude_user;
    attr.exclude_hv = (uint32_t) request->exclude_hv;
    attr.exclude_idle = (uint32_t) request->exclude_idle;

    errno = 0;
    const long opened = syscall(SYS_perf_event_open, &attr,
                                (pid_t) (int32_t) request->pid, -1, -1,
                                (unsigned long) request->attr_flags);
    if (opened < 0) {
        const int32_t saved_errno = errno;
        free(impl);
        glk43499_internal_set_call(&result->call, GLK43499_CALL_SYSTEM_ERROR,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, saved_errno, 0);
        return GLK43499_INVALID_HANDLE;
    }
    impl->fd = (int32_t) opened;
    impl->mmap_bytes = mmap_bytes;
    impl->page_size = request->page_size;
    impl->ring_pages = request->ring_pages;
    impl->ring = (uint8_t *) mmap(NULL, (size_t) mmap_bytes,
                                  PROT_READ | PROT_WRITE, MAP_SHARED, impl->fd,
                                  (off_t) 0);
    if (impl->ring == MAP_FAILED) {
        const int32_t saved_errno = errno;
        impl->ring = NULL;
        int32_t first_errno = saved_errno;
        const int32_t clean = glk43499_perf_release_resources(impl, &first_errno);
        /* B6：清理不干净时不得 free（retained=1，身份保留到进程退出）。 */
        const int32_t retained = clean != 0 ? 0 : 1;
        if (clean != 0) free(impl);
        glk43499_internal_set_call(
            &result->call,
            GLK43499_CALL_SYSTEM_ERROR,
            retained != 0 ? GLK43499_EFFECT_DIRTY
                          : GLK43499_EFFECT_NOT_APPLIED,
            retained != 0 ? GLK43499_RESOURCE_QUARANTINED
                          : GLK43499_RESOURCE_INVALID,
            first_errno, (uint64_t) retained);
        return GLK43499_INVALID_HANDLE;
    }
    if (!glk43499_perf_read_ring_geometry(impl)) {
        int32_t first_errno = 0;
        const int32_t clean = glk43499_perf_release_resources(impl, &first_errno);
        const int32_t retained = clean != 0 ? 0 : 1;
        if (clean != 0) free(impl);
        glk43499_internal_set_call(
            &result->call,
            retained != 0 ? GLK43499_CALL_SYSTEM_ERROR
                          : GLK43499_CALL_INVALID_ARGUMENT,
            retained != 0 ? GLK43499_EFFECT_DIRTY
                          : GLK43499_EFFECT_NOT_APPLIED,
            retained != 0 ? GLK43499_RESOURCE_QUARANTINED
                          : GLK43499_RESOURCE_INVALID,
            first_errno, (uint64_t) retained);
        return GLK43499_INVALID_HANDLE;
    }
    if (request->auto_enable != 0) {
        errno = 0;
        if (ioctl(impl->fd, (unsigned long) PERF_EVENT_IOC_ENABLE, 0) != 0) {
            const int32_t saved_errno = errno;
            int32_t first_errno = saved_errno;
            const int32_t clean =
                glk43499_perf_release_resources(impl, &first_errno);
            const int32_t retained = clean != 0 ? 0 : 1;
            if (clean != 0) free(impl);
            glk43499_internal_set_call(
                &result->call, GLK43499_CALL_SYSTEM_ERROR,
                retained != 0 ? GLK43499_EFFECT_DIRTY
                              : GLK43499_EFFECT_NOT_APPLIED,
                retained != 0 ? GLK43499_RESOURCE_QUARANTINED
                              : GLK43499_RESOURCE_INVALID,
                first_errno, (uint64_t) retained);
            return GLK43499_INVALID_HANDLE;
        }
        impl->enabled = 1;
    }
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_OPEN, 0, 0);
    return glk43499_internal_ptr_handle(impl);
}

/**
 * @brief 停止采样并把环数据区整块拷贝到调用方缓冲（机制入口）。
 * @param perf 无状态 token。
 * @param out 调用方输出缓冲；不能为 NULL。
 * @param out_capacity 输出容量（字节）。
 * @param result 必填输出。
 * @return call_status；token 不消费。
 * @note 无状态（H3-a）：不检查是否已 collect/closed；重复调用=机械重读。
 */
int32_t glk43499_perf_collect(glk43499_handle perf, uint8_t *out,
                              uint64_t out_capacity,
                              struct glk43499_perf_collect_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    memset(&result->receipt, 0, sizeof(result->receipt));
    if (perf == GLK43499_INVALID_HANDLE || out == NULL) {
        glk43499_internal_set_call(&result->call,
                                   GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_INVALID, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_perf_impl *impl = glk43499_perf_from_handle(perf);
    if (out_capacity < impl->data_size) {
        glk43499_internal_set_call(&result->call,
                                   GLK43499_CALL_INVALID_ARGUMENT,
                                   GLK43499_EFFECT_NOT_APPLIED,
                                   GLK43499_RESOURCE_OPEN, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct perf_event_mmap_page *meta =
        (struct perf_event_mmap_page *) impl->ring;
    const uint64_t tail_before = (uint64_t) meta->data_tail;
    if (impl->enabled != 0) {
        errno = 0;
        if (ioctl(impl->fd, (unsigned long) PERF_EVENT_IOC_DISABLE, 0) != 0) {
            const int32_t saved_errno = errno;
            glk43499_internal_set_call(&result->call,
                                       GLK43499_CALL_SYSTEM_ERROR,
                                       GLK43499_EFFECT_DIRTY,
                                       GLK43499_RESOURCE_OPEN, saved_errno, 0);
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        impl->enabled = 0;
    }
    const uint64_t head =
        __atomic_load_n(&meta->data_head, __ATOMIC_ACQUIRE);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    memcpy(out, impl->ring + impl->data_offset, (size_t) impl->data_size);
    __atomic_store_n(&meta->data_tail, head, __ATOMIC_RELEASE);
    result->receipt.data_offset = impl->data_offset;
    result->receipt.data_size = impl->data_size;
    result->receipt.data_head = head;
    result->receipt.data_tail_before = tail_before;
    result->receipt.bytes_copied = impl->data_size;
    result->receipt.tail_moved = 1;
    glk43499_internal_set_call(&result->call, GLK43499_CALL_OK,
                               GLK43499_EFFECT_CLEAN_SUCCESS,
                               GLK43499_RESOURCE_OPEN, 0, 0);
    return GLK43499_CALL_OK;
}

/**
 * @brief 终结一次 perf 采样会话并消费 token（机制入口）。
 * @param perf 无状态 token。
 * @param result 必填输出。
 * @return call_status；ABI 无效或 token=0 时入口未接受（token 不消费）。
 * @note 释放顺序：best-effort DISABLE → munmap → close(fd)；任一失败⇒DIRTY+
 *       retained=1，对象不 free（进程退出兜底）。
 */
int32_t glk43499_perf_close(glk43499_handle perf,
                            struct glk43499_close_result *result) {
    if (result == NULL ||
        !glk43499_internal_output_abi(&result->call,
                                      (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (perf == GLK43499_INVALID_HANDLE) {
        glk43499_internal_set_close(result, GLK43499_CALL_INVALID_ARGUMENT,
                                    GLK43499_EFFECT_NOT_APPLIED,
                                    GLK43499_RESOURCE_INVALID, 0, 0, 0, 0, 0);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    struct glk43499_perf_impl *impl = glk43499_perf_from_handle(perf);
    int32_t first_errno = 0;
    int32_t disable_failed = 0;
    if (impl->enabled != 0) {
        errno = 0;
        if (ioctl(impl->fd, (unsigned long) PERF_EVENT_IOC_DISABLE, 0) != 0) {
            first_errno = errno;
            disable_failed = 1;
        } else {
            impl->enabled = 0;
        }
    }
    const int32_t clean = glk43499_perf_release_resources(impl, &first_errno);
    if (clean != 0) {
        free(impl);
        if (disable_failed != 0) {
            /* B5：DISABLE 失败仍要报 SYSTEM_ERROR/DIRTY；释放已干净⇒retained=0。 */
            glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                        GLK43499_EFFECT_DIRTY,
                                        GLK43499_RESOURCE_CLOSED, first_errno, 1,
                                        0, 1, 0);
            return GLK43499_CALL_SYSTEM_ERROR;
        }
        glk43499_internal_set_close(result, GLK43499_CALL_OK,
                                    GLK43499_EFFECT_CLEAN_SUCCESS,
                                    GLK43499_RESOURCE_CLOSED, 0, 1, 0, 1, 0);
        return GLK43499_CALL_OK;
    }
    glk43499_internal_set_close(result, GLK43499_CALL_SYSTEM_ERROR,
                                GLK43499_EFFECT_DIRTY,
                                GLK43499_RESOURCE_QUARANTINED, first_errno, 1,
                                0, 0, 1);
    return GLK43499_CALL_SYSTEM_ERROR;
}

#ifdef __cplusplus
}
#endif
