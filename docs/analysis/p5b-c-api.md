# P5b 证据：纯 C 句柄 API（2026-10-21）

> 目标：为 JNI 化提供无全局、可持有、可销毁的 C 边界；本轮不改变 CLI 可执行产物。

## 新增

- `glk43499_api.h/.c`：
  `Glk43499Handle` 堆对象持有 `GlkSession outer` + `Glk43499Session inner`；
  `create/run/destroy` 只操作调用方句柄。
- `glk43284_api.h/.c`：
  `Glk43284Handle` 堆对象持有 `Glk43284State`；
  `Glk43284Config` 传端口/SPI/AES；
  `glk43284_state_init` 从 CLI setup 提取，payload 仍复制到 state。
- `glk43284_state.h` 增 `state_init/run_state/state_cleanup` 原型。
- CMake 将两个 API 源加入现有 executable。

## 无全局核验

```text
43499/43284 llvm-nm b/B = 0
43284 无 D 段；剩余 d 为 .data.rel.ro 常量 + libc fini
```

## 构建与门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings
43284 important：functions=12 base=52 cur=52 PASS
43499 important：54 函数 220→218，COUNT/ORDER FAIL=0，87 arg diff
```

## 尚未做

- 两个 target 仍是 `add_executable`；JNI `System.loadLibrary` 需要 SHARED。
- `attachFd43499/43284`、`attachFdLegacy` 尚未实现。
- JNI 句柄转换与异常路径尚未落地；本轮只提供 C 边界。

## 下一步

1. 新增 `libghostlock_jni.so`（SHARED）承载 `attachFdLegacy`。
2. 43499/43284 改为 SHARED，保留 `main` 仅作 CLI 兼容或拆出 `*_cli`。
3. JNI `create/run/destroy` 用 `jlong` 持有 API 句柄。
4. 统一日志 sink（避免把 fd 再做成全局）。
