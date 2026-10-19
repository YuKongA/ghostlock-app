> 归档说明（2026-10-17）：归档原因 = **载体已删除**（Lua / GLKv3 文档 / script 面 / pipeline-terminator / nlohmann wire_doc / 访问能力层 —— 见 docs/plan/MASTER-PLAN.md §0.1），本文仅作历史记录。原始路径 = docs/analysis/access-capability-layering.md；归档路径 = docs/archive/20261017-1835-access-capability-layering.md。

# 访问能力分层（Access Capability Layering）— L 级设计稿

> **已撤销（2026-10-17 用户裁定）**：本层从未接线（`JniAccessSession.pageCache` / `kernelMemory` 恒为 null），
> 其类型（`AccessDescriptor` / `AccessCapability` / `PageCacheAccess` / `KernelMemoryAccess` / `AccessDomain`）已从源码树删除。
> 保留此文仅为设计记录。

- 状态：**Proposed**（待 design-critic 评审 + 用户裁定）
- 日期：2026-10-17
- 沿革：用户在 2026-10-17 提出「把能力接口与获取 root 的编排链彻底分层」，并指出
  `contract::KernelAccess` 同时承载内核内存与页缓存、语义过宽；本稿把该取向落成可评审的
  类型与迁移步骤。
- 关系：**不改动 ADR-0014 的对外裁定**（脚本面只暴露 kernel memory）；本稿新增的是
  **contract 内部的强类型 façade**，页缓存面不进入 Lua 面。ADR-0012 的两处权威不变。

## 1 问题（现树实测）

| # | 事实 | 位置 |
|---|---|---|
| 1 | `KernelAccess` 的文件注释是「kernel memory, by address」，却承载 `AccessChannel::PageCache` | `core/src/contract/kernel_access.hpp:3`、`:30-38` |
| 2 | 43284 的 `write(address, …)` **忽略 address**，真正落点是 `AccessParams::offset` ⇒ 调用形状与实际权威不一致 | `core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp`（写循环用 `request.params->offset`） |
| 3 | `supports_width()` 只有一个布尔，表达不了「任意地址 / 任意值 / 需要旧字节 / 瞬态」 | `kernel_access.hpp:171` |
| 4 | `read()` 的来源被抹平：43284 的读是该文件旧块（carrier read），不是漏洞产生的原语读 | `pagecache_steps.cpp`（journal/verify 读） |
| 5 | 43499 的 `arbitrary_write` 至今 `false`（fail-closed 正确），但接口层看不出来 | `backend/cve_2026_43499/capabilities.hpp` |

## 2 裁定（提议）

1. **保留 `KernelAccess` ABI 不动**。新增 `contract/access_capability.hpp`，用**强类型 façade**
   表达两类能力：`KernelMemoryAccess`（`KernelAddress` → 字读写）与 `PageCacheAccess`
   （`FilePageTarget{handle, offset}` → 16 B 块读写）。
2. **地址类型必须不同**：`KernelAddress` 与 `FilePageTarget` 不可互换，不再用裸
   `std::uint64_t` 混用。
3. **页缓存的 offset 只有一个权威**：`FilePageTarget.offset` 由 façade 填进
   `FilePageParameters`，`address` 参数恒为 0。
4. **能力描述符**（`CapabilityDescriptor`）替代「一个布尔宽度」：space / read_granularity /
   write_granularity / alignment / arbitrary_address / arbitrary_value /
   requires_existing_bytes / transient / read_provenance。
5. **读来源三分类**（`ReadProvenance`）：Primitive（漏洞本身的读）· Carrier（读载体旧块）·
   Verification（写后回读）。描述符只报该能力实际提供的那一种。
6. **fail-closed**：`KernelMemoryAccess::capability()` 的 `arbitrary_value` 为 `false`；
   页缓存面 `arbitrary_address=true` 仅限文件范围内。

## 3 类型

    struct KernelAddress final { std::uint64_t value; };
    struct FilePageTarget final { std::uint64_t handle; std::uint64_t offset; };

    enum class CapabilitySpace : std::uint8_t { Unsupported, KernelVirtual, FilePageCache };
    enum class ReadProvenance : std::uint8_t { None, Primitive, Carrier, Verification };

    struct CapabilityDescriptor final {
        CapabilitySpace space;
        std::size_t read_granularity, write_granularity, alignment;
        bool arbitrary_address, arbitrary_value, requires_existing_bytes, transient;
        ReadProvenance read_provenance;
    };

façade 只做三件事：填参数、调 `KernelAccess`、把结果原样返回（不改状态机、不持有资源）。

## 4 能力对照（fail-closed）

| 属性 | 43499 内核内存 | 43284 页缓存 |
|---|---:|---:|
| space | KernelVirtual | FilePageCache |
| write_granularity | 8 | 16 |
| read_granularity | 0（不宣称） | 16 |
| alignment | 8 | 16 |
| arbitrary_address | false（受布局/route 约束） | true（文件范围内） |
| arbitrary_value | false（受树操作约束） | true（单块 16 B） |
| requires_existing_bytes | false | true |
| transient | false（内核态） | true（页缓存，重启复原） |
| read_provenance | Primitive | Carrier |

## 5 迁移步骤（每步独立过门禁）

1. **S1（本批）**：新增 `contract/access_capability.hpp` + 单测；无调用方改动、零行为变化。
2. **S2**：43284 的 JNI/Kotlin 调用点改用 `PageCacheAccess`（offset 只从 `FilePageTarget` 进）。
3. **S3**：43499 的调用点改用 `KernelMemoryAccess`；`arbitrary_write` 仍由 provider 的
   `available()/supports()` 回答，描述符只做汇总。
4. **S4**：Lua 面不变（仍只见 kernel memory）；若要把页缓存面暴露给脚本，必须先改 ADR-0014。

## 6 不变量

1. `Unsupported / Declared / Initializing / Ready / Failed / Closed` 六态不变。
2. 生命周期仍在 native session：façade 不持有 SA、socket、bridge、pipe、fd。
3. `write landed` 与 `terminal completed` 是两个独立结果，façade 不合并。
4. 页缓存 16 B 宽度、旧块读取、sequence 连续性不得因分层改变。
5. 描述符是**声明**，不是**实测**；实测仍以 `state()/supports()` 与真机判据为准。

## 7 测试（S1）

- `page_cache_facade_writes_at_target_offset`：offset 只来自 `FilePageTarget`，address==0。
- `page_cache_facade_rejects_non_block_width`：8 B ⇒ `BadWidth`，不调用 provider。
- `kernel_memory_facade_maps_words`：8 B 读写的地址/值透传。
- `descriptors_are_fail_closed`：43499 `arbitrary_value=false`；43284 `requires_existing_bytes/transient=true`。
- `read_provenance_is_explicit`：两面的 `read_provenance` 分别是 Carrier / Primitive。

## 8 文档与 UML

- `docs/development/full-process-uml.md`：Class 图新增两个 façade 与两个强类型；Sequence 不变。
- ADR：本稿不改 ADR-0014 的脚本面裁定；若 S4 被提出，另开 ADR。

## 9 非目标

- 不重命名 `KernelAccess`（避免既有调用与门禁同时失效）。
- 不把 `KernelAccess` 拆成两个虚基类。
- 不宣称任何本机未验证的能力（43499 `arbitrary_write` 仍 false）。
