# Document ③ 段（收尾 payload）字段口径定稿 · 2026-10-08

> 分析类（≤8192 B）。**本件只记录已定稿条文，不含裁决**（裁决见 Lead；有疑问回报，不自行改判）。
> 相关：[lua-surface-annex-document.md](lua-surface-annex-document.md) §B1 · [A6 待批单](../plan/A6-baseline-approval.md) §1 第 8 项 · `core/src/terminal/payload.hpp` · `core/src/terminal/execution.hpp`。

## §1 段名与键名

- **段名** = **`backend.cve_2026_43499.payload`**（owner-qualified，沿用 R3 段风格）。
- **每 `name` 四键**：

| 键 | 类型 | 约束 |
|---|---|---|
| `.bin` | **bin** | 正文（收尾脚本 / LKM 字节） |
| `.sha256` | **str** | **64 小写 hex**，与 `support::sha256_hex`（`core/src/support/sha256.hpp:28`）**同口径** |
| `.size` | **uint** | 字节数 |
| `.kind` | **str** | ∈ `script` \| `module` \| `plan` |

## §2 标识规则（**2026-10-08 修正：名字受终端闭集约束**）

- **Frontend 是声明闭集**：`contract/kind.hpp` 的 `kComponents` 中 `ComponentKind::Frontend` = **`root_child`（:81，`selectable=true`）· `umh_forward`（:82，`selectable=true`）· `shizuku`（:83，**`selectable=false`**，**不可作前缀**）。
- **`name` = 以 selectable 的 Frontend token 开头 + 可选 `_<suffix>`**（`suffix` = `[a-z0-9_]{1,16}`）⇒ **不允许无关自由名**；`shizuku`（`selectable=false`）**不可作前缀**。
- 例：某终端需两份交付物 ⇒ **`root_child_script`** / **`root_child_module`**（仍满足前缀约束）。
- **⚠ 陷阱（2026-10-08 实测发现）**：**wire 的字符串是长度分隔、无终止符**（MessagePack str）⇒ **任何把它当 C 串用的代码都是错的**。实测：读取器第一版按 `text[64] == 0` 校验摘要 ⇒ ①**越界读** ②**对所有文档恒定误判缺件**（**必然假红**级，不是风格问题）。**修法** = 按 `mpack_node_strlen` 长度拷进**有界缓冲**再校验（载荷条目现携带定长副本 `sha256[65]` / `kind[16]`，字节仍是**带显式长度的视图**）；物证 = `tests/document_reader_test.cpp` 三段全绿。
- **段内唯一**；**大小写敏感、无归一化**；**`handoff.payload_ref` 与 `name` 逐字相等**（直通 `store_payload` 的 name，`core/src/terminal/payload_store.hpp:30`）。
- **沿革（不删）**：先冻结为**自由名** → 读 `kind.hpp` 后改为**受终端闭集约束**（2026-10-08 修正）。

## §3 空载荷

- **不设 `empty`**；**缺席即缺席** —— 计划不声明该时机 ⇒ **`moment_runs` 恒 false** ⇒ **什么都不跑**。
- 段内**出现 name 必须带 `bin` + `sha256` + `size`**（缺一即拒）。

## §4 ②/③ 共用形状（**不另造第二套**）

- 凡「**带声明摘要的字节**」一律 **`{bin, sha256, size}` + `kind`**：**② 段计划** = `kind=plan`；**③ 段** = `script` / `module`。
- native 用**一条校验路径**覆盖两段：**`terminal/payload.hpp:53` 的 `check_payload`**，三义归一为具名 **`payload-missing`**。

### §4.1 manifest 形态（**2026-10-08 修正**）

- **每个 selectable Frontend 一行 family**（共 **2 行**：`root_child` · `umh_forward`）：`payload.<terminal>.*.{bin,sha256,size,kind}`；另加 **一行名字规则**（前缀 = selectable Frontend token）。
- **Adapter** 对该 family 内路径**放行并按规则校验名字**；**family 外仍 fail-closed**。
- **理由（三条）**：① 终端已是**声明闭集**，自由名会为同一件事**开后门**；② Kotlin Adapter 对未声明路径 **fail-closed**，自由名**无法枚举**（故选「**名字受限**」而非「给 Adapter 加通配匹配」）；③ `payload_ref` 指向**正确终端**的收尾件由**结构**保证而非约定。
- **定案（2026-10-08）**：`shizuku` **不列 family 行** —— 它是**映射到 `root_child` 的组合 token**（实测与 kotlin-app 一致），**不是终端** ⇒ **family 行数 = selectable Frontend 行数 = 2** ✓。**若某 token 日后变为 selectable，family 应自动多一行（实现按 selectable 行遍历，不硬编码 2）**。

## §5 顺序（**native 先 · Kotlin 后**）

1. **native 先**：键声明 + 读取器 + **manifest 由 native 重新导出**；
2. **Kotlin 后**：发射 ③ 段。

**理由**：Kotlin 的 Adapter 对**未声明路径 fail-closed**，且 **manifest 生成器已随旧树消失** ⇒ 顺序颠倒会直接拒绝。

## §6 载荷来源

- **收尾脚本** = **`tools/lkm/ghostlock/root_cmd.sh`**：**3349 B** / sha256 **`88903984a3021bc4b17a16d9c2ea1d98de09ca1ff8948214caad46fc102d7e73`**（实测复核一致）；**旧模板不复活**。
- **LKM** = 按**设备 KMI 派生 label**（**8 label 列全**）；设备侧从 **APK assets** 取（`LkmImageProvisioner.labelFor` / `assetPath`）。
- **Gradle 导出**：按 **`-PlkmLabel=` 单标签**；**未指定则只发脚本、不写占位**。

## §7 验收（可失败）

- 白盒：`check_payload` 对 `{bin, sha256, size, kind}` 四键齐备断言；`sha256` 口径与 `sha256_hex` **逐字符相同**；`kind` 非法值 ⇒ 拒。
- 黑盒：`handoff.payload_ref` 与段内 `name` **逐字不等** ⇒ **`payload-missing`**；`sha256`/`size` 不符 ⇒ **`payload-missing`**；**`name` 不以声明的 terminal token 开头**（自由名）⇒ **拒绝**（Adapter 按名字规则校验；family 外路径仍 fail-closed）。
- 顺序：manifest 未重新导出前，Kotlin 发射 ⇒ **fail-closed 拒绝**（预期红，用于证明顺序约束生效）。
- **实证（native 已入库）**：`core/src/profile/payload_keys.hpp`（5810 B）· `payload_keys.cpp`（1462 B）· 测试 `core/src/tests/payload_keys_test.cpp`（5924 B）；**两条证伪**证明规则**可失败**：① **取全部 Frontend ⇒ 组合 token（`shizuku`）被误收**（正确实现只取 selectable）；② **去掉后缀必填 ⇒ 裸名混过**（正确实现要求 `_<suffix>`）。
