# 内核 Profile 适配指南

本指南说明如何让 GhostLock 支持一款新内核。

GhostLock 用 `uname -r` 精确匹配内核版本：匹配不到时应用会直接拒绝运行。
因此你通常不需要改代码，只要为新内核补一份 profile（一份 HOCON 配置）。

> 只有内核几何（符号与结构体偏移）是必填项。`recommend_shizuku` 和 `execution`
> 里的重试次数、等待时间都是建议值：profile 可以省略，省略时使用随包默认值
> （见[公共执行默认值](defaults_ZH.md)）。

## 开始之前

- 一台能用 `adb` 连接的目标设备，或与设备同版本的固件包。
- 项目能构建：`./gradlew :app:assembleDebug` 通过。
- 提取工具能编译（[`tools/extract_rs`](../../tools/extract_rs)，Rust）。

## 配置文件放在哪

所有内置配置都在 `app/src/main/assets/kernel_profiles/`：

- `index.conf`：内置内核清单。新 profile 最终要登记到这里。
- `<uname-r>.conf`：一款内核一份完整 profile，文件名必须与 `uname -r` 完全一致。
- `execution-tuning.conf`：所有内核共用的调优值。
- `execution-<route>.conf`：各攻击路线（route）专属的调优值。
  这些 preset 由 resolver 加载；设备 profile 不 include。
- `credential-6x.conf`、`kernelsnitch-6x.conf`：6.x 内核共享的凭据模板与 KernelSnitch 参数。
- `*-template.conf`：带逐字段注释的参考模板。可以在调试页手动加载，但不参与设备匹配。
- `docs/kernel_profiles/templates/`：按大版本整理的模板文档。

这些文件都按 HOCON（Typesafe Config）解析，支持 `#` 注释、`${?变量}`、尾逗号，
以及 `include "file.conf"`（相对同目录、可嵌套）。JSON 仍是合法 HOCON，无需转换。

每个字段的含义、必填矩阵与校验规则见 [Profile 结构文档](PROFILE_SCHEMA_ZH.md)；
下面只讲适配流程，不重复字段说明。

## 适配步骤

1. **确定内核版本。**
   运行 `adb shell uname -r`，把输出完整记下来；任何字符差异都会导致匹配失败。

2. **选择模板。**
   按大版本打开对应文档：
   [5.x](templates/kernel-5.x.template_ZH.md)、
   [6.1](templates/kernel-6.1.template_ZH.md)、
   [6.6](templates/kernel-6.6.template_ZH.md)、
   [6.12](templates/kernel-6.12.template_ZH.md)。

3. **复制成新的 profile。**
   把模板复制到 `app/src/main/assets/kernel_profiles/<uname-r>.conf`，
   文件名与 `release` 必须一致，然后填入必填字段。

4. **提取偏移。**
   编译并运行提取工具：

   ```sh
   cargo build --release --manifest-path tools/extract_rs/Cargo.toml
   build/extract/release/ghostlock-extract boot.img --format json --out offsets.json
   ```

   工具支持 `boot.img`（可附带 `xbl_config.img`）、完整 OTA zip，或指向它们的
   `http(s)` 链接。kallsyms 可以显式传 `--kallsyms`，省略时会尝试恢复镜像内嵌表。
   `pselect_waiter_shift` 和 `off_slide_loggers_0_1` 由内置的 arm64 反汇编器推导。
   联发科（MediaTek）镜像没有 `xbl_config.img`，通常也没有内嵌 BTF：
   物理加载地址由 kallsyms 的 `_text` 推导，必要时用 `--phys` 覆盖。

5. **逐字段转录并自检。**
   字段含义与必填要求见 [Profile 结构文档](PROFILE_SCHEMA_ZH.md)。
   确认所有必填的 `off_*` 非零，task/cred 布局来自同一镜像，然后按需设置
   `recommend_shizuku`（默认 `0`；只有 5.15 的 multicast 配置建议开启）。

6. **确有必要时才改 `execution`。**
   没有真机实测证据就保留[公共默认值](defaults_ZH.md)；也可以先在 App 的参数覆盖页试（见下节），确认后再写回 profile。

7. **登记到 `index.conf`。**
   增加一条 `{release, file}`，然后运行校验与构建：

   ```sh
   jq . app/src/main/assets/kernel_profiles/index.conf
   cargo test --manifest-path tools/extract_rs/Cargo.toml
   ./gradlew clean :app:assembleDebug
   ```

8. **真机验证。**
   用 `./gradlew installDebug` 装到设备上，在相同环境、固定核心、单一攻击路线下反复测试。
   测试前先让机身降温，避免 CPU 降频影响结果。未通过真机验证前，不要把它标记为“已支持”。

## 在 App 内快速修改配置

调参不必每次都改 assets 再重装 App。App 里有两层覆盖，保存后立即生效，并参与下一次运行：

- **参数覆盖页（一般参数覆盖）**：调整 `execution.*` 的调优值，例如尝试次数、等待/超时时间、核心选择。
- **高级配置覆盖页**：修改 `route` / `fallback.to`，以及任意数值路径（以 sparse HOCON 保存），适合试验新的偏移或几何。
- **导出配置**：把合并后的完整 HOCON 写到你选定的目录（SAF），可以直接当作新 profile 的底稿。

覆盖按 release 保存，优先级高于内置 profile 和导入的偏移。各层优先级与存储位置见
[Profile 结构文档](PROFILE_SCHEMA_ZH.md) 第 7 节。给内置库新增内核仍需走上面的流程并提交 assets。

`defaults.json` → 内置 release JSON → 用户稀疏 override → UI 显式核心选择。后层覆盖前层。Kotlin 生成单个 `active-profile.json`，Native 不再搜索或合并配置。

- 模板里的 `off_* = 0` 是相对内核镜像基址的符号偏移，必须自己提取。
- task/cred/multicast 布局写错可能造成任意内核内存破坏、黑屏或重启。
- 调大 `execution` 的尝试次数、调短等待时间会明显升温，并可能降低成功率。
- 新 profile 在通过真机验证前，不要提交为“已支持”。
