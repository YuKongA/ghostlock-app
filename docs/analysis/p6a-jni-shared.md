# P6a 证据：SHARED JNI 产物（2026-10-21）

> 目标：让 `System.loadLibrary` 能加载三个库，并暴露 JNI/API 符号；日志 sink 留 P6c。

## CMake

- `ghostlock43499` / `ghostlock43284`：`add_executable` → `add_library(... SHARED)`。
- 新增 `ghostlock_jni` SHARED（`glk_jni/legacy_attach.c`）。
- 两个 backend 设 `C_VISIBILITY_PRESET hidden`；内部调用恢复直接调用，不再出现 `@plt` 后缀。
- 仅导出：
  `glk43499_handle_create/run/destroy`、
  `glk43284_handle_create/run/destroy`、
  `Java_..._attachFd43499/43284/Legacy`。

## 产物

```text
libghostlock43499.so  ELF 64-bit LSB shared object, ARM aarch64
libghostlock43284.so  ELF 64-bit LSB shared object, ARM aarch64
libghostlock_jni.so   ELF 64-bit LSB shared object, ARM aarch64
```

`llvm-nm -D` 可见三个 attachFd 符号和两个 API 组。

## 零全局

```text
43499 b/B = 0；仅 .data.rel.ro 常量
43284 b/B/D = 0；仅 .data.rel.ro 常量
ghostlock_jni 无可写符号
```

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
43284 important：12/52→52 PASS
43499 important：54/220→218；COUNT/ORDER FAIL=0；TRACE DIFF=87
mergeDebugNativeLibs：三个 libghostlock*.so 均进入 out/lib/arm64-v8a
```

## 未完成 / 阻塞

- `attachFd*` 目前是 no-op（返回 -1），P6c 才接日志 sink；这样不引入可写 fd 全局。
- 完整 `:app:assembleDebug` 被既有资源文件名阻塞：
  `animator/anim_outline_horizontal 2.xml`（与本批无关）。
- 真机 `System.loadLibrary` 未跑。

## 下一步

1. P6c 设计 per-DSO 日志 sink（显式 context，避免全局 fd）。
2. JNI 会话方法（`nativeCreate/Run/Destroy`）和 Kotlin 包装。
3. 真机加载/调用门禁。
