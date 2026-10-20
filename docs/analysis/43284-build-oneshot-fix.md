# 构建一键化修复（2026-10-19）

## 已完成

1. 根 `build.gradle.kts`：不设 default task；入口是 `gradle assembleDebug`（任务名后零参数），不是裸 `gradle`。
2. `LkmSupport.ensureEngine`：
   - 安装路径 + 服务状态 + 自动启动 + 45s 轮询；
   - Apple container 已移到 docker/podman 之后；
   - 失败具名：engine-start-failed / engine-timeout / engine unavailable。
3. `BuildLkmImagesTask`：
   - 删除 `cachedAll && !engineAlive` 静默复用；
   - `-PlkmCacheOnly=true` 必须通过 ledger/hash；
   - 默认路径必须拿到 engine。
4. `verifyLkmLedger` 依赖 `buildLkmImages`；`copyLkmIntoAssets` 依赖 ledger。
5. Windows：`objcopy.exe`、`cargo.exe`、`resolveTool` PATHEXT、卷路径反斜杠转 `/`。

## 验证

```text
./gradlew clean
./gradlew          # no task argument
BUILD SUCCESSFUL in 1m 30s
49 actionable tasks
```

日志：`/tmp/gradle-zero2.log`。

## 未完成

- Windows/macOS x86_64/Linux arm64 实机矩阵未跑。
- arm64 主机 `--platform linux/amd64` 仿真只在本机 macOS arm64 通过；未做能力预检。
- CI 仍是 Ubuntu-only，且 `make -C core` / `exportProfiles` 失效步骤未改。
- `:app:testDebugUnitTest` 仍依赖已删除的 `jni_protocol_manifest` 生成器；与本构建修复分开。

