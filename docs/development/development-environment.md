# 配置开发环境

按本文完成安装后，你可以在当前终端使用项目的构建、测试和计划管理工具。**首次配置读第 1–4 节；查某个命令来自哪里，直接看[命令参考](#environment-command-sources)。**

安装器目前提供 macOS、Linux 和 Windows/WSL2 路线。全新系统实装尚未验证；已有验证为安装器回归测试和下载元数据检查，不代表产品构建或真机通过。

## 1. 选择平台和开发任务

### 平台要求

| 平台 | 开始前需要 | 执行位置 |
|---|---|---|
| macOS，Apple Silicon / Intel | 系统工具及 Homebrew | Terminal，普通用户 |
| Linux x86_64 | 常规 workstation、GNU 工具、sudo 权限 | 终端，普通用户 |
| Windows 11 x64 | 系统 PowerShell；允许启用 WSL2 | PowerShell 安装，之后在 Ubuntu 中开发 |

Linux 安装器面向 Ubuntu 24.04+、Debian 13+、Fedora 43+、openSUSE Tumbleweed 和当前 Arch，通过发行版包管理器安装依赖。Linux arm64 仅提供规划工具安装；Android host 工具需要 x86_64。Atomic/Silverblue、Alpine、NixOS 不使用本文的系统包安装路线。

### 选择安装内容

| 你要做什么 | 安装参数 | 内容 |
|---|---|---|
| 文档、设计、任务管理 | `planning` | Git、ripgrep、Python、Node、Beads、OpenSpec、Markdown lint |
| Android 开发、host native 测试 | `android`（默认） | planning + C/C++、CMake/CTest、Ninja、JDK、Android SDK/NDK |
| 还需要 retained Rust extractor | `full` | android + Rust/Cargo |

不用预先安装 Python、Node、Java 或 Android Studio。安装器补齐所选任务的依赖；默认安装不包含模拟器镜像。

## 2. 取得代码

**已有工作副本：直接进入该目录，继续第 3 节。**

已有 Git 时：

```sh
git clone https://github.com/YuKongA/ghostlock-app.git
cd ghostlock-app
```

没有 Git 时，从[仓库页面](https://github.com/YuKongA/ghostlock-app)下载 ZIP 并解压，先在解压目录运行第 3 节的安装器。安装完成后 Git 即可用，再执行上面的 clone 命令。正式开发使用 Git 工作副本，并在其中重跑所需安装命令，生成该工作副本的环境入口；ZIP 没有项目计算 versionCode 所需的 Git 历史。

Windows 下，正式工作副本建议放在 WSL 的 Linux home，而不是 Windows 挂载盘。已有 Windows 工作副本也可从下面的 PowerShell 入口开始安装。

<a id="environment-install"></a>

## 3. 安装并激活

以下示例安装 `android`；只做文档时替换为 `planning`，需要 Rust 时替换为 `full`。

### macOS / Linux

在代码根目录执行：

```sh
bash tools/engineering/install/install.sh --profile android
source build/engineering/env-android.sh
```

第一次安装可能需要下载较大的 JDK、SDK 和 NDK。Linux 会提示 sudo 密码；不要用 `sudo bash`运行整个安装器。Android SDK 许可证由你交互确认。

macOS 若弹出 Command Line Tools 安装窗口，完成安装后重跑同一命令。脚本返回退出码 3 表示在等待这个系统步骤，不是安装完成。

### Windows / WSL2

在代码目录的 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\engineering\install\install.ps1 -Profile android
```

首次启用 WSL2 时：

1. 根据系统提示以管理员身份运行并重启 Windows。
2. 执行 `wsl -d Ubuntu-24.04`，完成 Linux 用户创建。
3. 回到 PowerShell，重跑上面的安装命令。

安装完成后进入 **WSL Ubuntu**，按脚本打印的绝对路径执行 `source`。如果工作副本已在 WSL 中，也可以直接执行 macOS/Linux 小节的两条命令。

项目现有 host 测试和 Allure 调用链依赖 POSIX；这里使用 Linux 工具链，不是 Windows 原生构建。Windows SDK/NDK 不与 WSL 共用。

### 以后重新打开终端

在同一工作副本根目录，只需重新激活对应环境：

```sh
source build/engineering/env-android.sh
```

planning/full 分别使用 `env-planning.sh`、`env-full.sh`。这些文件位于忽略的 build 目录；安装器不修改你的 shell 启动文件。

<a id="android-setup"></a>

## 4. 确认工具可用，然后开始工作

### 所有安装档

```sh
node --version
npm --prefix tools/engineering run --silent bd -- version
npm --prefix tools/engineering run --silent openspec -- --version
```

三条命令应正常退出并显示版本。Beads/OpenSpec 的版本以[工程工具 package.json](../../tools/engineering/package.json)为准。

### android / full 再检查

```sh
java -version
"$JDK_TOOLCHAIN_HOME/bin/javac" -version
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --list_installed
cmake --version
ctest --version
ninja --version
```

应能看到 daemon JDK、编译 JDK 和仓库所要求的 SDK 包；具体版本及其配置位置见[开发工具参考](#environment-command-sources)。这些检查确认工具可启动，不执行产品测试。

**到这里环境配置结束。** 接下来按你的任务选择入口：

- 查任务、读设计：[Beads / OpenSpec 日常使用](#planning-tools)。
- 编译或测试：按[工程规范中的适用门命令](engineering-standards.md#engineering-gate-commands)执行，不把所有门当成初始化步骤。
- 配置模拟器、连接设备或使用 Rust：[可选配置](#optional-tools)。
- 某一步失败：[常见问题](#environment-troubleshooting)。

<a id="planning-tools"></a>

## 5. Beads / OpenSpec 日常使用

下面的命令均在仓库根目录执行。npm 脚本实际在 `tools/engineering` 中运行；Beads 的 `spec_id` 仍以仓库根目录为基准。

### 新 clone：导入任务

安装器已经执行过 npm 安装。**仅当新 clone 尚未初始化本地任务库时**，执行：

```sh
npm --prefix tools/engineering run --silent bd -- bootstrap --yes
```

它读取已提交的任务投影。已有任务库不重建；不要改用 `bd init`，该版本的 init 会产生额外初始化提交。

### 查任务和设计

```sh
npm --prefix tools/engineering run --silent bd -- list
npm --prefix tools/engineering run --silent bd -- show <id> --json --brief-deps
npm --prefix tools/engineering run --silent openspec -- list --json
npm --prefix tools/engineering run --silent openspec -- instructions apply --change <name> --json
```

从任务的 `spec_id` 打开对应设计。`--brief-deps`只缩短依赖对象，notes/comments仍会返回；只需导航字段时可使用：

```sh
npm --prefix tools/engineering run --silent bd -- show <id> --json --brief-deps |
  node -e 'const rows = JSON.parse(require("node:fs").readFileSync(0, "utf8")); const fields = ["id", "title", "status", "spec_id", "acceptance_criteria", "dependencies"]; console.log(JSON.stringify(rows.map(row => Object.fromEntries(fields.map(key => [key, row[key]]))), null, 2));'
```

### 更新任务与保存记录

以下是按需操作，不要整段执行。认领、关闭及设计批准仍遵守原任务要求；工具的 ready 状态不代表获准实施。

```sh
npm --prefix tools/engineering run --silent bd -- update <id> --claim
npm --prefix tools/engineering run --silent bd -- update <id> --append-notes '<实际命令、结果与未运行项>'
npm --prefix tools/engineering run --silent bd -- close <id> --reason '<满足原判据的结论及证据位置>'
npm --prefix tools/engineering run --silent bd -- update <id> --spec-id 'tools/engineering/openspec/changes/<name>/design.md#<anchor>'
npm --prefix tools/engineering run --silent bd -- dep relate <task-id> <decision-id>
```

提交前导出最后一次更新：

```sh
npm --prefix tools/engineering run --silent bd -- export -o .beads/issues.jsonl
```

这里的输出路径相对 npm 工作目录，实际文件是 `tools/engineering/.beads/issues.jsonl`。

- 原生任务库：`tools/engineering/.beads/embeddeddolt`；JSONL 是 CLI 生成的 Git 交接投影，不手编，也不包含完整 Dolt 历史。
- 当前自动导出间隔为 1ms；auto-push、backup、export git-add 均关闭，没有安装 Git hooks 或配置共享 Dolt remote。“无 remote”提示不等于任务丢失，不需为本地使用配置服务或 push。
- 设计正文、任务字段及批准证据的唯一归属见[工具分工](documentation-standards.md#planning-tool-authority)；相关 skill 由根 AGENTS.md 路由，不另装全局 skill。

<a id="optional-tools"></a>

## 6. 可选配置

### 模拟器与真机

用 `sdkmanager --list`选择完整的 `system-images;...`包，再按任务需要安装：

```sh
bash tools/engineering/install/install.sh --profile android --system-image '<完整镜像包名>'
source build/engineering/env-android.sh
avdmanager create avd -n <名称> -k '<同一镜像包名>'
emulator -avd <名称>
```

Linux 模拟器加速需要 KVM 权限；macOS 使用 Hypervisor.Framework。安装器不调整内核、用户组或虚拟化设置。

真机先运行 `adb devices -l`，后续命令使用 `adb -s <SERIAL> ...`。Linux 可能需要 USB/udev 规则；WSL USB接入参考[Microsoft USB文档](https://learn.microsoft.com/windows/wsl/connect-usb)。原[真机门禁](engineering-standards.md#engineering-device-gate)不变，AVD不能替代其证据。

### Rust extractor

激活 full 档后：

```sh
rustc --version
cargo --version
```

需要运行 extractor 测试时，进入 `tools/extract_rs` 再执行 `cargo test --release`。项目根目录没有 Cargo.toml。

### IDE 与已有 SDK

IDE需要使用与激活脚本一致的 JDK 和 `GRADLE_USER_HOME`。安装器保留 `local.properties`；已有合法 `sdk.dir`时使用该 SDK，否则通过 `ANDROID_HOME`选择受管 SDK。NDK由模块的 `ndkVersion`选择，不写弃用的 `ndk.dir`。

<a id="environment-troubleshooting"></a>

## 7. 常见问题

| 现象 | 处理方式 |
|---|---|
| 新终端找不到 node/java/adb | 先source本checkout对应的`build/engineering/env-*.sh`，不要先重复全局安装 |
| macOS要求安装CLT，退出3 | 完成Apple安装窗口，再重跑原命令 |
| Windows首次启用WSL后未继续安装 | 完成重启及Ubuntu用户创建，再重跑PowerShell入口 |
| WSL提示Windows SDK路径冲突 | 使用Linux checkout及SDK；保留Windows工作副本的local.properties |
| 下载失败或校验不匹配 | 按输出的URL检查网络/代理；修正原因后重跑，不关闭TLS或跳过校验 |
| npm成功，但bd命令失败 | 检查原生binary下载错误；npm安装成功不等于CLI已能启动 |
| Beads提示没有Dolt remote | 本地使用无需remote；跨机协作另按官方Dolt方式配置 |
| Arch安装出现旧数据库或镜像404 | 按发行版策略维护一致的系统；安装器不做部分升级 |
| host CMake低于3.22或compiler不支持所需C++23 | 检查发行版开发包；根Gradle允许`-Pcmake=`、`-Pctest=`、`-Pninja=`指定工具 |
| inventory未覆盖plugin树 | 现有verifyNativeTestInventory接线问题，不是缺依赖；本安装器不修改检查范围 |

安装失败前已安装的系统包会保留。安装器复用完整工具版本目录；不会用恢复Git工作树的方式处理安装失败。

<a id="environment-command-sources"></a>

## 8. 命令与安装参考

本节用于查阅，不需要在首次配置时逐行阅读。**版本以链接的仓库配置为准，安装动作以脚本为准**；以下快照日期为2026-11-22。

### 系统命令

| 命令 | 提供者 / 安装来源 | 开发用途 |
|---|---|---|
| `bash`、`sh`、`pwd`、`cd`、`source`、`export`、`printf` | 系统shell；后几项是builtin，不是额外包 | 执行安装器/Wrapper、切cwd与激活环境 |
| `sudo`、`apt-get`/`dnf`/`zypper`/`pacman` | workstation系统管理工具 | 只用于系统依赖安装；macOS `brew`为用户前提 |
| `uname`、`grep`、`sed`、`find`、`head`、`tail`、`cat`、`chmod`、`mkdir`、`cp`、`mv`、`rm` | macOS BSD系统工具；Linux GNU工具预设 | 文本定位/文件操作；不得因命令存在绕过工作树保全纪律 |
| `curl`、`tar`、`unzip` | macOS系统；Linux上表包 | HTTPS引导/归档解压；安装器大文件下载实际用Python urllib |
| `git` | Git包或Apple CLT | `clone/status/diff/log/show/add/commit`；需要稳定versionCode时完整历史，自动提交规则仍见AGENTS |
| `rg` | `ripgrep`包 | 按问题/锚点检索；不是Git、Node或Python附带 |
| `powershell.exe`、`wsl.exe` | Windows系统 / Windows可选WSL功能 | 执行.ps1、初始化Linux；不要求全局更改ExecutionPolicy |
| `xcode-select`、`xcrun`、`/usr/libexec/java_home` | macOS系统/CLT | Apple工具定位；`java_home`不负责安装JDK，也未必枚举用户自定义JDK目录 |
| `sha256sum` / `shasum -a 256` / `Get-FileHash -Algorithm SHA256` | 分别GNU coreutils / macOS / PowerShell内建 | 仅在身份敏感下载/产物核验时使用；安装器用Python hashlib，不要求三者全装 |

### 开发工具及版本来源

| 命令 / 组件 | 提供者、版本唯一来源 | 实际用途 / 定位 |
|---|---|---|
| `node`、`npm`、`npx` | Node官方发行版；[package](../../tools/engineering/package.json)直接声明≥22.20；[lockfile](../../tools/engineering/package-lock.json)部分依赖要求`^22.22.2 || ^24.15.0 || >=26` | 安装器选Node24最新LTS patch；不能只满足直接声明便认定整个工具树可用；不用裸npx临时拉最新版 |
| `npm --prefix tools/engineering ci` | npm，消费同目录lockfile | 固定工程CLI安装；npm script实际cwd为`tools/engineering` |
| `npm … run --silent bd -- <子命令>` | `@beads/bd` **1.3.4**，同一package/lockfile | npm脚本加`--sandbox`；`list/show/ready/bootstrap/update/dep/export`都是bd子命令，不需额外Dolt服务 |
| `npm … run --silent openspec -- <子命令>` | `@fission-ai/openspec` **2.1.1**，同一package/lockfile | `list/new/status/instructions/validate/archive`为OpenSpec子命令；按skill使用，不将工具ready当批准 |
| `tools/engineering/node_modules/.bin/markdownlint-cli2` | npm `markdownlint-cli2` **0.23.3** | 只lint指定Markdown文件；直接从仓库根调用以保持配置/路径基准 |
| `java`、`javac`、`jar`、`jarsigner`、`keytool` | JDK；daemon **25**：[criteria](../../gradle/gradle-daemon-jvm.properties)；编译 **21**：[app](../../app/build.gradle.kts)、[plugin API](../../plugin/api/build.gradle.kts) | launcher/daemon与编译toolchain职责不同；签名命令仅在获准签名时用，普通构建不生成团队密钥 |
| `./gradlew` / `gradlew.bat` | 仓库Wrapper：[properties](../../gradle/wrapper/gradle-wrapper.properties)，Gradle **9.8.1** | 不安装全局Gradle；`.bat`只是Windows launcher，不代表本项目全部task已原生适配Windows |
| AGP **9.1.0**、Kotlin **2.4.20** | [根build.gradle.kts](../../build.gradle.kts)；Gradle从插件仓库下载 | 没有“先全局安装AGP/kotlinc”的步骤 |
| `sdkmanager`、`avdmanager` | Google Android SDK Command-Line Tools | `$ANDROID_HOME/cmdline-tools/latest/bin/`；Windows原生为.bat；CLI自身未被项目pin |
| `adb`、`fastboot` | SDK `platform-tools`包 | `$ANDROID_HOME/platform-tools/`；不是cmdline-tools提供；设备操作必须指定目标serial |
| `emulator` | SDK `emulator`包，镜像另装 | `$ANDROID_HOME/emulator/`；安装SDK不等于已有AVD/可用加速器 |
| platform API **37 minor2** | [app](../../app/build.gradle.kts) `release(37){minorApiLevel=2}` | SDK包`platforms;android-37.2`；不以基础37替代 |
| `d8`、`dexdump`、`aapt2`、`apksigner`、`zipalign` | SDK Build Tools **37.0.0**：[Diagnose producer](../../plugin/packages/diagnose/build.gradle.kts) | `build-tools/37.0.0/`；producer按host选后缀；包存在不证明plugin边界过门 |
| Android `clang/clang++`、`llvm-nm`、`llvm-objdump` | side-by-side NDK **30.0.16248370**，app/Diagnose `ndkVersion` | `ndk/<version>/toolchains/llvm/prebuilt/<host>/bin`；Android arm64-v8a交叉工具，不是host compiler |
| host `cc/c++`、`clang/clang++`或`gcc/g++` | macOS CLT / Linux开发包 | [host CMake](../../tools/tests/CMakeLists.txt)用C11/C++23和POSIX；MSVC不是等价替代 |
| `cmake`、`ctest` | 系统CMake包；host最低 **3.22**，无精确pin | 前者配置/构建，后者运行测试；SDK CMake不自动进入host PATH |
| `ninja`、`make`、`pkg-config` | Ninja、系统开发包、pkgconf/pkg-config | host generator使用Ninja；Rust native依赖/开发包可能消费make/pkg-config；不是Gradle自身提供 |
| `python3` | Python **≥3.9**；[Allure bridge](../../tools/allure_report.py)使用标准库 | Gradle直接调用python3；Windows `py`不自动等价；普通bridge无需pip包 |
| `python3 tools/kotlin_architecture_d0.py …`、`python3 tools/tests/test_*.py` | 仓库检查器/测试文件 + Python解释器 | 检查器不是pip包；`--help`/指定现有测试按本批改动面选择 |
| `allure` | npm `allure` **3.33.0**：[bridge VERSION](../../tools/allure_report.py) | 原Gradle门自动安装到`build/tooling/allure/node_modules/.bin/allure`；是Node版Allure3，不是Java版Allure2 |
| `rustup`、`rustc`、`cargo` | rustup；[Cargo.toml](../../tools/extract_rs/Cargo.toml) MSRV **1.97.1**，依赖由Cargo.lock锁定 | 仅retained extractor；`cargo test --release` cwd为`tools/extract_rs`，根目录没有Cargo.toml |
| CDA、作者自检、design-critic | 仓库[skills](../../tools/engineering/skills/)文本入口 | 无独立可执行命令，也不要求pip/npm再次安装 |

### 构建与测试命令的调用链

验证判据仍只在[工程规范§8.1](engineering-standards.md#engineering-gate-commands)维护；这里解释既有命令消费什么，避免缺依赖时盲装工具。

| 现有操作 | 命令来源与调用链 |
|---|---|
| `./gradlew --version` / `./gradlew javaToolchains` | Wrapper → Java launcher/daemon criteria → 已配置JDK；仅环境/工具链定位，不是产品通过 |
| `./gradlew nativeTestHost --console=plain` | 根Gradle task → host CMake/Ninja/compiler → CTest → Python bridge → Node/Allure；配置阶段仍消费Android工程 |
| `./gradlew nativeBuildAndroid --console=plain` | Gradle/AGP → SDK/NDK/CMake → Diagnose plugin producer → APK边界检查 |
| `./gradlew :app:testDebugUnitTest` | AGP/Kotlin/JDK21 → app测试，当前也消费builtin plugin资产 |
| `./gradlew :app:verifyDebugUnitTestXml :app:verifyReleaseUnitTestXml` | 原任务名 → 测试producer与Python/Allure结果解释；不再手写统计 |
| `./gradlew :app:assembleDebug` | AGP/SDK/NDK及插件producer → debug APK；普通本地构建无需团队发布密钥 |
| `cd tools/extract_rs && cargo test --release` | Cargo → rustc/linker/native依赖；只在extractor改动面适用时运行 |
| `python3 tools/cmp_disasm.py …` | 仓库Python脚本 → `LLVM_OBJDUMP`或PATH/NDK的llvm-objdump；仍是可选诊断，非门禁 |
| `…/allure agent query --from <报告目录> summary` | 已由原门安装的Allure3 → 已生成报告；不替代原测试退出状态和当前producer证据 |

### 系统包清单

planning安装左列；android/full在同一包安装命令中追加右列。

| 系统 / 命令来源 | planning 包 | android/full 追加包 |
|---|---|---|
| Homebrew：`brew install` | `git ripgrep python ca-certificates` | `cmake ninja`；compiler来自 Apple CLT |
| Debian/Ubuntu APT：`sudo apt-get update`，`sudo apt-get install -y` | `git ripgrep python3 ca-certificates curl tar xz-utils unzip` | `build-essential cmake ninja-build pkg-config` |
| Fedora DNF：`sudo dnf install -y` | `git ripgrep python3 ca-certificates curl tar xz unzip` | `gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config` |
| openSUSE Zypper：`sudo zypper --non-interactive install` | 同 Fedora planning 列 | `gcc gcc-c++ make cmake ninja pkg-config` |
| Arch Pacman：`sudo pacman -S --needed --noconfirm` | `git ripgrep python ca-certificates curl tar xz unzip` | `base-devel cmake ninja pkgconf` |
| Windows：系统 `wsl.exe --install --distribution Ubuntu-24.04 --no-launch` | 安装/启用 WSL2 Ubuntu，随后使用上述 APT分支 | 同 Ubuntu，不混装 Windows compiler |

系统包管理器会处理所列软件包及其依赖。macOS `bash`/`curl`/`tar`/`unzip` 来自系统，Linux相应缺项由上表补齐。安装器Python仅用标准库，不依赖 pip、requests、jq 或 uv。

### 安装选项与文件位置

| 选项 / 位置 | 用途 |
|---|---|
| `--accept-licenses` / PowerShell `-AcceptLicenses` | 明确允许自动接受Android SDK许可证；默认交互确认 |
| `--system-image` / PowerShell `-SystemImage` | 安装指定镜像和emulator，不创建或启动AVD |
| `--plan` | 仅显示需求，不联网、不安装；此预览需要已有Python3 |
| `~/.local/share/ghostlock-development/` | Node、JDK、默认SDK及专用Gradle user home |
| `build/engineering/env-<profile>.sh` | 当前checkout/profile的激活文件，设置PATH及对应工具环境变量 |
| `tools/engineering/node_modules/` | lockfile安装的工程CLI |
| `CARGO_HOME` / `RUSTUP_HOME` | 既有设置或rustup默认目录，不更改全局Rust默认版本 |

安装器源码：[Shell入口](../../tools/engineering/install/install.sh)、[Windows入口](../../tools/engineering/install/install.ps1)、[共用实现](../../tools/engineering/install/install.py)。Node选择24系列最新LTS patch；JDK按仓库major从Adoptium获取；Android CLI按Google元数据匹配OS/架构。下载按上游校验值核对。SDK CMake3.22.1是安装器兼容选择，不是仓库精确pin；Rust安装manifest MSRV对应版本，不表示只允许该版本。

更新工程依赖时使用 `npm --prefix tools/engineering ci`，版本升级修改原package/lockfile。保留OpenSpec本地schema与skill适配，避免上游生成器覆盖为另一套任务清单。

### 安装器维护验证

仅修改安装器时使用：

```sh
bash -n tools/engineering/install/install.sh
python3 tools/tests/test_development_install.py
bash tools/engineering/install/install.sh --plan --profile planning
bash tools/engineering/install/install.sh --plan --profile android
```

回归与只读预览不等于干净OS实装。当前Windows PowerShell运行、各OS全新安装、产品构建及设备验证仍为NOT_RUN。[CI](../../.github/workflows/build.yml)的JDK21启动环境与daemon criteria25分工不同，也不替代跨平台安装验证。

### 官方来源（按工具查阅）

- [Apple CLT](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools)、[Homebrew安装前提](https://docs.brew.sh/Installation)。
- [Ubuntu APT](https://ubuntu.com/server/docs/how-to/software/package-management/)、[Fedora C++包](https://developer.fedoraproject.org/tech/languages/c/cpp_installation.html)、[openSUSE Zypper](https://doc.opensuse.org/documentation/tumbleweed/zypper/)、[Arch Pacman](https://wiki.archlinux.org/title/Pacman)。
- [WSL安装](https://learn.microsoft.com/windows/wsl/install)、[WSL USB设备](https://learn.microsoft.com/windows/wsl/connect-usb)。
- [Node 24官方归档与校验文件](https://nodejs.org/download/release/latest-v24.x/)、[Adoptium API下载说明](https://github.com/adoptium/api.adoptium.net/blob/main/docs/cookbook.adoc)、[Gradle JDK自动发现](https://docs.gradle.org/current/userguide/toolchains.html)。
- [Android SDK下载](https://developer.android.com/studio)、[sdkmanager](https://developer.android.com/tools/sdkmanager)、[NDK安装](https://developer.android.com/studio/projects/install-ndk)、[AVD](https://developer.android.com/studio/run/managing-avds)。
- [Rustup安装](https://rust-lang.github.io/rustup/installation/index.html)、[CMake](https://cmake.org/download/)、[Ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages)。

### 本文结构参考

- [Firefox Linux build](https://firefox-source-docs.mozilla.org/setup/linux_build.html)：准备系统、bootstrap、开始构建的顺序。
- [Chromium Linux build](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/linux/build_instructions.md)：先交代前置条件和工作目录，再给具体操作。
- [Tauri prerequisites](https://tauri.app/start/prerequisites/)：按操作系统组织依赖入口。

只借鉴文档组织方式，不引入这些项目的工具、硬件要求或构建流程。
