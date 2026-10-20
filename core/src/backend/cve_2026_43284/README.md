# CVE-2026-43284 backend（重置中）

本目录只保留抽象 POD 结构，具体实现已删除，按上游源码逐步重写。

删除范围：

- backend 具体实现（landing / hook / ipsec / embed / lkm / steps / session_frame）
- device/43284 编排
- boundary/jni/api_43284.*、native_43284.cpp
- boundary/protocol/ops_43284.*、marshal_43284.hpp

重写顺序：

1. 纯函数（纯 C++，无 KernelAccess）
2. JNI 薄导出
3. Kotlin 编排
4. 删除旧抽象残留
