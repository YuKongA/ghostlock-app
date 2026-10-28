/**
 * @file abi_layout_probe.c
 * @brief 43499 ABI v4 host 布局探针：打印 sizeof / offsetof 矩阵。
 *
 * @note 用途：与 `docs/plan/43499-r1-freeze-abi.md`（R2 重出版）的 sizeof 矩阵
 *       对拍；矩阵漂移即 R2 门禁 FAIL。只读公共头，不链接 core 源文件。
 * @note 编译/运行（host，系统 clang）：
 *       clang -std=c11 -I app/src/main/cpp/gl43499/core \
 *             tools/abi_layout_probe.c -o /tmp/abi_layout_probe && /tmp/abi_layout_probe
 * @note 输出键为结构名与代表字段 offset；C11 `_Static_assert` 已在头内先行拦截漂移。
 * @note 阶段：R2b（43499 Native 全参数化）。
 */
#include <stddef.h>
#include <stdio.h>

#include "api.h"

/**
 * @brief 打印一个结构尺寸。
 * @param name 结构名（诊断用）。
 * @param value sizeof 结果。
 */
static void print_size(const char *name, size_t value) {
    printf("sizeof %-32s %zu\n", name, value);
}

/**
 * @brief 打印一个字段 offset。
 * @param name `结构.字段` 名（诊断用）。
 * @param value offsetof 结果。
 */
static void print_offset(const char *name, size_t value) {
    printf("offsetof %-30s %zu\n", name, value);
}

int main(void) {
    print_size("glk43499_abi_header", sizeof(struct glk43499_abi_header));
    print_size("glk43499_call_result", sizeof(struct glk43499_call_result));
    print_size("glk43499_close_result", sizeof(struct glk43499_close_result));
    print_size("glk43499_placement_request",
               sizeof(struct glk43499_placement_request));
    print_size("glk43499_placement_facts",
               sizeof(struct glk43499_placement_facts));
    print_size("glk43499_placement_result",
               sizeof(struct glk43499_placement_result));
    print_size("glk43499_page_bytes", sizeof(struct glk43499_page_bytes));
    print_size("glk43499_landing_request",
               sizeof(struct glk43499_landing_request));
    print_size("glk43499_landing_receipt",
               sizeof(struct glk43499_landing_receipt));
    print_size("glk43499_landing_result", sizeof(struct glk43499_landing_result));
    print_size("glk43499_actor_setup", sizeof(struct glk43499_actor_setup));
    print_size("glk43499_actor_prepare_receipt",
               sizeof(struct glk43499_actor_prepare_receipt));
    print_size("glk43499_actor_execute_receipt",
               sizeof(struct glk43499_actor_execute_receipt));
    print_size("glk43499_actor_prepare_result",
               sizeof(struct glk43499_actor_prepare_result));
    print_size("glk43499_actor_execute_result",
               sizeof(struct glk43499_actor_execute_result));
    print_size("glk43499_primitive_addresses",
               sizeof(struct glk43499_primitive_addresses));
    print_size("glk43499_multicast_spec",
               sizeof(struct glk43499_multicast_spec));
    print_size("glk43499_pselect_spec", sizeof(struct glk43499_pselect_spec));
    print_size("glk43499_tcp_zerocopy_spec",
               sizeof(struct glk43499_tcp_zerocopy_spec));
    print_size("glk43499_primitive_spec", sizeof(struct glk43499_primitive_spec));
    print_size("glk43499_perf_open_request",
               sizeof(struct glk43499_perf_open_request));
    print_size("glk43499_perf_open_result",
               sizeof(struct glk43499_perf_open_result));
    print_size("glk43499_perf_collect_receipt",
               sizeof(struct glk43499_perf_collect_receipt));
    print_size("glk43499_perf_collect_result",
               sizeof(struct glk43499_perf_collect_result));

    print_offset("abi_header.struct_size",
                 offsetof(struct glk43499_abi_header, struct_size));
    print_offset("call_result.call_status",
                 offsetof(struct glk43499_call_result, call_status));
    print_offset("placement_request.page_size",
                 offsetof(struct glk43499_placement_request, page_size));
    print_offset("placement_request.identity_start",
                 offsetof(struct glk43499_placement_request, identity_start));
    print_offset("landing_request.expected_page_base",
                 offsetof(struct glk43499_landing_request, expected_page_base));
    print_offset("actor_setup.owner_cpu",
                 offsetof(struct glk43499_actor_setup, owner_cpu));
    print_offset("primitive_spec.kind",
                 offsetof(struct glk43499_primitive_spec, kind));
    print_offset("multicast_spec.max_frame",
                 offsetof(struct glk43499_multicast_spec, max_frame));
    print_offset("multicast_spec.disarm_required",
                 offsetof(struct glk43499_multicast_spec, disarm_required));
    print_offset("pselect_spec.max_nfds",
                 offsetof(struct glk43499_pselect_spec, max_nfds));
    print_offset("tcp_zerocopy_spec.max_mapping",
                 offsetof(struct glk43499_tcp_zerocopy_spec, max_mapping));
    print_offset("perf_open_request.ring_pages",
                 offsetof(struct glk43499_perf_open_request, ring_pages));
    print_offset("perf_collect_receipt.data_head",
                 offsetof(struct glk43499_perf_collect_receipt, data_head));
    return 0;
}
