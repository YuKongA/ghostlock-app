# 43499 · 第 48 轮：偏移无误 ⇒ 新候选 = **哨兵基址与槽基址不一致** ✓✓

## 一、排除（✓，逐键核对 ✓）

```
夹具 `route.multicast_waiter.payload` 共 **33** 键 ✓，其中：
  waiter_pc = **0x18** ✓ / waiter_left = **0x28** ✓ / waiter_right = **0x20** ✓ / waiter_task = 0x30 ✓ /
  waiter_lock = 0x38 ✓ / waiter_head_word = **0x00** ✓ / head_next = 0x08 ✓ / head_prev = 0x10 ✓ /
  wake_state = 0x40 ✓ / prio = 0x44 ✓ / deadline = 0x48 ✓ / ww_ctx = 0x50 ✓；
  槽：waiter = **0x1180** ✓ / lock = 0x0e80 ✓ / fake_task = 0x1280 ✓ / cred_copy = 0x1080 ✓；
  且本树确实从这些键读（`main.cpp:1024-1031` ✓）⇒ ⇒ **「字段偏移缺失/错误」排除** ✗ ✓。
```

## 二、新候选：基址不一致（✓✓，与老教训同类 ✓）

```
真机：`w+0x1180=1` ✓（head 对 ✗ hmm：对 ✓）而 `w+0x1198`（pc ✗）/ `w+0x11a8`（left ✗）不是期望值 ✓；
本树页有**两块**（`memcpy(page_image.data() + kOrder3, page_image.data(), kOrder3)` ✓）
⇒ ⇒ 若**哨兵读的基址**（`words_region.payload()` ✓ + 0x1180 ✓）与**建页时用的块基址**不是同一块 ✗，
   就会"head 恰好对上、pc/left 对不上" ✓（两块内容一致 ⇒ 若整体错位则应全错 ✗ ⇒ 更像**块内偏移差** ✓）。
⇒ 与本项目老教训同类：第 26 轮"多算了一个 `payload_delta`"✗ ⇒ **基址/增量必须与建页所用一致** ✓。
```

## 三、下一步（✓，两处对照即可定案 ✓）

```
① 打印**建页时**用的块基址（`repair_base`/`page 基址 + payload_delta` ✓）与**哨兵读时**用的基址 ✓，
   一次跑即可看到两者是否差一个 `kOrder3`/`payload_delta` ✗；
② 若差 ⇒ 修成同一来源 ✓（守则"一处权威" ✓），并复看 `w+0x1198 = value` / `w+0x11a8 = target` ✓；
③ 判据：`main: victim uid=0` ✓。
```

## 四、门禁（✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
