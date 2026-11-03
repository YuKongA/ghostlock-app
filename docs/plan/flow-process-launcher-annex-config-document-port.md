# Flow 进程启动器计划 · 附录 C v3：上游配置/document 管线移植（2026-11-02 用户裁定）

> 状态：**v3（2026-11-02），G15 修订 + G16 复核（阻断 0/重要 4；映射文档已交付、U1–U11 已裁决）**；可进入 C1a。用户原话：「…把上游的流程：加载磁盘存储的配置->validate->添加必要的execution params->传入C++->解析成document这个流程搬过来」。
> 触发：B4-real/G-43499-real 单次真实 43499（A301SO 冷机、opt-in、生产 `:flowhost`+真实 ELF）>7min 无 `GLK-REAL43499`/host 证据，Lead 中止；未 panic/未 root，已 reboot。当前 Kotlin GLK1 自定义二进制 + native typed parse 判定为未按上游流程处理。
> 归属：主计划 v5 + `annex-b4-real`；B4-real 重试改为 C4。参考实现只读取证：native `2933b838^:core/...`，Kotlin `d2778a1a^:profile-core/...`。

## 1 目标流程（上游）

```
Kotlin profile-core:
  加载磁盘存储/内置资产 config（HoconSupport/ProfileResolver）
  → validate（owner schema/manifest v3；缺/越界 fail-closed）
  → ProfileMerger 添加必要 execution params（默认/推荐 CPU/缺失参数）
  → Glkv3 Value → Glkv3Encoder（MessagePack, schema=3, payload family）
  → document bytes
C++:
  stdin/document → mpack → profile::glkv3 Document（neutral，不命名字段）
  → key_catalog/document_pairs/payload_keys 绑定（strict 未知拒绝）
  → 当前 kernel_offsets/runtime_config → 现有 orchestrator 攻击 pipeline
```
当前实现（GLK1）在 C3 切换后删除生产调用，C5 删除代码；迁移期不允许运行时双轨。

## 2 B1 闭合：映射文档（C1 前置物）

- 交付：`docs/analysis/43499-document-key-mapping.md`（Lead 委派只读分析，**C1 写码前必须完成并由 Lead 接受**）。
- 内容：上游 owner/section/key/wire/required/default → 当前 `binary.cpp` section/key / `kernel_offsets` / `runtime_config` 字段 → 单位/宽度/缺失/未知动作；payload family 三键；仅上游有/仅当前有/语义不同清单；≥3 高风险项；C2 的 ProfileResolver/Merger 行为差异表；C1 host golden 用例清单。
- C1 验收以该表为准：host golden 逐 key 断言 + strict 未知/缺失负例；**表中未列出的 key 不得静默放行**。
- 若映射存在无法裁决项 ⇒ C1 停下报 Lead（不猜、不复制旧 typed parse 绕过）。

## 3 批次（单变量；每批门禁）

- **C1 native document 读取（并列，不切生产）**
  - 搬入（从 `2933b838^`）：`core/lib/mpack/{LICENSE,README.md,mpack.c,mpack.h}`（MIT vendored、C99、不改源码；独立 `-x c` 编译与告警隔离；`MPACK_STDIO/MPACK_EXTENSIONS` 关闭）、`core/src/profile/{glkv3_document,document_pairs,payload_keys,key_catalog,layer}.{hpp,cpp}`、`contract/step_catalog.hpp`（仅 type/常量依赖；不恢复 dispatch）。
  - **不恢复**：`plan_gate.hpp`、`run_plan.*`、`plan_delivery.*`、`route_catalog.hpp`（C1 最小集；见 §5 I1）。
  - 绑定：新 `profile/document_bind.*` 把 Document 绑到当前 `kernel_offsets`/`runtime_config`（按 §2 映射表）；旧 `binary.cpp` 保留但生产入口仍走 GLK1（并列）。
  - 测试（实际存在，已核对 `git ls-tree`）：`document_reader_test`、`document_pairs_test`、`payload_keys_test`、`payload_manifest_test`、**`real_document_test`**（G4/FIX-PAY 必需）、`step_catalog_test`（按依赖取舍）+ golden `payload-manifest.pin.tsv`；host 编译零告警。
- **C2 Kotlin document 编码（并列）**
  - 依赖裁决：**采用 `org.msgpack:msgpack-core:0.9.12`**（与上游 profile-core 一致；纯 Java、无传递依赖、Apache-2.0）；C2 记录 APK 增量（目标 ≤300 KiB）；不自写 packer，避免语义偏差。
  - 搬入/适配：`Glkv3Value`/`Glkv3Encoder`/`NativeProfileGlkv3Adapter`（manifest v3 TSV 仅 43499 所需 owner）+ 当前 ProfileResolver 输出→document。
  - **C2a 先对拍**：当前 `ProfileResolver/ProfileMerger` vs 上游 `ProfileMerger/Resolver` 行为差异（默认值/推荐 CPU/缺失 execution params/merge 顺序），依据 §2 文档；缺语义则搬上游实现，不静默复用。
  - 测试：`Glkv3EncoderTest`/adapter manifest test + Kotlin bytes↔native C1 parse 交叉 golden（用上游 fixtures `glkv3-native-fixture.tsv`、`device-document-fixture.hex`、`payload-document-fixture.hex`）。
- **C3 切生产入口（只切，不删）**
  - `Glued43499ExecParams`/`Flow43499GluedEntry` 改传 GLKv3 document bytes（stdin；evidence `profile.transport=glkv3-document`）；host/entry 单测。
  - fixture no-op APK 设备门禁（argv/host/Finished.Ok/stdin 读取成功）+ B2/B4a/B4b/取消/keepalive 全回归；GLK1 仍可在测试/debug 路径取用但生产不再调用。
- **C4 真实 43499 重试（B4-real，允许 panic、单次、失败即停、不同 boot 不重跑）**
  - 冷机、opt-in、生产通道、真实 ELF；evidence=host/entry 事实 + ELF 输出/攻击阶段事实；不要求攻击成功判 supported；panic/重启记录。
  - 若 C4 显示缺 plan_gate/plan 校验事实 ⇒ 新增 **C4a** 单独批次再真实重试（不在 C4 内混入）。
- **C5 收口**
  - 删除 GLK1 生产代码：`NativeProfile.toBinary()`、`ProfileExporter` GLK1 输出、`profile/binary.cpp` GLK1 parse 生产调用、`profile/entry.cpp` GLK1 stdin/file 读、`Glued43499ExecParams` profile.conf/bin 键；仓外备份 + 删除清单 + 回退点；UML §11.9/计划/approval 更新。

## 4 门禁汇总

- C1：host 编译零告警；document_reader/document_pairs/payload_keys golden 按映射表逐 key；strict 未知/缺失负例；无攻击路径调用。
- C2：`testDebugUnitTest` 全绿；Kotlin↔native 交叉 golden；manifest 一致性；APK 增量记录。
- C3：`assembleDebug/AndroidTest/Release`；fixture 设备门禁；B2/B4a/B4b/G10/G11/G12 回归不退化。
- C4：单次冷机真实门禁（annex-b4-real 判据），允许 panic，失败即停。
- C5：全量单测/构建/`git diff --check`；删除面无编译引用。

## 5 G15 重要项闭合

- **I1 plan_gate 范围**：C1–C3 最小集不含 plan_gate/run_plan/plan_delivery/route_catalog（明确裁决）；C4 前不恢复；若 C4 证据表明需要计划校验，C4a 单批补。C4 判据用现有 orchestrator 证据 + ELF 输出，不预设计划事实。
- **I2 参考测试修正**：`document_reader_test.cpp` **存在**（`2941b...` 实测 `git ls-tree 2933b838^` 有之，critic 取证遗漏），C1 按实际集合恢复：`document_reader_test`、`document_pairs_test`、`payload_keys_test`、`payload_manifest_test`、`payload-manifest.pin.tsv`、`step_catalog_test`；`plan_gate_test` 不在 C1。
- **I3 msgpack**：裁决=`org.msgpack:msgpack-core:0.9.12`（上游版本；纯 Java/无传递/Apache-2.0）；C2 记录 APK 增量；不自写 packer。
- **I4 Resolver/Merger**：C2a 行为差异表 + golden 对拍；不静默复用；缺语义搬上游。
- **I5 估算/单 writer/删除面**：C1 2–3 人日（native 搬入+bind+golden）；C2 2 人日（依赖+encoder+对拍）；C3 1 人日；C4 0.5 人日；C5 1 人日（agent 工时）。单 writer：C1 native（coding-agent）、C2 Kotlin（同一 writer，串行）、C3 同一 writer；C4 Lead 执行；C5 writer。C3 只切不删、C5 删；删除清单/backup 见 C5。
- **I6 最小范围/许可/APK**：C1–C4 不包括 profile-core plugin/route/component catalogs、Lua/plugin 路径、M0；mpack 原样 vendor 并保留 `LICENSE`/`README.md` 署名（MIT）；msgpack-core Apache-2.0；APK 预算 ≤300 KiB 增量；P4 旧文档 `docs/archive/20261017-1835-config-wire-redesign-plan.md` 头部标 superseded（user 2026-11-02 新裁定），指针到本附录。

## 6 风险/开放

- 映射表未完成前 C1 不开工；如映射不可行则回到“最小参数化 GLK1 修复”备选（报 Lead，不猜）。
- 攻击路径/真机：C4 前不跑真实 43499；fixture 门禁缺一不可。
- 上游 document strict 语义（未知 section/key 拒绝）可能暴露现有 profile 的多余字段 → C2/C3 需要 profile 资产清理（单独清单）。
- GLK1 删除面大：C5 前保留备份与 `git` 可回退；不 commit 前以 /tmp 备份 + MANIFEST 为准。

## 7 Lead 裁决（U1–U11，2026-11-02；映射文档已接受）

映射文档 `docs/analysis/43499-document-key-mapping.md`（685 行，104 键）**接受为 C1 前置权威**；下列裁决写入 C1 验收。

| ID | 裁决 | 理由/边界 |
|---|---|---|
| U1 root 选择 | C3 document 路径的 route 取 **root `route`（str→RouteKind）**，必需；`combination` 在 C1 不使用（不搬 run_plan/chain catalog）；`backend.cve_2026_43499.route` 忽略并记 evidence；GLK1 header route 仅旧路径保留至 C5 | 避免恢复 chain catalog；FIX-TSV/FIX-DEV 均以 route 为准；解码器不认 combination |
| U2 strict 粒度 | 对 owner `root` 与 `cve_2026_43499`：未知 section/key **拒绝**（具名）；其它 owner（`plugin.*`/`cve_2026_43284`）**忽略 + evidence `document.foreign_owner:<name>`**；类型/宽度不符拒绝；缺失 required 拒绝 | 最小严格面；多 backend 工具文档可共存但本批不消费 |
| U3 15 常量键 | **接受并忽略 + evidence**（逐键名）；与 native 常量冲突时记 `document.constants.mismatch:<key>`，不失败；`abi.payload` 键由 C1a payload 读取器校验，binder 无字段不绑定 | 上游 fixture 无 abi.payload；避免把常量问题带进 C1 |
| U4 safe_mode | 绑定 root `safe_mode`(bool) → 当前 `meta.safe_mode`（默认 false）；C2 UI 可写；`OPS:322/417` 消费 | 恢复上游安全语义；风险：safe_mode=true 禁用 /data/adb/modules |
| U5 fallback/recommend | `root.fallback_route`(str，可选)→当前 `meta.fallback_route`；缺省时用 HOCON `fallback.to` 包装值；`recommend_shizuku` 仅 UI，不进 document | 保留 W2/W3 重试路径 |
| U6 重复目标 | **文档顺序 last-wins**（按 section 遍历顺序），并记 `document.duplicate-target:<key>`；G12 固定断言 | 与 GLK1 section 后写覆盖一致 |
| U7 payload | C1a 只搬 payload **读取器**（FIX-PAY/G7）；C1b binder 不映射 payload（当前无字段），evidence 记 `payload.present/kind/sha256` | H2 的字段化留待 C-later |
| U8 kernel_minor | 忽略 + evidence（无字段；不拒绝） | 不阻塞 C1 |
| U9 host 测试入口 | C1a 增加 host-only CMake target（如 `glued43499_document_tests`，不打包）；Lead 门禁用文档化命令跑 golden；不得依赖设备 | 当前只有 exec target |
| U10 golden 权威 | SELECTION 用 `FIX-TSV`+`SELECTION_HEX` 交叉；设备资产用 `FIX-DEV`；payload 用 `FIX-PAY`；冲突时选择以 GOLD/FIX-TSV 为准、缺失键行为以 FIX-DEV 为准 | 三份 fixture 覆盖不同面 |
| U11 execution 默认 | **C0（本批）先修 H1**：运行时 `Glued43499ProfileCodec` 走 `ProfileMerger.resolveMerged`（tuning/presets 资产），C1/C2 不得再以“缺省 0”编码；C2 编码输入必须是 merged profile | 已派 C0（task-161）；这是 B4-real 首要嫌疑 |

## 8 C1 拆分（v3）

- **C1a（可立即开工）**：native 读取栈（mpack/glkv3_document/document_pairs/payload_keys/key_catalog/layer）+ host-only 测试 target + G1–G4/G7/G8（读取/负例/payload/跨端字节），**不含 binder、不切生产**。U9 具体化：CMake host-only target 名 `glued43499_document_tests`（`EXCLUDE_FROM_ALL`，不打包）；Lead 门禁命令 `cmake -S app/src/main/cpp/43499glued -B build/host-tests -DCMAKE_BUILD_TYPE=Release && cmake --build build/host-tests --target glued43499_document_tests -j` 后运行该测试二进制（C1a 首批冻结；需 `-Wall -Wextra -Wconversion -Wsign-conversion` 零告警）。
- **C1b（在 C1a 后）**：Document→当前字段 binder，按 §7 裁决实现，G5/G6/G9–G12（字段 golden、strict、宽度/符号、选择矩阵、重复键）；仍不切生产。
- C2 在 C1b 验收后开工；C3 切生产、C5 删 GLK1。C4 才重试真实 43499（允许 panic、单次、失败即停）。
- C0（H1）与 C1a 独立，可并行（不同写面：Kotlin codec vs native 读取栈）；仍遵守单 writer 串行提交，不并发改同一文件。
