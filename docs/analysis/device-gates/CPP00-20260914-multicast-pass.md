# CPP00 混合 C/C++ 构建门禁：Multicast 通过

- 时间：2026-09-14 15:41–15:42（America/Toronto）
- 应用：`versionCode=178`，CPP00 提交 `5e5f213`
- 设备/内核：A301SO，`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
- 入口：Direct；CPU：main 0 / consumer 1
- 路线：Multicast waiter；TCP、Select 与运行时回退未启用，日志不可判定
- 温度：日志未记录，无法判定

## 结果

- 静态 libc++ 的混合 GNU C11/C++20 产物能够由现有 Kotlin/Direct 管线正常加载和执行。
- W1、W1b、W2、W2b、W3 TIF_SECCOMP 与 seccomp mode 均完成。
- 六次 Multicast 路线均为 `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`，线程全部 join。
- child UID 为 0，seccomp filter-free worker 验证通过；KernelSU module 加载并最终输出 `KernelSU ready`。
- 未发生运行时回退、dirty cleanup、卡死或重启。

原始日志：[CPP00-20260914-multicast-pass.native.log](CPP00-20260914-multicast-pass.native.log)
