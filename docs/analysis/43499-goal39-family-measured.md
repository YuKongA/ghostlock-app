# 43499 · 第 39 轮：族判据的**测量**（判据定义被修正 ✓）

## 一、做法（✓，只打印不改行为 ✓）

```
在既有 accept 打印里加一列 `fam`（先按 `(target >> 30) == (kAliasBase >> 30)` 判 ✓）+ `target` ✓。
```

## 二、读数（✓✓，一次跑）

```
main: accept step=w1 … fam=1 target=0xffffff802adaad88 ✓（w1 恒同窗 ✓）
main: accept step=w2 … fam=0 target=0xffffff89ccc84f98 ✗
main: accept step=w2 … fam=1 target=0xffffff802e844f98 ✓
main: accept step=w2 … fam=0 target=0xffffffc03dc34648 ✗（⇒ 越界类 ✓）
main: accept step=w2 … fam=0 target=0xffffff897139ab98 ✗
```

## 三、结论（✓，判据定义修正 ✓）

```
`>>30` **太严** ✗ —— 它把同窗的 `0xffffff89…` 也判成"不同族" ✗；
正确的窗 = **39 位线性映射区间** `[0xffffff8000000000, 0xffffffc000000000)` ✓ ⇒ 按此判：
  · `0xffffff80…/0xffffff89…` ✓ **在内** ✓（绝大多数 ✓）；
  · **`0xffffffc03dc34648` ✗ 在外** ✓ ⇒ 与第 10 轮"16 次里 1 次越界"**一致** ✓（抽样 ~1/8 ✗）。
⇒ ⇒ 因此守卫的**上界**确实可以用"39 位区间"表达 ✓ —— 而它**能从上界推导**（VA 位数 ✓），
   或按第 38 轮的保守做法用 `kAliasBase + 窗口` ✓；**绝不能**写成 `>>30` 相等 ✗（会误拦 ✓）。
```

## 四、下一步（✓）

```
① 把测量改成按**区间**判 ✓（`target >= kAliasBase && target < kAliasBase + (1<<38)` ✓ 即 `0xffffffc000000000` 上界 ✓）；
② 真机确认：`fam` 列只对 `0xffffffc0…` 样本为 0 ✓；
③ 之后按退役语义**越界不写** ✓（具名 ✓）⇒ 看是否影响 `uid` ✓。
```

## 五、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```
