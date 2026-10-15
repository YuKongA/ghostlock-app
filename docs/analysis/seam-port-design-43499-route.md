# 附：43499 侧下一件的切法（一页可验收）

> 本件是 `landing-port-design-43499.md` 的附件 · 2026-10-09

## 19 43499 侧下一件的切法（一页可验收 · 供派工时用）

**目的**：把「接真控制器」切成**一次能完成**的件（本会话的教训：290 行的整体委派零产出，切成「ECB 单块 + 一条向量」后立刻推进）。

### 第 1 件（最小可验收）：派发层 + 一个**假策略**

- **交什么**：`core/src/backend/cve_2026_43284/../cve_2026_43499/route/policy_dispatch.hpp`（新件）—— 只含**派发**本身：一条**编译期策略链**（照旧 `route_policy.hpp:234` 的 `for_each_policy` + 泛型 lambda ✓），每条策略由 `P::supported(values)` 把门 ✓，命中即 `P::run(request)` ✓，未命中返回 `ROUTE_UNSUPPORTED` ✓；
- **与旧件的唯一实质差异（ADR-0015）**：旧签名 `run_route(const profile::TargetProfile &, const WriteRequest *)` ⇒ 新的**不持 profile** ✓ 改为收**一组由脚本给的值**（先定义最小的那个值结构，字段只要派发真正会用到的 ✓）；
- **验收判据**：一个**完全虚构的策略**（测试里定义 ✓）——`supported` 为真 ⇒ 它被调用**恰好一次** ✓ 且返回值被透传 ✓；`supported` 为假 ⇒ **不调用** ✓ 且返回 `ROUTE_UNSUPPORTED` ✓；两条策略都真 ⇒ **只有第一条**被调用（命中即停 ✓）；**并证伪一次**（把 `handled` 逻辑去掉 ⇒ 断言必须失败 ✓）；
- **不需要**：route 体、`race`、任何系统调用、任何 profile ✓；
- **不要动** `core/Makefile`（接线归 Lead ✓）。

### 第 2 件（之后）：一条 route 体

- 按切片 1 表的 **22 处耦合**逐处搬（表已存在 ✓）；判据是**能直跑**（不需要设备 ✓）⇒ 之后再接 `Race` ✓。

### 第 3 件：接 `RouteExec` 缝

- 把我已落的 `landing/multicast_landing.hpp` 的 `RouteExec::execute(address, value, offset)` 接到第 2 件的 route 体上 ✓ ⇒ 那时 43499 侧**第一次在 host 上闭环到真控制器** ✓。

⇒ 这样切的好处：**第 1 件不依赖任何攻击代码** ✓ ⇒ 它可以独立完成并验收 ✓ ⇒ 即使后面两件拖延，派发层也已经是**真件**而不是占位 ✓。

## 20 缺口（Lead 实测 · 真机阶段的第一步）：工厂没有定义，两个 Access 都不可达

在进入真机阶段前我先量「43284 的 `Access` 有没有人调」 ⇒ 结果是**没有** ⇒ 这决定性地改变了下一步该做什么。

### 实测

```
grep -rn 'KernelAccess::create|create(BackendId' core/src/
  → 唯一命中：contract/kernel_access.hpp:162 的**声明**（static std::unique_ptr<KernelAccess> create(BackendId)）
  → 没有任何**定义**，也没有任何**调用者**
grep -rln 'cve_2026_43284/kernel_access.hpp' core/src/
  → tests/pagecache_chain_test.cpp（测试）+ 该文件自身
grep -rn 'cve_2026_43284' core/src/pipeline/ core/src/*.cpp
  → 只命中 main.cpp 的 lkm 件（--probe 路径），**不是** KernelAccess
```

### 结论（两种说法，都要说清）

1. **实现面**：两个 backend 的 `Access` 都写完了 ⇒ 这一步**可以算完成**；
2. **可达面**：它们**从生产代码里不可达** ⇒ 契约里声明的工厂 `create(BackendId)` **没有定义** ⇒ 于是「真机测试」还差**可达性**这一步。

### 因此真机阶段的第一步不是设备，而是**工厂**

要做的是一件小件：给 `KernelAccess::create(BackendId)` **一个定义**（按 id 分派到各 backend 的 `make_unique` ⇒ 头文件按需 include 两个 backend 的 `kernel_access.hpp`）⇒ **并给一条「id → 正确类型」的测试**（每个 id 造一次 ⇒ 断言 `state()/supports()` 与预期的 backend 一致 ⇒ 未知 id ⇒ 空指针 ⇒ **并证伪一次**）。

⇒ 这一件完成后，两个 `Access` 才**真的能被调用** ⇒ 之后的临时入口（真机用）也才有东西可调。

### 与目标的关系（如实说）

目标要求「完成两个 backend 的 kernelaccess 实现后 ⇒ 通过**任何临时方法**测试其在真机可用」⇒ 现在卡在**可达性**上 ⇒ 而**不是**卡在设备上（设备另有依赖：SA 材料来自会话（SPI/端口/密钥）⇒ 不在仓库里）。
