# 43499 · 完整因果链：奇偶 ok=0 ⇒ 页用错 base ⇒ 元组不符 ⇒ 门拒绝（round 208）✓✓

## 一次运行的全部关键行（逐字 ✓）

```
main: prepare parity try=1…8  base=0xffff800000008000 delta=0xfffffffffffff180 cand=0xffff800000008100 ok=**0** ✗
main: step=w1 target=0xffffff802adaad88 layout matches=1 **accepts=0** ✗
main: page tuple at pc=18 -> 0xffff800000008100 (want 0xffffff802adaad80) | right=20 -> 0x0 (want 0xffff800000008100)
                              | left=28 -> 0xffffff802adaad88 (want 0x0)                       ✗ 三处都不符
main: gate probe route=select_stack accepts=**0** leaked=0xffffff802adaad88 base=0xffff800000008000
main: step result w1 => payload-layout-not-accepted ✗
main: refused (payload-layout-not-accepted) ✗
```

## 因果链（本轮定死 ✓）

```
① 奇偶自检 **8 次全 ok=0** ✗（`cand = base + 0x100` 找不到 ✓）⇒ 但它**不阻断** ✓（流程继续 ✓）；
② 于是页面按**直接映射候选**（0xffff800000008000 ✓）构造 ✗，而不是按泄漏到的 0xffffff802adaad88 ✓；
③ 元组三处与"想要的值"全不符 ✗（pc/right/left ✓）⇒ 门 accepts=0 ✗；
④ 步骤 w1 报 payload-layout-not-accepted ✗ ⇒ 整跑在此结束 ✓。
⇒ ⇒ **这是一条自洽的链** ✓ ⇒ 但它是"input 不对"的链 ✗：真正的因在 ①（奇偶）。
```

## 待解（下一批 ✓）

```
奇偶自检 ok=0 ✗ 是**何时**出现的：本晚早些时候 canonical 是能过奇偶的 ✓（有 route status 输出 ✓）；
⇒ 需要查：`prepare parity` 的判据（谁把 ok 置 1 ✓、扫描扫的是什么 ✓）⇒ 以及它是否**依赖路由/文档里的某个键** ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
