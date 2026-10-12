# 43499 · 第 49 轮：两处基址的表达式到手（+ 一条负增量线索）✓✓

## 一、两个基址（✓，源码逐行 ✓）

```
哨兵读（`main.cpp:711` ✓）：`our_page = words_region.base() + WordsRegion::kPayloadOffset()` ✓
                       哨兵再按 `+0x1180 / +0x1198 / +0x11a8` 取字 ✓ ⇒ 即"页区基 + 槽内偏移"✓；
建页（`main.cpp:718` ✓）：「缓冲 = **payload_delta**（页面在块内的位移）+ 页长；页面**建在 delta 处**」✓；
  且 `:731-735` 明确注明：**`payload_delta` 是负值（-0x10000 ✓）**，「当无符号加会错位」⇒ 已按**有符号**处理 ✓；
建页时的槽地址（修复轮读数 ✓）：`repair_inputs.waiter_word = repair_base + page_inputs.slots.waiter` ✓，
  而 `repair_base = prepared2.base + payload_delta` ✓ ⇒ ⇒ **建页 = 块基 + delta + 槽偏移** ✓。
```

## 二、由此得到的两条待判（✓，都可用"打印两个基址"一次定案 ✓）

```
① **基址是否同一来源**：若哨兵的"页区基"**不含** `payload_delta`（或含的方式不同 ✗）⇒
   两处相差 delta（例：0x10000 ✓）⇒ 恰好呈现"部分字段看似正确、部分错" ✓；
② **有符号/无符号**：`:731-735` 已修过一次同类问题 ✓ ⇒ 需确认**哨兵侧**的地址算术也按**有符号**处理 ✓，
   否则读的是"降到低地址"的字 ✗（⇒ 与读数里出现的 32-bit 量级数字相符 ✓）。
```

## 三、下一步（✓，一次打印即可二分 ✓）

```
在既有 accept/哨兵打印里加上：`page_block_base`（建页用的 `base + payload_delta` ✓）与
`sentinel_base`（`words_region.base() + kPayloadOffset()` ✓）⇒ 一次真机跑即可看出两者是否相等 ✗；
若不等 ⇒ 统一为**同一来源** ✓（守则"一处权威" ✓）⇒ 之后复看 `w+0x1198 = value` ✓ / `w+0x11a8 = target` ✓，
再判 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
