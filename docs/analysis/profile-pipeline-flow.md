# 全流程 UML：HOCON → 配置模型 → wire → profile → 最终处理

> 术语澄清（先说结论，避免混淆）：
> - **HOCON**：`app/src/main/assets/profile/*.conf`，仅 App 侧解析，文件内 `schema_version = 1`。
> - **"kprofile"**：代码里**没有**叫这个名字的类型。实际是 HOCON 解析后的两层内存模型：`ValueMap`（原始合并结果）与 `Profile`/`ProfileConfig`（App 展示 + 原生文档载体）。
> - **wire**：**GLKv3 MessagePack** 字节流（根 map，`schema = 3`），是唯一的跨语言传输格式。
> - **profile（native）**：`profile::Document`（中性 ID/分段容器）→ 按 owner 绑定的强类型 `Schema::View`。
> - `profile.conf` / `profile.bin`（`Download/GhostLock/<时间>/`）是**调试转储**，不是传输通道。

## 1. 端到端序列（App → native → 设备）

```mermaid
sequenceDiagram
    autonumber
    participant UI as App UI (Compose)
    participant VM as GhostlockViewModel
    participant Repo as AndroidGhostlockRepository
    participant CC as AndroidProfileConfigController
    participant AC as AssetConfigLoader + HoconSupport
    participant PM as ProfileMerger / ProfileResolver
    participant NPD as NativeProfileDocument
    participant GE as NativeProfileGlkv3Adapter + Glkv3Encoder
    participant CB as ChannelBStdin
    participant N as native main.cpp
    participant PE as profile_entry
    participant OR as pipeline::orchestrator
    participant BE as backend (43499 / 43284)
    participant TE as terminal (root_child / umh_forward)

    UI->>VM: 点「执行」(mode + backend)
    VM->>Repo: runExploit() / runExploitWithShizuku()
    Repo->>CC: load(release, pair)
    CC->>AC: readIndex() / readAsset(<release>.conf, execution-<route>.conf)
    AC->>AC: include 展开 + HOCON 解析(schema_version=1 校验)
    AC-->>CC: ValueMap（builtin / imported / overrides + route presets）
    CC->>PM: resolveMerged(...) 合并 + 校验
    PM-->>CC: 合并后的 ValueMap（= App 侧 profile）
    CC->>NPD: buildNativeDocument(release, full) → 缓存 document
    Repo->>CC: nativeDocument(config)
    CC->>GE: adapt(document, terminal.token) → encode(...)
    GE-->>Repo: GLKv3 bytes（MessagePack，schema=3）
    Repo->>CB: compose(doc[, 43284 会话帧])
    CB-->>N: stdin: [u32be len][doc] [+ [u32be 84][frame]]
    N->>PE: read_glk1_frame_stdin() / read_glk1_file()
    PE-->>N: profile::Document（release/tokens/sections）
    N->>OR: selection{backend,steps,terminal} + run_orchestrated_pipeline()
    OR->>BE: backend stage（owner schema 绑定 + steps/chain）
    BE-->>OR: chain 结果（writes/hook/trigger/LKM）
    OR->>TE: terminal stage（root_child / umh_forward 就绪确认）
    TE-->>OR: Done / Failed
    OR-->>N: RunResult{code, stage}
```

## 2. 数据形态流转（每一跳的确切产物）

```mermaid
flowchart LR
    A["assets/profile/<br/>index.conf · &lt;release&gt;.conf · execution-*.conf<br/>（HOCON 文本, schema_version=1）"]
    B["ValueMap<br/>（include 展开 + HOCON 解析）"]
    C["合并后的 ValueMap<br/>builtin → imported → overrides<br/>+ execution-*.conf 路由预设"]
    D["NativeProfileDocument<br/>owner 分段：backend.* · cred · execution.* · route.*"]
    E["Glkv3Document<br/>根键：schema=3, backend, terminal, release, sections"]
    F["MessagePack 字节<br/>canonical：最短整数 + 键按 UTF-8 排序"]
    G["profile::Document<br/>release / backend_token / terminal_token / sections[]"]
    H["ComponentSelection<br/>contract::BackendKind × StepSetKind × TerminalKind"]
    I["owner Schema::View<br/>43499: backend_profile::bind<br/>43284: profile::bind&lt;Cve2026_43284Schema&gt;"]
    J["runtime 使用<br/>chain / route / 终端 / 会话密钥(仅43284)"]

    A --> B --> C --> D --> E --> F -->|stdin 或文件| G --> H --> I --> J
```

## 3. 两条入口 + 会话密钥通道

```mermaid
flowchart TD
    subgraph App 侧
      D1["GLKv3 文档字节"]
      F1["84B 会话密钥帧<br/>（version/kind/spi/encap_port/sender_port/icv/aes/hmac/trailer）"]
    end
    E1["--ghostlock-app-call<br/>（stdin）"]
    E2["--load-prebuilt-profile <bin><br/>（文件）"]
    P1["profile_entry::read_glk1_frame_stdin()"]
    P2["profile_entry::read_glk1_file()"]
    SEC{"backend == 43284<br/>且 --enable-status-record ?"}
    ACK["status-record ACK 通道<br/>（stdin 逐行 \x1eGLK_STATUS_ACK）"]

    D1 --> E1 --> P1
    F1 -. "仅 43284 追加在文档之后" .-> E1
    D1 --> E2 --> P2
    P1 --> SEC
    SEC -->|是| ACK
    SEC -->|否| X["不读会话帧"]
```

## 4. native 内部组件与所有权

```mermaid
flowchart TD
    MAIN["main.cpp（薄适配：parse → stage 序列）"]
    ENTRY["profile_entry（stdin/file → profile::Document）"]
    CAT["pipeline/component_catalog.hpp<br/>（组合唯一权威：稀疏 triple）"]
    ORCH["pipeline/orchestrator.hpp<br/>（按 catalog 分派）"]
    PIPE["pipeline/pipeline.hpp<br/>Pipeline&lt;Backend,Terminal&gt;"]
    SESS["session::CoreSession<br/>g_exploit_session（唯一可变全局）"]
    CT["contract/**（中性词汇 + owner schema + 能力接口）"]
    BE49["backend::cve_2026_43499<br/>route · primitives · steps(W1/W2/W3)"]
    BE84["backend::cve_2026_43284<br/>ipsec · pagecache · steps/chain · lkm_window"]
    T1["terminal::root_child"]
    T2["terminal::umh_forward"]
    PLG["plugin/**（外挂对策：loader/registry/controller/host_ops/kernel_channel）"]
    PLAT["platform/**（设备事实、vivo 对策）"]

    MAIN --> ENTRY --> ORCH
    MAIN --> CT
    MAIN --> SESS
    MAIN --> PLG
    ORCH --> CAT --> PIPE
    PIPE --> BE49
    PIPE --> BE84
    PIPE --> T1
    PIPE --> T2
    BE49 --> PLG
    BE84 --> PLG
    PLAT --> PLG
```

## 5. 我这些天改了什么 / 没改什么（可 `git log` 复核）

| 区域 | 是否改动 | 说明 |
|---|---|---|
| HOCON 加载（`AssetConfigLoader`/`HoconSupport`） | **未改** | 今天对该路径 0 提交 |
| 合并/解析（`ProfileMerger`/`ProfileResolver`） | **未改** | 同上 |
| GLKv3 编解码（`Glkv3Encoder`/`NativeProfileGlkv3Adapter`/`glkv3_parse`） | **未改** | 同上（`schema==3` 与键集未动） |
| `ChannelBStdin` / 会话帧布局 | **未改** | 只做过**字节对拍验证**（与 native `session_frame.cpp` 一致） |
| owner schema / bind 机制 | **未改** | `profile::bind<>`、`schema.hpp` 未动 |
| 43284 生产绑定（`execution_binding`/`backend_terminal`/`chain`） | **改过** | hook guard `Reject→Skip`、终点标记、等待 5s→15s、**设备事实降级**、LKM 窗口接线 |
| `terminal/umh_forward` 就绪探针 | **改过** | 改为「app 域可观测的标记优先；不可观测 ≠ 未就绪」 |
| `plugin/`（原 `ancillary`+`countermeasure`） | **改过** | γ 批机械改名 + δ 批新增 `kernel_channel`/`host_ops` |
| `contract/abi/glk_contract_abi.h` | **改过** | 仅**追加** LKM 段与注释；旧字段/位值不变 |
| LKM（`tools/lkm/ghostlock`） | **改过** | 常驻通道 + 会话绑定卸载 + pre-UMH 注册/权限 |
| App Kotlin | **改过** | 路由（43284 直连 vs Shizuku）、文案、构建目录外移 |
| `app/src/main/assets/**` | **未改** | 0 提交 |

## 6. 若你怀疑「被弄乱」，优先核对这些不变量

1. `schema_version = 1` 只属于 **HOCON**；wire 必须是 `schema = 3`——两者混用会在 `glkv3_parse` 直接拒绝；
2. GLKv3 根键**只有** `backend / release / schema / sections / terminal`（可选键缺失即不写）；
3. 分段名 owner-qualified：`backend.<id>`、`cred`、`execution.*`、`route.*`；
4. 会话密钥**永不进文档**，只在 `[u32be len][doc]` 之后的 84B 帧里、且仅 43284；
5. 三端一致性由测试兜底：`route_catalog_test`、`profile_manifest_v3_test`（↔ `profile-manifest-v3.tsv`）、`profile_manifest_test`（↔ `profile-manifest.tsv`）与对应 Kotlin 的 `*AgreementTest`。
