# KernelSU 移植残留清单（非 UI 层）

> 2026-10-18 用户裁定：KernelSU 管理器的 **UI** 整体留在 `app/src/main/kotlin/com/ghostlock/app/ui/`，
> 其余为上游项目的**非 UI 残留**，已在文件头标 `TODO(wipe-on-wire)`，**接线真链时删除或替换**。

## 目录位置

```text
app/src/main/kotlin/com/ghostlock/app/ui/      UI 与残留（包名已统一为 com.ghostlock.app.ui.*（2026-10-18 用户裁定去掉 ksu 包））
app/src/main/java/com/ghostlock/app/ui/       3 个上游 .java（magica）
app/src/main/aidl/com/ghostlock/app/ui/       IKsuInterface.aidl
```

## 带 `TODO(wipe-on-wire)` 标记的文件

| 组 | 内容 | 接线时怎么处理 |
|---|---|---|
| `data/`（在 ui 包内） | repository / model / registry 的数据层 | 删除；换成 GhostLock 自己的数据层（DeviceInfo / Plan / 配置） |
| `profile/`（在 ui 包内） | AppProfile 配置模型 | 删除；GhostLock 的 profile 走 `core/` 与 GLK 配置 |
| `magica/*.java` | 开机接收器 / zygote 预加载 / 服务 | 删除；GhostLock 的开机自启用真 engine 的 receiver |
| `Kernels.kt` | 内核版本解析 | 删除；改用 `core/` 的设备探测 |
| `Natives.kt` | KernelSU 的 JNI 桩（已改成固定值） | 删除；换成 GhostLock 三个 JNI 库的调用 |
| `KsuApp.kt` | `lateinit var ksuApp` 全局 Application | 删除；宿主已有 `GhostLockApp` |
| `BuildConfig.kt` | 手写的上游 BuildConfig 常量 | 删除；改用 `com.ghostlock.app.BuildConfig` |
| `util/KsuCli.kt` | ksud 命令行桩（NoopShell） | 删除；换成 GhostLock 的 ShellRunner（普通 / Shizuku / Root） |

## 已在移植时删除的上游功能（无需再接）

`ui/screen/install`、`ui/screen/flash`、`ui/screen/modulerepo`、`ui/webui`、`data/registry`、
`ui/util/Download*`、`component/choosekmidialog`、`component/uninstalldialog`、`ModuleRepo*` 等
（共 47 个 .kt + 2 个 .java），对应路由与入口已在 `Routes.kt` / `MainActivity` / `IntentDispatcher` 中移除。

## 保留的 UI（不标残留）

`ui/MainActivity.kt`、`ui/navigation3/`、`ui/screen/{home,superuser,module,settings,about,sulog,colorpalette,appprofile,template,templateeditor,scriptrun,executemoduleaction}`、
`ui/component/`、`ui/theme/`、`ui/viewmodel/`（视图模型随 UI 保留，接线时把数据源换成 GhostLock）。
