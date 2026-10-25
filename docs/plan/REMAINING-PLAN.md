# 剩余计划（dev-nocpp · 2026-10-22）

> 适用范围：当前 `dev-nocpp` 的 43499/43284 C native 分支。
> `MASTER-PLAN.md` 与 kt-step 计划属于旧 C++/Kotlin 架构，本文件只整理**当前分支**剩余项，
> 不覆盖历史计划。
> **2026-10-22 JNI 重写（待批）**：P0-4 / P6c-4 / P0-1 / P0-2 / P1 已被
> [43499-43284-jni-rewrite-plan.md](43499-43284-jni-rewrite-plan.md) 并入；本文件只保留未被并入的清理项。

## 当前事实

```text
43499 冷机 A301SO：单次 PASS
  child is root! / KernelSU ready / 无 panic
  binary sha256 b2925af4…b2ef3

43499/43284 b/B = 0
android native build = 0 warnings
S1–S8 参数化/拆分完成
P6c-1 GlkFdSink/glk_logf 完成，但 attachFd* 仍是 no-op
APK 不是当前构建；Kotlin 编译仍被既有缺失类阻断
```

## P0：稳定性收口（完成前不得标 supported）

### P0-1 43499 冷机重复性

- 同一 binary、同一设备、冷机重复 5 次。
- 每次记录：
  `W1 / W1b / W2 prebuild / W2b / child uid=0 / KernelSU ready / bootreason`。
- 目标：至少 4/5 冷机 PASS，且无 kernel panic；若 panic 出现，按 P0-2 分层。
- 产物：`device-gates/20261022-43499-repeat-<n>.md`。

### P0-2 panic 分层

若重复性出现 panic，按层隔离，不混跑：

```text
A. W2 route only（不执行 W2b）
B. W2b activation/repair only
C. post-root G / root shell
D. KSU late-load
```

每层记录：

```text
ro.boot.bootreason
/sys/fs/pstore（有权限时）
kernel stack
route_last_step / errno
```

禁止把 fd sentinel、KernelSnitch、W2b、KSU 的 panic 合并归因。

### P0-3 43284 当前 C 版本真机门禁

现有 43284 门禁是重构前记录。当前 C 状态需要重新跑：

- 冷机
- 固定 CPU 对
- 单 route
- KernelSU 未加载
- 记录 patch1 / carrier / hook / trigger / wait 的实际结果

未过只标 `device-gate-pending`。

### P0-4 APK / Kotlin 解阻

当前 `assembleDebug` 被既有 Kotlin 缺失类阻断：

```text
AttackSettings
BootSettings
LkmImagePatch
NativeMemfd
EngineLog
PageCacheEngine
RunCoordinator 2.kt 重复
```

完成后：

1. `assembleDebug` 产出当前 APK；
2. 确认 APK 只含当前 3 个 SO：
   `libghostlock43499.so` / `libghostlock43284.so` / `libghostlock_jni.so`；
3. 安装后通过 app/JNI 入口冷机复跑；
4. 归档 APK 的 sha256。

## P1：历史稳定性修复回移

按 critic 结论，只回移行为修复，不合并 C++ 树。

| 项 | 内容 | 当前状态 |
|---|---|---|
| `993f399e` | compact select 失败后新页重试 | 外层 retry 是否等价待确认 |
| `452bd1fd` | leak walk 逐段推进 + 防 spin | 已加 setup 参数守卫，未移植 walk 测试 |
| `c6776ad` | 验证改为有界重试 | 43284 C 路径待映射 |
| `396e52d` | 测量 direct map 末端（`.ghostlock_iomem`） | 当前只用 `IDENTITY_END` 封顶 |
| `277579ee` | 43284 retry send，不只重读 | 待核对 |

每项独立 host/device 证据；不与 panic 分层批次混。

## P1：P6c 日志

### P6c-2 43499 调用点

```text
pr_info/pr_warning/pr_error → glk_logf(&s->log, ...)
PI 窗口丢弃
每批 important/flatten 计数/顺序
```

### P6c-3 43284 调用点

```text
DF_LOG* → glk_logf(&st->log, ...)
```

### P6c-4 JNI / Kotlin

```text
create43499(home, logFd)
create43284(..., logFd)
destroy busy 返回 1
真机验证：写一行、destroy 后 fd 关闭
```

完成后可删除 `NativeLogBridge` 三库循环与 `libghostlock_jni` 兼容壳。

## P2：清理与简化

### 源树清理（确认未编译才动）

```text
app/src/main/cpp/cmake-build-debug/      IDE 构建缓存
app/src/main/cpp/gl43499/fops 2.c       重复文件
app/src/main/cpp/gl43284/CMakeLists.txt 根 CMake 未 add_subdirectory
gl43284/backend_stub.c / bootstrap.c    未在根 CMake 编译
common/hmac_sha256.*                    未加入 gl_common
```

### SO 结构

保持当前 3 个 SHARED：

```text
libghostlock43499.so
libghostlock43284.so
libghostlock_jni.so
```

`gl_common` 仍 STATIC；不把 43284/43499 合并。

## 门禁模板

```text
./gradlew :app:externalNativeBuildDebug --offline
0 warnings

host hash test rc=0
important_flatten S1 19/19 PASS
important_flatten S4 17/17 PASS
llvm-nm 43499/43284 b/B = 0

冷机 43499:
  W1 SELinux permissive
  W1b scratch repaired
  W2 prebuild
  W2b repair
  child uid=0
  KernelSU ready
  ro.boot.bootreason != kernel_panic
```

## 不做

```text
不整树合并 origin/main 的 C++
不把路径移动/删除/squash 当稳定性修复
不以单次冷机 PASS 标 supported
不同时跑多个 writer
不未经要求 commit
```
