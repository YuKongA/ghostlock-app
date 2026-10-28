/**
 * @file perf.h
 * @brief 43499 无状态 perf 采样原语的 C11 ABI（机制-only，零解析）。
 *
 * @note 本模块只把内核 `perf_event_open` 的采样环**整块**拷贝给调用方，并把
 *       环几何/游标作为回执事实返回；**不解析任何 record 字节**、不做候选选择/
 *       投票/地址校验/重试。解析、投票、地址推导与 W2 前置校验全部在 Kotlin。
 * @note 无状态句柄（H3-a）：token 是不透明指针，Native 不检查
 *       OPEN→COLLECTED→CLOSED；close 唯一性由 Kotlin owner 保证。同一 token
 *       重复 collect 属机械重读（无状态错），close 后再使用属不支持调用。
 * @note ABI v4 起没有 reserved 槽；字段只允许在结构尾部 append-only 追加，
 *       每个结构带 `struct_size`+`abi_version` 并由 core 严格 `==` 校验。
 * @note 阶段：R3e-1（design-critic 三轮阻断 0 后实现批）；默认关闭
 *       （`perf.enabled=0`），G1 真机门禁为生产启用硬门。
 */
#ifndef GHOSTLOCK_GL43499_CORE_PERF_H
#define GHOSTLOCK_GL43499_CORE_PERF_H

#include <stdint.h>

#include "abi.h"
#include "result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打开一次 perf 采样事件的显式参数（全部来自 Kotlin，Native 无默认值）。
 *
 * @note `pid`=0 表示当前进程；否则为被采样进程主线程 TID（域 0..INT32_MAX）。
 * @note `cpu` 必须是 UINT64_MAX（内核 `-1`：per-task 事件）；Native 只做域校验。
 * @note attr 字段一一对应 `struct perf_event_attr` 的成员；未列出的 attr 字段
 *       由 Native 以 `{0}` 初始化（机制零值，非目标/容量参数）。
 * @note `page_size` 必须等于本机 `sysconf(_SC_PAGESIZE)`；`ring_pages` 为环数据
 *       页数，mmap 长度=(ring_pages+1)*page_size。
 */
struct glk43499_perf_open_request {
    struct glk43499_abi_header abi;
    uint64_t pid;
    uint64_t cpu;
    uint64_t attr_type;
    uint64_t attr_config;
    uint64_t attr_sample_period;
    uint64_t attr_sample_type;
    uint64_t attr_sample_regs_intr;
    uint64_t attr_flags;
    uint64_t attr_disabled;
    uint64_t exclude_user;
    uint64_t exclude_hv;
    uint64_t exclude_idle;
    uint64_t page_size;
    uint64_t ring_pages;
    uint64_t auto_enable;
};

/** @brief openPerfSampler 的回执（只含公共调用回执，无额外事实）。 */
struct glk43499_perf_open_result {
    struct glk43499_call_result call;
};

/**
 * @brief collectPerfSamples 的环事实回执。
 *
 * @note `data_head`/`data_tail_before` 是**自事件创建起的单调绝对字节位置**
 *       （非环内下标）；环内定位=`pos % data_size`，解析由 Kotlin 完成。
 * @note `bytes_copied` 是本次整块拷贝的字节数（=data_size）；`tail_moved`=1
 *       表示 Native 已把 `data_tail` 推进到 `data_head`。
 */
struct glk43499_perf_collect_receipt {
    uint64_t data_offset;
    uint64_t data_size;
    uint64_t data_head;
    uint64_t data_tail_before;
    uint64_t bytes_copied;
    uint64_t tail_moved;
};

/** @brief collectPerfSamples 的输出：公共回执 + 环事实。 */
struct glk43499_perf_collect_result {
    struct glk43499_call_result call;
    struct glk43499_perf_collect_receipt receipt;
};

/**
 * @brief 打开一个 perf 采样事件并完成 mmap（必要时自动 enable）。
 *
 * @param request 调用期借用的显式参数；`abi.struct_size` 必须等于本结构 sizeof。
 * @param result 必填输出；`call.abi.struct_size` 必须等于本结构 sizeof。
 * @return 成功时为新的无状态 handle；失败返回 0（`result.call` 仍有效）。
 * @note 顺序：输出 ABI → 输入 ABI → 域/几何校验 → perf_event_open → mmap →
 *       环几何校验 →（auto_enable=1）IOCTL_ENABLE。任一失败按逆序清理已建资源，
 *       不产生 token；失败回执 `effect=NOT_APPLIED`、`retained=0`。
 * @note Native 不解析环内数据；只读取 mmap 页头部的几何字段。
 */
glk43499_handle glk43499_perf_open(
    const struct glk43499_perf_open_request *request,
    struct glk43499_perf_open_result *result);

/**
 * @brief 停止采样并把环数据区整块拷贝到调用方缓冲。
 *
 * @param perf 仍由调用方唯一持有的无状态 handle。
 * @param out 调用方提供的输出缓冲；不能为 NULL。
 * @param out_capacity `out` 的字节容量；必须 >= data_size（否则 INVALID_ARGUMENT，
 *        token 不消费）。
 * @param result 必填输出；`call.abi.struct_size` 必须等于本结构 sizeof。
 * @return glk43499_call_status；token 不消费（close 仍由 owner 执行）。
 * @note 顺序：输出 ABI → 容量 → IOCTL_DISABLE → 读 data_head（acquire fence）→
 *       memcpy(data_size) → `data_tail=data_head`（release store）。成功后
 *       `effect=CLEAN_SUCCESS`、`resource_state=OPEN`。
 * @note 无状态（H3-a）：Native 不检查是否已 collect/closed；同一 handle 重复
 *       collect 是机械重读，不返回状态错。
 */
int32_t glk43499_perf_collect(glk43499_handle perf, uint8_t *out,
                              uint64_t out_capacity,
                              struct glk43499_perf_collect_result *result);

/**
 * @brief 终结一次 perf 采样会话（消费 token）。
 *
 * @param perf 仍由调用方唯一持有的 handle。
 * @param result 必填输出；`call.abi.struct_size` 必须等于本结构 sizeof。
 * @return glk43499_call_status；输出 ABI 无效或 token=0 时入口未接受
 *         （token 不消费）；一旦接受，无论结果都消费 token。
 * @note 顺序：best-effort IOCTL_DISABLE（仅当已 enable）→ munmap → close(fd)。
 * @note 任一释放步骤失败⇒`effect=DIRTY`、`retained_for_process_exit=1`、
 *       对象不 free（进程退出兜底）；这不影响已取得的地址有效性。
 */
int32_t glk43499_perf_close(glk43499_handle perf,
                            struct glk43499_close_result *result);

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_perf_open_request) == 128,
                       "perf_open_request layout drift (expected 128)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_perf_open_result) == 32,
                       "perf_open_result layout drift (expected 32)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_perf_collect_receipt) == 48,
                       "perf_collect_receipt layout drift (expected 48)");
GLK43499_STATIC_ASSERT(sizeof(struct glk43499_perf_collect_result) == 80,
                       "perf_collect_result layout drift (expected 80)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_PERF_H */
