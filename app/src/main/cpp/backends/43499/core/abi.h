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
 *       必须先整体清零，再填写 result.call.abi。ABI v4 起**没有 reserved 槽**：
 *       字段只允许在结构尾部 append-only 追加，同一构建内 JNI 与 .so 自洽，不存在
 *       跨版本二进制兼容需求。嵌套结构的尺寸由顶层 struct_size 覆盖，被选中的
 *       union 分支以外的尾字节必须为零，否则拒绝调用。
 * @note callee 先验证指针、struct_size（必须**严格等于**调用方声明的结构尺寸）、
 *       abi_version 与 union 尾字节，再写当前 struct_size 范围内的输出；输出 ABI
 *       无效时，handle 入口返回 0，int32_t 入口返回 GLK43499_CALL_BAD_ABI，且不
 *       写出结构边界。调用返回后调用方仍拥有全部输入/输出存储。
 * @note 阶段：ABI v4（43499 Native 全参数化 R2a）。
 */
#ifndef GHOSTLOCK_GL43499_CORE_ABI_H
#define GHOSTLOCK_GL43499_CORE_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 43499 公开 C ABI 版本；ABI v4 起等于 4。 */
#define GLK43499_ABI_VERSION UINT32_C(4)

/** @brief 无效句柄；任何入口收到该值都必须拒绝。 */
#define GLK43499_INVALID_HANDLE UINT64_C(0)

/** @brief 由 Kotlin 唯一 owner 持有的不透明 Native 资源 token。 */
typedef uint64_t glk43499_handle;

/**
 * @brief 编译期断言（C11 `_Static_assert` / C++ `static_assert` 的统一拼写）。
 * @param condition 编译期布尔表达式。
 * @param message 失败时的诊断字符串字面量。
 * @note 只用于 ABI 布局/尺寸漂移检查；不参与运行期控制流。
 */
#ifdef __cplusplus
#define GLK43499_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define GLK43499_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#ifdef __cplusplus
static_assert(sizeof(uintptr_t) <= sizeof(glk43499_handle),
              "glk43499_handle must preserve a Native pointer token");
#else
_Static_assert(sizeof(uintptr_t) <= sizeof(glk43499_handle),
               "glk43499_handle must preserve a Native pointer token");
#endif

/**
 * @brief 每个可扩展 ABI 结构的公共前缀。
 * @note ABI v4 起**没有 reserved 槽**；未来只允许在结构尾部 append-only 追加字段。
 * @note struct_size 必须**严格等于**调用方实际声明的结构尺寸；补零/向后兼容
 *       不再支持（同 APK 内 JNI 与 .so 同构建）。
 */
struct glk43499_abi_header {
    uint32_t struct_size;
    uint32_t abi_version;
};

/** @brief 以当前类型大小初始化 ABI 头；仅适用于零初始化的结构。 */
#define GLK43499_ABI_HEADER_INIT(type_) \
    { (uint32_t) sizeof(type_), GLK43499_ABI_VERSION }

GLK43499_STATIC_ASSERT(sizeof(struct glk43499_abi_header) == 8,
                       "ABI v4 abi_header layout drift (expected 8)");

#ifdef __cplusplus
}
#endif

#endif /* GHOSTLOCK_GL43499_CORE_ABI_H */
