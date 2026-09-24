# Batch 4 切片 3 计划：backend/middleware 从 ExploitProcedure 抽入 Pipeline（2026-09-24）

> 承接 Batch 4 D1=B。切片 1（frontend handoff 抽离）、切片 2（pipeline 形状 + backend 占位）已完成。
> **本切片触及攻击关键路径**，`cmp_disasm` 与真机门禁是门槛。
> 不可变基线：切片 2 候选 `4ee24fbccf7f7e20f3c0aaa9bb3860512c2bd2fcbb8c6ac2ee8f4651ee587bec`
> （已另存 `/private/tmp/ghostlock-b4-slice2-base`）。

## 现状与抽离对象

- `ExploitProcedure`（Template Method 虚基类）同一条 `run` 承载：
  - **frontend**：`handoff`（切片 1 已迁出为 `session/root_child_frontend.*`）；
  - **backend**：`setup` / `w1` / `w2` / `w3` 与共享 `attack_write` / `retry_write_stage`；
  - **middleware**：`attack_write` 内经 `race::run_main_route_threads` 的 route/race 执行，以及
    route 子类 override 的 hook（`resident_write` / `w2_fast_repair_*` / `w1_scratch_repair` /
    `w1_resident_repair` / `w3_exact_target`）。
- 8 个攻击函数（`cmp_disasm` 目标）中，本仓 `attack_write`=`do_one_write`、
  `race::run_main_route_threads`、multicast workers、threads 在其内。其余 `setup/w1/w2/w3/run`
  **不在** 8 函数内。

## 关键事实（决定机器码影响）

- 抽离 **只重排不在 8 函数内的成员**（如 `setup`/`w1`）→ `cmp_disasm` 可保持 PASS。
- 抽离 **`attack_write` 内的 route 执行调用**（`run_main_route_threads`）→ 改 `do_one_write`，
  **8 函数出现差异**，必须逐条复核 + 重建基线 + 真机门禁。
- `w2`/`w3` 依赖 route 子类 override 的 hook，**不能**在不动 route（middleware）语义的情况下
  机械搬离；抽 backend 必须先定义 backend↔middleware 的 hook 契约。

## 分片（建议）

- **3a（不改 8 函数）**：把 `setup`、`w1` 的**编排**移入 `session/backend/cve_2026_43499_backend.*`
  （自由函数，接收 `ExploitSession&`）；`ExploitProcedure::run` 经 `Pipeline` 静态调用 backend。
  `w2/w3` 暂留（因 route hook 交织），在计划中标注。预期 `cmp_disasm` PASS。
- **3b（改 8 函数）**：把 `attack_write` 的 route 执行与 route hook 契约迁入 middleware procedure
  （`route/*`），`attack_write` 变薄入口或迁为 middleware 步骤。**预期 `do_one_write` 差异**，
  以 `4ee24fbc…` 为基线逐条复核，并重新真机门禁。
- **3c**：`w2/w3` 的 victim 生命周期与 route hook 契约收敛（需要 backend↔middleware 接口设计）。

## 不变量

- victim/child 协议与 handoff 时序、资源所有权与清理顺序不变；`ExploitSession` 字段布局不变；
  PI 窗口内无间接调用；route 子类 hook 的语义与调用顺序不变。

## 验证

| 项 | 命令 | 预期 |
|---|---|---|
| host | `make -C src native-host-tests` | 通过 |
| 反汇编 | `cmp_disasm /private/tmp/ghostlock-b4-slice2-base build/native/ghostlock` | 3a：8 函数 PASS；3b：`do_one_write` 差异经逐条复核 |
| 构建/静态 | `make -B -C src ghostlock`、`lint-tidy` | 零告警、0 findings |
| 真机 | 固定 CPU 对、冷机、KernelSU 未加载、multicast | 归档 |

## 待确认

- 是否按 **3a → 3b → 3c** 分片推进（每片独立验证）？3b 会改 8 函数，需接受“经复核差异 + 新门禁”。
- 是否允许为 backend↔middleware 的 route hook 定义编译期契约（不改 route 算法）。

## 进度

- [x] 只读梳理 `ExploitProcedure` 结构与 hook；标出 8 函数影响面；保存基线 `4ee24fbc…`。
- [x] 产出本切片 3 计划（3a/3b/3c）。
- [ ] 用户确认分片与 hook 契约范围。
- [ ] 实现 3a（预期不改 8 函数）。
