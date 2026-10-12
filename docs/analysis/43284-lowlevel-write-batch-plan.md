# 43284 底层写接线 · 批次计划（L 级 · 待认可）

> 关系：补足目标里最后一条未达成的子句 ——「确保 lua 直调底层的 kernelaccess 也可正常执行攻击」✓。
> 现状测量见 `docs/analysis/43284-lowlevel-lua-measurement.md` ✓：底层除 `writeop` 外**全部可达** ✓
> （`calls=6 handoffs=1 steps=0` ✓），只在 `writeop` 上被**具名拒绝** `write-path-unbuilt` ✓。

## 一、目标与判据

- 目标：让底层行 `writeop` / `copyop` 接到**已存在**的写执行体（`PageCacheWrite` + `RealSpliceIo` + 门/桥六件套 ✓，
  它们目前只服务于封装步骤 ✓）⇒ 纯底层 Lua（`procedures/pagecache_load_lkm.lua` ✓）能走完自己的行 ✓；
- 判据：① 该行不再报 `write-path-unbuilt` ✓，且 `calls` 计数随之增长 ✓；② 设备上不改动封装步骤的行为 ✓
  （回归：43284 的 `step` 路径与 43499 的窗口链都保持现状 ✓）；③ 归档一份 `device-gates/43284-lowlevel-*.md` ✓。

## 二、范围与顺序（每步可单独验）

1. **抽一处权威**：把"门 + 桥 + 写"的组装从步骤执行体里抽成**一处**可被两步共用的构造（不新增行为 ✓）；
2. **接线 `writeop`**：在 main 的底层行分派里，用**同一处构造**执行一次写 ✓（参数按映射表读 ✓，缺位具名拒绝 ✓）；
3. **接线 `copyop`**（若与 `writeop` 同构则合并 ✓，否则单独一步 ✓）；
4. **真机**：跑纯底层 Lua ⇒ 该行不再具名拒绝 ✓；再跑封装步骤做回归 ✓；归档 ✓。

## 三、风险

- 攻击关键路径 ✓：改动的是**共用的构造** ⇒ 必须保证封装步骤的路径**逐字不变**（先抽再接线，两步分开验 ✓）；
- 资源生命周期按 §5 逐条核对（门/桥的 fd 与映射 ✓，成功/失败/早退都要收 ✓）；
- 同批门禁连续失败 2 次即停 ✓。

## 四、交付与技术债

- 交付：共用构造 ✓ + 两行接线 ✓ + 真机证据 ✓；
- 技术债：底层行目前只覆盖写；读类行（若有）另批 ✓。

## 五、接线映射（round 104 补：从底层 Lua 的实际调用反推）

底层 Lua（`procedures/pagecache_load_lkm.lua` ✓）里三行的参数形状就是这个映射的依据：

```lua
surface.writeop(h, "absolute", 0x0, 0x14012d83, 4)                 -- space, off, value, width
surface.copyop(h, "direct_map", 0x0, "kernel_offset", 0x0, block)   -- src_space, src_off, dst_space, dst_off, bytes
surface.settle(h, delay) / surface.handoff(h, terminal, payload_ref) / surface.close(h)
```

⇒ C++ 侧映射（`PageCacheWrite` 的面：`open_doors(const SaMaterial &)` + `write_block(position, …)` ✓）：

| Lua 行 | C++ |
| --- | --- |
| `writeop(h, space, off, value, width)` | `door = PageCacheDoorLinux(carrier, len)`；`io = RealSpliceIo(...)` |
| （续） | `win = PageCacheWrite(door, io)`；`win.open_doors(sa)`；`win.write_block(off, value, width)` |
| `copyop(...)` | 同构造 + 另一侧 `read_at`/`splice_one`（若与 writeop 同构则合并 ✓） |
| 无 `sa` 时 | 走既有 `session-frame-*` 具名拒绝 ✓（**不再**报 `write-path-unbuilt` ✓） |

**判据（与 §一 一致 ✓）**：该行不再报 `write-path-unbuilt` ✓、`calls` 计数增长 ✓；
无 SA 的真机运行应当报 `session-frame-*`（诚实 ✓）；归档一份 `device-gates/43284-lowlevel-*.md` ✓。
