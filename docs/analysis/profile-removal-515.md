# 43499 native profile 表移除（2026-10-22）

## 已实施

- 新增 `glk43499_fixed_515.c`：只保留测试机
  `5.15.189-android13-8-00016-g51bba4309aac-ab14546557` 的单条 profile。
- `glk43499_handle_create_default()` 改为调用 `glk43499_fixed_target_515()`，
  不再调用 `select_offsets()`。
- `main.c` 的 profile 处理函数置于 `GLK_ENABLE_NATIVE_PROFILES` 下（未定义）。
- 43499 CMake 移除 `offsets_json.c`，定义 `GLK_NO_KNOWN_OFFSETS=1`。
- `glk43499_handle_create_default()` 改用 `glk43499_fixed_config_515()`；
  `init_cpu_config` / `set_runtime_home` 已删除。
- 不再读取 `GHOSTLOCK_CORE` / `GHOSTLOCK_CONSUMER_CORE` / `GHOSTLOCK_HOME` / `TMPDIR`。

## 二进制结果

```text
known_offsets          不存在
g_profile_map          不存在
g_symbol_map           不存在
g_task_map             不存在
b/B = 0
.so = 329,576 bytes
```

## 仍保留

- `init_cpu_config` / `set_runtime_home` 仍有 native 默认；
- `offsets_json.c` 文件仍在仓库，但已不参与 43499 构建；
- 43284/JNI 不受本批影响。

## 验证

```text
CMake build 0 warnings
5.10 select probe: step=0 calls=1 success=1
```

A301 离线，fixed default 尚未做完整 multicast → KernelSU 复测。
