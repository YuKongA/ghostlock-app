# Kotlin 禁用台账（2026-10-10）

> 依据：用户指令「先把 kotlin 那边的旧代码清理一下，把所有 UI 和 profile 部分都改成 .disabled」+「临时启动器也删了，之后写一个新的」。
> 形式遵守 AGENTS §6「禁用而非删除用最可逆形式 + 恢复清单」：**除临时启动器外没有删除任何内容**（内容全在盘上，只改了扩展名）。

## 一、做了什么

| 项 | 处置 | 数量 |
| --- | --- | --- |
| `app/src/main/kotlin/**/*.kt` | 改名 `*.kt.disabled`（覆盖 `ui/`、`domain/`、`shizuku/`、`data/`、`data/ipsec/`） | 53 |
| `app/src/test` 与 `app/src/androidTest` 下 `*.kt` | 改名 `*.kt.disabled` | 59 |
| `temp/TempLaunchActivity.kt` | **删除**；内容另存 `docs/archive/20261010-2048-TempLaunchActivity.kt.txt`（该文件当时未被 git 跟踪，故必须留副本） | 1 |
| `app/src/main/AndroidManifest.xml` | Shizuku service + Main/TempLaunch 两个 activity 就地注释 + `//TODO(kotlin-disable-20261010)` | 3 |
| `settings.gradle.kts` | `include(:profile-core)` 注释 + TODO | 1 |
| `app/build.gradle.kts` | profile-core 三处引用注释（`lkmManifestFile` 的**路径本身保留可读**，只是模块不进构建） | 3 |

## 二、两条必须知道的事实

1. **`SessionSecretFrame` / `ChannelBStdin` 原本住在 `profile-core`**（`profile-core/src/main/kotlin/com/ghostlock/app/data/profile/`）⇒ profile-core 不进构建后，app 侧的 `AndroidIpsecSessionFactory` 无法编译，已一并禁用。
   ⇒ **新启动器必须自带那个 88 字节帧格式**（4 B 长度前缀 + 载荷）；若要复用，把类搬进 app，**不要重新引入 profile-core**（用户裁定其删除）。
2. 临时启动器删除后 **SA 供给路径暂时缺位**（`am start` → external files 写帧 → adb 抓取那条）。native 侧 `tools/pagecache_load_lkm.cpp` 的 `--frame-stdin` 模式**未动**，新启动器可以直接用。

## 三、恢复方式（全部可逆）

1. **生产源码**：对 `app/src/main/kotlin` 下所有 `*.kt.disabled` 去掉 `.disabled` 后缀（`find` + 改名，逐条如此，无内容改动）。
2. **测试**：对 `app/src/test` 与 `app/src/androidTest` 同样去掉后缀。
3. **临时启动器**：把 `docs/archive/20261010-2048-TempLaunchActivity.kt.txt` 复制回 `app/src/main/kotlin/com/ghostlock/app/temp/TempLaunchActivity.kt`，并取消 manifest 里那段注释。
4. **profile-core**：取消 `settings.gradle.kts` 与 `app/build.gradle.kts` 里 `kotlin-disable-20261010` 注释。

## 四、门禁状态

- `./gradlew :app:compileDebugKotlin` → **BUILD SUCCESSFUL**（全部 Kotlin 禁用后模块仍可编译：APK 仍是资产载体，UI/profile 逻辑已移出构建）。
- `./gradlew markdownlint` → RC=0。

## 五、登记的技术债（恢复时机：P3 迁移/生成器批次之前保持 `.disabled`）

- **manifest 对拍门禁暂停**：`ProfileManifestV3AgreementTest` / `ManifestWidthCrossModuleTest` / `FieldLabelsManifestAgreementTest` / `ControllerInternalsTest` 随测试一并禁用 ⇒ 新链路（JSON 源 + 生成式 manifest）就位时**必须重建这组对拍**。
- **SA 供给缺位**：43284 链暂不能在冷机自取 SA，等新启动器（且新启动器自带帧格式）。
- **Kotlin 侧 profile 检查已清空**：按用户裁定「profile-core 删了、检查全靠 kotlin」，重建时应在 app 内新写，而不是恢复 profile-core。
