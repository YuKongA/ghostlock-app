/**
 * @file api_unavailable.c
 * @brief 43499 资源原语尚未实现时的 fail-closed C ABI。
 *
 * @note 本文件不创建资源、不保存指针、不生成句柄。真实实现批必须从 CMake 删除
 *       本文件，禁止 unavailable 与真实定义共存。
 */
#include "api.h"

#include <stddef.h>

/** @brief 检查调用方初始化的顶层 ABI 头，失败时不写输出。 */
static int32_t glk43499_valid_header(const struct glk43499_abi_header *header,
                                    uint32_t minimum_size) {
    return header != NULL && header->struct_size >= minimum_size &&
           header->abi_version == GLK43499_ABI_VERSION &&
           header->reserved[0] == 0 && header->reserved[1] == 0;
}

/** @brief 检查固定宽度保留字段是否全零。 */
static int32_t glk43499_zero_u64(const uint64_t *values, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (values[index] != 0) return 0;
    }
    return 1;
}

/** @brief 检查 Placement 顶层 ABI 和保留字段。 */
static int32_t glk43499_valid_placement(
    const struct glk43499_placement_request *request) {
    return request != NULL &&
           glk43499_valid_header(&request->abi, (uint32_t) sizeof(*request)) &&
           request->reserved32 == 0 &&
           glk43499_zero_u64(request->reserved,
                             sizeof(request->reserved) / sizeof(request->reserved[0]));
}

/** @brief 检查 PageBytes 顶层 ABI 和保留字段。 */
static int32_t glk43499_valid_page_bytes(const struct glk43499_page_bytes *bytes) {
    return bytes != NULL &&
           glk43499_valid_header(&bytes->abi, (uint32_t) sizeof(*bytes)) &&
           glk43499_zero_u64(bytes->reserved,
                             sizeof(bytes->reserved) / sizeof(bytes->reserved[0]));
}

/** @brief 检查 Landing 顶层 ABI 和保留字段。 */
static int32_t glk43499_valid_landing(
    const struct glk43499_landing_request *request) {
    return request != NULL &&
           glk43499_valid_header(&request->abi, (uint32_t) sizeof(*request)) &&
           glk43499_zero_u64(request->reserved,
                             sizeof(request->reserved) / sizeof(request->reserved[0]));
}

/** @brief 检查 ActorSetup 顶层 ABI 和保留字段。 */
static int32_t glk43499_valid_actor_setup(const struct glk43499_actor_setup *setup) {
    return setup != NULL &&
           glk43499_valid_header(&setup->abi, (uint32_t) sizeof(*setup)) &&
           setup->reserved32 == 0 &&
           glk43499_zero_u64(setup->reserved,
                             sizeof(setup->reserved) / sizeof(setup->reserved[0]));
}

/** @brief 检查 Primitive 公共地址和所选 union 分支的保留字段。 */
static int32_t glk43499_valid_primitive(
    const struct glk43499_primitive_spec *primitive) {
    if (primitive == NULL ||
        !glk43499_valid_header(&primitive->abi, (uint32_t) sizeof(*primitive)) ||
        primitive->reserved32 != 0 ||
        !glk43499_zero_u64(primitive->reserved,
                           sizeof(primitive->reserved) / sizeof(primitive->reserved[0]))) {
        return 0;
    }
    const struct glk43499_primitive_addresses *addresses = NULL;
    const uint64_t *reserved = NULL;
    size_t reserved_count = 0;
    size_t selected_size = 0;
    switch (primitive->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG:
            addresses = &primitive->value.multicast_sendmsg.addresses;
            reserved = primitive->value.multicast_sendmsg.reserved;
            reserved_count = sizeof(primitive->value.multicast_sendmsg.reserved) /
                             sizeof(primitive->value.multicast_sendmsg.reserved[0]);
            selected_size = sizeof(primitive->value.multicast_sendmsg);
            if (primitive->value.multicast_sendmsg.reserved32 != 0) return 0;
            break;
        case GLK43499_PRIMITIVE_PSELECT:
            addresses = &primitive->value.pselect.addresses;
            reserved = primitive->value.pselect.reserved;
            reserved_count = sizeof(primitive->value.pselect.reserved) /
                             sizeof(primitive->value.pselect.reserved[0]);
            selected_size = sizeof(primitive->value.pselect);
            break;
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE:
            addresses = &primitive->value.tcp_zerocopy_receive.addresses;
            reserved = primitive->value.tcp_zerocopy_receive.reserved;
            reserved_count = sizeof(primitive->value.tcp_zerocopy_receive.reserved) /
                             sizeof(primitive->value.tcp_zerocopy_receive.reserved[0]);
            selected_size = sizeof(primitive->value.tcp_zerocopy_receive);
            break;
        default:
            return 0;
    }
    const uint8_t *union_bytes = (const uint8_t *) &primitive->value;
    for (size_t index = selected_size; index < sizeof(primitive->value); ++index) {
        if (union_bytes[index] != 0) return 0;
    }
    return glk43499_zero_u64(addresses->reserved,
                             sizeof(addresses->reserved) / sizeof(addresses->reserved[0])) &&
           glk43499_zero_u64(reserved, reserved_count);
}

/**
 * @brief 写入统一的 unavailable 回执。
 * @param result 已由 JNI 初始化并校验容量的调用回执。
 */
static void glk43499_set_unavailable(struct glk43499_call_result *result) {
    if (result == NULL) return;
    result->call_status = GLK43499_CALL_UNSUPPORTED;
    result->effect = GLK43499_EFFECT_NONE;
    result->resource_state = GLK43499_RESOURCE_INVALID;
    result->system_error = 0;
    result->generation = 0;
    result->retained_resource_count = 0;
}

/** @brief 输出 ABI 有效时记录输入 ABI/保留字段错误。 */
static void glk43499_set_bad_abi(struct glk43499_call_result *result) {
    result->call_status = GLK43499_CALL_BAD_ABI;
    result->effect = GLK43499_EFFECT_NOT_APPLIED;
    result->resource_state = GLK43499_RESOURCE_INVALID;
    result->system_error = 0;
    result->generation = 0;
    result->retained_resource_count = 0;
}

/** @brief 输出 ABI 有效时记录无效 handle。 */
static void glk43499_set_invalid_handle(struct glk43499_call_result *result) {
    result->call_status = GLK43499_CALL_INVALID_ARGUMENT;
    result->effect = GLK43499_EFFECT_NOT_APPLIED;
    result->resource_state = GLK43499_RESOURCE_INVALID;
    result->system_error = 0;
    result->generation = 0;
    result->retained_resource_count = 0;
}

glk43499_handle glk43499_prepare_placement(
    const struct glk43499_placement_request *request,
    struct glk43499_placement_result *result) {
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    if (!glk43499_valid_placement(request)) {
        glk43499_set_bad_abi(&result->call);
        return GLK43499_INVALID_HANDLE;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_INVALID_HANDLE;
}

int32_t glk43499_cancel_placement(
    glk43499_handle placement,
    struct glk43499_close_result *result) {
    (void) placement;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}

int32_t glk43499_close_placement(
    glk43499_handle placement,
    struct glk43499_close_result *result) {
    (void) placement;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}

glk43499_handle glk43499_land_page(
    glk43499_handle placement,
    const struct glk43499_page_bytes *bytes,
    const struct glk43499_landing_request *request,
    struct glk43499_landing_result *result) {
    (void) placement;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    if (!glk43499_valid_page_bytes(bytes) || !glk43499_valid_landing(request)) {
        glk43499_set_bad_abi(&result->call);
        return GLK43499_INVALID_HANDLE;
    }
    if (placement == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_INVALID_HANDLE;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_INVALID_HANDLE;
}

int32_t glk43499_close_page(
    glk43499_handle page,
    struct glk43499_close_result *result) {
    (void) page;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (page == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}

glk43499_handle glk43499_prepare_actor(
    glk43499_handle page,
    const struct glk43499_actor_setup *setup,
    const struct glk43499_primitive_spec *primitive,
    struct glk43499_actor_prepare_result *result) {
    (void) page;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_INVALID_HANDLE;
    }
    if (!glk43499_valid_actor_setup(setup) || !glk43499_valid_primitive(primitive)) {
        glk43499_set_bad_abi(&result->call);
        return GLK43499_INVALID_HANDLE;
    }
    if (page == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_INVALID_HANDLE;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_INVALID_HANDLE;
}

int32_t glk43499_execute_actor(
    glk43499_handle actor,
    struct glk43499_actor_execute_result *result) {
    (void) actor;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}

int32_t glk43499_cancel_actor(
    glk43499_handle actor,
    struct glk43499_close_result *result) {
    (void) actor;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}

int32_t glk43499_close_actor(
    glk43499_handle actor,
    struct glk43499_close_result *result) {
    (void) actor;
    if (result == NULL ||
        !glk43499_valid_header(&result->call.abi, (uint32_t) sizeof(*result))) {
        return GLK43499_CALL_BAD_ABI;
    }
    if (actor == GLK43499_INVALID_HANDLE) {
        glk43499_set_invalid_handle(&result->call);
        return GLK43499_CALL_INVALID_ARGUMENT;
    }
    glk43499_set_unavailable(&result->call);
    return GLK43499_CALL_UNSUPPORTED;
}
