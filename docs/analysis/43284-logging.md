# 43284 正式路径日志规范与覆盖

## 规则

- `info`：一句人类可读短文，无参数。
- `warn` / `error`：必须且只能有一个 reason。
- `debug`：分支级；message 固定为 `符号.分支`；参数逐项 `key=value`。
- `verbose`：重要函数入口参数与出口结果。

唯一日志系统是 `object log`（`app/log/log.kt`）。`StageLog` 只是把 `log` 与 UI 的 `onEvent` 连起来的发射桥。

## 已覆盖

| 文件 | 日志 |
|---|---|
| PageCacheEngine / StageRunner / StagePageCacheEngine | 入口/出口、每个 stage、cleanup、close |
| EnvironmentStage / WriteStage / HelperStage / ModuleStage | 参数、分支、成功 |
| HookStage / TriggerStage / WaitStage / CleanupStage | payload/site/verify/trigger/hit/timeout/restore |
| StageConfigProvider | SA/writer/script/module/target/plan/jump/site/original/restore |
| RawSaSession / RestoreLedger | open/record/restore/clear |
| LkmSelector / LkmImagePatch | label/path/magic/size 分支 |
| BlockWriter | direct/helper/readOld/JNI 失败/成功 |
| DfrootLibcxx / ElfTargetReader | 解码/读取分支 |
| KotlinElfPlanner | plan/planEntry/encodeBranch |
| EntryPayload | initModuleBytes/exitOnly/bytes/plan |
| LkmWindow43284 | open/close/isOpen 走 object log |
| CarrierCandidates.select | 逐候选 probe/selected/fallback |

## 示例

```text
V/Ghostlock: BlockWriter.writeRange path=/system/lib64/libc++.so offset=0 bytes=464
D/Ghostlock: WriteStage.run.via-helper helper=/apex/.../crash_dump64
W/Ghostlock: stage MODULE_PARTIAL: module-short:0/769 helper-exit
```

## 覆盖边界

- 各 Stage 的 `abort()` / `close()` 已有 debug。
- `StageSession.close`、`StageConfigProvider.readRange/writeLe` 已有 verbose/debug。
- JNI wrapper 与 C++ `gl_helper_*` / `gl_esp_*` 已覆盖：入口 verbose，关键分支 debug。
- data class / enum 的访问器不单独打日志（无行为分支）。

