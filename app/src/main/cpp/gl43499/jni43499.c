/**
 * @file jni43499.c
 * @brief Kotlin 具名 DTO 与 43499 C11 Placement/Page/Actor ABI 的无状态翻译层。
 *
 * @note 本文件不缓存 jobject、数组、handle 或 native 状态。页面与 Carrier 字节先
 *       复制到 native 临时缓冲；core 调用期间不持有 pinned array/raw elements。
 * @note 当前 core 是 fail-closed unavailable 实现；所有入口明确返回 UNSUPPORTED。
 */
#include <jni.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/api.h"

/* ------------------------------------------------------------------------- */
/* JNI 原子回执协议（F-C）                                                    */
/* ------------------------------------------------------------------------- */
/*
 * 返回值的三种状态（Kotlin 侧按此结算 token，不再依赖任何单一哨兵）：
 * 1. 正常返回：handle 入口返回 token（0 仍是无效 handle），int 入口返回 core 的
 *    call_status；此时回执必须**完整**写回（callStatus 是最后写的字段）。
 * 2. NOT_ACCEPTED（`-2`）：前置校验失败（result 对象缺失/类型不符、token 前置
 *    条件不满足、输入翻译失败）。此时**不调用 core**、不写回执，token 未被消费；
 *    Kotlin 必须恢复 token。
 * 3. INCOMPLETE（handle 入口 `-1`、int 入口 `INT32_MIN`）：已经调用过 core，但
 *    回执字段无法完整写回。此时**不得**恢复 token（Native 可能已消费），Kotlin
 *    必须 fail-stop：丢弃旧 token、不再 close、不猜测 consumed。
 */
#define GLK43499_JNI_NOT_ACCEPTED_INT ((jint) -2)
#define GLK43499_JNI_INCOMPLETE_INT ((jint) INT32_MIN)
#define GLK43499_JNI_NOT_ACCEPTED_HANDLE ((jlong) -2)
#define GLK43499_JNI_INCOMPLETE_HANDLE ((jlong) -1)

/** @brief Kotlin 结果 DTO 的 JVM 类名（@Keep 固定，R8 不重命名）。 */
#define GLK43499_JNI_CLASS_PLACEMENT_RESULT \
    "com/ghostlock/app/jni/Native43499PlacementResult"
#define GLK43499_JNI_CLASS_LANDING_RESULT \
    "com/ghostlock/app/jni/Native43499LandingResult"
#define GLK43499_JNI_CLASS_CLOSE_RESULT \
    "com/ghostlock/app/jni/Native43499CloseResult"
#define GLK43499_JNI_CLASS_ACTOR_PREPARE_RESULT \
    "com/ghostlock/app/jni/Native43499ActorPrepareResult"
#define GLK43499_JNI_CLASS_ACTOR_EXECUTE_RESULT \
    "com/ghostlock/app/jni/Native43499ActorExecuteResult"

/**
 * @brief 校验 JNI 输出对象非空、无待处理异常且类型匹配。
 * @param env 当前 JNI 环境。
 * @param object 调用方传入的结果对象。
 * @param class_name @Keep 固定的 DTO 类名。
 * @return 1 可写；0 前置拒绝（Kotlin 必须按 not-accepted 恢复 token）。
 * @note 失败时清除查找类产生的待处理异常，不向 Java 抛。
 * @note 阶段：每个 JNI 入口调用 core 之前的第一次校验。
 */
static int32_t glk43499_jni_result_ready(JNIEnv *env, jobject object,
                                         const char *class_name) {
    if (env == NULL || object == NULL) return 0;
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    jclass type = (*env)->FindClass(env, class_name);
    if (type == NULL) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    const jboolean matches = (*env)->IsInstanceOf(env, object, type);
    (*env)->DeleteLocalRef(env, type);
    return matches == JNI_TRUE ? 1 : 0;
}

/**
 * @brief 清掉 JNI 侧的待处理异常（字段写回失败时使用；JNI 入口不得抛）。
 * @param env 当前 JNI 环境。
 * @note 阶段：回执写回失败的收口。
 */
static void glk43499_jni_clear_pending(JNIEnv *env) {
    if (env != NULL && (*env)->ExceptionCheck(env) == JNI_TRUE) {
        (*env)->ExceptionClear(env);
    }
}

/** @brief 初始化由 JNI 拥有的顶层 ABI 结构。 */
#define GLK43499_INIT_ABI(value_, type_) \
    ((value_).abi = (struct glk43499_abi_header) GLK43499_ABI_HEADER_INIT(type_))

/**
 * @brief 获取对象字段 ID。
 * @param env 当前 JNI 环境。
 * @param object 非空 Kotlin DTO。
 * @param name JVM 字段名。
 * @param signature JVM 字段签名。
 * @return 字段 ID；查找失败时保留 JVM 异常并返回 NULL。
 */
static jfieldID glk43499_field(JNIEnv *env, jobject object, const char *name,
                              const char *signature) {
    if (object == NULL) return NULL;
    jclass type = (*env)->GetObjectClass(env, object);
    if (type == NULL) return NULL;
    jfieldID field = (*env)->GetFieldID(env, type, name, signature);
    (*env)->DeleteLocalRef(env, type);
    return field;
}

/** @brief 按被 @Keep 固定的 JVM 类名识别 Kotlin Primitive 实现。 */
static int32_t glk43499_is_instance(JNIEnv *env, jobject object,
                                   const char *class_name) {
    jclass type = (*env)->FindClass(env, class_name);
    if (type == NULL) return 0;
    const jboolean matches = (*env)->IsInstanceOf(env, object, type);
    (*env)->DeleteLocalRef(env, type);
    return matches == JNI_TRUE;
}

/** @brief 读取一个 Kotlin Long 字段。 */
static int32_t glk43499_get_long(JNIEnv *env, jobject object, const char *name,
                                uint64_t *out) {
    jfieldID field = glk43499_field(env, object, name, "J");
    if (field == NULL) return 0;
    *out = (uint64_t) (*env)->GetLongField(env, object, field);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/** @brief 读取一个 Kotlin Int 字段。 */
static int32_t glk43499_get_int(JNIEnv *env, jobject object, const char *name,
                               uint32_t *out) {
    jfieldID field = glk43499_field(env, object, name, "I");
    if (field == NULL) return 0;
    *out = (uint32_t) (*env)->GetIntField(env, object, field);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/** @brief 读取 PrimitiveSpec 的 ByteArray 字段。 */
static jbyteArray glk43499_get_byte_array(JNIEnv *env, jobject object,
                                         const char *name) {
    jfieldID field = glk43499_field(env, object, name, "[B");
    if (field == NULL) return NULL;
    return (jbyteArray) (*env)->GetObjectField(env, object, field);
}

/** @brief 写回 Kotlin Int 字段。 */
static int32_t glk43499_set_int(JNIEnv *env, jobject object, const char *name,
                               int32_t value) {
    jfieldID field = glk43499_field(env, object, name, "I");
    if (field == NULL) return 0;
    (*env)->SetIntField(env, object, field, (jint) value);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/** @brief 写回 Kotlin Long 字段。 */
static int32_t glk43499_set_long(JNIEnv *env, jobject object, const char *name,
                                uint64_t value) {
    jfieldID field = glk43499_field(env, object, name, "J");
    if (field == NULL) return 0;
    (*env)->SetLongField(env, object, field, (jlong) value);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/** @brief 写回 Kotlin Boolean 字段。 */
static int32_t glk43499_set_bool(JNIEnv *env, jobject object, const char *name,
                                uint32_t value) {
    jfieldID field = glk43499_field(env, object, name, "Z");
    if (field == NULL) return 0;
    (*env)->SetBooleanField(env, object, field, value != 0 ? JNI_TRUE : JNI_FALSE);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/** @brief 初始化 core 输出结构的 ABI 前缀。 */
static void glk43499_init_call_result(struct glk43499_call_result *result,
                                     uint32_t struct_size) {
    memset(result, 0, struct_size);
    result->abi.struct_size = struct_size;
    result->abi.abi_version = GLK43499_ABI_VERSION;
}

/** @brief 写回 call_result 中除 `callStatus` 之外的全部字段。 */
static int32_t glk43499_write_call_result_body(
    JNIEnv *env, jobject target, const struct glk43499_call_result *source) {
    return glk43499_set_int(env, target, "effect", source->effect) &&
           glk43499_set_int(env, target, "resourceState", source->resource_state) &&
           glk43499_set_int(env, target, "systemError", source->system_error) &&
           glk43499_set_long(env, target, "retainedResourceCount",
                             source->retained_resource_count);
}

/**
 * @brief 最后写回 `callStatus`（原子回执的完成标志）。
 * @param env 当前 JNI 环境。
 * @param target Kotlin 结果对象。
 * @param status core 的状态。
 * @return 1 成功；0 失败（调用方清异常并返回 incomplete 哨兵）。
 */
static int32_t glk43499_write_call_status(JNIEnv *env, jobject target,
                                          int32_t status) {
    return glk43499_set_int(env, target, "callStatus", status);
}

/**
 * @brief 完整写回公共调用回执：先写其它字段，最后写 callStatus。
 * @return 1 回执完整；0 表示 incomplete（调用方必须清异常并返回哨兵）。
 */
static int32_t glk43499_write_call_result(
    JNIEnv *env, jobject target, const struct glk43499_call_result *source) {
    if (target == NULL) return 0;
    if (!glk43499_write_call_result_body(env, target, source)) return 0;
    return glk43499_write_call_status(env, target, source->call_status);
}

/**
 * @brief 完整写回 close/cancel 回执（callStatus 最后写）。
 * @return 1 完整；0 incomplete。
 */
static int32_t glk43499_write_close_result(
    JNIEnv *env, jobject target, const struct glk43499_close_result *source) {
    if (target == NULL) return 0;
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (!glk43499_set_bool(env, target, "stopRequested", source->stop_requested) ||
        !glk43499_set_bool(env, target, "workersJoined", source->workers_joined) ||
        !glk43499_set_bool(env, target, "ownedChainReleased",
                           source->owned_chain_released) ||
        !glk43499_set_bool(env, target, "retainedForProcessExit",
                           source->retained_for_process_exit)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/**
 * @brief 把 Java ByteArray 复制到 native-owned 临时缓冲。
 * @param env 当前 JNI 环境。
 * @param source 非空数组。
 * @param out_data 成功时接收缓冲地址。
 * @param out_size 成功时接收字节数。
 * @return 1 成功；0 表示空数组、分配失败或 JVM 异常。
 */
typedef int32_t glk43499_copy_status;
enum {
    GLK43499_COPY_INVALID = 0,
    GLK43499_COPY_OK = 1,
    GLK43499_COPY_NO_MEMORY = 2
};

static glk43499_copy_status glk43499_copy_bytes(
    JNIEnv *env, jbyteArray source, uint8_t **out_data, uint64_t *out_size) {
    *out_data = NULL;
    *out_size = 0;
    if (source == NULL) return GLK43499_COPY_INVALID;
    const jsize length = (*env)->GetArrayLength(env, source);
    if (length <= 0 || (*env)->ExceptionCheck(env) == JNI_TRUE) {
        return GLK43499_COPY_INVALID;
    }
    uint8_t *copy = (uint8_t *) malloc((size_t) length);
    if (copy == NULL) return GLK43499_COPY_NO_MEMORY;
    (*env)->GetByteArrayRegion(env, source, 0, length, (jbyte *) copy);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        memset(copy, 0, (size_t) length);
        free(copy);
        return GLK43499_COPY_INVALID;
    }
    *out_data = copy;
    *out_size = (uint64_t) length;
    return GLK43499_COPY_OK;
}

/** @brief 清零并释放调用期字节副本。 */
static void glk43499_clear_free(uint8_t *data, uint64_t size) {
    if (data == NULL) return;
    volatile uint8_t *cursor = data;
    for (uint64_t index = 0; index < size; ++index) cursor[index] = 0;
    free(data);
}

/** @brief Kotlin PlacementRequest 到 C11 POD 的逐字段翻译。 */
static int32_t glk43499_read_placement_request(
    JNIEnv *env, jobject source, struct glk43499_placement_request *target) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_placement_request);
    return source != NULL &&
           glk43499_get_long(env, source, "pageSize", &target->page_size) &&
           glk43499_get_long(env, source, "allocationSize", &target->allocation_size) &&
           glk43499_get_long(env, source, "requiredAlignment", &target->required_alignment) &&
           glk43499_get_long(env, source, "scanLowerBound", &target->scan_lower_bound) &&
           glk43499_get_long(env, source, "scanUpperBound", &target->scan_upper_bound) &&
           glk43499_get_long(env, source, "scanStep", &target->scan_step) &&
           glk43499_get_long(env, source, "scanRoundLimit", &target->scan_round_limit) &&
           glk43499_get_long(env, source, "timeoutNanos", &target->timeout_ns) &&
           glk43499_get_int(env, source, "placementCpu", &target->placement_cpu) &&
           glk43499_get_int(env, source, "measurementCpu", &target->measurement_cpu) &&
           glk43499_get_int(env, source, "objectsPerSlab", &target->objects_per_slab) &&
           glk43499_get_int(env, source, "prepareObjectCount", &target->prepare_object_count) &&
           glk43499_get_int(env, source, "sprayObjectCount", &target->spray_object_count) &&
           glk43499_get_int(env, source, "preObjectCount", &target->pre_object_count) &&
           glk43499_get_int(env, source, "postObjectCount", &target->post_object_count);
}

/** @brief Kotlin LandingRequest 到 C11 POD 的逐字段翻译。 */
static int32_t glk43499_read_landing_request(
    JNIEnv *env, jobject source, struct glk43499_landing_request *target) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_landing_request);
    return source != NULL &&
           glk43499_get_long(env, source, "expectedPageBase", &target->expected_page_base) &&
           glk43499_get_long(env, source, "pageSize", &target->page_size) &&
           glk43499_get_long(env, source, "allocationSize", &target->allocation_size) &&
           glk43499_get_long(env, source, "requiredAlignment", &target->required_alignment) &&
           glk43499_get_long(env, source, "reclaimPayloadSize", &target->reclaim_payload_size) &&
           glk43499_get_long(env, source, "socketSendBufferSize",
                             &target->socket_send_buffer_size) &&
           glk43499_get_long(env, source, "reclaimSendLimit", &target->reclaim_send_limit) &&
           glk43499_get_long(env, source, "timeoutNanos", &target->timeout_ns);
}

/** @brief Kotlin ActorSetup 到 C11 POD 的逐字段翻译。 */
static int32_t glk43499_read_actor_setup(
    JNIEnv *env, jobject source, struct glk43499_actor_setup *target) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_actor_setup);
    uint32_t owner_priority = 0;
    uint32_t waiter_priority = 0;
    uint32_t consumer_priority = 0;
    if (source == NULL ||
        !glk43499_get_int(env, source, "ownerCpu", &target->owner_cpu) ||
        !glk43499_get_int(env, source, "waiterCpu", &target->waiter_cpu) ||
        !glk43499_get_int(env, source, "consumerCpu", &target->consumer_cpu) ||
        !glk43499_get_int(env, source, "ownerPriority", &owner_priority) ||
        !glk43499_get_int(env, source, "waiterPriority", &waiter_priority) ||
        !glk43499_get_int(env, source, "consumerPriority", &consumer_priority) ||
        !glk43499_get_int(env, source, "threadCount", &target->thread_count) ||
        !glk43499_get_long(env, source, "prepareTimeoutNanos", &target->prepare_timeout_ns) ||
        !glk43499_get_long(env, source, "parkedTimeoutNanos", &target->parked_timeout_ns) ||
        !glk43499_get_long(env, source, "executionTimeoutNanos", &target->execution_timeout_ns) ||
        !glk43499_get_long(env, source, "shutdownTimeoutNanos", &target->shutdown_timeout_ns)) {
        return 0;
    }
    target->owner_priority = (int32_t) owner_priority;
    target->waiter_priority = (int32_t) waiter_priority;
    target->consumer_priority = (int32_t) consumer_priority;
    return 1;
}

/** @brief 读取三个 Carrier 共用的最终地址。 */
static int32_t glk43499_read_primitive_addresses(
    JNIEnv *env, jobject source, struct glk43499_primitive_addresses *target) {
    return glk43499_get_long(env, source, "finalWaiterAddress",
                             &target->final_waiter_address) &&
           glk43499_get_long(env, source, "finalTaskAddress",
                             &target->final_task_address) &&
           glk43499_get_long(env, source, "finalLockAddress",
                             &target->final_lock_address);
}

/** @brief Kotlin sealed PrimitiveSpec 到 C11 tagged union 的 prepare-time 翻译。 */
static int32_t glk43499_read_primitive(
    JNIEnv *env, jobject source, struct glk43499_primitive_spec *target,
    uint8_t **frame_copy, uint64_t *frame_size,
    glk43499_copy_status *copy_status) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_primitive_spec);
    if (source == NULL) return 0;
    jbyteArray frame = glk43499_get_byte_array(env, source, "frameBytes");
    if (frame == NULL) return 0;
    *copy_status = glk43499_copy_bytes(env, frame, frame_copy, frame_size);
    if (*copy_status != GLK43499_COPY_OK) {
        if (frame != NULL) (*env)->DeleteLocalRef(env, frame);
        return 0;
    }
    (*env)->DeleteLocalRef(env, frame);

    if (glk43499_is_instance(env, source,
            "com/ghostlock/app/jni/Native43499MulticastSpec")) {
        target->kind = GLK43499_PRIMITIVE_MULTICAST_SENDMSG;
    } else if (glk43499_is_instance(env, source,
                   "com/ghostlock/app/jni/Native43499PselectSpec")) {
        target->kind = GLK43499_PRIMITIVE_PSELECT;
    } else if (glk43499_is_instance(env, source,
                   "com/ghostlock/app/jni/Native43499TcpZerocopySpec")) {
        target->kind = GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE;
    } else {
        return 0;
    }

    switch (target->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG: {
            struct glk43499_multicast_spec *spec = &target->value.multicast_sendmsg;
            spec->frame_bytes = *frame_copy;
            spec->frame_size = *frame_size;
            return glk43499_read_primitive_addresses(env, source, &spec->addresses) &&
                   glk43499_get_long(env, source, "socketBufferSize",
                                     &spec->socket_buffer_size) &&
                   glk43499_get_long(env, source, "operationBudget",
                                     &spec->operation_budget) &&
                   glk43499_get_long(env, source, "timeoutNanos", &spec->timeout_ns) &&
                   glk43499_get_int(env, source, "addressFamily", &spec->address_family);
        }
        case GLK43499_PRIMITIVE_PSELECT: {
            struct glk43499_pselect_spec *spec = &target->value.pselect;
            spec->frame_bytes = *frame_copy;
            spec->frame_size = *frame_size;
            return glk43499_read_primitive_addresses(env, source, &spec->addresses) &&
                   glk43499_get_long(env, source, "descriptorCapacity",
                                     &spec->descriptor_capacity) &&
                   glk43499_get_long(env, source, "operationBudget",
                                     &spec->operation_budget) &&
                   glk43499_get_long(env, source, "timeoutNanos", &spec->timeout_ns);
        }
        case GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE: {
            struct glk43499_tcp_zerocopy_spec *spec =
                &target->value.tcp_zerocopy_receive;
            spec->frame_bytes = *frame_copy;
            spec->frame_size = *frame_size;
            return glk43499_read_primitive_addresses(env, source, &spec->addresses) &&
                   glk43499_get_long(env, source, "mappingSize", &spec->mapping_size) &&
                   glk43499_get_long(env, source, "receiveSize", &spec->receive_size) &&
                   glk43499_get_long(env, source, "mappingOffset", &spec->mapping_offset) &&
                   glk43499_get_long(env, source, "operationBudget",
                                     &spec->operation_budget) &&
                   glk43499_get_long(env, source, "timeoutNanos", &spec->timeout_ns);
        }
        default:
            return 0;
    }
}

/** @brief 写回 preparePlacement 的候选页事实。 */
static int32_t glk43499_write_placement_result(
    JNIEnv *env, jobject target, const struct glk43499_placement_result *source) {
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (glk43499_set_long(env, target, "candidatePageBase",
                             source->facts.candidate_page_base) &&
           glk43499_set_long(env, target, "pageSize", source->facts.page_size) &&
           glk43499_set_long(env, target, "allocationSize",
                             source->facts.allocation_size) &&
           glk43499_set_long(env, target, "requiredAlignment",
                             source->facts.required_alignment) &&
           glk43499_set_long(env, target, "measuredObjectAddress",
                             source->facts.measured_object_address) &&
           glk43499_set_long(env, target, "measurementRounds",
                             source->facts.measurement_rounds) &&
           glk43499_set_long(env, target, "retainedChildCount",
                             source->facts.retained_child_count) &&
           glk43499_set_long(env, target, "retainedDescriptorCount",
                             source->facts.retained_descriptor_count)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 Landing 提交点和计数。 */
static int32_t glk43499_write_landing_result(
    JNIEnv *env, jobject target, const struct glk43499_landing_result *source) {
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (glk43499_set_long(env, target, "actualPageBase",
                             source->receipt.actual_page_base) &&
           glk43499_set_long(env, target, "contentGeneration",
                             source->receipt.content_generation) &&
           glk43499_set_long(env, target, "closedPrepareCount",
                             source->receipt.closed_prepare_count) &&
           glk43499_set_long(env, target, "closedSprayCount",
                             source->receipt.closed_spray_count) &&
           glk43499_set_long(env, target, "closedPreCount",
                             source->receipt.closed_pre_count) &&
           glk43499_set_long(env, target, "closedPostCount",
                             source->receipt.closed_post_count) &&
           glk43499_set_long(env, target, "reclaimSendCount",
                             source->receipt.reclaim_send_count) &&
           glk43499_set_bool(env, target, "commitReached",
                             source->receipt.commit_reached) &&
           glk43499_set_bool(env, target, "placementConsumed",
                             source->receipt.placement_consumed) &&
           glk43499_set_bool(env, target, "reclaimRetained",
                             source->receipt.reclaim_retained)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 prepareActor 的准备事实。 */
static int32_t glk43499_write_actor_prepare_result(
    JNIEnv *env, jobject target, const struct glk43499_actor_prepare_result *source) {
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (glk43499_set_long(env, target, "pageContentGeneration",
                          source->receipt.page_content_generation) &&
           glk43499_set_int(env, target, "primitiveKind",
                            source->receipt.primitive_kind) &&
           glk43499_set_int(env, target, "threadCount",
                            (int32_t) source->receipt.thread_count) &&
           glk43499_set_bool(env, target, "pageConsumed",
                             source->receipt.page_consumed) &&
           glk43499_set_bool(env, target, "ownerReady", source->receipt.owner_ready) &&
           glk43499_set_bool(env, target, "waiterReady", source->receipt.waiter_ready) &&
           glk43499_set_bool(env, target, "consumerReady",
                             source->receipt.consumer_ready) &&
           glk43499_set_bool(env, target, "carrierPrepared",
                             source->receipt.carrier_prepared)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 executeActor 的物理事实。 */
static int32_t glk43499_write_actor_execute_result(
    JNIEnv *env, jobject target, const struct glk43499_actor_execute_result *source) {
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (glk43499_set_long(env, target, "pageContentGeneration",
                          source->receipt.page_content_generation) &&
           glk43499_set_long(env, target, "consumerCallCount",
                             source->receipt.consumer_call_count) &&
           glk43499_set_long(env, target, "consumerSuccessCount",
                             source->receipt.consumer_success_count) &&
           glk43499_set_long(env, target, "consumerInflightAtReturn",
                             source->receipt.consumer_inflight_at_return) &&
           glk43499_set_bool(env, target, "ownerReady", source->receipt.owner_ready) &&
           glk43499_set_bool(env, target, "waiterParked",
                             source->receipt.waiter_parked) &&
           glk43499_set_bool(env, target, "carrierArmed",
                             source->receipt.carrier_armed) &&
           glk43499_set_bool(env, target, "windowOpened",
                             source->receipt.window_opened) &&
           glk43499_set_bool(env, target, "carrierDisarmed",
                             source->receipt.carrier_disarmed) &&
           glk43499_set_bool(env, target, "retainedForProcessExit",
                             source->receipt.retained_for_process_exit)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_preparePlacement(
    JNIEnv *env, jclass clazz, jobject request_object, jobject result_object) {
    (void) clazz;
    if (request_object == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_PLACEMENT_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    struct glk43499_placement_request request;
    struct glk43499_placement_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    if (!glk43499_read_placement_request(env, request_object, &request)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    const glk43499_handle handle = glk43499_prepare_placement(&request, &result);
    if (!glk43499_write_placement_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_HANDLE;
    }
    return (jlong) handle;
}

/**
 * @brief cancel/close Placement 的公共搬运（原子回执 + 三态返回值）。
 * @return core 状态；NOT_ACCEPTED / INCOMPLETE 为 JNI 侧哨兵。
 */
static jint glk43499_jni_placement_close_call(JNIEnv *env, jlong handle,
                                             jobject result_object,
                                             int32_t close_call) {
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status =
        close_call != 0
            ? glk43499_close_placement((glk43499_handle) handle, &result)
            : glk43499_cancel_placement((glk43499_handle) handle, &result);
    if (!glk43499_write_close_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_cancelPlacement(
    JNIEnv *env, jclass clazz, jlong handle, jobject result_object) {
    (void) clazz;
    return glk43499_jni_placement_close_call(env, handle, result_object, 0);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closePlacement(
    JNIEnv *env, jclass clazz, jlong handle, jobject result_object) {
    (void) clazz;
    return glk43499_jni_placement_close_call(env, handle, result_object, 1);
}

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_landPage(
    JNIEnv *env, jclass clazz, jlong placement, jobject request_object,
    jbyteArray final_bytes, jlong content_generation, jobject result_object) {
    (void) clazz;
    if (placement == 0 || request_object == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_LANDING_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    struct glk43499_landing_request request;
    struct glk43499_landing_result result;
    struct glk43499_page_bytes bytes;
    uint8_t *bytes_copy = NULL;
    uint64_t bytes_size = 0;
    glk43499_copy_status copy_status = GLK43499_COPY_INVALID;
    memset(&bytes, 0, sizeof(bytes));
    GLK43499_INIT_ABI(bytes, struct glk43499_page_bytes);
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));

    if (!glk43499_read_landing_request(env, request_object, &request)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    copy_status = glk43499_copy_bytes(env, final_bytes, &bytes_copy, &bytes_size);
    if (copy_status != GLK43499_COPY_OK) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    bytes.data = bytes_copy;
    bytes.size = bytes_size;
    bytes.content_generation = (uint64_t) content_generation;
    const glk43499_handle page = glk43499_land_page(
        (glk43499_handle) placement, &bytes, &request, &result);
    glk43499_clear_free(bytes_copy, bytes_size);
    if (!glk43499_write_landing_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_HANDLE;
    }
    return (jlong) page;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closePage(
    JNIEnv *env, jclass clazz, jlong page, jobject result_object) {
    (void) clazz;
    if (page == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status =
        glk43499_close_page((glk43499_handle) page, &result);
    if (!glk43499_write_close_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_prepareActor(
    JNIEnv *env, jclass clazz, jlong page, jobject setup_object,
    jobject primitive_object, jobject result_object) {
    (void) clazz;
    if (page == 0 || setup_object == NULL || primitive_object == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_ACTOR_PREPARE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    struct glk43499_actor_setup setup;
    struct glk43499_primitive_spec primitive;
    struct glk43499_actor_prepare_result result;
    uint8_t *frame_copy = NULL;
    uint64_t frame_size = 0;
    glk43499_copy_status copy_status = GLK43499_COPY_INVALID;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));

    if (!glk43499_read_actor_setup(env, setup_object, &setup) ||
        !glk43499_read_primitive(env, primitive_object, &primitive,
                                 &frame_copy, &frame_size, &copy_status)) {
        glk43499_clear_free(frame_copy, frame_size);
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    const glk43499_handle actor = glk43499_prepare_actor(
        (glk43499_handle) page, &setup, &primitive, &result);
    glk43499_clear_free(frame_copy, frame_size);
    if (!glk43499_write_actor_prepare_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_HANDLE;
    }
    return (jlong) actor;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_executeActor(
    JNIEnv *env, jclass clazz, jlong actor, jobject result_object) {
    (void) clazz;
    if (actor == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_ACTOR_EXECUTE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_actor_execute_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status =
        glk43499_execute_actor((glk43499_handle) actor, &result);
    if (!glk43499_write_actor_execute_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

/** @brief cancel/close Actor 的公共搬运（原子回执 + 三态返回值）。 */
static jint glk43499_jni_actor_close_call(JNIEnv *env, jlong handle,
                                         jobject result_object,
                                         int32_t close_call) {
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status =
        close_call != 0
            ? glk43499_close_actor((glk43499_handle) handle, &result)
            : glk43499_cancel_actor((glk43499_handle) handle, &result);
    if (!glk43499_write_close_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_cancelActor(
    JNIEnv *env, jclass clazz, jlong actor, jobject result_object) {
    (void) clazz;
    return glk43499_jni_actor_close_call(env, actor, result_object, 0);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closeActor(
    JNIEnv *env, jclass clazz, jlong actor, jobject result_object) {
    (void) clazz;
    return glk43499_jni_actor_close_call(env, actor, result_object, 1);
}
