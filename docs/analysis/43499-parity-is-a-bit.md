# 43499 · 奇偶 = 泄漏地址的"某一位"，由对齐格决定（round 132）

## 链条（每一环都有行号 ✓）

```
page_geometry.hpp:62-66   align_base(leaked, align) = leaked & ~(align − 1)
run_pipeline_temp.cpp:648 / probe:187   align_region_size = page_bytes * 8U
⇒ base 的 bit16 = leak 的 bit16（当 align = 0x80000 时）
⇒ ⇒ "放置的奇偶" = **泄漏地址的某一位** ⇒ 由**对齐格大小**决定是哪一位 ✓
```

## 真机实测（本轮之前的日志 ✓）

```
base=0xffff800000010000  ⇒ bit16 = 1 ⇒ 奇 ✓（本来能过）
base=0xffff800000080000  ⇒ bit16 = 0 ⇒ 偶 ✗
base=0xffff8000000b0000  ⇒ bit16 = 1 ⇒ 奇 ✓（w1 就是这么过的）
base=0xffff800000040000 / 0x…020000 / 0x…058000  ⇒ 都出现过
⇒ 观察到的 bit16 分布里有奇有偶（2/5/1/8/0xb/4）⇒ 不是恒定 ✗
```

## 推论（下一步只需核对一个公式 ✓）

1. 退役 `prepare_kernel_page ok attempt=1` 每次成功 ⇒ 它的 bit16 **几乎总是**奇 ⇒
2. 这说明**对齐格的取值**（决定"取泄漏的哪一位"）在两边可能不同 ⇒ 属**数值/公式**差异 ✓；
3. ⇒ 下一步：把本树的 `align = page_bytes × 8` 与退役的**同式核对**（退役 `prepare_kernel_page` 里对齐那几行 ✓），
   若不同则按退役改（改的是**公式**，不是机制 ✗）⇒ 再跑 ⇒ 判据：三步 `accepts=1` 不依赖重试 ✓。

## 备注

- 本轮不动码 ✓（先核对公式，改错了会把"偶"再放大一层 ✗）。
