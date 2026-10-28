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
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/gl_log.h"
#include "core/api.h"
#include "core/victim_protocol.h"

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

/* L7：三态哨兵单一来源（C 宏）+ Kotlin `Native43499Jni.RESULT_*` 对拍；
 * 本组值是唯一权威，禁止在入口内新写数字。 */
_Static_assert(GLK43499_JNI_NOT_ACCEPTED_INT == -2,
               "JNI NOT_ACCEPTED int sentinel drift");
_Static_assert(GLK43499_JNI_INCOMPLETE_INT == INT32_MIN,
               "JNI INCOMPLETE int sentinel drift");
_Static_assert(GLK43499_JNI_NOT_ACCEPTED_HANDLE == -2,
               "JNI NOT_ACCEPTED handle sentinel drift");
_Static_assert(GLK43499_JNI_INCOMPLETE_HANDLE == -1,
               "JNI INCOMPLETE handle sentinel drift");

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
#define GLK43499_JNI_CLASS_PERF_OPEN_RESULT \
    "com/ghostlock/app/jni/Native43499PerfOpenResult"
#define GLK43499_JNI_CLASS_PERF_COLLECT_RESULT \
    "com/ghostlock/app/jni/Native43499PerfCollectResult"
#define GLK43499_JNI_CLASS_VICTIM_SPAWN_RESULT \
    "com/ghostlock/app/jni/VictimSpawnJniResult"
#define GLK43499_JNI_CLASS_VICTIM_CLOSE_RESULT \
    "com/ghostlock/app/jni/VictimCloseJniResult"
#define GLK43499_JNI_CLASS_LIMITS_RESULT \
    "com/ghostlock/app/jni/Native43499LimitsResult"

/** @brief helper 路径字节上限（1<<12；JNI 域校验，非协议 ABI）。 */
#define GLK43499_JNI_VICTIM_PATH_MAX (UINT32_C(1) << 12)

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
 * @brief 逐条检查 JNI 写回结果（替代长 `&&` 链）。
 *
 * @note E1 真机（A301SO）复现：result writer 的长 `&&` 链偶发返回 0
 *   （pending_exception=0），逐条显式检查后稳定通过；语义完全相同（fail-closed）。
 *   根因（toolchain/codegen）待 follow-up 定位；此处只做机械等价改写。
 */
#define GLK43499_WRITE_OR_FAIL(call_) \
    do {                              \
        if (!(call_)) return 0;       \
    } while (0)

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

/** @brief 按有符号 Int 读取字段（priority 等显式 int32 域；不做 u32 位绕行）。 */
static int32_t glk43499_get_int32(JNIEnv *env, jobject object,
                                  const char *name, int32_t *out) {
    jfieldID field = glk43499_field(env, object, name, "I");
    if (field == NULL) return 0;
    *out = (int32_t) (*env)->GetIntField(env, object, field);
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

static int32_t glk43499_write_call_status(JNIEnv *env, jobject target,
                                          int32_t status);

/**
 * @brief 前置字段缺失/越界时的 fail-closed 回执：写 INVALID_ARGUMENT 后清异常。
 * @param env 当前 JNI 环境。
 * @param result Kotlin 结果对象（可为 NULL）。
 * @note P0a：Kotlin DTO 尚未携带新参数时，JNI 明确拒绝并写回具名状态，
 *       而不是静默用 0 默认值继续（Native 不得回退默认值）。
 */
static void glk43499_jni_reject_invalid_argument(JNIEnv *env,
                                                 jobject result) {
    glk43499_jni_clear_pending(env);
    if (result != NULL) {
        (void) glk43499_write_call_status(env, result,
                                          GLK43499_CALL_INVALID_ARGUMENT);
    }
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
 * @brief 写回 JNI 侧失败回执（core 未被调用；用于 OOM/参数域失败）。
 *
 * @param env 当前 JNI 环境。
 * @param result Kotlin 结果对象；可为 NULL（只清异常）。
 * @param status 具名 call_status（SYSTEM_ERROR/INVALID_ARGUMENT）。
 * @param effect 物理效果（core 未调用 ⇒ NOT_APPLIED）。
 * @param resource 资源状态（按入口资源的打开状态）。
 * @param system_error 仅 SYSTEM_ERROR 有意义（OOM=ENOMEM）。
 * @return 1 回执写回成功；0 写回失败（调用方返回 INCOMPLETE）。
 * @note L2：本 helper 只用于 **非翻译失败** 的 JNI 侧失败（OOM/参数域）；
 *       GetFieldID/类型/数组读取失败一律 NOT_ACCEPTED 且不写回执。
 */
static int32_t glk43499_write_jni_failure_result(
    JNIEnv *env, jobject result, int32_t status, int32_t effect,
    int32_t resource, int32_t system_error) {
    glk43499_jni_clear_pending(env);
    if (result == NULL) return 1;
    struct glk43499_call_result receipt;
    glk43499_init_call_result(&receipt, (uint32_t) sizeof(receipt));
    receipt.call_status = (glk43499_call_status) status;
    receipt.effect = (glk43499_effect) effect;
    receipt.resource_state = (glk43499_resource_state) resource;
    receipt.system_error = system_error;
    receipt.retained_resource_count = 0;
    return glk43499_write_call_result(env, result, &receipt);
}

/**
 * @brief 完整写回 close/cancel 回执（callStatus 最后写）。
 * @return 1 完整；0 incomplete。
 */
static int32_t glk43499_write_close_result(
    JNIEnv *env, jobject target, const struct glk43499_close_result *source) {
    if (target == NULL) return 0;
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    /* L6：close/cancel 共用本唯一 writer；uint32 语义统一归一为 0/1 的
     * Kotlin Boolean，禁止各入口自行解释 bool。 */
    const uint32_t stop_requested = source->stop_requested != 0 ? 1U : 0U;
    const uint32_t workers_joined = source->workers_joined != 0 ? 1U : 0U;
    const uint32_t owned_chain_released =
        source->owned_chain_released != 0 ? 1U : 0U;
    const uint32_t retained_for_process_exit =
        source->retained_for_process_exit != 0 ? 1U : 0U;
    if (!glk43499_set_bool(env, target, "stopRequested", stop_requested) ||
        !glk43499_set_bool(env, target, "workersJoined", workers_joined) ||
        !glk43499_set_bool(env, target, "ownedChainReleased",
                           owned_chain_released) ||
        !glk43499_set_bool(env, target, "retainedForProcessExit",
                           retained_for_process_exit)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/**
 * @brief 读取一个非空 UTF-8 字符串并深拷贝（JNI 临时引用不外泄）。
 * @param env 当前 JNI 环境。
 * @param source 非空 jstring。
 * @param max_bytes 字节上限（含 NUL 前）。
 * @param out_text 必填；成功写 malloc 的 C 字符串。
 * @return 1 成功；0 失败（调用方清理异常）。
 */
static int32_t glk43499_jni_read_utf8(JNIEnv *env, jstring source,
                                      uint32_t max_bytes, char **out_text) {
    if (env == NULL || source == NULL || out_text == NULL) return 0;
    const char *chars = (*env)->GetStringUTFChars(env, source, NULL);
    if (chars == NULL) return 0;
    const jsize length = (*env)->GetStringUTFLength(env, source);
    if (length <= 0 || (uint32_t) length > max_bytes) {
        (*env)->ReleaseStringUTFChars(env, source, chars);
        return 0;
    }
    char *copy = (char *) malloc((size_t) length + 1U);
    if (copy == NULL) {
        (*env)->ReleaseStringUTFChars(env, source, chars);
        return 0;
    }
    memcpy(copy, chars, (size_t) length);
    copy[length] = '\0';
    (*env)->ReleaseStringUTFChars(env, source, chars);
    *out_text = copy;
    return 1;
}

/**
 * @brief 读取字符串数组为 NULL 结尾的 C 数组（空数组返回 count=0/values=NULL）。
 * @param env 当前 JNI 环境。
 * @param source Java String[]（可为 NULL）。
 * @param max_count 条数上限。
 * @param max_bytes 每项字节上限。
 * @param out_values 必填；成功写 malloc 的 char**（失败保持 NULL）。
 * @param out_count 必填；写回条数。
 * @return 1 成功；0 失败（已释放部分分配）。
 */
static int32_t glk43499_jni_read_utf8_array(JNIEnv *env, jobjectArray source,
                                            uint32_t max_count,
                                            uint32_t max_bytes,
                                            char ***out_values,
                                            uint32_t *out_count) {
    if (env == NULL || out_values == NULL || out_count == NULL) return 0;
    *out_values = NULL;
    *out_count = 0;
    if (source == NULL) return 1;
    const jsize count = (*env)->GetArrayLength(env, source);
    if (count < 0 || (uint32_t) count > max_count) return 0;
    char **values = (char **) calloc((size_t) count + 1U, sizeof(char *));
    if (values == NULL) return 0;
    for (jsize index = 0; index < count; ++index) {
        jobject item = (*env)->GetObjectArrayElement(env, source, index);
        if (item == NULL ||
            !glk43499_jni_read_utf8(env, (jstring) item, max_bytes,
                                    &values[index])) {
            if (item != NULL) (*env)->DeleteLocalRef(env, item);
            for (jsize done = 0; done < index; ++done) free(values[done]);
            free(values);
            return 0;
        }
        (*env)->DeleteLocalRef(env, item);
    }
    *out_values = values;
    *out_count = (uint32_t) count;
    return 1;
}

/**
 * @brief 释放 glk43499_jni_read_utf8_array 的结果。
 * @param values 字符串数组（可 NULL）。
 * @param count 条数。
 */
static void glk43499_jni_free_utf8_array(char **values, uint32_t count) {
    if (values == NULL) return;
    for (uint32_t index = 0; index < count; ++index) free(values[index]);
    free(values);
}

/** @brief Kotlin PerfOpenRequest 到 C11 POD 的逐字段翻译（15 个必填字段）。 */
static int32_t glk43499_read_perf_open_request(
    JNIEnv *env, jobject source, struct glk43499_perf_open_request *target) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_perf_open_request);
    return source != NULL &&
           glk43499_get_long(env, source, "pid", &target->pid) &&
           glk43499_get_long(env, source, "cpu", &target->cpu) &&
           glk43499_get_long(env, source, "attrType", &target->attr_type) &&
           glk43499_get_long(env, source, "attrConfig", &target->attr_config) &&
           glk43499_get_long(env, source, "attrSamplePeriod",
                             &target->attr_sample_period) &&
           glk43499_get_long(env, source, "attrSampleType",
                             &target->attr_sample_type) &&
           glk43499_get_long(env, source, "attrSampleRegsIntr",
                             &target->attr_sample_regs_intr) &&
           glk43499_get_long(env, source, "attrFlags", &target->attr_flags) &&
           glk43499_get_long(env, source, "attrDisabled",
                             &target->attr_disabled) &&
           glk43499_get_long(env, source, "excludeUser",
                             &target->exclude_user) &&
           glk43499_get_long(env, source, "excludeHv", &target->exclude_hv) &&
           glk43499_get_long(env, source, "excludeIdle",
                             &target->exclude_idle) &&
           glk43499_get_long(env, source, "pageSize", &target->page_size) &&
           glk43499_get_long(env, source, "ringPages", &target->ring_pages) &&
           glk43499_get_long(env, source, "autoEnable", &target->auto_enable);
}

/** @brief 写回 collectPerfSamples 的公共回执 + 环事实（callStatus 最后写）。 */
static int32_t glk43499_write_perf_collect_result(
    JNIEnv *env, jobject target,
    const struct glk43499_perf_collect_result *source) {
    if (target == NULL) return 0;
    if (!glk43499_write_call_result_body(env, target, &source->call)) return 0;
    if (!glk43499_set_long(env, target, "dataOffset",
                           source->receipt.data_offset) ||
        !glk43499_set_long(env, target, "dataSize",
                           source->receipt.data_size) ||
        !glk43499_set_long(env, target, "dataHead",
                           source->receipt.data_head) ||
        !glk43499_set_long(env, target, "dataTailBefore",
                           source->receipt.data_tail_before) ||
        !glk43499_set_long(env, target, "bytesCopied",
                           source->receipt.bytes_copied) ||
        !glk43499_set_bool(env, target, "tailMoved",
                           source->receipt.tail_moved)) {
        return 0;
    }
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/**
 * @brief 把 core 已拷贝的 raw 环字节写回 Kotlin ByteArray。
 * @param env 当前 JNI 环境。
 * @param target 非空目标数组。
 * @param data 非空源缓冲。
 * @param bytes 要写回的字节数（0 合法=no-op）。
 * @return 1 成功；0 失败（JVM 异常保留由调用方清理）。
 * @note 只在 core 返回后调用，保持「core 期间不持 pinned array」约定。
 */
static int32_t glk43499_write_byte_array(JNIEnv *env, jbyteArray target,
                                         uint64_t capacity,
                                         const uint8_t *data,
                                         uint64_t bytes) {
    if (target == NULL) return 0;
    if (bytes == 0) return 1;
    if (data == NULL || bytes > capacity ||
        bytes > (uint64_t) INT32_MAX) {
        return 0;
    }
    (*env)->SetByteArrayRegion(env, target, 0, (jsize) bytes,
                               (const jbyte *) data);
    return (*env)->ExceptionCheck(env) == JNI_FALSE;
}

/**
 * @brief 把 Java ByteArray 复制到 native-owned 临时缓冲。
 * @param env 当前 JNI 环境。
 * @param source ByteArray；NULL=参数域非法。
 * @param out_data 成功时接收缓冲地址（长度 0 时为 NULL）。
 * @param out_size 成功时接收字节数（空数组=0）。
 * @return 分类状态；调用方按 L2/L3 映射回执。
 * @note 空数组（length=0）合法：OK + data=NULL/size=0，不分配。
 * @note malloc 失败=NO_MEMORY（SYSTEM_ERROR/ENOMEM）；GetFieldID/类型/数组读取
 *   失败=TRANSLATION_FAILED（NOT_ACCEPTED，不写回执）；NULL/负长度=INVALID。
 */
typedef int32_t glk43499_copy_status;
enum {
    GLK43499_COPY_INVALID = 0,
    GLK43499_COPY_OK = 1,
    GLK43499_COPY_NO_MEMORY = 2,
    GLK43499_COPY_TRANSLATION_FAILED = 3
};

static glk43499_copy_status glk43499_copy_bytes(
    JNIEnv *env, jbyteArray source, uint8_t **out_data, uint64_t *out_size) {
    *out_data = NULL;
    *out_size = 0;
    if (source == NULL) return GLK43499_COPY_INVALID;
    const jsize length = (*env)->GetArrayLength(env, source);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        return GLK43499_COPY_TRANSLATION_FAILED;
    }
    if (length < 0) return GLK43499_COPY_INVALID;
    if (length == 0) return GLK43499_COPY_OK;
    uint8_t *copy = (uint8_t *) malloc((size_t) length);
    if (copy == NULL) return GLK43499_COPY_NO_MEMORY;
    (*env)->GetByteArrayRegion(env, source, 0, length, (jbyte *) copy);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        memset(copy, 0, (size_t) length);
        free(copy);
        return GLK43499_COPY_TRANSLATION_FAILED;
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
           glk43499_get_int(env, source, "postObjectCount", &target->post_object_count) &&
           glk43499_get_long(env, source, "slabOrder", &target->slab_order) &&
           glk43499_get_long(env, source, "mmStructSize", &target->mm_struct_size) &&
           glk43499_get_long(env, source, "mmPartials", &target->mm_partials) &&
           glk43499_get_long(env, source, "prepareSlabs", &target->prepare_slabs) &&
           glk43499_get_long(env, source, "snitchCollisions",
                             &target->snitch_collisions) &&
           glk43499_get_long(env, source, "pollUs", &target->poll_us) &&
           glk43499_get_long(env, source, "identityStart",
                             &target->identity_start) &&
           glk43499_get_long(env, source, "identityEnd",
                             &target->identity_end) &&
           glk43499_get_long(env, source, "leakTagNibble",
                             &target->leak_tag_nibble) &&
           glk43499_get_long(env, source, "leakTagShift",
                             &target->leak_tag_shift) &&
           glk43499_get_long(env, source, "stagingCapacity",
                             &target->staging_capacity) &&
           glk43499_get_long(env, source, "maxContextEntries",
                             &target->max_context_entries) &&
           glk43499_get_long(env, source, "maxObjectsPerSlab",
                             &target->max_objects_per_slab) &&
           glk43499_get_long(env, source, "maxMeasurementRounds",
                             &target->max_measurement_rounds);
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
           glk43499_get_long(env, source, "timeoutNanos", &target->timeout_ns) &&
           glk43499_get_long(env, source, "fillByte", &target->fill_byte) &&
           /* R3a-2/B6：页接受判据（全部来自 Kotlin generator/请求）。 */
           glk43499_get_long(env, source, "pageAcceptRequired",
                             &target->page_accept_required) &&
           glk43499_get_long(env, source, "pageAcceptOffset",
                             &target->page_accept_offset) &&
           glk43499_get_long(env, source, "pageAcceptBit",
                             &target->page_accept_bit);
}

/** @brief Kotlin ActorSetup 到 C11 POD 的逐字段翻译。 */
static int32_t glk43499_read_actor_setup(
    JNIEnv *env, jobject source, struct glk43499_actor_setup *target) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_actor_setup);
    int32_t owner_priority = 0;
    int32_t waiter_priority = 0;
    int32_t consumer_priority = 0;
    if (source == NULL ||
        !glk43499_get_int(env, source, "ownerCpu", &target->owner_cpu) ||
        !glk43499_get_int(env, source, "waiterCpu", &target->waiter_cpu) ||
        !glk43499_get_int(env, source, "consumerCpu", &target->consumer_cpu) ||
        !glk43499_get_int32(env, source, "ownerPriority", &owner_priority) ||
        !glk43499_get_int32(env, source, "waiterPriority", &waiter_priority) ||
        !glk43499_get_int32(env, source, "consumerPriority",
                            &consumer_priority) ||
        !glk43499_get_int(env, source, "threadCount", &target->thread_count) ||
        !glk43499_get_long(env, source, "prepareTimeoutNanos", &target->prepare_timeout_ns) ||
        !glk43499_get_long(env, source, "parkedTimeoutNanos", &target->parked_timeout_ns) ||
        !glk43499_get_long(env, source, "executionTimeoutNanos", &target->execution_timeout_ns) ||
        !glk43499_get_long(env, source, "shutdownTimeoutNanos", &target->shutdown_timeout_ns) ||
        !glk43499_get_long(env, source, "consumerMax", &target->consumer_max) ||
        !glk43499_get_long(env, source, "consumerBurst", &target->consumer_burst) ||
        !glk43499_get_long(env, source, "lockPiSpinNs", &target->lock_pi_spin_ns) ||
        !glk43499_get_long(env, source, "pollUs", &target->poll_us) ||
        !glk43499_get_long(env, source, "setupSettleUs", &target->setup_settle_us) ||
        !glk43499_get_long(env, source, "requeueExpected",
                           (uint64_t *) &target->requeue_expected) ||
        !glk43499_get_long(env, source, "disarmSpinLimit",
                           &target->disarm_spin_limit)) {
        return 0;
    }
    target->owner_priority = owner_priority;
    target->waiter_priority = waiter_priority;
    target->consumer_priority = consumer_priority;
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
    glk43499_copy_status *copy_status, int32_t *invalid_kind) {
    memset(target, 0, sizeof(*target));
    GLK43499_INIT_ABI(*target, struct glk43499_primitive_spec);
    *copy_status = GLK43499_COPY_TRANSLATION_FAILED;
    *invalid_kind = 0;
    if (source == NULL) return 0;
    jbyteArray frame = glk43499_get_byte_array(env, source, "frameBytes");
    if (frame == NULL) return 0;
    *copy_status = glk43499_copy_bytes(env, frame, frame_copy, frame_size);
    if (*copy_status != GLK43499_COPY_OK) {
        if (frame != NULL) (*env)->DeleteLocalRef(env, frame);
        return 0;
    }
    (*env)->DeleteLocalRef(env, frame);

    /* L1：primitive kind 由 Kotlin DTO 显式字段给出，JNI 不再 IsInstanceOf 判类。
     * 字段读取失败=翻译失败（NOT_ACCEPTED 无回执）；未知/越界=参数域失败。 */
    uint32_t primitive_kind = 0;
    if (!glk43499_get_int(env, source, "primitiveKind", &primitive_kind)) {
        return 0;
    }
    if (primitive_kind < (uint32_t) GLK43499_PRIMITIVE_MULTICAST_SENDMSG ||
        primitive_kind > (uint32_t) GLK43499_PRIMITIVE_TCP_ZEROCOPY_RECEIVE) {
        *invalid_kind = 1;
        return 0;
    }
    target->kind = (int32_t) primitive_kind;

    switch (target->kind) {
        case GLK43499_PRIMITIVE_MULTICAST_SENDMSG: {
            struct glk43499_multicast_spec *spec = &target->value.multicast_sendmsg;
            spec->frame_bytes = *frame_copy;
            spec->frame_size = *frame_size;
            uint32_t physical_id = 0;
            uint64_t futex_flag = 0;
            if (!glk43499_read_primitive_addresses(env, source,
                                                   &spec->addresses) ||
                !glk43499_get_long(env, source, "socketBufferSize",
                                   &spec->socket_buffer_size) ||
                !glk43499_get_long(env, source, "operationBudget",
                                   &spec->operation_budget) ||
                !glk43499_get_long(env, source, "timeoutNanos",
                                   &spec->timeout_ns) ||
                !glk43499_get_int(env, source, "addressFamily",
                                  &spec->address_family) ||
                !glk43499_get_long(env, source, "armHoldNs",
                                   &spec->arm_hold_ns) ||
                !glk43499_get_long(env, source, "armSequence",
                                   &spec->arm_sequence) ||
                !glk43499_get_long(env, source, "maxFrame",
                                   &spec->max_frame) ||
                !glk43499_get_int(env, source, "ghostPid", &physical_id) ||
                !glk43499_get_long(env, source, "futexWaiterFlag",
                                   &futex_flag) ||
                !glk43499_get_int(env, source, "disarmRequired",
                                  &spec->disarm_required)) {
                return 0;
            }
            /* 位掩码以 Long 承载（避免 Kotlin Int 负数）；显式收窄后由 Native 校验域。 */
            if ((uint64_t) (uint32_t) futex_flag != futex_flag) return 0;
            spec->ghost_pid = (int32_t) physical_id;
            spec->futex_waiter_flag = (uint32_t) futex_flag;
            return 1;
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
                   glk43499_get_long(env, source, "timeoutNanos", &spec->timeout_ns) &&
                   glk43499_get_long(env, source, "perAttemptDelayNs",
                                     &spec->per_attempt_delay_ns) &&
                   glk43499_get_long(env, source, "maxNfds",
                                     &spec->max_nfds) &&
                   /* R3a-2/P1/P7/P10：consumer 起跑延迟、select 分支、
                    * fd 备份起点步长（全部来自请求）。 */
                   glk43499_get_long(env, source, "enterDelayNs",
                                     &spec->enter_delay_ns) &&
                   glk43499_get_long(env, source, "fdReserveStride",
                                     &spec->fd_reserve_stride) &&
                   glk43499_get_long(env, source, "selectUseTimeval",
                                     &spec->select_use_timeval);
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
                   glk43499_get_long(env, source, "timeoutNanos", &spec->timeout_ns) &&
                   glk43499_get_long(env, source, "punchSpin", &spec->punch_spin) &&
                   glk43499_get_long(env, source, "postHoldNs", &spec->post_hold_ns) &&
                   glk43499_get_long(env, source, "zcAddressOffset",
                                     &spec->zc_address_offset) &&
                   glk43499_get_long(env, source, "maxMapping",
                                     &spec->max_mapping) &&
                   glk43499_get_long(env, source, "maxFrame",
                                     &spec->max_frame) &&
                   /* R3a-2/T2–T4：四字段偏移、arm 序列与 punch 相位独立上限。 */
                   glk43499_get_long(env, source, "armSequence",
                                     &spec->arm_sequence) &&
                   glk43499_get_long(env, source, "zcLengthOffset",
                                     &spec->zc_length_offset) &&
                   glk43499_get_long(env, source, "zcTaskOffset",
                                     &spec->zc_task_offset) &&
                   glk43499_get_long(env, source, "zcLockOffset",
                                     &spec->zc_lock_offset) &&
                   glk43499_get_long(env, source, "punchPhaseSpin",
                                     &spec->punch_phase_spin) &&
                   /* R3b：发送小包/映射页填充字节各自独立（legacy 0x33/0x55）。 */
                   glk43499_get_long(env, source, "sendFillByte",
                                     &spec->send_fill_byte) &&
                   glk43499_get_long(env, source, "mappingFillByte",
                                     &spec->mapping_fill_byte);
        }
        default:
            return 0;
    }
}

/** @brief 写回 preparePlacement 的候选页事实。 */
static int32_t glk43499_write_placement_result(
    JNIEnv *env, jobject target, const struct glk43499_placement_result *source) {
    GLK43499_WRITE_OR_FAIL(
        glk43499_write_call_result_body(env, target, &source->call));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "candidatePageBase", source->facts.candidate_page_base));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "pageSize", source->facts.page_size));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "allocationSize", source->facts.allocation_size));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "requiredAlignment", source->facts.required_alignment));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "measuredObjectAddress",
        source->facts.measured_object_address));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "measurementRounds", source->facts.measurement_rounds));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "retainedChildCount", source->facts.retained_child_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "retainedDescriptorCount",
        source->facts.retained_descriptor_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagStep", (int32_t) source->diag_step));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagSyscall", (int32_t) source->diag_syscall));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagErrno", (int32_t) source->diag_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagChildReaped", (int32_t) source->diag_child_reaped));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagChildStatus", source->diag_child_status));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagChildProcExists", (int32_t) source->diag_child_proc_exists));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagChildUid", source->diag_child_uid));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagOpenErrno", source->diag_open_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagChildPid", source->diag_child_pid));
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 Landing 提交点和计数。 */
static int32_t glk43499_write_landing_result(
    JNIEnv *env, jobject target, const struct glk43499_landing_result *source) {
    GLK43499_WRITE_OR_FAIL(
        glk43499_write_call_result_body(env, target, &source->call));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "actualPageBase", source->receipt.actual_page_base));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "contentGeneration", source->receipt.content_generation));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "closedPrepareCount",
        source->receipt.closed_prepare_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "closedSprayCount", source->receipt.closed_spray_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "closedPreCount", source->receipt.closed_pre_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "closedPostCount", source->receipt.closed_post_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "reclaimSendCount", source->receipt.reclaim_send_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "commitReached", source->receipt.commit_reached));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "placementConsumed", source->receipt.placement_consumed));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "reclaimRetained", source->receipt.reclaim_retained));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "pageAcceptValue", source->receipt.page_accept_value));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "pageAcceptChecked", source->receipt.page_accept_checked));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "pageAcceptOk", source->receipt.page_accept_ok));
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 prepareActor 的准备事实。 */
static int32_t glk43499_write_actor_prepare_result(
    JNIEnv *env, jobject target, const struct glk43499_actor_prepare_result *source) {
    GLK43499_WRITE_OR_FAIL(
        glk43499_write_call_result_body(env, target, &source->call));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "pageContentGeneration",
        source->receipt.page_content_generation));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "primitiveKind", source->receipt.primitive_kind));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "threadCount", (int32_t) source->receipt.thread_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "pageConsumed", source->receipt.page_consumed));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "ownerReady", source->receipt.owner_ready));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "waiterReady", source->receipt.waiter_ready));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "consumerReady", source->receipt.consumer_ready));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "carrierPrepared", source->receipt.carrier_prepared));
    return glk43499_write_call_status(env, target, source->call.call_status);
}

/** @brief 写回 executeActor 的物理事实。 */
static int32_t glk43499_write_actor_execute_result(
    JNIEnv *env, jobject target, const struct glk43499_actor_execute_result *source) {
    GLK43499_WRITE_OR_FAIL(
        glk43499_write_call_result_body(env, target, &source->call));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "pageContentGeneration",
        source->receipt.page_content_generation));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "consumerCallCount", source->receipt.consumer_call_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "consumerSuccessCount",
        source->receipt.consumer_success_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "consumerInflightAtReturn",
        source->receipt.consumer_inflight_at_return));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "ownerReady", source->receipt.owner_ready));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "waiterParked", source->receipt.waiter_parked));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "carrierArmed", source->receipt.carrier_armed));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "windowOpened", source->receipt.window_opened));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "carrierDisarmed", source->receipt.carrier_disarmed));
    GLK43499_WRITE_OR_FAIL(glk43499_set_bool(
        env, target, "retainedForProcessExit",
        source->receipt.retained_for_process_exit));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "carrierOperations", source->receipt.carrier_operations));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "carrierError", source->receipt.carrier_error));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "carrierLastStep", source->receipt.carrier_last_step));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "carrierLastErrno", source->receipt.carrier_last_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "carrierPunchFailed",
        source->receipt.carrier_punch_failed));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "ghostDisarmResult",
        source->receipt.ghost_disarm_result));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "ghostDisarmErrno", source->receipt.ghost_disarm_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagStage", (int32_t) source->diag_stage));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagRequeueRet", source->diag_requeue_ret));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagRequeueErrno", source->diag_requeue_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagFailStage", (int32_t) source->diag_fail_stage));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagPublishCount", (int32_t) source->diag_publish_count));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagRequeueCalls", (int32_t) source->diag_requeue_calls));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagVersion", (int32_t) source->diag_version));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagWaiterWaiting", (int32_t) source->diag_waiter_waiting));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagOwnerStarted", (int32_t) source->diag_owner_started));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagRequeueExpected", (int32_t) source->diag_requeue_expected));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagWaiterWaitRet", source->diag_waiter_wait_ret));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagWaiterWaitErrno", source->diag_waiter_wait_errno));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagWaiterInFutex", (int32_t) source->diag_waiter_in_futex));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagWaiterDispatched", (int32_t) source->diag_waiter_dispatched));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagTargetValueAtRequeue",
        (int32_t) source->diag_target_value_at_requeue));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsSettleDoneNs",
        (int64_t) source->diag_ts_settle_done_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsWaiterEnterNs",
        (int64_t) source->diag_ts_waiter_enter_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsWaiterExitNs",
        (int64_t) source->diag_ts_waiter_exit_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsRequeueNs", (int64_t) source->diag_ts_requeue_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagParkedTimeoutNs", (int64_t) source->diag_parked_timeout_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagSetupSettleUs", (int64_t) source->diag_setup_settle_us));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagPollUs", (int64_t) source->diag_poll_us));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagPrepareTimeoutNs", (int64_t) source->diag_prepare_timeout_ns));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsSec", source->diag_ts_sec));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(
        env, target, "diagTsNsec", source->diag_ts_nsec));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagFutexOp", (int32_t) source->diag_futex_op));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagFutexVal", (int32_t) source->diag_futex_val));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagFutexUaddr2", (int32_t) source->diag_futex_uaddr2));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagFutexVal3", (int32_t) source->diag_futex_val3));
    GLK43499_WRITE_OR_FAIL(glk43499_set_int(
        env, target, "diagOwnerTid", source->diag_owner_tid));
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
                                             jboolean close_operation) {
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    /* L6：close/cancel 由 jboolean 唯一区分；bool 回执由唯一 writer 归一。 */
    const int32_t status =
        close_operation == JNI_TRUE
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
    /* L5：handle==0 前置拒绝，不先触碰 result 对象/类查找。 */
    if (handle == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    return glk43499_jni_placement_close_call(env, handle, result_object, JNI_FALSE);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closePlacement(
    JNIEnv *env, jclass clazz, jlong handle, jobject result_object) {
    (void) clazz;
    /* L5：handle==0 前置拒绝，不先触碰 result 对象/类查找。 */
    if (handle == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    return glk43499_jni_placement_close_call(env, handle, result_object, JNI_TRUE);
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
        glk43499_clear_free(bytes_copy, bytes_size);
        if (copy_status == GLK43499_COPY_NO_MEMORY) {
            if (!glk43499_write_jni_failure_result(
                    env, result_object, GLK43499_CALL_SYSTEM_ERROR,
                    GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_INVALID,
                    ENOMEM)) {
                glk43499_jni_clear_pending(env);
                return GLK43499_JNI_INCOMPLETE_HANDLE;
            }
            return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
        }
        if (copy_status == GLK43499_COPY_INVALID) {
            if (!glk43499_write_jni_failure_result(
                    env, result_object, GLK43499_CALL_INVALID_ARGUMENT,
                    GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_INVALID,
                    0)) {
                glk43499_jni_clear_pending(env);
                return GLK43499_JNI_INCOMPLETE_HANDLE;
            }
            return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
        }
        /* L2：数组读取/类型翻译失败 ⇒ NOT_ACCEPTED，不写回执。 */
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

    int32_t invalid_kind = 0;
    const int32_t translated =
        glk43499_read_actor_setup(env, setup_object, &setup) &&
        glk43499_read_primitive(env, primitive_object, &primitive,
                                &frame_copy, &frame_size, &copy_status,
                                &invalid_kind);
    if (!translated) {
        glk43499_clear_free(frame_copy, frame_size);
        if (copy_status == GLK43499_COPY_NO_MEMORY) {
            if (!glk43499_write_jni_failure_result(
                    env, result_object, GLK43499_CALL_SYSTEM_ERROR,
                    GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_INVALID,
                    ENOMEM)) {
                glk43499_jni_clear_pending(env);
                return GLK43499_JNI_INCOMPLETE_HANDLE;
            }
            return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
        }
        if (invalid_kind != 0) {
            if (!glk43499_write_jni_failure_result(
                    env, result_object, GLK43499_CALL_INVALID_ARGUMENT,
                    GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_INVALID,
                    0)) {
                glk43499_jni_clear_pending(env);
                return GLK43499_JNI_INCOMPLETE_HANDLE;
            }
            return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
        }
        /* L2：GetFieldID/类型/数组读取失败 ⇒ NOT_ACCEPTED，不写回执。 */
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    /* L8：priority 以显式 int32 读取；nice 域在 JNI 入口先校验（core 同域兜底），
     * 越界写 INVALID_ARGUMENT，不进入 core。 */
    if (setup.owner_priority < PRIO_MIN ||
        setup.owner_priority >= PRIO_MAX ||
        setup.waiter_priority < PRIO_MIN ||
        setup.waiter_priority >= PRIO_MAX ||
        setup.consumer_priority < PRIO_MIN ||
        setup.consumer_priority >= PRIO_MAX) {
        glk43499_clear_free(frame_copy, frame_size);
        if (!glk43499_write_jni_failure_result(
                env, result_object, GLK43499_CALL_INVALID_ARGUMENT,
                GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_INVALID, 0)) {
            glk43499_jni_clear_pending(env);
            return GLK43499_JNI_INCOMPLETE_HANDLE;
        }
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
                                         jboolean close_operation) {
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    /* L6：close/cancel 由 jboolean 唯一区分；bool 回执由唯一 writer 归一。 */
    const int32_t status =
        close_operation == JNI_TRUE
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
    /* L5：handle==0 前置拒绝，不先触碰 result 对象/类查找。 */
    if (actor == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    return glk43499_jni_actor_close_call(env, actor, result_object, JNI_FALSE);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closeActor(
    JNIEnv *env, jclass clazz, jlong actor, jobject result_object) {
    (void) clazz;
    /* L5：handle==0 前置拒绝，不先触碰 result 对象/类查找。 */
    if (actor == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    return glk43499_jni_actor_close_call(env, actor, result_object, JNI_TRUE);
}

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_openPerfSampler(
    JNIEnv *env, jclass clazz, jobject request_object, jobject result_object) {
    (void) clazz;
    if (request_object == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_PERF_OPEN_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    struct glk43499_perf_open_request request;
    struct glk43499_perf_open_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    if (!glk43499_read_perf_open_request(env, request_object, &request)) {
        /* L2：翻译失败 ⇒ NOT_ACCEPTED，不写 core 状态/回执。 */
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    const glk43499_handle handle = glk43499_perf_open(&request, &result);
    if (!glk43499_write_call_result(env, result_object, &result.call)) {
        /* B7：回执写失败返回 -1（fail-stop）前先关闭已建 handle，防 fd/mmap 泄漏；
         * 吞掉 close 回执（调用方已 fail-stop，不再使用该 token）。 */
        if (handle != GLK43499_INVALID_HANDLE) {
            struct glk43499_close_result discarded;
            glk43499_init_call_result(&discarded.call, (uint32_t) sizeof(discarded));
            (void) glk43499_perf_close(handle, &discarded);
        }
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_HANDLE;
    }
    return (jlong) handle;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_collectPerfSamples(
    JNIEnv *env, jclass clazz, jlong handle, jbyteArray out,
    jobject result_object) {
    (void) clazz;
    if (handle == 0 || out == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_PERF_COLLECT_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const jsize capacity = (*env)->GetArrayLength(env, out);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        /* L2：数组长度读取失败=翻译失败 ⇒ NOT_ACCEPTED，不写回执。 */
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    if (capacity < 0) {
        /* 不可达（jsize 非负）；保留参数域 fail-closed。 */
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    /* L3：空数组合法（capacity=0）；malloc 失败 ⇒ SYSTEM_ERROR/ENOMEM。
     * capacity=0 时仍需把非 NULL 缓冲交给 core 以得到具名容量失败。 */
    const size_t alloc_size = capacity > 0 ? (size_t) capacity : 1U;
    uint8_t *buffer = (uint8_t *) malloc(alloc_size);
    if (buffer == NULL) {
        if (!glk43499_write_jni_failure_result(
                env, result_object, GLK43499_CALL_SYSTEM_ERROR,
                GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_OPEN, ENOMEM)) {
            glk43499_jni_clear_pending(env);
            return GLK43499_JNI_INCOMPLETE_INT;
        }
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_perf_collect_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status = glk43499_perf_collect(
        (glk43499_handle) handle, buffer, (uint64_t) capacity, &result);
    if (status == GLK43499_CALL_OK) {
        const uint64_t copied_bytes = result.receipt.bytes_copied;
        /* L4：先取 core bytes_copied，校验 0≤bytes_copied≤capacity 后只拷
         * bytes_copied（不得按 data_size 全量拷贝/越界）。 */
        if (copied_bytes > (uint64_t) capacity) {
            glk43499_clear_free(buffer, alloc_size);
            if (!glk43499_write_jni_failure_result(
                    env, result_object, GLK43499_CALL_INVALID_ARGUMENT,
                    GLK43499_EFFECT_NOT_APPLIED, GLK43499_RESOURCE_OPEN, 0)) {
                glk43499_jni_clear_pending(env);
                return GLK43499_JNI_INCOMPLETE_INT;
            }
            return GLK43499_CALL_INVALID_ARGUMENT;
        }
        if (!glk43499_write_byte_array(env, out, (uint64_t) capacity, buffer,
                                       copied_bytes)) {
            glk43499_clear_free(buffer, alloc_size);
            glk43499_jni_clear_pending(env);
            return GLK43499_JNI_INCOMPLETE_INT;
        }
    }
    glk43499_clear_free(buffer, alloc_size);
    if (!glk43499_write_perf_collect_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_closePerfSampler(
    JNIEnv *env, jclass clazz, jlong handle, jobject result_object) {
    (void) clazz;
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_CLOSE_RESULT)) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    const int32_t status =
        glk43499_perf_close((glk43499_handle) handle, &result);
    if (!glk43499_write_close_result(env, result_object, &result)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

/**
 * @brief 写回 `raiseProcessLimits` 回执（G06；callStatus 最后写）。
 *
 * @param env 当前 JNI 环境。
 * @param target Kotlin `Native43499LimitsResult`。
 * @param call_status GLK43499_CALL_* 总状态。
 * @param system_error 首次失败 errno（0=无）。
 * @param nofile_status RLIMIT_NOFILE 子状态（0=成功，否则 errno）。
 * @param nproc_status RLIMIT_NPROC 子状态（0=成功，否则 errno）。
 * @param nofile_soft_before NOFILE soft 前值。
 * @param nofile_hard NOFILE hard（保持不变）。
 * @param nofile_soft_after NOFILE soft 后值。
 * @param nproc_soft_before NPROC soft 前值。
 * @param nproc_hard NPROC hard（保持不变）。
 * @param nproc_soft_after NPROC soft 后值。
 * @return 1=完整写回；0=incomplete（调用方清异常并返回哨兵）。
 */
static int32_t glk43499_write_limits_result(
    JNIEnv *env, jobject target, int32_t call_status, int32_t system_error,
    int32_t nofile_status, int32_t nproc_status, uint64_t nofile_soft_before,
    uint64_t nofile_hard, uint64_t nofile_soft_after,
    uint64_t nproc_soft_before, uint64_t nproc_hard,
    uint64_t nproc_soft_after) {
    if (target == NULL) return 0;
    GLK43499_WRITE_OR_FAIL(
        glk43499_set_int(env, target, "systemError", system_error));
    GLK43499_WRITE_OR_FAIL(
        glk43499_set_int(env, target, "nofileStatus", nofile_status));
    GLK43499_WRITE_OR_FAIL(
        glk43499_set_int(env, target, "nprocStatus", nproc_status));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(env, target, "nofileSoftBefore",
                                             nofile_soft_before));
    GLK43499_WRITE_OR_FAIL(
        glk43499_set_long(env, target, "nofileHard", nofile_hard));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(env, target, "nofileSoftAfter",
                                             nofile_soft_after));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(env, target, "nprocSoftBefore",
                                             nproc_soft_before));
    GLK43499_WRITE_OR_FAIL(
        glk43499_set_long(env, target, "nprocHard", nproc_hard));
    GLK43499_WRITE_OR_FAIL(glk43499_set_long(env, target, "nprocSoftAfter",
                                             nproc_soft_after));
    return glk43499_write_call_status(env, target, call_status);
}

/**
 * @brief 读取并抬升 RLIMIT_NOFILE / RLIMIT_NPROC 的 soft 到 hard（G06；机制单步）。
 *
 * @param env 当前 JNI 环境。
 * @param clazz JNI 类（未用）。
 * @param result_object Kotlin `Native43499LimitsResult`。
 * @return GLK43499_CALL_* 总状态；前置拒绝 -2、回执不完整 INT32_MIN。
 *
 * @note 纯机械：getrlimit→setrlimit(soft=hard)→getrlimit 回读；不 fork、不读 profile、
 *   不判断攻击可继续性（调用方按返回值 fail-closed）。
 * @note 失败时仍写回已取得的 before/after 与 per-limit errno（可证伪）。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_raiseProcessLimits(
    JNIEnv *env, jclass clazz, jobject result_object) {
    (void) clazz;
    if (!glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_LIMITS_RESULT)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    int32_t system_error = 0;
    int32_t nofile_status = 0;
    int32_t nproc_status = 0;
    uint64_t nofile_soft_before = 0;
    uint64_t nofile_hard = 0;
    uint64_t nofile_soft_after = 0;
    uint64_t nproc_soft_before = 0;
    uint64_t nproc_hard = 0;
    uint64_t nproc_soft_after = 0;

    struct rlimit nofile;
    if (getrlimit(RLIMIT_NOFILE, &nofile) != 0) {
        nofile_status = errno;
        system_error = errno;
    } else {
        nofile_soft_before = (uint64_t) nofile.rlim_cur;
        nofile_hard = (uint64_t) nofile.rlim_max;
        struct rlimit raised = nofile;
        raised.rlim_cur = raised.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &raised) != 0) {
            nofile_status = errno;
            if (system_error == 0) system_error = errno;
        } else if (getrlimit(RLIMIT_NOFILE, &nofile) != 0) {
            nofile_status = errno;
            if (system_error == 0) system_error = errno;
        } else {
            nofile_soft_after = (uint64_t) nofile.rlim_cur;
            nofile_hard = (uint64_t) nofile.rlim_max;
        }
    }

    struct rlimit nproc;
    if (getrlimit(RLIMIT_NPROC, &nproc) != 0) {
        nproc_status = errno;
        if (system_error == 0) system_error = errno;
    } else {
        nproc_soft_before = (uint64_t) nproc.rlim_cur;
        nproc_hard = (uint64_t) nproc.rlim_max;
        struct rlimit raised = nproc;
        raised.rlim_cur = raised.rlim_max;
        if (setrlimit(RLIMIT_NPROC, &raised) != 0) {
            nproc_status = errno;
            if (system_error == 0) system_error = errno;
        } else if (getrlimit(RLIMIT_NPROC, &nproc) != 0) {
            nproc_status = errno;
            if (system_error == 0) system_error = errno;
        } else {
            nproc_soft_after = (uint64_t) nproc.rlim_cur;
            nproc_hard = (uint64_t) nproc.rlim_max;
        }
    }

    const int32_t call_status =
        (nofile_status == 0 && nproc_status == 0) ? GLK43499_CALL_OK
                                                  : GLK43499_CALL_SYSTEM_ERROR;
    if (!glk43499_write_limits_result(
            env, result_object, call_status, system_error, nofile_status,
            nproc_status, nofile_soft_before, nofile_hard, nofile_soft_after,
            nproc_soft_before, nproc_hard, nproc_soft_after)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return call_status;
}

/* ------------------------------------------------------------------------- */
/* 批 B1：单步 victim transport JNI（core/victim_protocol 的 1:1 翻译）        */
/* ------------------------------------------------------------------------- */
/*
 * 返回值协议：handle 入口复用 -2(NOT_ACCEPTED)/-1(INCOMPLETE)；int 入口复用
 * -2/INT32_MIN；其余负值为 core 的具名机械错误（GLK_VICTIM_ERR_*），由 Kotlin
 * 映射成 VictimError。本层只做参数域校验/内存编组/一次 core 调用，不解析帧内容、
 * 不做业务判断、无状态机；token 生命周期由 Kotlin VictimSession owner 保证。
 */

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_spawnVictim(
    JNIEnv *env, jclass clazz, jstring helper_path, jobjectArray argv,
    jobjectArray envp, jlong raw_size, jobject result_object) {
    (void) clazz;
    info("jni.spawnVictim enter");
    if (helper_path == NULL || argv == NULL || result_object == NULL ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_VICTIM_SPAWN_RESULT)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    if (raw_size < (jlong) GLK_VICTIM_RAW_HEADER_BYTES) {
        glk43499_jni_reject_invalid_argument(env, result_object);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    char *path = NULL;
    char **argv_values = NULL;
    char **env_values = NULL;
    uint32_t argv_count = 0;
    uint32_t env_count = 0;
    const int32_t parsed =
        glk43499_jni_read_utf8(env, helper_path, GLK43499_JNI_VICTIM_PATH_MAX,
                               &path) &&
        glk43499_jni_read_utf8_array(env, argv, GLK_VICTIM_MAX_ARG_COUNT,
                                     GLK_VICTIM_MAX_ARG_BYTES, &argv_values,
                                     &argv_count) &&
        argv_count > 0 &&
        glk43499_jni_read_utf8_array(env, envp, GLK_VICTIM_MAX_ARG_COUNT,
                                     GLK_VICTIM_MAX_ARG_BYTES, &env_values,
                                     &env_count);
    if (!parsed) {
        free(path);
        glk43499_jni_free_utf8_array(argv_values, argv_count);
        glk43499_jni_free_utf8_array(env_values, env_count);
        /* L2：字符串/数组读取失败 ⇒ NOT_ACCEPTED，不写回执。 */
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    }
    struct glk_victim_spawn_request request;
    memset(&request, 0, sizeof(request));
    request.helper_path = path;
    request.argv = (const char *const *) argv_values;
    request.envp = env_count > 0 ? (const char *const *) env_values : NULL;
    request.raw_size = (uint64_t) raw_size;
    struct glk_victim_spawn_result spawn_result;
    memset(&spawn_result, 0, sizeof(spawn_result));
    const glk_victim_token token = glk_victim_spawn(&request, &spawn_result);
    free(path);
    glk43499_jni_free_utf8_array(argv_values, argv_count);
    glk43499_jni_free_utf8_array(env_values, env_count);

    struct glk43499_call_result call;
    glk43499_init_call_result(&call, (uint32_t) sizeof(call));
    if (token != 0) {
        call.call_status = GLK43499_CALL_OK;
        call.effect = GLK43499_EFFECT_NOT_APPLIED;
        call.resource_state = GLK43499_RESOURCE_OPEN;
    } else {
        call.call_status = GLK43499_CALL_SYSTEM_ERROR;
        call.effect = GLK43499_EFFECT_NOT_APPLIED;
        call.resource_state = GLK43499_RESOURCE_INVALID;
        call.system_error = spawn_result.system_error != 0
                                ? spawn_result.system_error
                                : (int32_t) EINVAL;
    }
    int32_t written = glk43499_write_call_result_body(env, result_object, &call);
    if (written != 0) {
        written = glk43499_set_int(env, result_object, "pid", spawn_result.pid);
    }
    if (written != 0) {
        written = glk43499_write_call_status(env, result_object,
                                             call.call_status);
    }
    if (written == 0) {
        if (token != 0) (void) glk_victim_close(token);
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_HANDLE;
    }
    info("jni.spawnVictim result=%s", token != 0 ? "ok" : "fail");
    return (jlong) token;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_sendVictim(
    JNIEnv *env, jclass clazz, jlong handle, jbyteArray frame,
    jint timeout_ms) {
    (void) clazz;
    if (handle == 0 || frame == NULL || timeout_ms <= 0) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const jsize frame_length = (*env)->GetArrayLength(env, frame);
    if (frame_length < (jsize) (GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES +
                                GLK_VICTIM_BODY_OVERHEAD_BYTES) ||
        frame_length > (jsize) GLK_VICTIM_FRAME_MAX_WIRE) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    uint8_t *buffer = (uint8_t *) malloc((size_t) frame_length);
    if (buffer == NULL) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    (*env)->GetByteArrayRegion(env, frame, 0, frame_length, (jbyte *) buffer);
    if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
        glk43499_jni_clear_pending(env);
        free(buffer);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const int32_t status = glk_victim_send(
        (glk_victim_token) handle, buffer, (uint32_t) frame_length, timeout_ms);
    free(buffer);
    return status == GLK_VICTIM_OK ? (jint) frame_length : (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_receiveVictim(
    JNIEnv *env, jclass clazz, jlong handle, jbyteArray out,
    jint timeout_ms) {
    (void) clazz;
    if (handle == 0 || out == NULL || timeout_ms <= 0) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const jsize capacity = (*env)->GetArrayLength(env, out);
    if (capacity < (jsize) GLK_VICTIM_FRAME_MAX_WIRE) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    uint8_t *buffer = (uint8_t *) malloc((size_t) capacity);
    if (buffer == NULL) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    uint32_t out_length = 0;
    const int32_t status = glk_victim_receive(
        (glk_victim_token) handle, buffer, (uint32_t) capacity, &out_length,
        timeout_ms);
    if (status == GLK_VICTIM_OK) {
        if (out_length > (uint32_t) capacity ||
            out_length < GLK_VICTIM_FRAME_LENGTH_PREFIX_BYTES +
                             GLK_VICTIM_BODY_OVERHEAD_BYTES) {
            free(buffer);
            glk43499_jni_clear_pending(env);
            return GLK43499_JNI_INCOMPLETE_INT;
        }
        (*env)->SetByteArrayRegion(env, out, 0, (jsize) out_length,
                                   (const jbyte *) buffer);
        if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
            glk43499_jni_clear_pending(env);
            free(buffer);
            return GLK43499_JNI_INCOMPLETE_INT;
        }
    }
    free(buffer);
    return status == GLK_VICTIM_OK ? (jint) out_length : (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_readRawVictim(
    JNIEnv *env, jclass clazz, jlong handle, jlong offset, jbyteArray out,
    jint length) {
    (void) clazz;
    if (handle == 0 || out == NULL || offset < 0 || length <= 0) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    if (length > (*env)->GetArrayLength(env, out)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    uint8_t *buffer = (uint8_t *) malloc((size_t) length);
    if (buffer == NULL) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const int32_t status = glk_victim_read_raw(
        (glk_victim_token) handle, (uint64_t) offset, buffer, (uint32_t) length);
    if (status == GLK_VICTIM_OK) {
        (*env)->SetByteArrayRegion(env, out, 0, (jsize) length,
                                   (const jbyte *) buffer);
        if ((*env)->ExceptionCheck(env) == JNI_TRUE) {
            glk43499_jni_clear_pending(env);
            free(buffer);
            return GLK43499_JNI_INCOMPLETE_INT;
        }
    }
    free(buffer);
    return status == GLK_VICTIM_OK ? (jint) length : (jint) status;
}

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_rawSizeVictim(
    JNIEnv *env, jclass clazz, jlong handle) {
    (void) clazz;
    (void) env;
    if (handle == 0) return (jlong) GLK43499_JNI_NOT_ACCEPTED_HANDLE;
    uint64_t size = 0;
    const int32_t status =
        glk_victim_raw_size((glk_victim_token) handle, &size);
    if (status != GLK_VICTIM_OK) return (jlong) status;
    return (jlong) size;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_waitExitVictim(
    JNIEnv *env, jclass clazz, jlong handle, jint timeout_ms) {
    (void) clazz;
    if (handle == 0 || timeout_ms <= 0) {
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    (void) env;
    int32_t exit_code = -1;
    const int32_t status =
        glk_victim_wait((glk_victim_token) handle, timeout_ms, &exit_code);
    return status == GLK_VICTIM_OK ? (jint) exit_code : (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_killVictim(
    JNIEnv *env, jclass clazz, jlong handle) {
    (void) clazz;
    (void) env;
    if (handle == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    int32_t exit_code = -1;
    const int32_t status =
        glk_victim_kill((glk_victim_token) handle, &exit_code);
    return status == GLK_VICTIM_OK ? (jint) exit_code : (jint) status;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_detachVictim(
    JNIEnv *env, jclass clazz, jlong handle) {
    (void) clazz;
    (void) env;
    if (handle == 0) return GLK43499_JNI_NOT_ACCEPTED_INT;
    return (jint) glk_victim_detach((glk_victim_token) handle);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_closeVictim(
    JNIEnv *env, jclass clazz, jlong handle, jobject result_object) {
    (void) clazz;
    if (handle == 0 ||
        !glk43499_jni_result_ready(env, result_object,
                                   GLK43499_JNI_CLASS_VICTIM_CLOSE_RESULT)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_NOT_ACCEPTED_INT;
    }
    const int32_t status = glk_victim_close((glk_victim_token) handle);
    struct glk43499_close_result result;
    glk43499_init_call_result(&result.call, (uint32_t) sizeof(result));
    if (status == GLK_VICTIM_OK) {
        result.call.call_status = GLK43499_CALL_OK;
        result.call.effect = GLK43499_EFFECT_CLEAN_SUCCESS;
        result.call.resource_state = GLK43499_RESOURCE_CLOSED;
        result.stop_requested = 1;
        result.workers_joined = 1;
        result.owned_chain_released = 1;
    } else {
        result.call.call_status = GLK43499_CALL_INVALID_ARGUMENT;
        result.call.effect = GLK43499_EFFECT_NOT_APPLIED;
        result.call.resource_state = GLK43499_RESOURCE_INVALID;
    }
    if (!glk43499_write_call_result(env, result_object, &result.call)) {
        glk43499_jni_clear_pending(env);
        return GLK43499_JNI_INCOMPLETE_INT;
    }
    return (jint) status;
}

/* ------------------------------------------------------------------------- */
/* E4 零 fork ①：debug/measurement JNI 查询与 canary（生产不调用）             */
/* ------------------------------------------------------------------------- */

JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardSnapshot(
    JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    const uint64_t fork_count = (uint64_t) glk_victim_fork_count();
    const uint64_t vfork_count = (uint64_t) glk_victim_vfork_count();
    return (jlong) ((fork_count << 32) | vfork_count);
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardReset(
    JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    glk_victim_fork_count_reset();
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_VictimTransportJni_forkGuardCanary(
    JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    /* 只用于 E4 “能失败”验证：故意 fork 一次，子进程立即 _exit；
     * 父进程 waitpid 回收。生产/JNI 路径不调用本入口。
     * task-60 严格 ②：仅 Debug（GLK_VICTIM_FORK_WRAP）编入 fork；Release 无
     * fork/vfork 导入，canary 返回 NOT_ACCEPTED（不可用）。 */
#if defined(GLK_VICTIM_FORK_WRAP)
    const pid_t child = fork();
    if (child == 0) _exit(0);
    if (child < 0) return (jint) -errno;
    int32_t status = 0;
    if (waitpid(child, &status, 0) != child) return (jint) -errno;
    return 0;
#else
    return GLK43499_JNI_NOT_ACCEPTED_INT;
#endif
}

/**
 * @brief task-111 debug-only：futex 机制自测（返回 10 个 int32 结果）。
 * @param variant 0=val3=0/1=val3=owner_tid/2=absolute CLOCK_REALTIME。
 * @return int[10]（variant/owner_ret/owner_errno/owner_tid/waiter_ret/waiter_errno/
 *   waiter_elapsed_us/requeue_ret/requeue_errno/waiter_woke）。
 */
JNIEXPORT jintArray JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_actorFutexSelfTest(
    JNIEnv *env, jobject thiz, jint variant) {
    (void) thiz;
    int32_t output[10] = {0};
    if (glk43499_actor_futex_selftest((int32_t) variant, output) != 0) {
        return NULL;
    }
    jintArray array = (*env)->NewIntArray(env, 10);
    if (array == NULL) return NULL;
    (*env)->SetIntArrayRegion(env, array, 0, 10, (const jint *) output);
    return array;
}

JNIEXPORT jlongArray JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_actorDebugSnapshot(
    JNIEnv *env, jobject thiz, jlong handle) {
    (void) thiz;
    int64_t output[10] = {0};
    if (glk43499_actor_debug_snapshot((glk43499_handle) handle, output) != 0) {
        return NULL;
    }
    jlongArray array = (*env)->NewLongArray(env, 10);
    if (array == NULL) return NULL;
    (*env)->SetLongArrayRegion(env, array, 0, 10, (const jlong *) output);
    return array;
}
