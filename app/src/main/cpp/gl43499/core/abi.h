/**
 * @file abi.h
 * @brief 43499 C11 资源原语 ABI 的基础类型与版本头。
 *
 * @note 本文件只描述跨 JNI 实现层传递的固定宽度 POD。句柄是不透明的
 *       Native 资源对象 token；当前实现允许由对象地址转换，值 0 永远无效。
 *       调用方必须保持唯一所有权，不得伪造、复制、复用已消费或已关闭的 token。
 * @note owned-entry 只有在输出 ABI 校验成功且输入 token 非零后才接受 token。
 *       接受前失败时所有权仍归调用方；接受后 close 无论返回 released 或 retained
 *       都消费 token，调用方不得恢复或重试同一值。
 * @note 所有顶层 API 输入结构必须先整体清零，再填写自身 abi；所有输出结构
 *       必须先整体清零，再填写 result.call.abi。嵌套结构由顶层 struct_size
 *       覆盖。所有 reserved/reserved32 与未选 union 分支都必须为零，否则拒绝调用。
 * @note callee 先验证指针、struct_size、abi_version 和保留字段，再写当前
 *       struct_size 范围内的输出；输出 ABI 无效时，handle 入口返回 0，int32_t
 *       入口返回 GLK43499_CALL_BAD_ABI，且不写出结构边界。调用返回后调用方仍
 *       拥有全部输入/输出存储，callee 返回的所有保留字段也必须为零。
 */
#ifndef GHOSTLOCK_GL43499_CORE_ABI_H
#define GHOSTLOCK_GL43499_CORE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 43499 公开 C ABI 版本，与项目统一版本保持一致。 */
#define GLK43499_ABI_VERSION UINT32_C(3)

/** @brief 无效句柄；任何入口收到该值都必须拒绝。 */
#define GLK43499_INVALID_HANDLE UINT64_C(0)

/** @brief 由 Kotlin 唯一 owner 持有的不透明 Native 资源 token。 */
typedef uint64_t glk43499_handle;

#ifdef __cplusplus
static_assert(sizeof(uintptr_t) <= sizeof(glk43499_handle),
              "glk43499_handle must preserve a Native pointer token");
#else
_Static_assert(sizeof(uintptr_t) <= sizeof(glk43499_handle),
               "glk43499_handle must preserve a Native pointer token");
#endif

/**
 * @brief 每个可扩展 ABI 结构的公共前缀。
 * @note reserved 必须全零；未来只允许在结构尾部追加字段。
 */
struct glk43499_abi_header {
    uint32_t struct_size;
    uint32_t abi_version;
    uint64_t reserved[2];
};

/** @brief 以当前类型大小初始化 ABI 头；仅适用于零初始化的结构。 */
#define GLK43499_ABI_HEADER_INIT(type_) \
    { (uint32_t) sizeof(type_), GLK43499_ABI_VERSION, {UINT64_C(0), UINT64_C(0)} }

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_ABI_H */
