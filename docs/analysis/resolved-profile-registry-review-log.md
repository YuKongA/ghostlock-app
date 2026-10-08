# 评审与不符合项关闭记录（配套 resolved-profile-registry-plan v2.4）

> 分析类文档。记录三轮非作者评审的不符合项、关闭位置、门禁状态与遗留未验证项。
> 评审对象：[resolved-profile-registry-plan.md](resolved-profile-registry-plan.md)（**v2.4**，自包含）。
> **2026-10-07 新裁决**：terminal 轴取消（改由 step 执行器表达）⇒ **ADR-0006 归属需修订**；document 按 **plan 的 backend 集合**发射（旧单 backend `.bin` 仍可解析）；与「双读取路径废止」同批、判据共用（§3.4/§5.1）。
> **复验（非作者）**：A/B/C 28 条 = **23 全关 / 5 部分 / 0 未关**；G1–G13 = 6 ✅ / 4 ◐ / 3 ⏳；另发现 2 条 P0。5 部分 + 2 P0 已关闭（§6）。

## 1. 评审轮次

| 轮 | 评审人（角色） | 结果（分项：阻断 + 重要 + 建议） |
|---|---|---|
| 1 | native-core（执行/绑定） | 12 条（**4 阻断** N1–N4 + 8 重要/建议） |
| 2 | kotlin-app（App/解析） | 16 条（**6 阻断** K1–K6 + 10 重要） |
| 3 | docs-uml（文档/契约） | 28 条（5 阻断 A1–A5 + 14 重要 B1–B14 + 9 建议 C1–C9） |
| 合计 | — | **56 条 = 15 阻断 / 30 重要 / 11 建议**（1 轮按 6 重要 + 2 建议计） |

## 2. 阻断项关闭表（15 条，A2=N4 合并一行）

| 编号 | 问题 | 关闭位置（v2.2） |
|---|---|---|
| N1/N2 | 暴露 backend 类型 / session 无法承载 bind | §3.2/§3.4（只暴露中性类型；`bind_view<B>` 由 backend 提供） |
| N3 | 段过滤改拒绝 = 行为变更 | §5（举证/拒绝向量/旧 `.bin`） |
| N4=A2 | manifest 行数 104 错 | §1/§3.2（**110**，含 plugin 6 行） |
| A1 | 「权威=注册表」与 I4 冲突 | §2 I9 + §3.4（catalog 权威不变 + 双向 `static_assert`） |
| A3 | §13「未决项 0」不成立 | §8 + §10（外部依赖 4 条） |
| A4 | ADR-0006 决策 4 误标 | §3.4（决策 4 保留；改 **ADR-0004 R21 需修订**） |
| A5 | 以「已定」口吻废止双读取路径 | §8-1（待用户裁决 + 沿革） |
| K1/K3/K4 | declarations 无处安放 / 缺 HOCON 写回 / 帧并入 encode | §3.3 |
| K2 | Loader 签名不足 | §3.6（Layers + pair + presets + 解析器×2） |
| K5 | multicast 无来源 | §3.6（键位矛盾；未决前不置 required，§8-3） |
| K6 | 归一入口矛盾 + LPC 被自身规则拒 | §3.6（LPC 产已归一 tree 输入） |

## 3. 重要项关闭位置（汇总）

## 3. 重要项关闭位置（汇总，v2.2）

- **native**：N5/N6 §3.2；N7/N8/N11 §3.2+§4；N9 §4/§6；N10 §3.2；N12 §3.4。
- **文档**：B1 §8；B2/B3 单篇 `*-plan.md` **16259 B ≤16 KB**；B4 四张 mermaid；B5 **§3.8**（新增新旧对照）；B6 §9；B7/I10 §2+§3.6；B8 §3.6 + 本文件 §7；B9 §5；B10 §5+§6+本文件 §7；B11 §1（4 处口径/50 行）；B12 §6；B13 §9；B14 §3.2。
- **Kotlin**：K7–K9 §3.6；K10 §4（`AdvancedUI`+`Ctrl.updateAdvanced` 更正）；K11 §4；K12 §3.2（运行时 manifest 驱动，无 codegen）；K13 §3.3；K14 §3.6；K15 §5；K16 计划 §7（Bin 恢复路径）。
- **建议**：C1–C9 已并入；C6/C7 见 §6。

## 4. 编码前门禁（G1–G13）——复验终判（v2.2 后）

| 门 | 终判 | 关闭位置 / 依据 |
|---|---|---|
| G1 组合权威归属 | ✅ | 计划 §2 I9 + §3.4（catalog 权威 + registry 编译期投影 + 双向 `static_assert`）；待用户确认 §8-2 |
| G2 行数 104→110 | ✅ | §1/§3.2（110 = 92+9+6 plugin+3 root） |
| G3 未决项表述 | ✅ | §8 待批准 + §10 进度 |
| G4 ADR 条款 | ◐ | §3.4：0006 决策 4 保留（投影）；**0006 决策 1/2/4 terminal 归属需修订**（轴取消）；0004 R21 条文 + 锁点 |
| G5 双读取路径废止 | ⏳ 待用户裁决 | §8-1（同批留沿革）——设计侧不可关闭 |
| G6 决定状态/篇幅/命名/图/模板 | ✅ | 单篇 **16223 B ≤16 KB**、4 mermaid、模板八节齐（§3.8） |
| G7 I2 与 PROFILE_SCHEMA | ◐ | §9 已列改动（`:3`/`:16` 数字 + `:13`(+`_ZH`)），**须作为 B1 第一步执行**（执行前置，非设计缺口） |
| G8 required 消费者 + PathNotInSelection | ✅ | §3.6/§5 |
| G9 冻结面 + 测试清单 | ✅ | 冻结面 §5 点名；测试清单 §7（本文件新增） |
| G10 数字口径 + 禁读机制 | ✅ | §1（`active_profile()` 4 处口径）、§6（private + 受控访问器） |
| G11 ADR-0003 + 防火墙定层 | ✅ | §9（升 Accepted + 改决策 1）、§3.2（`session→profile` 合法、`ProfileReader` 不 include profile、空账本保持） |
| G12 需求写入 + UML 清单 | ⏳ 待用户批准后执行 | §9 |
| G13 用户批准 D-A..D-H 与双路径废止 | ⏳ 待批准 | §8 |

**统计：9 ✅ / 1 ◐ / 3 ⏳**（◐ = G7，属 B1 执行前置；⏳ 三项均为用户动作）。

## 5. 遗留未验证项（由后续门禁关闭，不在设计阶段假设成立）

1. 构建/测试未实跑（评审只读）；2. A1–A8 正确性未独立审计；3. ADR-0001/0002 全文未读；
4. `ResolvedProfile` 与槽位/布局测试相容性未验；5. 66 资产 required 可行性未逐一验证；6. B4.2 与 `kStepSetAliases` 绑定机制未展开；
7. golden 未逐字节全核；8. 旧 `.bin` 策略需在 B3 落地验证。

## 6. 关闭记录（复验的 5 部分 + 2 P0）

| 编号 | 问题 | v2.2 关闭位置 |
|---|---|---|
| B5 | 缺「数据流/控制流差异」节 | 计划 §3.8 |
| B10 | 冻结面未点名 / 58→66 / 测试清单 | 计划 §5（冻结面点名）+ §6（判据→测试文件）+ 本文件 §7 |
| B14 | `contract::ProfileReader` 引用面未定 | 计划 §3.2（**不 include `profile/`**；空账本保持） |
| C6 | 旧计划措辞超前 | 计划头部「**将**转历史档案（待 §8-4）」+ §8-4 |
| C7 | UML 清单缺 §2.7、无逐节点 | 计划 §9（补 **§2.7** + §3.1/§3.2 各 6 类 + 删除注记） |
| P0-1 | `contract::TargetProfile` 拼写错 | 计划 §3.2（改 `profile::TargetProfile`；口径按命名空间） |
| P0-2 | `BuiltinProfileCatalog.kt` 模块错 | 计划 §4（改标 **app 模块**，第二读路 `:71-100`） |

另关（R1/R2/R3）：`target()` 改 `const`、单一读入口判据覆盖 43284 侧、`active_profile()` 改 4 处并注明口径 —— 见计划 §1/§4/§6。

## 7. G9 测试清单与 required 消费者（计划 §3.6/§4/§6 引用）

**命令**：`make -C src native-host-tests`、`./gradlew :app:testDebugUnitTest`（各 EXIT=0）；重导 `make -C src profile-manifest-v3|combination-manifest|vocabulary-manifest|stepset-steps-manifest|lkm-kmi-manifest|glkv3-golden-hex`。

- **改写 2**：`MergedQueueCarriageTest`、`QueueWireCarriageTest`
- **重冻 6 类**：`native-doc-golden-v3.sha256`（58→66）、`Glkv3Golden.SELECTION_HEX`、`Glkv3Golden.NO_SELECTION_HEX`、`glkv3-native-fixture.tsv`、5 张 manifest 双副本、`combination-resolve-vectors.tsv`
- **新增 5 类**：单模型（Loader→Tree）· 查重/行数 `static_assert` 证伪 · 拒绝向量（`PathNotInSelection`/`UndeclaredArray`）· required/`DefaultUsed` · CPU 单一来源（无第二读路）
- **行号同步 1**：`ControllerInternalsTest`
- **勿动**：plugin golden（`plugin-probe-golden.tsv`、`plugin-wire-shape-golden.bin`）
- **顺手清理**：`ExporterAgreementTest.kt:32-42` 的 `~/.ghostlock/build/root/profiles` 外部根（2026-10-06 裁决：构建须落仓库内真实 `build/`、禁止链接）
- **native 判据对应**：`glkv3_schema_test`、`document_schema_test`、`profile_entry_test`、`queue_wire_test`、`profile_registry_test`、`profile_bind_compat`、`profile_string_binding_test`、`profile_manifest_v3_test`、`glkv3_codec_test`、`stepset_steps_manifest_test`

**required 消费者 5 类**（D-G/B8）：App 管线（tuning/route preset 填值）· `--load-prebuilt-profile`（旧 `.bin` 缺键 ⇒ Rejected，写进发行说明）· golden 58→66（先接纳在途 8 档再重冻）· `glkv3-native-fixture.tsv`（`profile-core/src/test/resources/`，91 行）· extractor（本批不置 required）。
