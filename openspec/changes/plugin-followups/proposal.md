## Why

由真实consumer触发的插件后继需求。`gl-9lz`现在细化第二个plugin vertical：让kernel profile以独立data-only package和typed Document更新，避免只改设备配置仍须重装APK，并删除Diagnose为旧wire生成的假profile。其余后继仍只保留既有边界。原批准/历史结果按原Beads记录，设计工件齐备不代表production实现或门禁通过。

## What Changes

- 精确设计format 1.1的`PROVIDER`/Document声明、独立kernel-profile package和multi-record builtin seed。
- 让Diagnose通过API 1.1可选读取当前kernel release精确匹配的冻结Document，并以Document bindings替换RunHost全局必需`profileArtifactId`。
- 增加仅data-only、显式用户准入、重启后生效的provider archive导入/恢复；损坏的固定user-imported profile record只能经窄恢复操作移除，不自动fallback。当前READY generation不热换，因此不提前实现live object pin/lease。
- 由既有one-shot bootstrap在读取catalog前清理catalog staging/scratch及Document create→unlink两个exact owned transient命名空间；不新增后台cleaner、第二套GC或通用目录清扫。
- production实现仍须作者自检、独立审查和用户明确批准；本次不启动Custom、真实攻击backend、extractor/toolchain或通用hot update。

## Capabilities

- `plugin-data-provider`：data-only package、typed Document、builtin/user-import provenance与restart-gated activation。
- `runhost-wire`：从单一必需profile改为manifest声明的零到多个Document binding；保持现有PFD owner、Payload/Finish和lifecycle合同。

## Impact

- `gl-2zi`：P-BACKEND-CORE-001：由真实 consumer 触发配置实现；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-9lz`：P-BACKEND-X-002：data-only profile provider / Document；本change新增exact设计与delta specs，状态、批准与实际证据仍由Beads唯一维护。
- `gl-tz3`：P-BACKEND-X-002：真实 Custom consumer 的公开能力合同；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-r8h`：P-BACKEND-X-002：外部 extractor Maven consumer；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-dt8`：P-BACKEND-X-002：external import/update 与 READY 后对象保护；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-2u2`：真实 backend 候选：43284 保持断开；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-2ot`：真实 backend 候选：98260 保持断开；状态、依赖、批准与实际证据由Beads唯一维护。
- `gl-m0f`：真实 backend 候选：43499 保持断开；状态、依赖、批准与实际证据由Beads唯一维护。
