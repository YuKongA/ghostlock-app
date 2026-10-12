# 43284 侧现状与两半的依赖关系（round 103）

## 已经实现的两件事（目标原文里的那两件 ✓）

```
wire-43284-lowlevel.json : step.route=pagecache ✓ required_param=["sa"] ✓
                           Lua = procedures/pagecache_load_lkm.lua ✓（封装步骤路线 ✓）
wire-43284-needs-sa.json : Lua 里 surface.backend("cve_2026_43284") ⇒ 直接调底层 ✓
                           （即目标里"完全重写成 lua 不调用封装接口测试底层可访问性" ✓）
wire-43284-steps.json    : backend.cve_2026_43284.steps = ["patch1","carrier","hook","trigger","wait"] ✓
                           （即目标里"对于 43284 也是（封装步骤）" ✓）
```

## 真机现状（本轮逐字）

```
./ghostlock < wire-43284-lowlevel.framed   ⇒  RC=9  main: refused (write-path-unbuilt)
```

## 为什么停在这里（读码得到，非猜测）

```
class PageCacheWrite : PageCacheWindow {
    bool open_doors(const SaMaterial &sa) noexcept override;      ◀ 写路径**必须**先有 SA
    bool write_block(std::uint64_t position, …);
};
⇒ SA 经会话帧建立（ip xfrm）⇒ 需要 CAP_NET_ADMIN ⇒ shell 做不到
⇒ 所以这一行诚实地报 write-path-unbuilt（而不是谎报"没设备"）
```

## 结论：两半是**有序**的

1. **43499** 那条链负责把 root 拿到（本晚已修 14 处，写仍未落地 ✗）；
2. root 到手后，**43284** 的 SA/会话帧才立得起来 ⇒ 它的写路径才能接上；
3. ⇒ 目标里那句"…改成符合 schema 的配置传入执行，**或 43499 的逻辑**"正是这个顺序 ✓。

## 下一步（面向 43284 的可做项）

- 可做（不依赖 root ✓）：把 `writeop` 接到"门 + 桥 + 写"的**共用构造**上（计划已写 ✓，
  `docs/analysis/43284-lowlevel-write-batch-plan.md` ✓）⇒ 这样在**有 SA 的场合**（真机 + 会话帧 ✓，
  或 root 之后 ✓）该行即可工作 ✓；判据：该行不再报 `write-path-unbuilt` ✓、`calls` 计数增长 ✓；
- 不可做（需要 root ✗）：SA 本身。
