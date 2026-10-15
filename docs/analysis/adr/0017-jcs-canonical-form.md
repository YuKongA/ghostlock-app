# ADR-0017：GLKv3 的规范化与摘要（JCS / RFC 8785 · sha256 · base64 载荷通道）

- 状态：**Proposed**（待用户批准）
- 日期：2026-10-12
- 索引：[README.md](README.md)
- 相关：契约权威 = `profile/wire-schema.json`（`identity` 段）· 实现 = `core/src/profile/glkv3_document.cpp`（**形式**校验）＋ `core/src/profile/plan_delivery.cpp`（**重算与比对**） · **完成本文件即补齐 docs/ 下的 JCS 缺口**（native-core 勘察发现，2026-10-12）

## 背景（Context）

- **契约要求 JCS 规范化**，但 **`docs/` 下此前没有任何 JCS / RFC 8785 条目** ⇒ 读者无从知道「canonical 形式是什么、摘要覆盖什么、实现做到哪一步」。
- 契约的权威**只有一处**：`profile/wire-schema.json:5-9` 的 `identity` 段 ——

```json
"identity": { "canonical": "jcs-rfc8785", "digest": "sha256", "per_payload": "sha256" }
```

- `docs/analysis/adr/` 与其它 analysis 文档均无对应条目 ⇒ 本条按 ADR 惯例补记（编号 0017）。

## 决策（Decision）

1. **canonical 形式 = JSON + JCS（RFC 8785）**：GLKv3 的规范化形式取 JCS；**摘要算法 = sha256**。
2. **两级摘要（各自覆盖范围）**：
   - **`identity.per_payload`**：**逐个 payload** 各一个 sha256 ⇒ 覆盖**该 payload 的字节**；
   - **`identity.digest`**：**文档级**一个 sha256 ⇒ 覆盖**规范化（JCS）后的整份文档**（**不是**各 payload 摘要的再摘要 —— 该点属契约语义，见「未验证」）。
3. **wire 侧 payload 的编码决定 = base64 字符串**：复用 profile schema 既有的 **`str(base64)`** 类型承载载荷字节，**替代**原先 mpack 的 **`bin`** 类型。

## 实现边界（**分工如实记录** · 2026-10-12 更正）

- **两处分工（实测）**：
  - `glkv3_document.cpp:276-290`：**只做形式校验** —— 恰 **64 个 hex 字符**（`digest_length != support::kSha256HexBytes - 1U` ⇒ 视同缺失；随后 `support::sha256_text_is_well_formed(...)`）；
  - **`plan_delivery.cpp:54-58`：per_payload 摘要的「重算与比对」在这里** —— 注释逐字「The digest the family checked the shape of, **recomputed over the bytes**」；实现 = `support::sha256_hex(support::sha256(entry.bytes, entry.size), computed)` ⇒ **逐字节比对**，不等即**具名拒绝**（`:58` 起）。
- **更正记录**：本条此前沿勘察口径写的「core 只校验形式、不重算」与「摘要与字节不符的文档会被接受」**均不成立**，已删 ✗。
- **保留的技术债（准确版）**：**文档级 `identity.digest` 在 core 的「使用点」尚未查明**（真未验证，见下 §未验证 1）。
- 明确区分：**契约的声明**（JCS/sha256）与**实现的校验强度**（形式校验 + per_payload 重算比对）是两件事，本条分开记录。

## 未验证（如实，不得推测）

1. **文档级 `identity.digest` 在 core 的使用点尚未查** ⇒ 是否任何位置读取/比较它，**未验证**；
2. **`identity.canonical` 是否存在任何执行点**（是否仅为声明）**未验证**；
3. **JCS 规范化在本树内没有任何已知执行点** ⇒ 若确无，则本条为**纯约定**（契约声明），**未验证**；
4. ~~**`per_payload` 的读取点**~~ ⇒ **已查明**：**`plan_delivery.cpp:54-58`**（重算 `sha256(entry.bytes, entry.size)` 并逐字节比对，不等即具名拒绝）；
5. **profile-schema 侧 `str(base64)` 的定义位置**只按勘察记录转述，**未逐字核**（未验证）。

## 后果（Consequences）

- 后人不会再以为「JCS 已实现」；契约（`wire-schema.json`）与实现强度（`glkv3_document.cpp`）**分别可见**；
- **per_payload 的完整性有实据**（重算 + 比对，`plan_delivery.cpp:54-58`）；**文档级 `identity.digest` 的校验强度仍属未查明项**（见 §未验证 1）。

## 沿革

- 2026-10-12：native-core 勘察指出「契约要求 JCS，docs/ 无条目」⇒ 本条建立（编号 0017；ADR 目录既有至 0016）。
