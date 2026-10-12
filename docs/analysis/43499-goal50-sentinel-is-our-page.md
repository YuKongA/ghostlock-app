# 43499 · 第 50 轮：**重大澄清** —— 哨兵读的是我们自己的页镜像 ✓✓✓

## 一、事实（✓，逐行 ✓）

```
`main.cpp:951`  `const auto *before = reinterpret_cast<volatile const std::uint32_t *>(**page**);` ✓
⇒ 哨兵的基址是 **`page`**（用户态的 `page_image` 缓冲 ✓），**不是**内核里的拷贝 ✗；
⇒ 因此 `sentinel-after2 == sentinel-before` ✓ 是理所当然的 ✓（内核改不了我们的用户态缓冲 ✓）。
```

## 二、这条澄清改了什么（✓✓，更正第 45 轮的说法 ✓）

```
第 45 轮我写"内核动过这一页 ✗、写落在我们页上 ✗" ⇒ **说法不准** ✗：
真实情况：那些数字（`w+0x1180=1` ✓ / `w+0x1198=491776` ✗ / `w+0x11a8=718974344` ✗）
         是**我们自己编码出来的页**的内容 ✓ ⇒ ⇒ **问题在"页编码"，不在"内核的写"** ✓（第 46 轮方向对 ✓）。
```

## 三、数字的形态（✓✓，指向一处具体错误 ✓）

```
期望：pc = `value` = 0xffffff802abfd588 ✓（64 位内核地址 ✓）；left = `target` = 0xffffff8… ✓；
实测：pc = **491776** = 0x78100 ✓；left = **718974344** ≈ 0x2ADE… ✓ ⇒ **都是 32-bit 量级的"小值"** ✗；
⇒ ⇒ 形态高度像"**写进去的是偏移/槽号，而不是最终地址**" ✗（或"值被截断/取错来源" ✓）；
⇒ 相关代码位置：`payload_page.cpp` 的 waiter 编码段 ✓（`:102 head_word` ✓ / `:105-110 encode_waiter_arm` ✓）
  与 `encode_waiter_arm` 内对 `pc/right/left` 的取值 ✓ ⇒ **下一步就读这两处** ✓。
```

## 四、下一步（✓，就地可读 ✓）

```
① 读 `write_encoder.cpp` 的 `encode_waiter_arm`（`:44` ✓）：看 `pc` 与 `left` 分别被写成**什么量** ✓；
② 读 `payload_page.cpp` 传进去的 `arm`（`arm_for(request, layout.value, pointer)` ✓）⇒ 该臂的 `pc/right/left` ✓；
③ 判据：真机 `w+0x1198 = value` ✓、`w+0x11a8 = target` ✓（全 64 位 ✓）⇒ 再看 `uid` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
