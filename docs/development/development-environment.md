# 开发环境：从 clone 到所需任务

本文供新开发者及其 Agent 配置工具，不定义产品支持状态或新门禁。先选任务，再安装对应依赖；版本以所链接的仓库配置为准，以下盘点日期为 **2026-11-21**。

**先确认 checkout**：本次提交不包含其它工作中的生产实现。盘点时 `plugin/**` 构建文件和 `tools/allure_report.py` 仍是未提交的在办输入，以下用代码路径标识，不伪装成 clone 后必有的文件。新 clone 先按实际 Gradle/lockfile 配置依赖；若缺这些输入，记录为“对应实现尚未进入本 checkout”，不创建占位文件、不照旧设计补实现。规划工具和任务 bootstrap 可独立配置；本文不声称当前重构树已能全量构建。

## 1. 按任务准备

| 要做什么 | 需要什么 | 下一步 |
|---|---|---|
| 文档、设计与任务管理 | Git、Node/npm | [文档与规划工具](#planning-tools)；不要求 Android、设备或 Rust |
| Android 编译、app 单测 | JDK25 + JDK21、Android SDK/NDK、Python3、Node/npm | [Android 配置](#android-setup)；单测也消费 builtin plugin 资产 |
| host native 测试 | CMake/CTest、Ninja、C/C++ 编译器，以及 Gradle 配置所需 Android 环境 | [按需工具](#optional-tools)；先检查下方已知接线问题 |
| 设备/AVD 验证 | 上述构建环境、platform-tools、获准的设备或 AVD | 原[真机门禁](engineering-standards.md#engineering-device-gate)，不以 AVD 代替真机证据 |
| retained Rust extractor | Rust/Cargo | [按需工具](#optional-tools)；普通 app 开发不必安装 |

## 2. 版本与官方入口

| 工具 | 本仓要求与唯一版本来源 | 官方安装/下载 |
|---|---|---|
| Git | 需要稳定 app versionCode 时保留完整 Git 历史；纯文档不要求补全历史 | [Git](https://git-scm.com/downloads) |
| Gradle | **9.8.1**，使用 [Wrapper](../../gradle/wrapper/gradle-wrapper.properties)，不装全局 Gradle | [Wrapper 文档](https://docs.gradle.org/current/userguide/gradle_wrapper.html) |
| Java | daemon **25**：[JVM criteria](../../gradle/gradle-daemon-jvm.properties)；模块 toolchain **21**：[app](../../app/build.gradle.kts)、`plugin/api/build.gradle.kts` | [Temurin 安装](https://adoptium.net/installation/)、[Gradle daemon JVM](https://docs.gradle.org/current/userguide/gradle_daemon.html#sec:daemon_jvm_criteria) |
| AGP / Kotlin | **9.1.0 / 2.4.20**：[根 Gradle](../../build.gradle.kts)；由 Gradle 获取，不单独安装 | [Android Gradle Plugin](https://developer.android.com/build/releases/gradle-plugin)、[Kotlin](https://kotlinlang.org/docs/gradle-configure-project.html) |
| Android SDK | compile **37 minor 2**；target37 / min31：[app](../../app/build.gradle.kts)；Build Tools **37.0.0**：`plugin/packages/diagnose/build.gradle.kts` | [Android Studio / SDK 下载](https://developer.android.com/studio)、[SDK 包管理](https://developer.android.com/tools/sdkmanager) |
| Android NDK | **30.0.16248370**：`plugin/packages/diagnose/build.gradle.kts`；当前 ABI 为 arm64-v8a | [NDK 安装](https://developer.android.com/studio/projects/install-ndk) |
| CMake / CTest / Ninja | CMake **≥3.22**：[host tests](../../tools/tests/CMakeLists.txt)；仓库未 pin 精确 CMake/Ninja 版本；Android 本机已安装3.22.1 | [CMake 下载](https://cmake.org/download/)、[Ninja 官方安装指引](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages) |
| Python | Allure bridge 需要 **≥3.9**，仅用标准库：`tools/allure_report.py`；其它脚本按实际任务检查 | [Python 下载](https://www.python.org/downloads/) |
| Node / npm | 规划工具需要 Node **≥22.20.0**；建议受支持的 LTS。工程工具依赖使用 lockfile | [Node 下载](https://nodejs.org/en/download)、[npm ci](https://docs.npmjs.com/cli/commands/npm-ci) |
| Allure Report | **3.33.0**：`tools/allure_report.py` 的 VERSION；接入 bridge 的 Gradle 门自动安装到 `build/tooling/allure` | [Allure 3 安装](https://allurereport.org/docs/v3/install/) |
| 文档 lint | markdownlint-cli2 **0.23.3**：[package.json](../../tools/engineering/package.json) / [lockfile](../../tools/engineering/package-lock.json) | [上游用法](https://github.com/DavidAnson/markdownlint-cli2) |

依赖解析使用 Google Maven、Maven Central、Gradle Plugin Portal 及 JitPack；首次安装还需要访问 Gradle、JDK、Android、npm 的下载源。网络错误先按实际失败 URL 排查，不通过改版本或关门禁绕过。

<a id="android-setup"></a>

## 3. Android 配置

1. 安装 JDK25 与 JDK21。daemon criteria 已声明平台对应的 JDK25 下载地址；这不代表 JDK21 编译工具链也自动准备好。macOS 用 `/usr/libexec/java_home -V` 检查；Linux/Windows 使用所装 JDK 的官方配置方法。不要为本项目改掉其它项目的 Java 默认值。
2. 安装 Android Studio，或官方 Android CLI / Command-Line Tools。SDK 路径写入未跟踪的 `local.properties`；保留该文件已有其它配置。
3. 用 SDK Manager 安装 API **37 minor 2**、Build Tools37.0.0、NDK30.0.16248370、CMake3.22.1；需要设备任务再安装 platform-tools、emulator 与对应 system image。本机核验包名为 `platforms;android-37.2`；其它机器先通过包列表确认，不把基础 API37 当成 minor2。
4. Android module 通过 DSL `ndkVersion` 选择 SDK side-by-side NDK；`local.properties` 只保留 `sdk.dir`。不要写已弃用且会触发 AGP `CXX5106` 的 `ndk.dir`；AGP 9.1 也不再用 `ANDROID_NDK_HOME` 代替该选择。

示例（替换为本机真实绝对路径，合并而非覆盖已有 `local.properties`）：

```properties
sdk.dir=/absolute/path/to/android-sdk
```

已有 Command-Line Tools 的 macOS/Linux 可使用以下兼容命令。Google 当前推荐 Android CLI 的 `android sdk`；选择一种包管理入口，不再叠加 bootstrap 脚本。

```sh
export ANDROID_HOME=/absolute/path/to/android-sdk
export PATH="$ANDROID_HOME/platform-tools:$PATH"
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --list
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" \
  "platforms;android-37.2" "build-tools;37.0.0" \
  "ndk;30.0.16248370" "cmake;3.22.1"
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
```

Windows 使用同一包清单与 `sdkmanager.bat` / `gradlew.bat`；plugin producer 按 host 选择 SDK/NDK 工具文件名，但 Windows/Linux 全链路在实际 CI 或对应 host 运行前仍为 `NOT_RUN`。正式签名密钥仅在授权发布时配置；普通本地编译不需要团队密钥。

<a id="optional-tools"></a>

## 4. 按需工具与当前接线问题

- **host C/C++**：macOS 安装 [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/)；Linux 使用发行版的 C/C++ compiler、CMake、Ninja。host 合同测试采用 C11/C++23，不能只检查命令存在。SDK 内 CMake 不自动等于 PATH 中的 host CMake；根 Gradle 支持 `-Pcmake=...` / `-Pctest=...` / `-Pninja=...`。
- **Rust**：[rustup 官方入口](https://rustup.rs/)。[retained extractor manifest](../../tools/extract_rs/Cargo.toml) 的 `rust-version=1.97.1` 是 MSRV，非精确 toolchain pin；依赖由 Cargo.lock 管理。当前独立 extractor 设计归属见[迁移指针](../analysis/kotlin-generic-kernel-extractor-plan.md)，不把其环境强加给 app。
- **设备**：[adb 文档](https://developer.android.com/tools/adb)、[AVD 管理](https://developer.android.com/studio/run/managing-avds)。按原批准范围选设备和 serial；host/AVD 成功不改变 supported 或真实设备验收。

以下是 **2026-11-21 工作树盘点发现的仓库接线问题**，不是要求新人多装工具，也不是本轮擅自修改生产构建的授权：

| 现象 | 实际原因与定位 |
|---|---|
| inventory 没覆盖新 plugin 树 | [根 Gradle](../../build.gradle.kts) 的 `verifyNativeTestInventory` 仍扫描旧 app/tools 范围；由测试接线批处理，不据此声称新 plugin 已完整验证 |

[CI](../../.github/workflows/build.yml) 显式安装与 module DSL 一致的 SDK side-by-side NDK30.0.16248370，并使用 JDK21；本地 macOS 构建成功不等于 GitHub Linux CI 已通过。

<a id="planning-tools"></a>

## 5. 文档与规划工具

仓库根目录执行 `npm --prefix tools/engineering ci`，使用项目本地 CLI，不依赖全局最新版。OpenSpec **2.1.1**、Beads **1.3.4** 由 `tools/engineering/package.json` 与同目录 lockfile 固定；CDA 仅安装 runtime skill、引用和许可证，上游 commit 为 `04d2e998f19b08be6ba7f697315f3a16ecdf2708`，本地入口补充项目分工；不运行上游评测 harness，不需要服务器、独立 Dolt 或额外 JDK。

- [OpenSpec 安装](https://openspec.dev/docs/installation)：proposal/design/specs设计与规范管理。
- [Beads 官方安装](https://github.com/gastownhall/beads/blob/main/docs/getting-started/installation.md)：执行任务、依赖、进度和批准/运行证据。
- [CDA 上游](https://github.com/Liparakis/ConstraintDrivenArchitecture)：作者的约束驱动设计方法；不替代独立审查。

```sh
npm --prefix tools/engineering ci
npm --prefix tools/engineering run --silent bd -- version
npm --prefix tools/engineering run --silent openspec -- --version
npm --prefix tools/engineering run --silent openspec -- list --json
npm --prefix tools/engineering run --silent bd -- bootstrap --yes
npm --prefix tools/engineering run --silent bd -- ready --json
```

`npm --prefix tools/engineering ci` 包含 Beads 的原生 binary 下载（对应固定版本及本机 OS/架构），不要用 `--ignore-scripts` 后宣称 bd 已安装。OpenSpec 项目适配、CDA、作者自检和独立审查入口位于 `tools/engineering/skills/`，由根 AGENTS.md 按任务指向；Agent 直接读取对应 SKILL.md，不依赖 `.agents/skills` 自动发现，不重复安装。命令报缺依赖时先 `npm --prefix tools/engineering ci`，不用会临时下载最新版的裸 `npx`。OpenSpec schema为ghostlock，proposal/design/specs为设计正文、tracks:null不生成tasks.md；工具分工只在[文档规范](documentation-standards.md#planning-tool-authority)维护。

### 日常命令

只看进度用 `show <id> --short` / `list`；读取任务的spec_id后直接打开相关OpenSpec章节。`--brief-deps`只缩短依赖对象；证据按需取notes/comments，不输出整个任务库或全部历史。

```sh
npm --prefix tools/engineering run --silent bd -- show <id> --json --brief-deps
npm --prefix tools/engineering run --silent bd -- update <id> --claim
npm --prefix tools/engineering run --silent bd -- update <id> --append-notes '<实际命令、输入、输出/退出码、NOT_RUN项>'
npm --prefix tools/engineering run --silent bd -- close <id> --reason '<满足原判据的结论及证据位置>'
npm --prefix tools/engineering run --silent bd -- update <id> --spec-id 'tools/engineering/openspec/changes/<name>/design.md#<anchor>'
npm --prefix tools/engineering run --silent openspec -- instructions apply --change <name> --json
npm --prefix tools/engineering run --silent bd -- dep relate <task-id> <decision-id>  # 参考，不新增阻断
```

以上命令从仓库根目录执行；npm 脚本的工作目录是 `tools/engineering`，所以 CLI 输出文件参数（如 `-o .beads/issues.jsonl`）相对此目录，而 `spec_id` 和设计中的源码路径仍相对仓库根目录。

设计正文只维护在OpenSpec，Beads `design`留空、`spec_id`指向设计文件/锚点，验收字段只引用适用矩阵。不要把整个JSONL当日常阅读入口。title / `external_ref` 保留语义编号；候选需求用 `deferred`，具名条件缺失用 `blocked`。只领取获准工作，close 不会替你验证行为或批准范围。人类查进度可运行 `npm --prefix tools/engineering run --silent bd -- list`，不必翻旧计划历史。

### 保存、交接与升级

- 任务的写入 authority 是 `tools/engineering/.beads/embeddeddolt` 中的原生数据库，CLI 维护其事务历史。`tools/engineering/.beads/issues.jsonl` 是 CLI 生成的任务交接投影，禁止手编；新 clone 的 `bootstrap` 导入该投影。它保留任务描述、OpenSpec指针、验收引用、notes、状态、依赖和注释；OpenSpec设计文件必须同批提交，不包含完整 Dolt 分支历史，不是跨机器并发同步。
- 已配置自动导出，间隔为 **1ms**（上游把 `0s` 解释为默认60s，不表示关闭节流）。在交付/提交前执行一次 `npm --prefix tools/engineering run --silent bd -- export -o .beads/issues.jsonl`，确认最后一次更新已落盘；正常提交它和原生配置，不提交数据库文件。
- 当前只配置本地任务协作，没有发布 Dolt remote。原生 CLI 的“无 remote”提示不证明任务丢失，也不授权 Agent 自动 push；真正跨机协作时使用官方 Dolt remote/push/pull，而不是手写 JSONL 合并器。
- 自动 backup、auto-push、export git-add 均关闭，未安装 Git hooks。正常本地 commit 由 Agent 核对本批范围和适用验证后主动完成。fresh clone 用 **bootstrap，不重复 init**：Beads1.3.4 的 `init` 会自动提交 agent 文件，`--skip-agents --skip-hooks` 不会取消此行为。
- 升级时精确修改 npm 版本/lockfile、CDA 来源与实际适配；保留OpenSpec设计/Beads执行的本地skill与schema适配；不直接用生成器更新覆盖成Markdown任务清单。只验证受影响的工具行为与既有门禁，不升级产品工具链。

## 6. 配置验证，不混同产品验收

先按任务检查所需工具：`node --version`、`npm --version`、`python3 --version`、JDK 列表、SDK 包列表；这些仅证明环境。需要 Gradle 时运行 `./gradlew --version`，需要 Android 编译时再运行原[适用门命令](engineering-standards.md#engineering-gate-commands)。未执行的构建、行为、AVD、真机门保留 **NOT_RUN**。

Agent 接手提示，可直接复用：

> 先读 AGENTS.md 与本环境文档的任务入口。确认本次任务需要的工具和仓库内版本来源，只补缺失项；保留已有 local.properties。区分环境缺失、仓库接线错误与行为失败。不要全量跑门、升级工具链、修改生产代码或重复创建报告；回报实际命令、结果和仍为 NOT_RUN 的对应验证。
