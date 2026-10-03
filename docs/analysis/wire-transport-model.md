# GLKv3 wire 设计（MessagePack + MPack，无 magic 头，2026-10-03）

> 类型：设计（L 级）。取代本文件早先的「CBOR 子集自研」草案。
> 结论：GLKv3 = **纯 MessagePack 文档**（根 map，必填 `schema`），解析交给成熟单文件库 **MPack**；
> 不设 magic/独立头；HOCON 仍是配置权威，wire 只是 Kotlin 解析/合并后的**机器产物**。

## 1. 目标与动机

- GLK1/v2 的值只有 u64，扩展性不足（无字符串/布尔/字节/嵌套），新 backend 的静态策略被容器将就。
- **不引入 JSON**：wire 选文本会与 HOCON 混淆（HOCON ⊃ JSON 语法），且开发者可能手改 wire。
- **不自研 parser**：边界/越界/深度 bug 风险高，改用成熟库。
- **不引入重格式**：HOCON 已负责人写配置，wire 只需紧凑的二进制机器产物。

## 2. 选型：MessagePack（MPack）

| 维度 | JSON | **MessagePack** | CBOR |
|---|---|---|---|
| 与 HOCON 混淆 | **有**（文本、语法子集） | 无（二进制） | 无 |
| 体积 | 大（+base64） | 小 | 小 |
| 原生 bytes | 否 | **是** | 是 |
| 类型面 | 少（无 int/float 区分） | 够用（int/float/bool/str/bin/array/map） | 更宽（tag/不定长/float16） |
| 单文件成熟库 | cJSON/yyjson | **MPack**（MIT，单 `.c`+`.h`，明确 untrusted-safe） | cn-cbor（较老） |
| 三端库 | 最成熟 | 成熟（Kotlin/Rust 均有） | 成熟 |

**选 MessagePack**：二进制（与 HOCON 区分）、紧凑、原生 bytes、库面最小；CBOR 的 tag/不定长属多余攻击面，
JSON 的文本属性与本项目「配置=HOCON」冲突。

## 3. GLKv3 结构（无 magic / 无独立头）

GLKv3 就是一个 MessagePack 值，根为 **map**：

```
{ "schema": 3,
  "release": "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
  "terminal": "root_child",          // TerminalKind token
  "backend":  "cve_2026_43499",      // BackendKind token
  "route":    "multicast_waiter",    // route token（backend 内部解释）
  "sections": {
    "meta": { "kernel_major": 5, "safe_mode": false },
    "offset": { "init_task": 34677760 },
    "backend.cve_2026_43499": { "steps": 2 },
    "backend.cve_2026_43284": { "kmi": 5150, "carrier_path": "..." }
  } }
```

- **magic 取消**：根必须是 map（`0x8x`）——非 map 立即拒；`"schema"` 不存在或不等于 3 立即拒。
- **版本**靠文档内 `schema` 整数，不是容器前缀；不匹配直接拒。
- **selection** 在文档内（token 字符串或整数），不再需要二进制 header。
- presence 由键出现表达（省略 ≠ 0/false/空串）。

## 4. 类型映射

| 逻辑 | MessagePack |
|---|---|
| 无符号整数（offset/长度/KMI） | uint |
| 有符号整数 | int |
| 布尔（`safe_mode`/`vr_guard`/`compact_waiter`） | bool（`false` 显式存在 ≠ 缺失） |
| 字符串（release/token/路径） | str（UTF-8） |
| 字节串（静态哈希/证书/symbol blob） | bin |
| 数组 | array |
| 对象/section | map（键为 str） |

## 5. I/O 分帧（保留，非 magic）

- stdin app-call 仍用 **4 字节大端长度前缀** + GLKv3 文档（现有 `read_glk1_frame_stdin`）；
- 会话秘密帧（B5 通道 B）走同一分帧，追加在 GLKv3 帧之后；
- **运行时密钥绝不进 GLKv3**：只经会话帧注入 native，`explicit_bzero` 用后即弃，不落盘、不进 argv。

## 6. native 集成（MPack）

- vendor `mpack.c`/`mpack.h`（MIT）到 `third_party/mpack/`，附 LICENSE 与署名；
- 编译：MPack 是 C，需以 CC 编译（Makefile 增加 C 源列表或用 `-x c`），关闭 `MPACK_STDIO`/`MPACK_EXTENSIONS`；
  第三方告警用单独 flag 或文件名隔离，不污染项目的零告警要求；
- 解析用 **expect API**（`mpack_expect_map`/`mpack_expect_str`/`mpack_expect_uint`…）按 schema 遍历，**不建节点树、不分配**；
  未知键用 `mpack_discard` 通用跳过（可扩展性）；
- 上层 schema 校验层（薄）：`schema==3`、必填键、路径→类型、未知 section/key 在 Production 严格拒绝；
- 上限：文档尺寸（沿用 1 MiB）、map/array 计数与嵌套深度，越界/类型不符 → fail-closed。

## 7. Kotlin / Extractor

- Kotlin：**官方 `kotlinx.serialization` 无 MessagePack 模块**（官方格式仅 JSON/CBOR/Protobuf/Properties；HOCON 由 **lightbend/config** 处理，本仓已在用）。
  故 Kotlin 侧用 **`org.msgpack:msgpack-core`**（或社区 `kotlinx-serialization-msgpack`）；`NativeProfile.kt` 手写二进制编解码退场，
  改为「HOCON(Typesafe) 解析 → 数据类 → MessagePack 序列化」；
- Extractor（Rust）：`rmp-serde` 输出/读取同一 map；
- 三端「路径→类型」manifest 继续对拍，防一方漂移。

## 8. 确定性（golden/哈希/对拍）

- canonical：整数最短形式；map 键按 UTF-8 字节序排序；不使用 float；str 必为合法 UTF-8；
- 同一逻辑文档 → 逐字节一致；`native-doc-golden.sha256` 新增 v3 向量；
- Kotlin/Rust 序列化开关需与 native 约定一致（排序、最短整数），否则对拍会抓出。

## 9. 迁移

状态（GLKv3-5，2026-10-03）：**v3 唯一写出；v2 只读 + 金标保留**。

- native：`v2 只读 + v3 读写`。生产/导出无 v2 写路径；`profile/binary.cpp` 的
  `serialize`、其 `write_le`/`present_count` 及仅供写出用的 `Field`/`kSections`
  字段表以 `GHOSTLOCK_ENABLE_V2_WRITER` 隔离，仅 host 测试编译单元定义，
  设备对象内不含 v2 序列化器（`llvm-nm` 无 write 符号）；
- Kotlin：生产与导出一律走 v3（`AndroidProfileConfigController.nativeDocument()`、
  `exportKernelProfiles` → `Glkv3Encoder`）。v2 写函数
  （`NativeProfileDocument.toBinary()`、`Profile.toBinary()`、
  `AndroidProfileConfigController.nativeDocumentV2()`）标注 `@VisibleForTesting`，
  仅金标/等价测试用，生产不写 v2；
- extractor：只产出 HOCON（`--format conf`）与 JSON（v1 offsets），不产出 wire；
- v2 只读保留：`--load-prebuilt-profile` 与 stdin 仍接受 v2；本批不删 v2 读路径；
- 唯一权威：同分支内 v3 为唯一写出格式，v2 只读，不长期并存。

## 10. 测试

- 往返（native/Kotlin/Rust 各自 + 三端对拍）；canonical 确定性 + golden；
- 拒绝向量：非 map 根、缺 `schema`、`schema != 3`、未知 section/key、类型不符、截断、超深/超大；
- **fuzz**：对 native 解码器做随机/变异输入（MPack 本身成熟，但校验层需覆盖）。

## 11. 风险

- MPack 以 C 编译的产品化细节（Makefile C 源、告警隔离）；
- Kotlin/Rust 序列化细节（map 排序/整数宽度）与 native 不一致 → 由对拍兜；
- 迁移面：native + Kotlin + extractor + golden + manifest + 文档同批；
- `sections` 的扁平语义（route 只应用当前 route）需在 v3 保留。

## 12. 待拍板

1. `schema` 值用 3；
2. selection 用 token 字符串还是整数（建议 token 字符串，可读且与 HOCON 同词）；
3. Kotlin 用 `kotlinx.serialization` 还是 `msgpack-core`（建议前者）；
4. v2 读的保留期限（建议一个分支内）。

## 13. 来源

- MessagePack spec；[MPack](https://github.com/ludocode/mpack)（MIT）；
- `src/core/profile/{binary,document,schema}.hpp`、`profile-core/.../NativeProfile.kt`；
- `docs/analysis/offset-ssot-plan.md`、`cve-2026-43284-b5-design.md`。
