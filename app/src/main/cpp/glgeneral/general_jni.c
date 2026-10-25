/**
 * @file general_jni.c
 * @brief KernelSnitch 的 JNI 边界（general / glgeneral）。
 *
 * 本文件只承担三件事：JNI 参数校验、句柄（shared state 地址）转换、把
 * KernelSnitch 的分阶段结果回传给 Kotlin。算法、线程与扫描全部保留在
 * glgeneral/kernelsnitch/kernelsnitch.h（header-only，函数均为 static）。
 *
 * @note 调用是阻塞的：findCollisions 会扫描整张 futex hash 表，
 *       bruteforce 最多遍历 64 GiB 内核地址区间。Kotlin 必须在后台线程调用，
 *       且同一进程内不要并发调用（KernelSnitch 自身有大量进程级资源）。
 * @note 句柄是 kernelsnitch_shared_state 的地址，只能由同一个 DSO 解读；
 *       调用方必须保证 create/destroy 各一次，destroy 之后句柄立即失效。
 * @note 日志宏来自 kernelsnitch/utils.h，当前写 stdout（Android 应用进程里
 *       不可见）。接入 logcat 需要先解冻 kernelsnitch/ 并改 utils.h，不在本批。
 */
/* kernelsnitch/utils.h 依赖 GNU 扩展的 cpu_set_t / sched_setaffinity；
 * <jni.h> 会先拉进 bionic 的 features.h，所以必须在任何 include 之前定义。 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <jni.h>

#include "kernelsnitch/kernelsnitch.h"

/** @brief 单次 create 允许的爆破线程数上限，避免 JNI 参数撑爆线程表。 */
#define GLK_KS_MAX_THREADS 1024

/** @brief slab order 上限；page_size << order 必须仍在 size_t 内。 */
#define GLK_KS_MAX_SLAB_ORDER 20

/** @brief core 上限，与常见 SoC 的 CPU 编号上界对齐。 */
#define GLK_KS_MAX_CORE 1024

/**
 * @brief 校验 create/run 的结构性参数。
 *
 * 只拒绝会让 KernelSnitch 内部出现 0 长度、负位移或越界线程表的值；
 * 扫描范围、碰撞数量等业务取值由 Kotlin 决定。
 *
 * @param core 绑定的 CPU；-1 表示不绑定，其余必须是 [0, GLK_KS_MAX_CORE)。
 * @return 1 合法，0 非法。
 */
static int glk_ks_args_valid(jlong mm_struct_sz, jlong mm_slab_order,
                             jlong thread_cnt, jlong collision_cnt, jint core) {
  if (mm_struct_sz <= 0 || thread_cnt <= 0) return 0;
  if (collision_cnt < 2) return 0;
  if (mm_slab_order < 0 || mm_slab_order > GLK_KS_MAX_SLAB_ORDER) return 0;
  if (thread_cnt > GLK_KS_MAX_THREADS) return 0;
  if (core < -1 || core >= GLK_KS_MAX_CORE) return 0;
  return 1;
}

/**
 * @brief 把 JNI 句柄还原成 KernelSnitch 共享状态。
 *
 * @param handle create() 返回的非 0 句柄。
 * @return 状态地址；handle 为 0 时返回 NULL。
 */
static struct kernelsnitch_shared_state *glk_ks_state(jlong handle) {
  return (struct kernelsnitch_shared_state *)(intptr_t)handle;
}

/**
 * @brief 建状态并按需绑核。
 *
 * @param core 绑定的 CPU；-1 表示先按 0 建立状态再解除绑定。
 * @return 共享状态；参数非法时返回 NULL。
 */
static struct kernelsnitch_shared_state *glk_ks_create(
    jlong mm_struct_sz, jlong mm_slab_order, jlong thread_cnt,
    jlong collision_cnt, int verbose, jint core) {
  if (!glk_ks_args_valid(mm_struct_sz, mm_slab_order, thread_cnt, collision_cnt,
                         core)) {
    return NULL;
  }
  struct kernelsnitch_shared_state *ks =
      kernelsnitch_setup((size_t)mm_struct_sz, (size_t)mm_slab_order,
                         (size_t)thread_cnt, (size_t)collision_cnt,
                         verbose ? 1u : 0u, core < 0 ? 0 : (int)core);
  if (ks && core < 0) reset_cpu_pin();
  return ks;
}

/**
 * @brief 创建一次 KernelSnitch 运行状态。
 *
 * @param mm_struct_sz    sizeof(mm_struct)，用于爆破步长。
 * @param mm_slab_order   mm_struct slab order。
 * @param thread_cnt      爆破线程数。
 * @param collision_cnt   需要建立的哈希碰撞数（至少 2）。
 * @param verbose         是否输出扫描细节。
 * @param core            绑定的 CPU；-1 表示不绑定。
 * @return 非 0 句柄；参数非法或初始化失败返回 0。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_create(
    JNIEnv *env, jobject thiz, jlong mm_struct_sz, jlong mm_slab_order,
    jlong thread_cnt, jlong collision_cnt, jboolean verbose, jint core) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks =
      glk_ks_create(mm_struct_sz, mm_slab_order, thread_cnt, collision_cnt,
                    verbose ? 1 : 0, core);
  return (jlong)(intptr_t)ks;
}

/**
 * @brief 执行一次碰撞扫描（阻塞）。
 *
 * @param handle create() 返回的句柄。
 * @return 是否找齐所需碰撞；句柄为 0 或状态不对时返回 false。
 */
JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_findCollisions(JNIEnv *env,
                                                             jobject thiz,
                                                             jlong handle) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks = glk_ks_state(handle);
  if (!ks || ks->state != KERNELSNITCH_INIT) return JNI_FALSE;
  kernelsnitch_find_collisions(ks);
  return kernelsnitch_found_collisions(ks) ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief 在碰撞已建立后爆破 mm_struct 地址（阻塞）。
 *
 * @param handle create() 返回的句柄。
 * @return mm_struct 地址；未找到或前提不满足返回 -1。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_bruteforce(JNIEnv *env,
                                                         jobject thiz,
                                                         jlong handle) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks = glk_ks_state(handle);
  if (!ks || ks->state != KERNELSNITCH_COLLISIONS_FOUND) return (jlong)-1;
  kernelsnitch_bruteforce(ks);
  return (jlong)ks->mm_struct;
}

/**
 * @brief 查询当前阶段，供 Kotlin 记录失败原因。
 *
 * @param handle create() 返回的句柄。
 * @return kernelsnitch_state 的枚举值；句柄为 0 时返回
 *         KERNELSNITCH_NOT_INIT。
 */
JNIEXPORT jint JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_state(JNIEnv *env, jobject thiz,
                                                    jlong handle) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks = glk_ks_state(handle);
  return ks ? (jint)ks->state : (jint)KERNELSNITCH_NOT_INIT;
}

/**
 * @brief 回收一次 KernelSnitch 运行状态。
 *
 * @param handle create() 返回的句柄；0 允许。
 * @return 本次运行找到的 mm_struct；未找到返回 -1。
 * @note 必须恰好调用一次；之后句柄失效。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_destroy(JNIEnv *env, jobject thiz,
                                                      jlong handle) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks = glk_ks_state(handle);
  if (!ks) return (jlong)-1;
  return (jlong)kernelsnitch_cleanup(ks);
}

/**
 * @brief 一次完成 setup → 碰撞 → 爆破 → cleanup 的便捷入口。
 *
 * @return mm_struct 地址；参数非法、碰撞未建立或未找到返回 -1。
 * @note 与 create/findCollisions/bruteforce/destroy 等价，供首轮接线使用。
 *       碰撞未找齐时不再进入爆破，直接回收并返回 -1。
 */
JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_NativeKernelSnitch_run(
    JNIEnv *env, jobject thiz, jlong mm_struct_sz, jlong mm_slab_order,
    jlong thread_cnt, jlong collision_cnt, jboolean verbose, jint core) {
  (void)env;
  (void)thiz;
  struct kernelsnitch_shared_state *ks =
      glk_ks_create(mm_struct_sz, mm_slab_order, thread_cnt, collision_cnt,
                    verbose ? 1 : 0, core);
  if (!ks) return (jlong)-1;
  kernelsnitch_find_collisions(ks);
  if (kernelsnitch_found_collisions(ks)) {
    kernelsnitch_bruteforce(ks);
  }
  return (jlong)kernelsnitch_cleanup(ks);
}
