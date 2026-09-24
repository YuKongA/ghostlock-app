# Batch 4 切片 3c 方案：backend ↔ middleware route hook 契约收敛（2026-09-24）

> 承接 D1=B。切片 1（handoff）、2（pipeline 形状）、3a（backend setup）、3b（middleware route 入口）
> 已完成，`cmp_disasm` 8 函数 PASS。**3c 会触及 multicast route workers（8 函数），需独立门禁。**

## 现状：route 相关虚 hook

`ExploitProcedure`（Template Method）定义、由 route 子类 override 的 hook：

| hook | 基类默认 | 覆盖者（middleware） | 作用 |
|---|---|---|---|
| `resident_write` | nullopt | multicast（resident 快速路径） | 单次写内 resident 直写 |
| `w1_attempt_cap` | base | ？ | 一次性 route 不可安全重试 |
| `w2_fast_repair_prebuild/activate` | true | ？ | W2 凭据修复 payload |
| `w1_scratch_repair` | true | multicast | W1 后私有 scratch 修复（W1b） |
| `w1_resident_repair` | true | multicast | resident policycap 修复 |
| `w3_exact_target` | false | tcp | W3 精确写 [target] |

这些是 route（middleware）对 backend（W1–W3）流程的定制点，目前经**虚分派**调用。

## 目标与约束

- 把上述 hook 从虚基类分派收敛为**编译期契约**：route policy 已有一组 `static constexpr` 能力
  （`route_policy.hpp` 的 `multicast/w2_fast_repair/w3_exact_target/tcp_payload_layout/allows_fallback`），
  但更细的行为 hook（resident_write / scratch repair）仍是**运行时虚函数**。
- 收敛方向：让 backend 在调用点经 **middleware policy type** 静态分派（`if constexpr` / policy 静态成员），
  不再经 vtable；保持 `route_lifecycle` 的“PI 窗口内无间接调用”原则。
- 不改变 route 算法、时序与 payload。

## 风险（关键）

- route 子类的实现体在 `multicast_waiter_route.cpp` / `tcp_zerocopy_route.cpp` /
  `select_stack_route.cpp`；其中 **`multicast_owner_worker` / `multicast_waiter_worker` 是 8 函数**。
  收敛 hook 会改这些文件的代码形状 → **8 函数可能变化**，需逐条复核 + 真机门禁。
- `w2/w3` 与 victim 生命周期交织；抽离必须保持 child/pipe 所有权与清理顺序不变。
- 现有架构明确“不用虚基类作为 route/provider 扩展机制”，但 `ExploitProcedure` 的虚 hook 是既有
  Template Method；3c 是把它转为编译期 policy 的最后一步，属结构性改动。

## 建议步骤（分片）

1. 在 `route_policy.hpp` 为每个 route policy 声明 hook 的**静态**实现（或 `static constexpr` 能力 +
   自由函数），先与虚实现并存、由 `if constexpr` 选择，验证行为一致（host + 真机）。
2. 逐步删除虚 hook，`ExploitProcedure` 经 middleware policy 静态调用。
3. 全程以 **3b 候选**（`build/native/ghostlock`，SHA 见下）为不可变基线做 `cmp_disasm`；预期
   `multicast_*_worker` 出现差异时逐条复核。

## 不变量

- victim/child 协议与 handoff 时序、资源所有权与清理顺序；`ExploitSession` 字段布局；route 算法/时序/
  payload；PI 窗口内无间接调用。

## 待确认

- 是否推进 3c（会触 8 函数，接受经复核差异 + 新门禁）？或先把 Batch 4 收尾（归档现有切片门禁、
  更新文档），3c 作为独立批次？
- 若推进，hook 收敛的粒度（全部虚 hook vs 先 resident/repair 两类）。

## 进度

- [x] 盘点 route 虚 hook 与 8 函数影响面；产出本方案。
- [ ] 用户确认推进方式与粒度。
- [ ] 实现（分片，带基线对比与真机门禁）。
