# Kernel Profile 结构文档（配置系统）

> **状态：HOCON 重构已于 2026-10-05 在 native 落地**（① `b55708a8`：根级标量通道 + 删 `common`/`countermeasure` owner + vr_guard (b)；②③④ `23958eb0`：`platform.abi.*` → `backend.cve_2026_43499.abi.*`（62 处）+ 43284 调参入 `backend.cve_2026_43284.execution.*` + 新 `wire_only` 标记 + manifest 114 行）。**App 侧（测试 + golden）跟进中** ⇒ 跨端记「**native 已定稿、App 跟进中**」。旧的 `common` / `platform` / `selection` 布局**已删除**（出现即拒）。
> 旧的 `common` / `platform` / `selection` 布局**已删除**（出现即拒）。本文是**结构与流程权威**；逐字段权威仍是 `profile-manifest-v3.tsv`（native 导出，两份逐字节一致）。

## 0. 版本与字段权威

- 只有一个数字：HOCON `schema_version = 3` 与 GLKv3 wire 的 `schema == 3` 是**同一个 3**，不要叠加版本号。
- **唯一迁移点**：Kotlin `LegacyProfileConverter`（legacy `1`/缺键 → 3 并记诊断；其它值一律拒绝）。
- native 只认 3；wire v2 已删除；extractor **只产出 3**（`--format conf` **同批产出新形状**）。
- `kernel_profiles-legacy/*.conf` 是 **v1 输入夹具**，**故意保留旧形状**。
- 字段权威：`make -C src profile-manifest-v3` → `profile-manifest-v3.tsv`（列：`owner / path / wire / required / default / source / doc`）。
- **如何查字段**（不要把权威抄进正文）：`grep '^cve_2026_43499' app/src/test/resources/profile-manifest-v3.tsv`（或 `^countermeasure`、`^backend`），重生成命令 `make -C src profile-manifest-v3`（两份逐字节一致）。本文**刻意只写结构、搬迁与门禁**，不列字段枚举。注意 manifest 的 `required` 列表示「**无条件必需**」；**条件性（如「选中该档时必需」）属校验器语义，不得写进该列**。
- **根级标量走「根段」承载**（`document.hpp` 的 `kRootSection`，空段名），使 owner bind 的**唯一取值路径** `find_value(section, key)` 与根键**同构**——两处按 section 拷贝的过滤副本**不会漏拷**（漏一处即静默 `kernel_major=0`）。**勿改回具名成员**。
- **`wire_only` FieldSpec**（`kmi` / `lkm_path` / `carrier_path`）：**wire/解码接受，但不进 manifest** ⇒ App 的**可写面不含**它们。关键区分：**manifest = 「profile 可写面」，不是 wire 面**；wire 面由 native 的解码/绑定路径定义。
- **物证（2026-10-05）**：manifest **114 行**（10 头 + **104 字段**）、两份逐字节一致 sha256 **`68bd7a506a210077`**、裸跑 `ok (104 fields, both copies)`、三个 wire-only 键 **0 命中**；门禁 host `EXIT=0`（告警 9 基线、58 tests、防火墙 `180/4/4/0/0`）· lint 0 · NDK 0；**六条负例**在 `src/core/tests/profile_v3_test.cpp:195-242`（`common.*`、`countermeasure.*`、段内 `kernel_major`、旧 `platform.abi.*`、旧扁平 43284 键 ⇒ 拒）。

## 1. 数据流

```
assets/kernel_profiles/*.conf（HOCON，新形状）
   └─ Kotlin：解析 → 归一 → merge（include）→ resolve
        ├─ App UI：从 `available` ∩ native catalog 里选 backend，再选该 backend 下的 token
        └─ Glkv3Encoder → [4B len][GLKv3 文档][会话帧] → native stdin（--ghostlock-app-call）
native：frame_v3（schema==3）→ Document → owner/根级键门禁 → SchemaRegistry bind → Pipeline
```

**profile 只声明可用项**；**运行时的选择权在用户/App**，并写入 wire 的 `backend.<id>.steps`。

## 2. 文件格式（HOCON）

- HOCON 支持注释、`${variables}` 与 `include`；JSON 是合法 HOCON。
- 根必须是 map，且必须含 `ghostlock { … }` 段（GLKv3 文档根）。
- **根级标量是封闭白名单**：`schema_version`、`release`、`kernel_major`、`kernel_minor`、`safe_mode`。
- 被 `include` 的片段不带 `schema_version`。

## 3. canonical 布局（新形状）

```hocon
ghostlock {
  schema_version = 3
  release        = "<uname -r 精确输出>"
  kernel_major   = null            # 以后有用，先留着
  kernel_minor   = null            # 重构新增
  safe_mode      = false

  available {                      # 两级：backend 键 → token 列表
    cve_2026_43499 = [ "mcast_rootchild", "pselect_rootchild" ]
    cve_2026_43284 = [ "umh" ]
  }

  backend {
    cve_2026_43499 {
      steps = "mcast_rootchild"    # 运行时由 App 选择后写入 wire
      abi { task_struct { … } cred { … } kernel { } offset { … } }   # 原 platform.abi.*
      route { } cred { } kernel { } offset { } execution { }
    }
    cve_2026_43284 {
      steps = "umh"
      execution {                  # late_load_args / selinux_exec_context /
        …                          # module_poll_attempts / module_poll_interval_ms / wait_timeout_ms
      }
    }
  }

  # countermeasure { }            # **已移除**（用户裁决 2026-10-05）：`common.vr_guard` 与
  #                               # `countermeasure.vivo_vr_guard.*`（含 wire/manifest 行）删除后 owner 变空
  #                               # ⇒ 出现即拒
}
```

重构后的 owner 集：**只有 `backend.<id>`**（外加根级标量与根级 `available{}`）。**`countermeasure.*` 已移除**——vr_guard 字段删除后**再无写入者** ⇒ `vr_guard_enabled()` 恒 false ⇒ `steps.cpp` 两处 `VivoPluginPolicies::apply(...)` **可证明 no-op**（**不动攻击路径**）；`platform/vivo/**` 代码与两处调用**保留（惰性）**，其**彻底删除属 (a) 期**：攻击路径改动、**待设备门禁**，排设备可用后第一批。

## 4. 可用项与选择

- `available { <backend> = [ tokens ] }` 是**两级**：先选可用的 **backend**，再在其下选**组合 token**。
- **`selection { backend, terminal }` 已删除**，**`terminal` 概念从 HOCON 移除**——token 已蕴含 terminal（`*_rootchild` / `*_shizuku` / `umh`）。
- token 词表权威是 `contract::kCombinationCatalog`（12 token = 7 已接线 + 5 计划）。App 只提供**同时存在于 `available` 与 native catalog** 的 token；运行时选择写入 wire 的 `backend.<id>.steps`。
- `index.conf` 用 `usable = [{ id, usable }]`（**构建/资产层语义**）——与 profile 层的 `ghostlock.available{}` **刻意不同名**，避免混淆。

## 5. 各 backend

**`cve_2026_43499`**
- `steps`——组合 token（选择轴）。
- `abi.*`——task_struct / cred / kernel / offset 事实；**`platform.abi.*` 迁到这里**。
- `route` / `cred` / `kernel` / `offset` / `execution`——route 私有与几何分组，同以前。

**`cve_2026_43284`**
- `steps` 留在 backend 顶层（选择轴）；43284 无 route 轴，token 是**裸 path 名**（`umh`）。
- `execution.*`——`late_load_args`、`selinux_exec_context`、`module_poll_attempts`、`module_poll_interval_ms`、`wait_timeout_ms`。
- **`kmi` / `lkm_path` / `carrier_path` 从 profile 删除**：
  - `kmi` 由 `release` 派生（`major*1000+minor`）；手写曾 fail-closed 为 `KmiFieldMismatch`——**现在键不存在（出现即拒）**；
  - `lkm_path` / `carrier_path` / 各 `.ko` 路径统一在 **GhostLock 内部目录**解析，**运行时现算并注入 wire**；**wire 字段保留**，profile 不再承载。

## 6. countermeasure（已移除）

- **`countermeasure.*` owner 已移除**（提交 **`b55708a8`**，用户裁决 2026-10-05 的 (b) 面）：`common.vr_guard` 与 `countermeasure.vivo_vr_guard.tracepoint_funcs` 连同其 **wire 行与 manifest 行**一起删除 ⇒ owner 变空、**出现即拒**。
- `defex` **已完成删除**（提交 `a68e2d5a`）。
- `platform/vivo/**`（8 文件 / 473 行）与 `backend/cve_2026_43499/steps.cpp` 的两处 `VivoPluginPolicies::apply(...)` 调用**保留（惰性）**——无写入者即**可证明 no-op**，**攻击路径不变**；其**彻底删除属 (a) 期**：攻击路径改动、**必须真机门禁**，排**设备可用后第一批**。

## 7. 已删除的 owner 与冻结的功能

| 项 | 状态 |
|---|---|
| `common` owner | **已删除**——其键上提为根级标量或消失 |
| `platform` owner | **已删除**——`platform.abi.*` → `backend.cve_2026_43499.abi.*` |
| `selection { backend, terminal }` | **已删除**——改 `available{}`；terminal 从 HOCON 移除 |
| `plugin` owner | 按用户指令 2026-10-05 **字面注释**（代码/测试保留；出现即拒）；恢复 = 撤销注释 + 跑门禁 |
| `payload` owner | 同样冻结——不再发射 `payload.*`；执行半场停止 |
| `countermeasure.*` owner | **已移除**（`b55708a8`）；出现即拒 |
| defex | **已删除**（提交 `a68e2d5a`） |
| vivo VR guard | **(b) profile 面已完成**（`b55708a8`：profile/wire/manifest 行删除）；**(a) 删代码挂账待设备门禁**——`platform/vivo/**` 与两处调用在此之前**保留（惰性）** |

## 8. GLKv3 wire

- MessagePack 文档、根 map、`schema == 3`、无 magic/独立头；canonical = 最短整数 + 键按 UTF-8 字节序；stdin 长度前缀分帧。
- **运行时注入字段**（`kmi`、模块/载体路径）由 native 从现算值写出；profile 不提供它们。
- 运行时选择由 `backend.<id>.steps` 承载；根级 `available` **不是** wire 段。
- 静态策略进文档；运行时密钥/SPI/端口绝不进文档（走会话帧，用后清零）。

## 9. 校验与诊断

fail-closed（整份拒绝，绝不静默降级）：
- 出现**已删除**的 owner（`common` / `platform` / `selection`）或**当前被注释**的 owner（`plugin` / `payload`）；
- 根级标量不在白名单内，或未知顶层 owner（如 `plugins` / `payloads` / `root`）；
- `available` 里的 token 不在 catalog 中，或 `steps` 与所选 token 矛盾；
- `schema_version != 3`，或根不是 map；
- 路径含 `..`、反斜杠或控制字符，或长度 >256 B；`sha256` 非小写 hex；
- 手写 `kmi`（键已删除，出现即拒）。

## 10. 存储与加载层次

- 内置 profile：`app/src/main/assets/kernel_profiles/`（`index.conf` + 每 release 一份 `<uname-r>.conf` + 公共 `execution-*.conf` / `credential-6x.conf` / `kernelsnitch-6x.conf`）。
- `index.conf`：`schema_version`、`usable`（backend 列表）与 `profiles`（release → 文件）。
- 覆盖项在 App 覆盖存储；canonical 文档以 App 发射的为准。

## 11. 命令与对拍测试

```sh
make -C src profile-manifest-v3      # 重新生成字段表（两份）
make -C src combination-manifest     # token 白名单导出
make -C src vocabulary-manifest      # 组件词汇导出
make -C src native-host-tests        # host 测试（含 manifest/对拍断言）
./gradlew :profile-core:test :app:testDebugUnitTest
```

## 12. 旧 JSON 导入

- v1 `offsets.json` **只在 Kotlin** 由 `LegacyProfileConverter` 转换；native 不解析 v1。
- `kernel_profiles-legacy/*.conf` **保留旧形状**作为夹具；它们不属于新布局，**不要**按新形状「修正」。

## 13. 修改配置的检查清单

1. 先在 native schema 加字段（唯一权威），再重生成 `profile-manifest-v3.tsv`。
2. 同批更新 Kotlin adapter/UI、本文档与 `PROFILE_SCHEMA.md`。
3. **不要**重新引入 `common` / `platform` / `selection`；新状态放既有 owner 或根级标量。
4. **不要**把运行时现值（`kmi`、模块/载体路径）写进 profile。
5. 跑 `PROFILE_TEMPLATE.conf` 里的验证配方；触及攻击路径时按 `docs/analysis/device-gates/` 归档。
