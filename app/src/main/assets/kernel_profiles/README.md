# Kernel profile 适配指南 / Kernel Profile Porting Guide

本目录是运行时 profile 的唯一内置数据源。This directory is the sole built-in source of runtime profiles.

## 文件布局 / Layout

- `index.json`：精确 `uname -r` 到独立 JSON 文件的白名单索引；未登记文件不会进入 App。Exact-release allowlist; unlisted files are not packaged into the generated support index.
- `defaults.json`：所有 release 共用的执行参数，详见 [defaults.md](defaults.md)。Shared execution tuning; see [defaults.md](defaults.md).
- `<uname-r>.json`：一个内核 release 一个完整 profile；文件名与 `release` 必须一致。One complete profile per kernel release; filename and `release` must match.
- `templates/`：不会进入运行索引的内核族模板。Non-runtime family templates.

## 新设备适配流程 / Porting a New Device

1. 运行 `adb shell uname -r`，保留完整字符串；任何字符差异都会使匹配失败。Capture the complete release string; any difference intentionally prevents a match.
2. 确定内核族并打开对应说明：[5.x](templates/kernel-5.x.template.md)、[6.1](templates/kernel-6.1.template.md)、[6.6](templates/kernel-6.6.template.md)、[6.12](templates/kernel-6.12.template.md)。Choose the matching family guide.
3. 复制对应 `.template.json`，以完整 release 命名并写入 `release`。Copy the template, name it after the exact release, and replace `release`.
4. 用 `ghostlock-extract --format json` 提取符号/BTF 数据；逐字段转录，不得直接复用另一固件的 `off_*`。Extract metadata and transcribe every field; never reuse symbol offsets merely because the major version matches.
5. 校验所有 `off_*` 必填地址非零、task/cred 布局来自同一镜像，并依据运行身份设置 `requires_shizuku`。Validate nonzero required addresses and ensure all layouts come from the same image.
6. 只在确有设备证据时覆盖 `execution`；否则保留 [公共默认值](defaults.md)。Override execution tuning only with measured evidence.
7. 在 `index.json` 增加 `{release,file}`，运行 `jq` 校验、Rust 测试和 `./gradlew clean :app:assembleDebug`。Add the index entry, validate JSON, then run tests and a clean build.
8. 真机按低温、固定核心、单路线重复测试；记录 App/Shizuku、W1/W2/W3、回退与清理结果。Device-test under controlled temperature/CPU conditions and record every stage, fallback, and cleanup outcome.

## 合并顺序 / Merge Order

`defaults.json` → 内置 release JSON → 用户稀疏 override → UI 显式核心选择。Later layers win. Kotlin 生成单个 `active-profile.json`，Native 不再搜索或合并配置。Kotlin emits one resolved profile; Native never selects configuration sources.

## 安全规则 / Safety Rules

- 模板中的 `off_* = 0` 表示“必须提取”，不是可运行默认值。Zero means unresolved, never a runnable default.
- task/cred/multicast 布局错误可能造成任意内核内存破坏、黑屏或重启。Incorrect layouts can corrupt arbitrary kernel memory and reboot the device.
- `execution` 调高尝试次数或缩短等待可能显著升温并降低成功率。More attempts or shorter waits can increase heat and reduce reliability.
- 新 profile 未通过真机门禁前不得提交为“已支持”。Do not claim support before the device gate passes.
