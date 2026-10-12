# 43499 · 计划：移植 `select_stack` route（L 级，待认可）

## 一、为什么现在提这个计划

- `multicast_waiter` 这条链：**可比对的层级已全部对齐** ✓（代码/常数/几何/数值/字段/字节/判据语义 ✓；
  两处真实缺口已按退役补齐 ✓；两次误判已更正 ✓）而 `enforce=1` ✗ 与 `uid=2000` ✗ 不变 ⇒
  在**无据**的情况下继续动机制已不合适 ✗；
- 本树**已有**另一支 route 的退役快照 ✓：`docs/analysis/cve-2026-43499-select-stack-route.cpp`（**640 行** ✓）；
- 目标原文里本就写着"**或 43499 的逻辑**" ✓ ⇒ 换一支 route 仍是 43499 ✓。

## 二、范围（只做一条 route，不并行）

| # | 文件 | 动作 |
| --- | --- | --- |
| 1 | `core/src/backend/cve_2026_43499/seam/select_stack_route.hpp` | 新增：`SelectStackRoute<Ops>`（照 `MulticastRoute` 的形状：`execute` / `report` / 三演员 ✓） |
| 2 | `…/seam/select_stack_route_linux.hpp` | 新增：ops（socket/select 相关 syscall ✓，与 `LinuxRouteOps` 同风格 ✓） |
| 3 | `main.cpp` 路由选择处 | 现在报 `route-not-ported` ✗ ⇒ 接上（保持"未移植即具名拒绝" ✓ 的既有风格 ✓） |
| 4 | 夹具 | 新增 `available.cve_2026_43499.route = "select_stack"` 的一份 + 该 route 的键集（形状照 multicast ✓） |
| 5 | `procedures/` | 一份 `select_stack` 的窗口 Lua（照 `43499_window.lua` ✓，步骤名按该 route 的语义 ✓） |

## 三、判据（与既有三级一致 ✓）

1. 门禁：`make -C core -j1 check`（108 PASS ✓ 不得降）+ `make -C core -B android`（0 警 ✓）+ `lint` ✓；
2. 真机（真机门禁规范 ✓：冷机、固定 CPU 对、单 route ✓）：该 route 走上**窗口路径** ✓、`repair 0/0` ✓；
3. **机制级**：`main: selinux enforce` 由 1 变 **0** ✓（直读仪器 ✓，已体检 ✓）；
4. **目标级**：`main: victim uid=0` ✓。

## 四、风险与顺序

- 属**攻击关键路径** ✓ ⇒ 先设计（本页 ✓）→ 最小实现 → 真机判据 ✓；
- **一批一类事** ✓：先只接 route 与键集（不含 Lua 步骤扩展 ✓），验过再进；
- 同批门禁连续失败 2 次即停、回 debug ✓；
- 若 `select_stack` 也撞到同类"不写入" ✗ ⇒ 那就说明问题不在 route ✓（这条 route 的诊断价值独立存在 ✓）。

## 五、今晚已完成的对照资产（本计划的前提 ✓）

- 三级判据 + **直读仪器**（`selinux enforce` ✓ 已用 `getenforce` 独立确认 ✓）；
- 结构对齐清单（14 处差异 ✓、四值逐字 ✓、三步三请求 ✓、修复轮 `0/0` ✓、stamp 元组 ✓、re-poison ✓）；
- 一套可复用的移植模式（`MulticastRoute<Ops>` + `ops_linux` + 窗口 runner ✓）。
