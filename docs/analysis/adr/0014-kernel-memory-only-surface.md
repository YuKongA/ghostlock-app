# ADR-0014：对外只暴露 kernel memory；上层步骤上移为 Lua 工具函数

- 状态：**Accepted**（**用户裁定** 2026-10-09）
- 日期：2026-10-09
- 沿革：用户原话「我希望像以前的 kernel-memory 一样，提供一个覆盖所有 backend 能力的虚基类，各 backend 再继承并实现其支持的能力，最后只暴露 kernel memory，然后 lua 只能通过写到具体地址的具体数字，不被原有的各种 selinux seccomp bypass 之类的上层步骤束缚。但是提供一套已经把基础攻击操作整合成上层步骤的 lua 工具函数，供可直接适配已有经验步骤的设备使用」
- **取代**：本会话早前把「三步」做成 native 侧能力/脚本动作的取向（`contract/capability_catalog.hpp` 的两级能力、`script_surface` 的步骤行草案、`lua-order-design.md` 的三形态）—— 那些**不再是对外接口的形状**；其文件保留为该取向的沿革

## 1 裁定

| # | 内容 |
|---|---|
| 1 | **一个虚基类覆盖所有 backend 能力** —— 即 `contract::KernelMemory` 式的接口：**纯虚原语** + NVI 便捷层 ✓；能力以**位集**表达（`CapabilityKind`，与插件 ABI 逐位锁定 ✓） |
| 2 | **各 backend 继承并实现其支持的部分** —— `backend/<id>/kernel_memory.hpp`：`final : public contract::KernelMemory` ✓；**不支持的能力以 `available() == false` 明示**，不是缺失（缺失即编译失败 ✓） |
| 3 | **对外只暴露 kernel memory** —— Lua 侧看到的**只有**这个对象 ✓；不再暴露 selinux/seccomp/bypass 之类的**上层步骤** ✓ |
| 4 | **Lua 只做「写具体地址的具体数字」** —— 即原语面（read/write/write_zero ✓ + 通道/载体选择 ✓），**不被上层步骤束缚** ✓ |
| 5 | **上层步骤改为 Lua 工具函数库** —— 把基础攻击操作**组合**成 `disenforce_selinux` / `credential_forgery` / `seccomp_bypass` 之类的**Lua 函数** ✓，供「已有经验步骤可直接适配」的设备使用 ✓ |

## 2 为什么（用户的理由，如实记录）

- 早前的取向**过于复杂、不易于人类理解** ✓（用户原话「太复杂太不易于人类理解了」）—— 三层（能力目录 × 脚本面 × 提供表）＋三形态裁决，读者要在三张表之间对照 ✓；
- 而「写地址写数字」是**唯一必需的能力** ✓：任何上层的效果都可以由若干次写组合出来 ✓ ⇒ 把上层**放在 Lua** 里 ✓，native 只留**不可再降的原语** ✓；
- 好处：**设备适配面变小** ✓（新设备只要实现原语 ✓），**经验步骤可复用** ✓（写在 Lua 里、随设备调整 ✓），**native 不再随每次步骤改动而变更** ✓。

## 3 受影响（本会话已落件）

| 件 | 处置 |
|---|---|
| `contract/capability_catalog.hpp`（两级能力目录） | **降为沿革** ✓ —— 其 L1/L2 两级的用途被 `CapabilityKind` 位集取代 ✓；文件保留 ✓，不再是对外声明面 |
| `backend/cve_2026_43499/capabilities.hpp`（提供表 + 对拍） | **降为沿革** ✓ —— 「提供什么」改由**该 backend 的 `KernelMemory` 实现里的 `available()`/`supports()`** 回答 ✓（一处、且是运行期真值 ✓） |
| `script_surface` 的步骤行草案（未落 ✓） | **不再需要** ✓ —— Lua 由**工具函数库**表达上层 ✓ |
| `lua-order-design.md` 的三形态 | **不再适用于步骤** ✓ —— 其核心洞察（顺序归 Lua ✓）**仍然成立** ✓，但载体从「脚本声明的步骤序列」变成「**Lua 工具函数的调用顺序**」✓ |
| `steps/step_runner.hpp` · `target_state`/`session.hpp` | **保留** ✓ —— 它们是**实现侧**（backend 如何完成一次写 ✓），不属对外接口 ✓ |

## 4 落地形状（动工前出设计稿 + 评审）

1. `contract/capability.hpp` —— **能力位集**（`CapabilityKind`）与结果/状态词汇（`CapabilityResult` / `CapabilityStatus` / `CapabilityState`）✓，与插件 ABI 逐位锁定 ✓；
2. `contract/kernel_memory.hpp` —— **虚基类**：纯虚 `read` / `write` / `write_zero` / `supports(...)` ✓ + NVI 的 `read64` / `write64` ✓；
3. `backend/<id>/kernel_memory.hpp` —— **继承并实现** ✓（43499 走它的 route/write 原语 ✓；43284 走 LKM/页缓存原语 ✓）；
4. **暴露点唯一**：组合根按**选中的 backend** 构造一个 `KernelMemory` ✓，Lua 只能拿到它 ✓；
5. **Lua 工具函数库**（新件 ✓）：`disenforce_selinux` / `credential_forgery` / `seccomp_bypass` 等 ✓ —— 用**原语**组合而成 ✓，按设备可替换 ✓。

## 5 未定（不猜）

- **原语的最小集合**：今天旧树有 `read` / `write` / `write_zero` ✓ + `MemoryChannel` / `CarrierKind` ✓ —— 新树是否照搬、以及是否需要「发现地址」这类**非写**原语（旧树的 `AddressDiscovery` 位 ✓）⇒ 设计稿定 ✓；
- **Lua 工具库的位置与形态**（随 app 资产 / 随 profile / 内嵌 ✓）⇒ 设计稿定 ✓；
- 旧 `script_surface` 的 7 个名字（`backend` / `writeop` / `handoff` / … ✓）**留哪些** ⇒ 设计稿定 ✓（`handoff`/`close` 属会话生命周期 ✓，与内存原语不同轴 ✓）。
