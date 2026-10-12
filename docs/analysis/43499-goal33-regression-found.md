# 43499 · 第 33 轮：**找到回归** —— 第 2 轮的"臂修正"把 w2 的写值变成 0 ✓✓✓

## 一、铁证（✓，一次跑内两行并排 ✓）

```
main: step=w2 target=0xffffff894a829998 layout matches=1 accepts=1 **value=0x0** mode=2      ✗
main: accept step=w2 arm=1 page=1 preserve=0 parent=0xffffff894a829990 layout_value=0x0 page_write_value=0x0  ✓
· `mode=2` = Credential ✓（语义：把 **init_cred 的别名**写进目标 ✓）；
· 而写值是 **0** ✗ ⇒ 写进目标的也是 0 ✗ ⇒ 受害者的 cred 成空 ⇒ **永不 root** ✓✓（= "写不落" ✓）。
· 对照（旧目标第 252 轮，同一步）：`main: step=w2 … **value=0xffffff802abfd588** mode=2` ✓ ⇒ **曾是非零** ✗。
```

## 二、回归点（✓，就是我第 2 轮那三行 ✓）

```
第 2 轮我把 w1/w2/w3 的 `preserve_child` 按退役公式 `preserve = (leaf != 0)` 改成 false/false/true ✗；
而 `write_layout.hpp:12-16` 的规则句是：
  「a write that **preserves the child** must **carry a value**，and one that does not must carry none」✓
⇒ ⇒ 于是 `preserve=false` ⇒ **value = 0** ✗ ⇒ Credential 写被清成零值 ✗ ⇒ **写不落** ✓✓。
⇒ 结论：**本树的 `preserve_child` 与退役的 `leaf` 极性相反** ✓ ⇒ 当时的"修正"方向错了 ✗
   （当时我确实写下过"这次改动可能错了 ✗"的怀疑，但随后按注释放弃了它 ✗✓）。
```

## 三、回退（✓，下一步 = 三行）

```
把 `main.cpp` 里三步的臂**恢复**为：w1 = true ✓、w2 = true ✓、w3 = false ✓（即第 2 轮改前的那一组 ✓）；
两处 **W2b 修复请求**（第 26 轮由 `replace_all` 改成 true ✗）⇒ 一并回到 false ✗——
  判法：**看写值**（`main: step=w2 … value=` 必须非零 ✓；修复行 `repair …` 同理 ✓）⇒ 据此定极性 ✓，
  而不是再猜注释 ✓。
判据：`main: step=w2 … value=0x…` **非零** ✓ 且 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```
