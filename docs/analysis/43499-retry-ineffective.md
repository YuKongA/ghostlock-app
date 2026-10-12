# 43499 · 奇偶预检证明正确，但"重试"无效 —— 元凶就是 quarantine（round 125）

## 真机逐字（本轮）

```
main: prepare parity try=1 base=0xffff800000040000 delta=0xfffffffffffff180 cand=0xffff80000003f280 ok=1
main: step=w1 target=0xffffff802adaad88 layout matches=1 accepts=1 value=0xffff80000003f280   ← 预检 = 判据 ✓

main: prepare parity try=1 base=0xffff800000010000 cand=0xffff80000000f280 ok=0
main: prepare parity try=2 base=0xffff800000010000 cand=0xffff80000000f280 ok=0
main: prepare parity try=3 base=0xffff800000010000 ... ok=0
main: prepare parity try=4 base=0xffff800000010000 ... ok=0      ← ★ **重试拿到同一个地址** ✗
```

## 两条结论（都有物证 ✓）

1. **预检算法正确** ✓：`candidate == layout value`（`0xffff80000003f280` ✓）⇒ 奇偶判断与判据一致 ✓；
2. **重试机制无效** ✗：`run_prepare` 每次都给出**同一块**（`0xffff800000010000` ×4 ✓）⇒ 奇偶不可能变 ✗。

⇒ ⇒ 而**这正是退役 `quarantine`（隔离/释放 reclaim sockets ✓）要解决的问题** ✓：
   不释放被隔离的块 ⇒ 地址池不轮换 ⇒ 同一地址反复回到手上 ⇒ **必须"释放再放"才能换地址** ✓。

## 下一步（顺序已清楚 ✓）

1. **实现 quarantine 等价物**：`prepare` 之前先"放开"上一轮的隔离块（本树对应物：先关闭/释放 shaping 资源
   ✓ 或在 `prepare` 内部加"释放上一轮 base"的步骤 ✓）⇒ 让重试真的换地址 ✓；
2. 再验奇偶：三步 `accepts=1` ✓；
3. 之后回到 `uid=0` ✓。

## 备注（诊断痕迹）

- 本轮加的那行 `prepare parity … delta/cand/ok` 是**诊断** ✓ ⇒ 顺序 1 完成后并入正式日志或清理 ✓（登记 ✓）。
