# 43499 JNI transport 批

- 日期：2026-10-26
- 级别：L（Kotlin/JNI/C ABI 跨端契约）
- 状态：用户已明确要求“修改jni”；本批只建立 fail-closed transport，不实现 Placement/Page/Actor 物理原语。
- 依赖：[43499-c11-header-contract-plan.md](43499-c11-header-contract-plan.md)。

## 范围

- 用 Placement/Page/Actor 九入口整体替换旧 `create/discover/land/executeRoute/runFlow/last*` JNI。
- Kotlin 公开具名 request/result/Primitive 类型；JNI 显式逐字段翻译，不把 C struct padding、指针或数组下标暴露给 Kotlin/Lua。
- 页面与 Carrier 字节先复制到 native 临时缓冲；core 调用期间不持有 pinned array/raw elements，返回后清零释放。
- 三种 Kotlin Primitive 实现以 `@Keep` 固定类/字段名；JNI 按具体类判型并在 C 侧赋唯一 Primitive enum，Kotlin 不手抄跨端 ID。
- 新增 `core/api_unavailable.c`：九入口完整存在但统一返回 `UNSUPPORTED`，确保 DSO 可链接、可装载且不伪造能力。
- CMake 仅加入 unavailable 实现；后续真实实现批必须删除/取代它，禁止 weak symbol 或 stub/real 共存。

## 不改

- 不实现 Placement、Landing、Actor、Carrier、registry 或攻击效果。
- 不改 Flow、BackendPort、victim、Finish、profile、legacy、legacy2。
- 不保留旧 JNI 兼容入口或一次完整 Flow 入口。

## 所有权与失败语义

- Kotlin 持有所有对象、ByteArray 与 opaque handle 数值。
- JNI 只持有调用期局部副本；不缓存 `jobject`、数组指针或句柄。
- handle 0 直接拒绝；非零 ID 的 kind/generation/stale 由未来 core registry 权威判断。
- unavailable 实现不创建 fd/pid/thread、不保存指针，返回 handle 0、`CALL_UNSUPPORTED`、`EFFECT_NONE`、`RESOURCE_INVALID`。

## 验证

- C11 `-Wall -Wextra -Werror` 编译 JNI 与 unavailable 实现。
- Kotlin 编译；JNI 名称与 external 方法一一对拍，旧 symbol 全部消失。
- DSO 链接并检查九个新 JNI symbol；stub 九入口均为显式 UNSUPPORTED。
- 该批不产生真机攻击能力声明；APK load/device gate 属后续接线批。

