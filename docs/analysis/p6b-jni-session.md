# P6b 证据：JNI 会话句柄（2026-10-21）

> 目标：JNI 层只持有 `jlong` 句柄，不引入进程级单例。

## 新增

- `gl43499/jni_exploit.c`：
  `create43499(String home)` → `glk43499_handle_create_default`；
  `run43499(long)` / `destroy43499(long)`。
- `glk43499_handle_create_default`：提取自 CLI 的 CPU/路径/root script/offset 选择，再创建句柄。
- `gl43284/jni_exploit.c`：
  `create43284(encapPort, senderPort, spi, byte[32])` →
  `Glk43284Config` → `glk43284_handle_create`；
  `run43284` / `destroy43284`。
- Kotlin：`com.ghostlock.app.nativebridge.NativeExploitBridge`，六个 external 方法。

## 导出

```text
Java_..._NativeExploitBridge_create43499 / run43499 / destroy43499
Java_..._NativeExploitBridge_create43284 / run43284 / destroy43284
```

## 零全局

```text
三个 SHARED 库 llvm-nm b/B = 0
```

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
43284 important：12/52→52 PASS
43499 important：54/220→218；COUNT/ORDER FAIL=0；TRACE DIFF=87
```

## 未完成 / 阻塞

- `attachFd*` 仍是 no-op；日志 sink P6c。
- 清理生成的 `packaged_res` 后，Kotlin 编译越过资源任务，但被工作树既有缺失类阻塞：
  `AttackSettings`、`BootSettings`、`LkmImagePatch`、`NativeMemfd`、`EngineLog`，
  以及 `RunCoordinator 2.kt` 重复文件。
  因此 `NativeExploitBridge.kt` 尚未经 Kotlin 编译器验证；native 符号已确认。
- 真机 `System.loadLibrary` / JNI 调用未跑。

## 下一步

1. P6c per-DSO 日志 sink。
2. 清理/绕过资源文件名阻塞，跑 Kotlin 编译。
3. 真机加载 + create/run/destroy 门禁。
