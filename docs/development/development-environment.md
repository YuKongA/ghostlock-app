# 开发环境：命令来源与从零安装

本文负责环境安装与命令定位，不定义产品支持状态、批准或新门禁。盘点日期 **2026-11-22**。以下命令均从仓库根目录执行，另注工作目录者除外。按任务读对应部分，不要求全文安装。

- 从系统自带工具开始：[一键安装](#environment-install)。macOS 额外假设已安装 Homebrew；Linux 假设常规 workstation + GNU 用户空间；Windows 假设 Windows 11 系统 PowerShell/WSL 组件入口。
- 不知道命令从哪里来：[命令来源表](#environment-command-sources)。有 Wrapper/本地 npm CLI 的工具不另装全局版。
- 只做文档/计划：[规划工具](#planning-tools)；构建：[Android](#android-setup)；设备/Rust：[按需工具](#optional-tools)。
- `plugin/**` 与 Allure bridge 已纳入 Git；以本 checkout 的构建配置为准。文件缺失应报告 checkout 不完整，不造占位实现。环境准备成功不表示当前重构树通过产品验收。

<a id="environment-install"></a>

## 1. 从零安装：选择一档执行

安装源码集中在 [`tools/engineering/install`](../../tools/engineering/install/)：[`install.sh`](../../tools/engineering/install/install.sh)负责系统前置包，[`install.py`](../../tools/engineering/install/install.py)负责共同下载/配置，Windows [`install.ps1`](../../tools/engineering/install/install.ps1)进入 WSL2 后执行同一个安装器。它们是安装工具，不是新的构建或审核流程。

| 参数 | 安装内容 | 不执行 |
|---|---|---|
| `--profile planning` | Git、rg、Python（安装器）、Node 24 LTS、工程 npm lockfile 中的 CLI | 不读 Android/Rust 配置，不装 JDK/SDK |
| `--profile android`（默认） | planning + host C/C++、CMake/CTest、Ninja、JDK daemon/toolchain、SDK platform/Build Tools/NDK/SDK CMake/platform-tools | 不创建或启动 AVD，不部署设备 |
| `--profile full` | android + rustup、manifest MSRV 对应 Rust/Cargo | 不编译 extractor，不跑任何产品门 |
| `--system-image 'system-images;…'` | android/full 额外安装 emulator 和明确指定的系统镜像 | 不替用户选择目标 API/ABI，不声称 AVD 等同真机 |
| `--accept-licenses` | 明确同意由脚本接受 Android SDK 许可证；省略则交互确认 | 不改其它许可/授权/项目批准 |
| `--plan` | 只读显示本 checkout 的版本需求；此预览需要已有 Python3 | 不先安装 Python，不联网、不写文件、不改系统包 |

### 1.1 先取得仓库，不假设 Git 已安装

Git 尚未安装时，从项目代码托管页面用浏览器下载源码 ZIP并解压，再从解压目录执行下面安装命令；ZIP中的安装器可准备环境，但没有 Git 历史，不用于计算正式 versionCode。Git 可用后，用项目实际 clone URL 执行 `git clone <仓库URL>`，进入 clone 后重跑所需安装档，使用该 checkout 输出的激活路径。不要把 ZIP 冒充完整 clone，稳定 versionCode 仍需完整历史。

### 1.2 macOS：Apple Silicon / Intel

在系统 Terminal 中执行（无需预装 Python、Node、Java或 Android Studio）：

```sh
bash tools/engineering/install/install.sh --profile android
```

`brew` 不在 PATH 时，脚本寻找 `/opt/homebrew/bin/brew`、`/usr/local/bin/brew`。缺 Xcode Command Line Tools 时会打开 Apple 安装入口并退出 3；完成系统对话框后重跑同一命令。Homebrew 本身不由本脚本重复安装。脚本不自动安装 Rosetta，不以 x64 SDK 冒充 Apple Silicon SDK。

### 1.3 Linux：常规可变 workstation

Ubuntu **24.04+**、Debian **13+**、Fedora **43+**、openSUSE Tumbleweed、当前 Arch 使用同一命令，按系统已有包管理器分支；这些是提供脚本的目标平台，不是本机实装通过声明：

```sh
bash tools/engineering/install/install.sh --profile android
```

非 root 用户需要 `sudo` 权限及密码；只有系统包阶段使用 sudo，后续工具归当前用户。不要用 `sudo bash install.sh` 把全部环境装到 root home。不覆盖 apt/dnf/zypper 仓库配置，不关闭 TLS，不自动全系统升级。Arch 使用现有一致的软件包数据库；出现镜像404/过期数据库时，先按发行版策略维护系统，安装器不自行执行部分升级。Atomic/Silverblue、Alpine/musl、NixOS、容器 minimal image 不等同此处的 workstation，不套用这些包命令。

Android 的 Linux host 预编译工具按 **x86_64** 路线提供；Linux arm64 可运行 planning 档，android/full 在安装前明确停止，不假装 NDK可用。

### 1.4 Windows 11 x64：系统 PowerShell → WSL2 Ubuntu

现有 host CMake 明示 POSIX，Gradle 使用 `python3`，Allure bridge 使用无 `.cmd` 的本地 CLI。**本安装入口选择 WSL2 Linux 工具链，不宣称 Windows 原生全链路兼容。** 不依赖用户预装 winget、Chocolatey、Git Bash、Python或 Node。

在项目目录的 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\engineering\install\install.ps1 -Profile android
```

首次启用 WSL 时按系统提示使用管理员 PowerShell、重启；随后执行 `wsl -d Ubuntu-24.04` 完成 Linux 用户初始化，再重跑同一安装命令。此类 OS 重启/首次用户创建是明确的续跑点，不写成无人值守一次必成功。`-AcceptLicenses`、`-SystemImage 'system-images;…'`对应 Linux 参数。Windows ARM64 不属于此 Android 安装目标。

WSL 建议将正式 Git checkout 放在 Linux home，包和 SDK也装在 Linux文件系统，不混用 Windows SDK/NDK。若现有 `local.properties` 指向 Windows盘 SDK，脚本提前停止并保留它；应在独立 Linux checkout 配置，不能将 Windows SDK 目录喂给 Linux sdkmanager。设备 USB转接/Windows emulator互联另见[设备说明](#optional-tools)，本脚本不修改宿主USB、防火墙或虚拟化配置。

### 1.5 安装器实际执行的系统包命令

先按 planning 列安装；android/full **在同一条 install 中追加**右列。包名在此解释，执行列表以对应脚本为准。

| 系统 / 命令来源 | planning 包 | android/full 追加包 |
|---|---|---|
| Homebrew：`brew install` | `git ripgrep python ca-certificates` | `cmake ninja`；compiler来自 Apple CLT |
| Debian/Ubuntu APT：`sudo apt-get update`，`sudo apt-get install -y` | `git ripgrep python3 ca-certificates curl tar xz-utils unzip` | `build-essential cmake ninja-build pkg-config` |
| Fedora DNF：`sudo dnf install -y` | `git ripgrep python3 ca-certificates curl tar xz unzip` | `gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config` |
| openSUSE Zypper：`sudo zypper --non-interactive install` | 同 Fedora planning 列 | `gcc gcc-c++ make cmake ninja pkg-config` |
| Arch Pacman：`sudo pacman -S --needed --noconfirm` | `git ripgrep python ca-certificates curl tar xz unzip` | `base-devel cmake ninja pkgconf` |
| Windows：系统 `wsl.exe --install --distribution Ubuntu-24.04 --no-launch` | 安装/启用 WSL2 Ubuntu，随后使用上述 APT分支 | 同 Ubuntu，不混装 Windows compiler |

系统包管理器可能安装/更新列出的包及其依赖；这不是纯只读检查。macOS `bash`/`curl`/`tar`/`unzip` 来自系统，Linux相应缺项由上表补齐。安装器Python仅用标准库，不依赖 pip、requests、jq 或 uv。

### 1.6 下载、持久化与激活

- Node：官方 `latest-v24.x` 的匹配 host/arch tar.gz，核对官方 `SHASUMS256.txt`；**24系列最新修订版是安装策略，不是仓库精确pin**，npm `--engine-strict`进一步检查整个lockfile。
- JDK：Adoptium API按仓库声明的major、OS和架构获取 Temurin JDK，核对API提供的SHA256；完整版本目录复用，新目录同盘暂存后落位，不覆盖使用中的JDK。版本/下载入口由安装器打印或保留版本化目录，不改系统 Java alternatives。
- Android CLI：Google `repository2-3.xml` 的 `cmdline-tools;latest`，同时选择 host-os/host-arch，按上游 checksum 类型校验；SDK具体包读取仓库配置，SDK CMake **3.22.1**是本安装器兼容选择，不冒充项目精确pin。
- Rust：仅full，官方下载 `rustup-init` 并核对SHA256，`--no-modify-path --default-toolchain none`；安装 manifest MSRV对应toolchain。MSRV并非唯一可用版本，不更改全局Rust默认值。
- 下载工具落在 `~/.local/share/ghostlock-development/`；Rust使用既有 `CARGO_HOME`/`RUSTUP_HOME`或rustup默认目录。保留已有 `local.properties`，有合法 `sdk.dir`就使用它，否则设置 `ANDROID_HOME` 指向受管SDK，不写弃用 `ndk.dir`。
- 每个 checkout/profile 单独生成未跟踪的 `build/engineering/env-<profile>.sh`。**安装结束按打印路径执行 `source`，后续命令在该 shell 中运行**。不修改 shell rc，不覆盖另一 checkout/profile 的激活入口。
- 激活文件设置 PATH、JAVA_HOME、JDK_TOOLCHAIN_HOME、ANDROID_HOME；专用 `GRADLE_USER_HOME`仅配置 `org.gradle.java.installations.fromEnv=JAVA_HOME,JDK_TOOLCHAIN_HOME`，不修改全局 `~/.gradle/gradle.properties`。IDE需使用相同Gradle user home/JDK，不能只配置终端。
- 失败立即返回非零；系统包已安装的部分保留，修正具名失败后重跑。不会重置Git、认领Beads、运行bootstrap/测试、启动AVD或安装APK。

```sh
# 已有Python时先看只读安装需求
bash tools/engineering/install/install.sh --plan --profile android
# 实际安装后，使用本checkout对应入口
source build/engineering/env-android.sh
```

<a id="environment-command-sources"></a>

## 2. 开发命令从哪里来

### 2.1 操作系统与安装底座

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

### 2.2 仓库工具、编译器与SDK

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

### 2.3 实际任务调用链（不是额外必跑清单）

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

<a id="android-setup"></a>

## 3. Android：安装后如何找到工具

```sh
source build/engineering/env-android.sh
java -version
"$JDK_TOOLCHAIN_HOME/bin/javac" -version
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --list_installed
adb version
cmake --version
ctest --version
ninja --version
```

SDK包安装由脚本读取配置；手工排障需要同一入口时：

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --list
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses
```

默认交互许可证；没有明确同意不自动管道yes。`local.properties`若已有其它配置保持原样。未使用安装器时可按实际绝对路径配置`sdk.dir`，或设置ANDROID_HOME；不要写`ndk.dir`，AGP通过module的`ndkVersion`选择NDK。下载源包括Google、Adoptium、Node、Gradle、Maven Central、Gradle Plugin Portal、JitPack、npm与rustup；失败按实际URL/包名排查，不降版本或关门禁绕过。

<a id="optional-tools"></a>

## 4. 按需设备、Rust与平台边界

- **AVD**：先通过`sdkmanager --list`选择与获准测试目标匹配的完整`system-images;...`包，传给安装器`--system-image`；然后用`avdmanager create avd -n <名称> -k '<同一镜像包>'`创建，`emulator -avd <名称>`启动。Linux硬件加速需要KVM权限，macOS使用Hypervisor.Framework；脚本不擅自调整组/内核/虚拟化设置。镜像可用不代表AVD已启动或产品已验收。
- **真机**：`adb devices -l`检查连接；之后`adb -s <SERIAL> ...`只对明确设备操作。Linux可能需要发行版USB/udev规则；Windows/WSL需要宿主USB转接（如Microsoft文档中的usbipd-win）或获准无线调试。设备授权、驱动与网络不能由“包安装成功”代替；安装器不运行fastboot flash、root或攻击操作。
- **Rust**：full档激活文件设置RUSTUP_TOOLCHAIN，`rustc --version`、`cargo --version`确认；不用全局`rustup default`替别的项目改默认版本。普通Android开发不需要Rust。
- **host compiler**：C11/C++23的真实编译能力以适用host测试验证，不以`c++ --version`替代。根Gradle仍支持`-Pcmake=...`、`-Pctest=...`、`-Pninja=...`指定现有工具，无需创建包装器。
- **现有接线问题**：根`verifyNativeTestInventory`仍扫描旧app/tools范围，未覆盖整个plugin树；这是检查接线问题，不是环境缺包，本安装工作不改它。
- **CI**：[workflow](../../.github/workflows/build.yml)使用Zulu21启动环境并显式安装NDK；daemon criteria仍25。CI未显式完整配置所有Node/Python/platform/Build Tools，不据此声称跨平台bootstrap或产品门已通过。

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

Beads 的安装阶段或首次调用会取得对应固定版本的原生 binary；npm 版本的 install-script 策略可能不同，最终以 `bd -- version` 实际执行成功为准，不以 npm 退出 0 或跳过脚本冒充可用。OpenSpec 项目适配、CDA、作者自检和独立审查入口位于 `tools/engineering/skills/`，由根 AGENTS.md 按任务指向；Agent 直接读取对应 SKILL.md，不依赖 `.agents/skills` 自动发现，不重复安装。命令报缺依赖时先 `npm --prefix tools/engineering ci`，不用会临时下载最新版的裸 `npx`。OpenSpec schema为ghostlock，proposal/design/specs为设计正文、tracks:null不生成tasks.md；工具分工只在[文档规范](documentation-standards.md#planning-tool-authority)维护。

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

首次定位设计时，可只投影导航字段，避免展开长 notes（`<id>` 替换为实际任务 ID）；批准与实际证据仍按需读取原 notes/comments，投影不证明实施获准：

```sh
npm --prefix tools/engineering run --silent bd -- show <id> --json --brief-deps |
  node -e 'const rows = JSON.parse(require("node:fs").readFileSync(0, "utf8")); const fields = ["id", "title", "status", "spec_id", "acceptance_criteria", "dependencies"]; console.log(JSON.stringify(rows.map(row => Object.fromEntries(fields.map(key => [key, row[key]]))), null, 2));'
```

以上命令从仓库根目录执行；npm 脚本的工作目录是 `tools/engineering`，所以 CLI 输出文件参数（如 `-o .beads/issues.jsonl`）相对此目录，而 `spec_id` 和设计中的源码路径仍相对仓库根目录。

设计正文只维护在OpenSpec，Beads `design`留空、`spec_id`指向设计文件/锚点，验收字段只引用适用矩阵。不要把整个JSONL当日常阅读入口。title / `external_ref` 保留语义编号；候选需求用 `deferred`，具名条件缺失用 `blocked`。只领取获准工作，close 不会替你验证行为或批准范围。人类查进度可运行 `npm --prefix tools/engineering run --silent bd -- list`，不必翻旧计划历史。

### 保存、交接与升级

- 任务的写入 authority 是 `tools/engineering/.beads/embeddeddolt` 中的原生数据库，CLI 维护其事务历史。`tools/engineering/.beads/issues.jsonl` 是 CLI 生成的任务交接投影，禁止手编；新 clone 的 `bootstrap` 导入该投影。它保留任务描述、OpenSpec指针、验收引用、notes、状态、依赖和注释；OpenSpec设计文件必须同批提交，不包含完整 Dolt 分支历史，不是跨机器并发同步。
- 已配置自动导出，间隔为 **1ms**（上游把 `0s` 解释为默认60s，不表示关闭节流）。在交付/提交前执行一次 `npm --prefix tools/engineering run --silent bd -- export -o .beads/issues.jsonl`，确认最后一次更新已落盘；正常提交它和原生配置，不提交数据库文件。
- 当前只配置本地任务协作，没有发布 Dolt remote。原生 CLI 的“无 remote”提示不证明任务丢失，也不授权 Agent 自动 push；真正跨机协作时使用官方 Dolt remote/push/pull，而不是手写 JSONL 合并器。
- 自动 backup、auto-push、export git-add 均关闭，未安装 Git hooks。正常本地 commit 由 Agent 核对本批范围和适用验证后主动完成。fresh clone 用 **bootstrap，不重复 init**：Beads1.3.4 的 `init` 会自动提交 agent 文件，`--skip-agents --skip-hooks` 不会取消此行为。
- 升级时精确修改 npm 版本/lockfile、CDA 来源与实际适配；保留OpenSpec设计/Beads执行的本地skill与schema适配；不直接用生成器更新覆盖成Markdown任务清单。只验证受影响的工具行为与既有门禁，不升级产品工具链。

## 6. 安装器验证与证据边界

安装器只验证版本、CLI可启动、SDK安装命令退出码，不执行产品门，也不自动Beads bootstrap。新clone需要任务库时由[规划工具](#planning-tools)的现有bootstrap命令导入，已有任务库不重建。

本次脚本修改的验证命令：

```sh
bash -n tools/engineering/install/install.sh
python3 tools/tests/test_development_install.py
bash tools/engineering/install/install.sh --plan --profile planning
bash tools/engineering/install/install.sh --plan --profile android
```

以上回归验证参数拒绝、只读预览、版本提取、SDK架构选择、校验失败与原配置保全，**不等于在干净OS完成安装**。macOS/Linux/Windows全新系统实装、Windows PowerShell运行、AVD/真机及产品门在未实跑时都保持 **NOT_RUN**。不为安装改真实合同、任务依赖或批准状态。

### 官方来源（按工具查阅）

- [Apple CLT](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools)、[Homebrew安装前提](https://docs.brew.sh/Installation)。
- [Ubuntu APT](https://ubuntu.com/server/docs/how-to/software/package-management/)、[Fedora C++包](https://developer.fedoraproject.org/tech/languages/c/cpp_installation.html)、[openSUSE Zypper](https://doc.opensuse.org/documentation/tumbleweed/zypper/)、[Arch Pacman](https://wiki.archlinux.org/title/Pacman)。
- [WSL安装](https://learn.microsoft.com/windows/wsl/install)、[WSL USB设备](https://learn.microsoft.com/windows/wsl/connect-usb)。
- [Node 24官方归档与校验文件](https://nodejs.org/download/release/latest-v24.x/)、[Adoptium API下载说明](https://github.com/adoptium/api.adoptium.net/blob/main/docs/cookbook.adoc)、[Gradle JDK自动发现](https://docs.gradle.org/current/userguide/toolchains.html)。
- [Android SDK下载](https://developer.android.com/studio)、[sdkmanager](https://developer.android.com/tools/sdkmanager)、[NDK安装](https://developer.android.com/studio/projects/install-ndk)、[AVD](https://developer.android.com/studio/run/managing-avds)。
- [Rustup安装](https://rust-lang.github.io/rustup/installation/index.html)、[CMake](https://cmake.org/download/)、[Ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages)。

Agent 接手提示，可直接复用：

> 从AGENTS.md按任务定位环境工具。先查命令来源与checkout版本权威，只装所需profile；安装前保全local.properties，安装后激活本checkout环境。区分环境缺失、仓库接线错误和行为失败。不要全量跑门、改生产代码或任务状态；回报实际命令、退出码和未运行平台，不将静态/模拟测试写成实装PASS。
