# 43499 · 链头：奇偶 = "page 落在哪个 32 KiB 半区"（round 209）✓✓

## 判据本体（逐字 ✓ main.cpp:394-414）

```cpp
/* Zero 路要求页面值的奇偶合格：`((page_value >> 16) & 1) == 1`
 *  （否则清掉 selinux_state.initialized ✗）⇒ page_value = base + delta + 0x100 ⇒ 奇偶由本次准备决定 */
for (prepare_try = 1; prepare_try <= 8 && !parity_ok; ++prepare_try) {
    run_prepare(...);
    const std::uint64_t candidate = prepared.base + 0x100U;
    parity_ok = prepared.base != 0U && (((candidate >> 16U) & 1U) == 1U);
}
⇒ 等价于：**`base` 必须落在 [0x10000, 0x1FFFF] 的奇数半区**（模 0x20000 ✓）。
```

## 两组的实测（本轮 ✓）

| 组 | 结果 | base 样本 |
| --- | --- | --- |
| canonical（3 跑 / 9 行 ✓） | **全 ok=1** ✓ | 0x…038000 ✓ / 0x…010000 ✓ / 0x…018000 ✓ / 0x…0d8000 ✓ / 0x…030000 ✓（**全在奇半区** ✓） |
| select（1 跑 / 8 行 ✗） | **全 ok=0** ✗ | 0xffff800000008000 ✗（**偶半区** ✗，且 8 次重试**同址** ✗） |

## 由此确定的事实（✓）

```
① 两支的夹具**除 route 键外完全相同** ✓（递归 diff 空 ✓）；
② 但 page base 的半区**系统性不同** ✗（canonical 奇 ✓ / select 偶 ✗，多跑样本都一致 ✓）；
③ 而 route 键在全树**只有 main.cpp:887** 一处被读 ✓（本轮 grep ✓）—— 这与 ② 矛盾 ✗；
④ 树里另有 plan gate / route catalog（`route-not-available` ✓）⇒ 下一批要查它是否**从文档读 route** ✓。
```

## 下一批（精确 ✓）

```
1. 查 `report`/`plan_gate`/`route_catalog` 是否**直接读文档的 route 键** ✓（若是，则 ③ 的前提被推翻 ✓）；
2. 查 `prepare` 的 spray 参数是否**随 route 变化** ✗（如按 route 选步骤集 ⇒ 参数不同 ✓）；
3. 若都否 ⇒ 用"同文档、仅改 route、连跑 6+6"复核 ②（半小时前那次是 4+4 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
