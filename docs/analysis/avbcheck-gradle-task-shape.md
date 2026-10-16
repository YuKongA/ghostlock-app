# avbcheck 的 Gradle 任务：必须写成任务类（configuration cache）（2026-10-15）

## 我踩的坑（两次提交都被撤回，原因在第二次才看清）

用脚本级任务（`val avbcheck by tasks.registering { doLast { providers.exec … } }`）跑得**看起来成功**：
任务体打印了 `avbcheck: host build + unit test passed`，CMake 构建与 ctest 都过 ✓ ——
**但整个 Gradle 构建仍然失败** ✗：

```text
Configuration cache problems found in this build.
- Task `:avbcheck` of type `org.gradle.api.DefaultTask`: cannot serialize Gradle script object references
  as these are not supported with the configuration cache.
```

⇒ 本仓启用了 **configuration cache**；**脚本级任务的 action 不能触碰脚本对象**（`providers`/`layout` 等）。

## 仓库里早就有正确形态（我没先读它，教训）

`app/build.gradle.kts:232` 的注释原文：

> Two task CLASSES with injected services and typed properties: the configuration cache cannot serialize
> script references, so a task action must never touch project/providers. Plain helpers live in LkmSupport.

照着 LKM 那套写（`BuildLkmImagesTask` / `VerifyLkmLedgerTask` 就是范式）：

```kotlin
abstract class AvbcheckHostTask : DefaultTask() {
    @get:InputDirectory abstract val sourceDir: DirectoryProperty
    @get:OutputDirectory abstract val outputDir: DirectoryProperty
    @get:Inject abstract val execOps: ExecOperations          // 服务注入，不用 providers
    @TaskAction fun run() { execOps.exec { … }; execOps.exec { … } }   // 全部类型化属性，无脚本引用
}
```

## 顺带记两条流程教训（都发生在同一天）

1. **提交硬门要检查退出码，不是日志字符串** —— 我两次用 `grep -q '成功字样'` 当门，结果一次提交了链接失败、一次提交了 Gradle 失败 ✗；
   正确写法：`if <命令>; then 提交; else 不提交; fi`（把命令本身当条件）。
2. **先读仓库既有的同类实现再动手** —— configuration cache 这条，仓库里写得明明白白，我先写后读，赔了两次撤回。

## 下一步

按任务类形态写 `AvbcheckHostTask`（+ 可选的 `AvbcheckDeviceTask` 走 NDK 工具链），
用**命令退出码**当提交门，然后才谈删掉 `tools/avbcheck/Makefile`。
