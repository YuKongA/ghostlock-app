# 43499 · 更正：组数改动**没有**解决奇偶（round 134）

## 事实（连跑 3 次 ✓）

```
run1/2/3: 每次都出现 accepts=0，且链在中途具名拒绝：
  main: step result w1 => payload-layout-not-accepted
  main: refused (payload-layout-not-accepted)
  main: step=w1 target=0xffffff802adaad88 layout matches=1 accepts=0
⇒ 无一行 uid（在 w1 就停了）
```

## 因此要更正 round 133 的措辞 ✗

- 我在 133 轮依据"一次跑三步全过 + try=1"写了"行为与退役对齐" ✗ ⇒ **那是过判** ✓（单次样本 ✗）；
- 正确表述：**组数改动只改变"泄漏落在哪"的分布** ✓，不保证奇偶 ✓；奇偶仍是每步约 1/2 的硬币 ✗；
- 组数保留（更接近退役的 groom 规模 ✓），但它**不是**奇偶的解 ✗。

## 与更早结论的一致性

这与 round 131 的结论一致 ✓：**要让 base 变，必须换掉状态本身**（三槽 `Current`/`Quarantined`/`Prebuilt` ✓）；
而"重试"在现状下拿同一个 base ✗ ⇒ 奇偶失败就直接具名拒绝 ✓。

## 下一步（唯一有据的路 ✓）

1. 按退役三槽语义，让奇偶不合格时**换状态**（`quarantine` 当前状态 ⇒ 再准备 ⇒ 合格则释放旧的 ✓）；
2. 判据：连续 3 次真机跑，`w1/w2/w3` 均 `accepts=1` 且**无 `payload-layout-not-accepted`** ✓；
3. 然后回到 `uid=0` ✓。
