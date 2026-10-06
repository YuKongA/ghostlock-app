# GhostLock-App

> English: [README.md](README.md)

## 文档

- [Kernel Profile 适配指南](docs/kernel_profiles/README_ZH.md) —— 如何支持一款新内核。GhostLock 按精确 `uname -r` 匹配，未匹配的内核直接拒绝运行并在 App 顶部显示状态。内置配置位于 `app/src/main/assets/kernel_profiles/`：每个 release 一个 HOCON 文件，`index.conf` 保存运行索引，`<major.minor>-template.conf` 提供各内核大版本模板。
- [支持设备列表](docs/kernel_profiles/SUPPORTED_DEVICES_ZH.md) —— 内置内核清单。
- [公共执行默认值](docs/kernel_profiles/defaults_ZH.md) —— 每个 `execution` 字段的默认值与取舍。
- [Profile 结构文档](docs/kernel_profiles/PROFILE_SCHEMA_ZH.md) —— profile 的完整结构、字段语义与数据流。
- [新增组件指南](docs/development/adding-a-component.md) —— 为 native 添加新 backend / terminal / route 的开发者指南。

新增设备的完整流程、内核版本模板跳转和公共参数理由见[Kernel Profile 适配指南](docs/kernel_profiles/README_ZH.md)。

明确标记为**需要 Shizuku**的固件通过 shell UserService 执行。先使用 ADB 启动 Shizuku，再点击顶部支持状态区域授权；其余固件沿用应用内执行路径。

## 快速开始

打开 **GhostLock** 点击 **执行**。需先装 KernelSU（`me.weishu.kernelsu`）、ReSukiSU（`com.resukisu.resukisu`）或 KowSU（`com.kowx712.supermanager`）以提供 `ksud`；缺 `ksud` 时 W1/W2 仍可拿到 uid 0，但不会加载模块。

执行链由 `Pipeline<Backend, Terminal>` 在编译期固定。选择是 **`backend.<id>.steps` 里的唯一组合 token**；`contract::kCombinationCatalog` 是唯一权威——**12 token = 7 可用 + 5 计划**。可用：`cve_2026_43499` 的六个 `{multicast_waiter, select_stack, tcp_zerocopy} × {rootchild, shizuku}` 与 `cve_2026_43284 × umh`；计划项（`available=false`）：`{mcast, pselect, tcp}_umh` 与 `cve_2026_43284 × {rootchild, shizuku}`——可解析，但选择门禁拒绝、App 置灰。两个 terminal（`root_child`、`umh_forward`）与两个 backend（`cve_2026_43499`、`cve_2026_43284`）均可用，其余 CVE 为纯头占位。route（`select_stack` / `tcp_zerocopy` / `multicast_waiter`）是**由 token 派生**的 backend 内部策略，不是独立组件。路线是双核竞争：6.6/6.12 树形 waiter 内核上主线程跑 `select` 爆破、consumer 线程扰动 waiter 优先级；6.1 紧凑 waiter 内核上主线程改走 `getsockopt(TCP_ZEROCOPY_RECEIVE)` 打洞页写入；5.15 内核走 multicast waiter 路线。CPU 对同样由解析后的 profile 决定。

## 命令行调试

adb/shell 环境无 seccomp 过滤，会跳过 W3，适合快速验证：

```powershell
make -C src ghostlock
./gradlew exportKernelProfiles
adb push build/native/ghostlock /data/local/tmp/ghostlock
adb push build/kernel-profiles/<release>.bin /data/local/tmp/profile.bin
adb shell chmod 755 /data/local/tmp/ghostlock
adb shell /data/local/tmp/ghostlock --load-prebuilt-profile /data/local/tmp/profile.bin
```

CLI 只承载**传输 / 运行控制 / 安全 / 可观测**（S4 R2b）：`--ghostlock-app-call`、`--load-prebuilt-profile <bin>`、`--enable-status-record`、`--dump-kernel-log <dir>`、`--force-attack`、`--allow-dev-target`（只放宽**绑定期** carrier 校验），以及只读诊断 `--probe-cve-2026-43284 <ko>` 与 `--plugin-probe <path.so> [--expect-sha256 <hex>]`。**选择与策略绝不来自 CLI**：staged 入口与 `--cve43284-*` 选择器已删除，未知参数直接 fail-closed。

## 偏移量提取

`tools/extract_rs` 从 `boot.img`（可加 `xbl_config.img`）、完整 OTA zip 或指向它的 `http(s)` 链接解析偏移量。kallsyms 传 `--kallsyms`，或省略以直接恢复镜像内嵌表。`pselect_waiter_shift` 与 `off_slide_loggers_0_1` 由内置 arm64 反汇编器推导。联发科镜像没有 `xbl_config.img` 且通常无内嵌 BTF：物理加载地址由 kallsyms `_text` 推导（可用 `--phys` 覆盖）。

```powershell
Push-Location tools/extract_rs
cargo build --release
Pop-Location
build/extract/release/ghostlock-extract.exe boot.img --xbl-config xbl_config.img --format conf --out profile.conf
build/extract/release/ghostlock-extract.exe OTA.zip --format conf --out profile.conf
```

提取结果使用 `--format conf` 输出：flatten（无 `include`、凭据/KernelSnitch 常量内联）的自包含 profile。提取器把镜像实际获得的所有字段都写出，未获得的字段直接省略，不会用相邻内核族的猜测值（未验证族的 6.6、缺省 `-2`、5.15 multicast 常量、phys 默认）补齐；route 由 `--analysis` 证据建议、`--route` 可覆盖。输出一律是 **unverified candidate**：可导入、可解析，缺失或无效字段由 App 在执行前校验拦截，不能仅凭生成成功声明设备支持。5.x 还会从 `init_cred` 推导凭据引用修复值、从 BTF 推导 multicast 几何（见 `docs/analysis/extractor-5x-derivation-plan.md`）。`--plugin-descriptor <probe-stdout.tsv>`（可重复、**仅 `--format conf`**）让对策插件自己的探针描述去填充 `plugin.<id>.extract.<key>`：键名与类型来自描述符，取值来自正在产出的 profile（R1）、镜像 BTF（R3）或 kallsyms（R2）。它只写 **`extract` 片段**（不是 wire 文档），追加在 `countermeasure` 之后；`required` 解析失败即中止，`optional` 缺失则省略、**绝不用 default 顶替**；不带该开关时输出**逐字节不变**。`--format json` 保留给 v1 导入路径。新增内置配置时以对应大版本模板为基础补齐和验证字段，再将独立 `.conf` 登记到 `kernel_profiles/index.conf`。旧 C `offsets.h` 注册表已经弃用并移除。

### 联发科

联发科镜像没有 `xbl_config.img`，通常也没有内嵌 BTF，提取器无法从镜像推导两个物理地址
（`kernel_phys_load`、`kernel_phys_offset`），会把它们留成 `null`。运行时按 SoC 公式回退，在联发科上
会在 W1 失败。请先在已 root 的设备上运行单独的 `tools/mtk-phys/` 提取器（读取 `/proc/iomem`），
再把两个值填入 App 的高级参数覆盖。参见 [MEDIATEK_ZH.md](docs/kernel_profiles/MEDIATEK_ZH.md)。

### 前置检查

提取器在提取偏移量前先反汇编 `remove_waiter()`。已包含修复的内核以退出码 `6` 拒绝；仅未修复内核继续。

### 手机端运行

完整 OTA 可直接在手机上分析：传完整包时自动提取 `boot` + `xbl_config`。在 App 沙箱内运行时，`--work-dir` 必须指向 App 可写目录。交叉编译后 push：

```powershell
rustup target add aarch64-linux-android
$ndk = "$env:ANDROID_HOME\ndk\<version>\toolchains\llvm\prebuilt\windows-x86_64\bin"
$env:CC_aarch64_linux_android = "$ndk\aarch64-linux-android35-clang.cmd"
$env:AR_aarch64_linux_android = "$ndk\llvm-ar.exe"
$env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = $env:CC_aarch64_linux_android
Push-Location tools/extract_rs
cargo build --release --target aarch64-linux-android
Pop-Location
adb push build/extract/aarch64-linux-android/release/ghostlock-extract /data/local/tmp/
adb shell /data/local/tmp/ghostlock-extract /sdcard/OTA.zip
```

### 外部导入偏移，免去重新构建应用

新增内核不再需要重新打包 App：点击 **导入 offsets.conf (HOCON)** 选择提取器产出的扁平 `.conf`，旧 JSON 报告仍可通过 **导入 offsets.json (v1)** 导入。v1 JSON 由 App 侧转换，无需再把文件推到设备；native 接收 App 经 stdin 传入的 GLKv3 文档（MessagePack 根 map，`schema == 3`；已删除的 v2 二进制一律拒绝），并先按当前 `uname -r` 匹配解析后的 profile，匹配成功才视为受支持。多次导入会合并；新文件含已存内核时，App 会先询问是否覆盖。

App 也能直接生成这份 profile：**解析完整包链接**（完整 OTA zip 的 `http(s)` 链接）与 **解析镜像**（`boot.img` + 可选 `xbl_config.img`）都在 App 进程内跑提取器，成功后把一份扁平 `.conf` 写入 App 数据目录：

```hocon
# GhostLock kernel profile（HOCON，canonical owner-qualified 布局）。
ghostlock {
  schema_version = 3
  release = "6.12.38-android16-5-g844001fb8721-ab14552068-4k"
  selection {
    backend  = "cve_2026_43499"
    terminal = "root_child"
  }
  common { kernel_major = 6 }
  platform {
    abi {
      kernel { kernel_phys_load = 0xC7800000 }
      task_struct { prio = 148, cred = 2304 }
    }
  }
  backend {
    cve_2026_43499 {
      steps = "mcast_rootchild"        # 唯一对用户可见的选择 token
      route { multicast_waiter { waiter_shift = 0 } }
      cred { caps_offset = 48, copy_size = 136, caps_count = 5, caps_value = -1 }
      offset { init_task = 37801728, init_cred = 37891184 }
    }
  }
}
```

完整字段表见 [PROFILE_SCHEMA_ZH.md](docs/kernel_profiles/PROFILE_SCHEMA_ZH.md)；提取器产出同一套 canonical 布局。

## 插件（P1）

在设置页导入对策 `.so`：App 把它复制到自己的 no-backup `countermeasures/` 根目录、本地算哈希，并通过**只读 native 探针**读取自描述（`--plugin-probe`，绝不在 JVM 内 `dlopen`）。只有 **enabled=true** 的插件才会以 `plugin.<id>.*` 写进 GLKv3 文档（默认关闭；`params.*` 的具体类型由插件自己的描述符决定；文档里出现 `enabled=false` 一律拒绝）。参考对策插件在独立项目 **`ghostlock-plugin-example`**（内置 ABI 头、`build.sh android|host|abi-check`、双语 README）；本仓库只留 `tools/plugins/README.md` 作为指引——插件作者不应需要 exploit 仓库才能构建。**P1 只交付「声明 → 校验 → 绑定」的 wire 层**：加载模块并按 stage 调用它的运行时**尚未接线**（见 branch-plan 的 task-9，需单独的 L 级设计与真机门禁）。

## 来源与许可证

基于以下项目改写，继承 Apache License 2.0（见 [LICENSE](LICENSE)）：

- [NebuSec/CyberMeowfia](https://github.com/NebuSec/CyberMeowfia)
- [JoinChang/ghostlock-oneplus](https://github.com/JoinChang/ghostlock-oneplus)
- [x-spy/CVE-2026-43499-popsicle](https://github.com/x-spy/CVE-2026-43499-popsicle)

### CVE-2026-43284 参考来源

43284 backend 是独立重写，编写时参考了下列上游项目（**仅参阅、未 vendored**）。其中多个项目
**未提供 LICENSE**，因此本仓库不再分发其源码；如需复用上游代码须先取得其授权：

- [ankitrawatgit/DirtyFrag-Android-Root-Jailbreak](https://github.com/ankitrawatgit/DirtyFrag-Android-Root-Jailbreak)（`de2ab7b`）—— IpSec + crash_dump + libc++ sentry + LKM/UMH 链
- [lsposed/lspromise](https://github.com/lsposed/lspromise)（`0258165`）—— 页缓存 splice 原语
- [diabl0w/DFRoot](https://github.com/diabl0w/DFRoot)（`3050d5b`）—— LKM/UMH（soft_reboot）
- [polygraphene/DFReroot](https://github.com/polygraphene/DFReroot)（`9edc769`）—— 最小 SELinux LKM
- [combeng6th/DirtyInit](https://github.com/combeng6th/DirtyInit)（`3409c35`）—— 无特权 XFRM/IpSec 思路

### Vendored 库

- [ludocode/mpack](https://github.com/ludocode/mpack) —— MessagePack 读/写库，MIT；vendored 于 [`src/lib/mpack/`](src/lib/mpack/)，LICENSE 随代码保留。

