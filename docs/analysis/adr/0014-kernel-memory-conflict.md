# ADR-0014 附：冲突与两条路（虚基类 vs 既有机器判据）

> 本件是 ADR-0014 的组成部分（正文 `0014-kernel-memory-only-surface.md`）· 2026-10-09 · 由 Lead 当场质疑并给出合规替代，待用户裁决

## 6 冲突与两条路（Lead 当场质疑 · 2026-10-09）

**用户要的形态里有一条与本树既有的三条机器判据正面冲突** ⇒ 按守则的「当责」条，先质疑、给代价、给合规替代，由用户裁决。

**冲突点：虚基类 / vtable** ✗（用户原话「提供…**虚基类**，各 backend 再继承」）。**实测证据（我逐条核过）**：

| # | 位置 | 内容 |
|---|---|---|
| 1 | `src/Makefile:806-808` | `nm -C <四个二进制> \| grep -c _ZTV` ⇒ **非零即 `VTT-HIT` + `exit 1`** ⇒ 落地虚基类**当场红**；**实测今天为 0** ✓ |
| 2 | `race/pi_window.hpp:14-16/:40` | 窗口禁令之一：**no indirect dispatch（no vtable, no function-pointer hop, no type-erased callable）** ✓，且注明 **each is machine-checked** ✓；违规项原文「a vtable … makes the window shape unverifiable」✓ |
| 3 | `script/call_sink.hpp:9-11` | 本树**既定选择**已写明：「**Shape: a function-pointer table, not a virtual class** … the gate scans built binaries for `_ZTV` symbols, so no vtable is introduced」✓ |

⇒ 理由不是洁癖 ✓：PI 窗口的**形状必须可验证** ✓（虚调用让「窗口里到底跑了什么」不可判定 ✓）。

### 路径 A（**推荐**，合规且更贴合用户原意）

**用旧树本来就有的 `KernelMemoryOps` —— 一张函数指针表** ✓：

```cpp
struct KernelMemoryOps final {
    std::int32_t (*read) (void *ctx, std::uint64_t address, void *out, std::size_t size) noexcept = nullptr;
    std::int32_t (*write)(void *ctx, std::uint64_t address, const void *in, std::size_t size) noexcept = nullptr;
    [[nodiscard]] bool available() const noexcept { return read != nullptr && write != nullptr; }
};
```

⇒ 三条意图**全部满足** ✓：① **一个接口覆盖所有 backend 能力** ✓（就是这张表 ✓）② **各 backend 实现自己支持的部分** ✓（各自**填表**，未支持者留空 + `available()` ✓ —— 与「继承但只实现支持的」等价 ✓）③ **对外只暴露这一个对象** ✓（Lua 只拿到这张表 ✓，而 `call_sink` 已经是「函数指针表给脚本层」的既有先例 ✓）。
**代价：0** ✓ —— 不碰任何机器判据 ✓；且它**正是旧树 `kernel_memory.hpp` 里的 `KernelMemoryOps`** ✓ ⇒ 比 vtable **更符合**用户说的「像以前的 kernel-memory 一样」✓。
**若要跨 backend 一个类型名** ✓：`variant`/tag + **直接调用的 switch** ✓（禁 `std::visit` 型间接跳转 ✓，`std::function` 亦禁 ✓）。

### 路径 B（**用户强制确认才可走**）

坚持真 vtable ✗ ⇒ 属**越过已批准的结构约束** ✓ ⇒ 须：**强制确认 + 沿革记录 + 技术债登记** ✓，且**同批改**三处：① `pi_window` 的禁令文本与 :40 的违规项 ✓；② `Makefile` 的 `_ZTV` 扫描 ✓（删或放宽 ✓）；③ `call_sink` 的既定选择注释 ✓ —— 即**明确承认「窗口形状不可验证」**这一代价 ✓。

⇒ **Lead 建议走 A** ✓：它零代价、不破判据，且与用户原话的「像以前一样」**更一致** ✓。
