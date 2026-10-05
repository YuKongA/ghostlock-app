# S4 · R6b 实施设计：组合 token（route 并入 step 的**自由组合**）

> 维护者语法（2026-10-05）：「组合 token 字符串用**简写**：`mcast`/`pselect`/`tcp`，后面的路径不要 `w1`/`w3`，而是 `rootchild`/`shizuku`/`umh`。」
> 维护者修正：「**step token 不是顶层表达，要在 `backend` 下面**；**既然 43284 没有其他 route 就不要前缀**。」
> 维护者前瞻：「43499 以后会支持 umh，43284 以后会支持 root_child」→ 需要能登记**计划项**。
> 前置：R3（canonical HOCON）、R4（string 类型）、R6a（fallback 出栈，`be7b58e`）。

## 1. Token 位置与语法

`token` 属于 **backend 自己的 section**（正是 R2 的 owner-qualified 思路）：

```hocon
backend {
  cve_2026_43499 {            # 有 3 条 route → 需要 route 前缀
    steps = "mcast_rootchild"
    route { mcast { … } }     # 只带所选 route 的几何
    kernel{…} cred{…} offset{…} execution{…}
  }
  cve_2026_43284 {            # 无 route 轴 → 裸 path 名，不加前缀
    steps = "umh"
    lkm_path = "…"  carrier_path = "…"
  }
}
```

- 语法：`<route>_<path>`，仅当**该 backend 有 route 轴**时才带前缀；
- `route ∈ { mcast, pselect, tcp }`（43499）；`path ∈ { rootchild, shizuku, umh }`（**与 backend 正交**）；
- 小写 + `_`；**唯一权威 = 各 backend 的组合白名单**（native catalog 导出），未知 token 绑定期**拒绝并回显 token**；
- 根 `selection{…}`（R3 引入）里的顶层 `steps` 取消：`selection.backend` 保留，`selection.terminal` 由 token 派生（保留为冗余一致性校验）。

## 2. 组合矩阵

### 2.1 `cve_2026_43499`（有 route 轴 → 带前缀）

| token | route | steps（派生） | terminal / entry | 状态 |
|---|---|---|---|---|
| `mcast_rootchild` | multicast_waiter | W1W3 | root_child / App | **可用** |
| `pselect_rootchild` | select_stack | W1W3 | root_child / App | **可用** |
| `tcp_rootchild` | tcp_zerocopy | W1W3 | root_child / App | **可用** |
| `mcast_shizuku` | multicast_waiter | W1W2 | root_child / Shell(Shizuku) | **可用** |
| `pselect_shizuku` | select_stack | W1W2 | root_child / Shell(Shizuku) | **可用** |
| `tcp_shizuku` | tcp_zerocopy | W1W2 | root_child / Shell(Shizuku) | **可用** |
| `mcast_umh` / `pselect_umh` / `tcp_umh` | 对应 route | 待定（暂 W1W3） | umh_forward | **计划**（43499×UMH） |

### 2.2 `cve_2026_43284`（**无 route 轴 → 裸 path 名**）

| token | steps（派生） | terminal / entry | 状态 |
|---|---|---|---|
| `umh` | PageCacheWrite | umh_forward / Shell(UMH) | **可用**（= 今日 wire 值） |
| `rootchild` | PageCacheWrite | root_child | **计划**（43284×root_child） |
| `shizuku` | PageCacheWrite | root_child / Shell(Shizuku) | **计划** |

**可用性模型**（沿用既有两层语义，不新增概念）：已接线 `combination_supported` ／ 设备已核实 `selection_supported` ／ **计划项**（登记 `available=false`）。
计划项：**解析接受、选择门禁拒绝**（错误回显 token），UI **置灰并标注「计划中」**（不隐藏，让用户看到路线图）。
`w1_w2`/`w1_w3`/`pagecache_write` 只作为 token 的**内部派生**，不再是用户可选值。

## 3. wire 影响

- `backend.<id>.steps` 由 **uint id → string token**（R4 的 string 类型已就绪）；这是本批唯一的 wire 形状变化；
- `schema == 3` 不变；manifest 该行的 `wire` 列改 `str`，文档同步；GLKv3 金标随值变化重算；
- 兼容：旧 uint（1 = W1W2、2 = W1W3、3 = PageCacheWrite）仍可解析，映射到等价 token（`*_rootchild` / `umh`）并在诊断记 `legacy_steps_id=<n>`；
- `route.<kind>` 只校验被选中那条的几何；**删除 `RouteKind::Auto` 的几何推断**（选择不再猜）。

## 4. native / Kotlin

**native**：每个 backend 的组合白名单表 `token → { route, steps, terminal, available }`（`contract`）；未知 token `Rejected`、计划项 `Unavailable`；
`component_catalog` 为每个**已接线**组合登记 triple 并 `Pipeline::target` static_assert 锁定。

**Kotlin/UI**：**单一下拉**（取代「模式 + route」双下拉）——**推荐**（设备事实 + 几何）+ **备选**；计划项置灰并标注；
选定后**只写一个 token**；`ExecutionModeMapping` 三模式降级为派生视图（日志/兼容）。

## 5. 门禁

| 门槛 | 判据 |
|---|---|
| host/NDK/lint | 全绿 + 白名单用例（未知 token 拒绝、计划项 `Unavailable`、旧 uint 兼容映射、`terminal` 不一致拒绝） |
| Kotlin | 推荐/备选、计划项置灰、wire 只含一个 token、旧 profile 兼容 |
| **真机 route 自由组合** | `mcast_rootchild` / `pselect_rootchild` / `tcp_rootchild` **各一次**（几何不可用则记为不可用而非失败） |
| `umh` | 由既有 43284 app-call 门禁覆盖 |
| `*_shizuku` | 需 Shizuku UserService → **待 App 内验证**（adb 试验台覆盖不到） |

## 6. 待维护者确认（2 项）

1. **根 `selection.terminal` 去留**：建议保留为**冗余一致性校验**（迁移期最安全），R5 再决定删除；
2. **计划项的 steps**：43499×UMH 暂按 `W1W3`、43284×rootchild/shizuku 暂按 `PageCacheWrite`（实现时定）。

## 7. v3 设计补丁（2026-10-05，来自全量结构审查 F1/F3/F4）

### 7.1 F1 · 组合维度分解（替代扁平乘积）

**问题**：`CombinationKind` 把 `backend × route × path` 枚举成 13 个值；册中还有 4 个占位 backend，扩展时按 backend 数线性膨胀。
**设计（v3）**：保留 `CombinationSpec`（token → 四元组，仍是唯一权威表），但把 id 分解为**两个正交词汇 + 一个 backend**：

```cpp
enum class PathKind : uint8_t { Rootchild, Shizuku, Umh };        // 与 backend 正交（维护者明确）
enum class RouteKind  : uint8_t { None, Mcast, Pselect, Tcp, ... }; // None = 该 backend 无 route 轴（见 7.2）
struct CombinationId { BackendKind backend; RouteKind route; PathKind path; };
```

- `CombinationKind` 仍可保留为**紧凑 id**（token 序号），但**不再作为扩展维度**：新增 backend 只需加一张表；
- 派生量 `{steps, terminal}` 由 `(backend, route, path)` 决定，写在表里（`CombinationSpec.steps/terminal`）而不是枚举名里；
- 迁移成本：`CombinationKind` 的使用点（catalog/诊断/测试）改为 `CombinationId` 或继续用 token 字符串。

### 7.2 F3 · `RouteKind::Auto` 退役：引入 `RouteKind::None`

**问题**：`Auto` 原义「按几何推断」（本批要删），现在又被复用为「该 backend 没有 route 轴」（43284 三条），一符两义。
**设计（v3）**：新增 `RouteKind::None`（= 无 route 轴，**不接受几何校验**）；`Auto` 仅保留给 v1/v2 遗留解码路径并标 `[[deprecated]]`；
`contract/model.hpp:258` 的缺省从 `Auto` 改为**显式 Unsupported/None**：文档未给 route 且组合要求 route → 绑定期拒绝（fail-closed）。

### 7.3 F4 · **token 表导出**（跨语言单一权威，落地前的硬前提）

**问题**：白名单与 `available` 只存在于 native；Kotlin 若手写第二份必然漂移（违反哲学 3/10 与「双侧一致性」约定）。
**设计（v3）**：
1. 新增导出（与 manifest 同机制）：`make -C src combination-manifest` → `combination-manifest.tsv`，列：
   `token<TAB>backend<TAB>route<TAB>path<TAB>steps<TAB>terminal<TAB>available<TAB>doc`；
2. 产物写入 **`app/src/test/resources/`（对拍用）** 与 **`profile-core/src/main/resources/`（Kotlin 运行时读取）** 两份（与 `profile-manifest-v3` 同做法，两份逐字节一致）；
3. Kotlin：UI 单下拉从该资源**运行时读取**（推荐/备选/计划项置灰），禁止硬编码；
4. 新增 `CombinationTokenAgreementTest`（Kotlin 读 test 资源 ↔ adapter 读 main 资源 ↔ 断言与 UI 呈现一致）；native 侧 `combination_manifest_test` 断言导出行与 `kCombinationCatalog` 一致；
5. **落地顺序硬约束**：F4 的导出 + 对拍先于 R6b 的 Kotlin UI 改动进入同一提交。

### 7.4 其它（F2/F5）

- **F2**：`CombinationSpec` 字段按大小降序重排（`string_view` 16 B 在前，`bool` 与枚举相邻），消除 10 B padding 与 10 条 clang-analyzer findings；
- **F5**：`terminal/root_child.hpp` 的声明随 T5 一起搬入 `backend/cve_2026_43499/terminal/`（改动 = 1 处 include + 2 处限定名）。

### 7.5 验收增量（在 §5 之上）

| 门槛 | 判据 |
|---|---|
| 导出对拍 | `combination-manifest.tsv` 两份逐字节一致；native `combination_manifest_test` 与 Kotlin `CombinationTokenAgreementTest` 均绿 |
| lint | 0 findings（F2 修复后 `performance.Padding` 必须消失） |
| 禁用硬编码 | `grep` Kotlin 源码中 token 字面量（如 `mcast_rootchild`）**只允许出现在测试**，运行时路径必须来自资源 |
