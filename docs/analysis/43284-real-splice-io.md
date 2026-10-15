# RealSpliceIo：整条链的三种写是同一个类（2026-10-15 勘察）

读 `core/src/backend/cve_2026_43284/landing/real_splice_io.hpp` 与 `core/src/tools/pagecache_load_lkm.cpp`。

## 接口

```cpp
class RealSpliceIo : public SpliceOps { … }        // 页面缓存写路径（CVE 原语）的实现类
struct SpliceIoOps   { pipe2 / splice / vmsplice / send_datagram / read_at / close_fd }   // 系统调用注入面
struct OldPageSource    { read16(ctx, offset, out) }           // 读旧页（16 B）
struct HelperWriteSource{ splice16(ctx, pipe_write_fd, …) }     // 让 helper 替我们 splice
```

## 工具里的三处构造（有现成范例）

```cpp
516:  RealSpliceIo patch_surface(real_splice_io(), patch_fd);                  // patch#1：绑 crash_dump64 的 fd
564:  RealSpliceIo surface(real_splice_io(), target_fd, 0, old_page, helper);   // 载体写：绑载体 fd + 两个源
613:  RealSpliceIo hook_surface(real_splice_io(), hook_fd);                    // hook：绑 libc++.so 的 fd
```

⇒ **整条链的三种「写」是同一个类，只是绑的对象不同。**

## 要暴露的三个薄原语（按裁定：接口不够就改接口）

```text
spliceOpen(handle, path)  → surfaceId     // 只读打开 + 构造 RealSpliceIo
spliceWrite16(handle, surfaceId, offset, bytes16, out_old16) → ok
spliceClose(handle, surfaceId) → ok
```

Kotlin 按工具的次序编排：`patch#1`（crash_dump64，逐 16 B 块写 helper + 读回比对）→ 载体写 `.ko` → hook 打 libc++。
**目标、块数、顺序、重试全在 Kotlin**；native 只做「打开 / 写 16 字节 / 关闭」。

## 下一步

读 `real_splice_io.hpp` 的**构造签名**与 `write16` / `read16` 的具体方法名（一次 grep），据此落上面三个原语；
然后 patch#1 复跑，判据：crash_dump64 的 offset 16 窗口与 `embeddedImage(0)` 对应字节一致。