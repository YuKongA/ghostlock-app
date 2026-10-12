# 43499 · 谜团解开：route 值经 `op_registry` 改变行为（round 210）✓✓✓

## 决定性一读（✓）

```cpp
// core/src/contract/op_registry.hpp:103
{"window.pagecache", route_bit("select_stack") | route_bit("tcp_zerocopy"), false, true, …}
```

⇒ `window.pagecache` 这个 op **只对 `select_stack`（与 `tcp_zerocopy`）开放** ✓；
⇒ 对 `multicast_waiter` **不开放** ✗ ⇒ ⇒ 所以 route 值**确实改变行为** ✓（经契约/plan gate ✓）。

## 我的前一处结论错在哪（✗，本轮更正 ✓）

```
我说"available.<id>.route 全树只有 main.cpp:887 一处被读" ✗ —— 那只覆盖了**直接读文档**的写法 ✓；
而 route 值还会经 `route_bit(...)` 进入 **op_registry**（编译期表 ✓）⇒ 被 plan gate 使用 ✓；
⇒ ⇒ 因此两个"仅差 route 键"的夹具**本就该表现不同** ✓ ⇒ 这解释了：
   · select 夹具落在**偶半区** ✗（window.pagecache 打开 ⇒ 改走另一条准备/窗口路径 ✓）；
   · canonical 夹具落在**奇半区** ✓（window.pagecache 关闭 ⇒ 走已知的 multicast 准备路径 ✓）。
```

## 由此得到的结论（重要 ✓）

```
1. select 夹具**不是**"只换了 route 的 canonical" ✓ —— 它在契约层被授予了不同的 op 集合 ✓；
2. 因此此前"同一夹具行为系统性不同"的矛盾**消解** ✓（不是矛盾，是契约差异 ✓）；
3. 也让"select 分支从未被走到"的解释更完整 ✓：那一轮的页来自**另一条准备路径** ⇒ 元组/base 不同 ✓。
```

## 下一批（精确 ✓）

```
查 `window.pagecache` 这个 op 对准备/窗口路径的**具体影响** ✓：
  · 谁按它决定"是否跑某段准备/窗口" ✓（plan gate → executors ✓）；
  · 若 select 本意就是走这条路 ⇒ 需要把 select 的准备参数与它对齐 ✓（而不是改成关掉它 ✗）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
