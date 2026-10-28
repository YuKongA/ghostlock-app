# 动态 BackendSpec 注册（2026-10-28 用户裁定）

> 背景：BackendSpec 需要动态实现；Lua 以后也会有接口，每个 Lua 脚本可声明自己的 uid 需求（ANY 或仅 Shizuku）。backend 只声明，UI 检查/拒绝，backend 不自检。

## 1. 落地

| 项 | 改动 |
|---|---|
| `BackendSpec.key` | 稳定唯一键（默认 `cve`；Lua 等动态 spec 覆盖为 `lua:<scriptId>`） |
| `Backends` | 从静态 `val all` 改为 **Compose 可观察动态注册表**：`register(spec)` / `unregister(key)` / `byKey` / `resetToBuiltins`；内置 4 项固定不可注销 |
| `BackendSelection` | 选择从下标改为 **稳定 key**（`selectedKey`），`index` 保留为兼容派生属性；动态增删后选择仍有效 |
| `BackendFlowRunner` | 身份从静态 `BackendId` 改为动态 `key: String`；Lua runner 可按脚本提供 key |
| uid 需求 | 每个 spec（含动态 Lua）声明 `uidRequirement`（`ANY_UID`/`SHELL_UID_2000`/`APP_UID`）；UI 只按「执行体种类 + Shizuku 授权二值」比对并拒绝，不读 uid；backend 不自检 |
| 守卫测试 | `DynamicBackendsTest`：注册/重复 key 拒绝/选择/注销/内置不可注销/动态 uid 需求；`BackendFlowRunnerTest` 改 key 对拍 |

## 2. Lua 接口预留

- 未来 Lua 加载器扫描脚本 → 每个脚本生成一个 `BackendSpec`（`key=lua:<id>`、`cve`/`titleRes`/`rows` 由脚本元数据提供、`uidRequirement` 来自脚本声明、`runner` 为 Lua Flow 适配器）→ `Backends.register(spec)`。
- 脚本需求解析/校验属于 UI/Lua 加载层；backend/Flow 不因执行体/uid 自检。
- 现有静态 `BackendCustom` 暂留作占位，后续可由动态 Lua spec 取代。

## 3. 未完成/验证

- 完整 `compileDebugKotlin`/单测仍被并发写者的在途 43499 文件与重复构建产物阻塞；本批改动文件无错误归属。
- 攻击线程/Flow 在选中执行体 JVM 内的迁移（Shizuku host JVM）仍是 R2 待办。
- 未 commit/push。
