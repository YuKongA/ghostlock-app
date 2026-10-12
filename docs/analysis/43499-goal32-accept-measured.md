# 43499 · 第 32 轮：采纳判据的**测量**（嫌疑排除 ✗，但露出一个新观察 ✗✓）

## 一、做法（✓，先测量后机制 ✓）

```
在建页处（`main.cpp` `build_payload_page` 之后）加入**只打印不拒绝**的诊断 ✓：
  `arm = layout_matches_request(page_request, write_layout)` ✓
  `page = layout_accepts_page(page_request, write_layout)` ✓
并打印 `preserve` / `parent` / `value` ✓（与退役两行警告同源 ✓）。
```

## 二、真机读数（✓，一次跑）

```
main: accept step=w1 arm=1 page=1 preserve=0 parent=0xffffff802adaad80 value=0x0
main: accept step=w2 arm=1 page=1 preserve=0 parent=0xffffff880f914f90 value=0x0
main: accept step=w2 arm=1 page=1 preserve=0 parent=0xffffff89ac6de190 value=0x0
…（w2 各轮同形 ✓）
⇒ ⇒ **arm=1 / page=1 全部通过** ✓ ⇒ 页**不是**"该被换"的页 ✗ ⇒
   ⇒ 「生产路径缺采纳判据」这条嫌疑**排除** ✗（测量一次即定 ✓）。
```

## 三、新观察（✗，下一步要查 ✓）

```
`write_layout.value = **0x0**` ✗（w1/w2 皆然 ✓）
而同一步的既有读数 `main: step=w2 target=… value=0xffffff802abfd588 mode=2` ✓（非零 ✓）。
⇒ 两处对不上 ✗ ⇒ 两种可能：
   ① `write_layout` 与本步实际用来编码页的布局**不是同一个对象** ✗；
   ② `value` 字段与"页里承载的写值"**本就不同** ✓（⇒ 那 `page_write_value` 才是真值 ✓）。
⇒ 判法：把打印改成**同一对象**上的 `value` 与页内实际写入的值对照 ✓（下一轮一行改 ✓）。
```

## 四、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```
