# CLion CMake 索引模型计划（2026-10-10）

## 现状与基线

- 分支基线：工作树已有未提交改动；本批次只修改 `core/CMakeLists.txt` 与本计划文档。
- `core/Makefile` 仍是生产构建权威，覆盖 host、Android、设备 probe、manifest、Lua core、测试、检查和清理逻辑。
- `core/CMakeLists.txt` 仅服务 CLion 的源码索引、补全和导航。

## 目标与约束

### 目标

提供一个不参与生产构建的 CLion 索引模型：动态收集 `src/` 下非测试文件，并补齐 Makefile 使用的头文件搜索路径。

### 非目标

- 不修改 `core/src` 的运行时代码、攻击路径或 profile/wire 格式。
- 不迁移 Makefile 目标、Android/probe/manifest/清理逻辑。
- 不改变 `core/Makefile`、Gradle 或任何生产产物。

## 改动清单

| 文件 | 改动 | 理由 |
|---|---|---|
| `core/CMakeLists.txt` | 保留 CLion 索引 target，动态收集非测试源码，加入 `src/` 与 Lua 头文件路径 | 修复补全和头文件解析 |
| `docs/analysis/cmake-migration-plan.md` | 修正为 CLion 索引模型说明 | 记录用户澄清后的范围 |

## 数据流/控制流差异

CLion 从 CMake target 获得源码成员和 include path；生产流程仍从 Makefile/Gradle 读取自己的源文件和参数。CMake 不负责 Android、probe、manifest 或发布产物。

## 兼容性与回滚

`core/Makefile` 保持不变。失败时仅回退 `core/CMakeLists.txt` 和本计划文档。

## 验证矩阵

| 阶段 | 命令 | 预期 |
|---|---|---|
| 配置 | `cmake -S core -B /tmp/ghostlock-cmake-index` | CLion 模型配置成功 |
| include | 检查 `compile_commands.json` | 含 `core/src` 与 `core/lib/lua/src` |
| 生产回归 | `git diff -- core/Makefile` | 无差异 |

## 明确保留

- 保留 `core/Makefile`、设备脚本、Gradle 构建逻辑和现有源码。
- 保留 `src/` 动态收集并排除 `test/tests` 的要求。
- 保留 mpack、Lua core 的第三方隔离和禁用警告策略。

## 进度

- [x] Explore：读取 `core/Makefile`、`core/CMakeLists.txt`、附件头文件和历史。
- [x] Design：用户澄清为 CLion 索引用途。
- [x] Implement：修改 CMake 索引 target 和头文件搜索路径。
- [ ] Verify：完成 CLion 配置和生产文件不变检查。
