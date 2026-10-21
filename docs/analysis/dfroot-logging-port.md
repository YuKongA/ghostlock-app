# DFRoot 日志系统移植

## 已完成

- `app/src/main/jni/df_log.h`：`DF_LOG_VERBOSE/DEBUG/INFO/WARN/ERROR`，默认最低级别 `INFO`。
- `app/src/main/jni/df_log.c`：输出到 stdout，格式 `<V|D|I|W|E> [NATIVE] <message>`，供 `ExploitRunner` 捕获。
- `CMakeLists.txt`：`dfroot` 链接 `df_log.c`。
- `exp.c`：include `df_log.h`，现有 `printf` 统一改为 `DF_LOGI`，保留原消息正文。
- 已补粒度的关键日志：
  - `patch_file_cbc.begin`（verbose：path/len/foff/helper）
  - `patch_file_cbc.done`（debug：path/blocks）
  - `patch_helper.done`
  - `patch_ko.done`（失败用 warn，单 reason）
  - `patch_hook.done`（lib/hook/shell）
  - `exploit.begin`、`patch_helper.begin`、`patch_ko.begin`、`cleanup.begin`、`createOrphanProcess.begin`

## 对拍前缀

```text
GhostLock: <~> [KOTLIN] / <*> [KOTLIN] / <V> [KOTLIN] / <V> [NATIVE]
DFRoot:    <I> [NATIVE]（当前全部 info）
```

## 语义映射

| DFRoot | GhostLock Stage |
|---|---|
| `=== exploit (patching files) ===` | `Stage page cache engine started` |
| `patch: crash_dump64 <- splicehelper` | `WriteStage.run name=helper` |
| `Using KMI: ...` / `patch: ... <- dfroot.ko` | `WriteStage.run name=module` |
| `patch: ... <- trampoline` / `trigger` | `HookStage` / `TriggerStage` |
| `=== init ===` / marker loop | `WaitStage` |
| `=== cleanup ===` | `CleanupStage` |

## 构建

```bash
cd /Users/nickji/Documents/Program/Analysis/DFRoot
make          # 需要 podman，先构建 8 个 KO，再 assembleRelease
```

或只编译 native：

```bash
clang -std=c11 -Wall -Wextra -fsyntax-only app/src/main/jni/df_log.c
```

## 下一步

1. 给 `patch_helper/patch_ko/patch_hook/do_one_write_cbc/createOrphanProcess/cleanup` 加入 `DF_LOGD`/`DF_LOGV` 的符号/分支/参数。
2. 把明显的失败消息改为 `DF_LOGE`/`DF_LOGW`，每个只留一个 reason。
3. 用同一 A301SO 跑 DFRoot 和 GhostLock，按上表做逐条对拍。

## 未验证

- DFRoot APK 尚未构建（`app/src/main/jni/ko` 未生成）。
- 日志级别分类尚未完成：当前现有 `printf` 全部是 info。

