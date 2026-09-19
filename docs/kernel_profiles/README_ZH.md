# 内核 Profile 适配指南

> TODO(profile-suggest-01)：只有内核几何为必填；`recommend_shizuku`、`execution`
> 重试次数与等待/超时时间均为建议值：profile 可以省略或覆盖，缺省时随包默认值
> 即建议值。

## 文件布局

- `app/src/main/assets/kernel_profiles/index.conf`：保存所有内置支持的内核配置文件的列表，新配置文件最终需要添加到此处。通过精确 `uname -r` 匹配。
- `app/src/main/assets/kernel_profiles/execution-tuning.conf`：所有 profile 通过 include 引入的通用 execution 调优；`execution-<route>.conf` 为各路由调优（仅由使用该路由及回退的 profile include）；`credential-6x.conf` 与 `kernelsnitch-6x.conf` 提供 6.x 内核的共享凭据模板与 KernelSnitch 值；`*-template.conf` 为逐字段注释的参考模板，可在调试页手动加载，但不参与设备匹配与自动回退。详见 [公共执行默认值](defaults_ZH.md)。
- `app/src/main/assets/kernel_profiles/<uname-r>.conf`：每个内核细分版本对应的完整 profile；文件名与 内核细分版本 `release` 必须一致。
- `docs/kernel_profiles/templates/`：模板文件夹。包含不同小版本的内核配置模版。

> **HOCON**：所有配置文件按 HOCON（Typesafe Config）解析，支持 `#` 注释、`${?变量}`、
> 尾逗号与 `include "file.conf"`（相对同目录、可嵌套）；JSON 文档仍是合法 HOCON，无需迁移。

## 新设备适配流程

1. 运行 `adb shell uname -r`，保留完整字符串；任何字符差异都会使匹配失败。
2. 确定内核族并打开对应文档：[5.x](templates/kernel-5.x.template_ZH.md)、[6.1](templates/kernel-6.1.template_ZH.md)、[6.6](templates/kernel-6.6.template_ZH.md)、[6.12](templates/kernel-6.12.template_ZH.md)。
3. 复制对应 `.conf`，以完整 内核细分版本 `release` 命名并写入必填字段。
4. 编译 `tools` 下的提取工具，执行 `ghostlock-extract --format json` 提取符号/BTF 数据；逐字段转录。
   - `tools/extract_rs` 从 `boot.img`（可加 `xbl_config.img`）、完整 OTA zip 或指向它的 `http(s)` 链接解析偏移量。kallsyms 传 `--kallsyms`，或省略以直接恢复镜像内嵌表。`pselect_waiter_shift` 与 `off_slide_loggers_0_1` 由内置 arm64 反汇编器推导。联发科镜像没有 `xbl_config.img` 且通常无内嵌 BTF：物理加载地址由 kallsyms `_text` 推导（可用 `--phys` 覆盖）。
5. 校验所有 `off_*` 必填地址非零、task/cred 布局来自同一镜像，并按需设置 `recommend_shizuku`（默认 `0`；仅 5.15 multicast 配置建议开启）。
6. 只在确有设备证据时覆盖 `execution`；否则保留 [公共默认值](defaults_ZH.md)。
7. 在 `index.conf` 增加 `{release,file}`，运行 `jq` 校验、Rust 测试和 `./gradlew clean :app:assembleDebug`。
8. 通过 `./gradlew installDebug` 使用真机在相同环境下、固定核心、单路线重复debug测试；注意测试前控制机身温度以防cpu降频；

## 合并顺序

`defaults.json` → 内置 release JSON → 用户稀疏 override → UI 显式核心选择。后层覆盖前层。Kotlin 生成单个 `active-profile.json`，Native 不再搜索或合并配置。

## 安全规则

- 模板中的 `off_* = 0` 表示“必须提取”，不是可运行默认值。
- task/cred/multicast 布局错误可能造成任意内核内存破坏、黑屏或重启。
- `execution` 调高尝试次数或缩短等待可能显著升温并降低成功率。
- 新 profile 未通过真机门禁前不得提交为“已支持”。
