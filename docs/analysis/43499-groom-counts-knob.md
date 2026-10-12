# 43499 · 放置的旋钮＝三阶段 groom 的组数（round 133）

## 定位链条（每一环都有行号/数值 ✓）

```
prepare_page.hpp:347   leaked = snitch.address()            （泄漏 mm_struct，与退役同一量 ✓）
page_geometry.hpp:66   base = leaked & ~(align−1)
夹具 heap 配置          page_size = 0x1000 ⇒ align = page_size × 8 = **0x8000**（与退役 ORDER3 掩码**同值** ✓）
⇒ ⇒ 奇偶 = 泄漏到的那个 mm_struct 的 bit16 ⇒ 由**组数**决定它落在哪 ✓
```

## 改的数值（纯数据 ✓，退役派生式为据 ✓）

| 项 | 改前 ✗ | 改后 ✓ | 退役依据 |
| --- | --- | --- | --- |
| `pre_groups` | 1 | **8** | `prepare_ctx.childs.assign(8 × mm_objs_per_slab, 0)` ✓ |
| `spray_groups` | 2 | **6** | `spray_ctx.childs.assign((1 + MM_PARTIALS=5) × mm_objs_per_slab, 0)` ✓ |
| `post_groups` | 1 | 1 | 未改（退役对应值待核 ✓） |
| `leak_groups` | 1 | 1 | 未改 ✓ |

## 真机（本轮逐字 ✓）

```
main: step=w1 target=0xffffff802adaad88 layout matches=1 accepts=1   ✓
main: step=w2 target=0xffffff8937081998 layout matches=1 accepts=1   ✓
main: step=w3 target=0xffffffc038d6beb0 layout matches=1 accepts=1   ✓
prepare parity try=1 …（两步都是**第一个就合格** ✓ —— 与退役 attempt=1 行为一致 ✓）
main: victim uid=2000 ✗
```

## 小结

- 从"50% 靠运气 + 重试无效"变成"**三步都在 try=1 合格**" ✓（行为与退役对齐 ✓）；
- 奇偶这一关**结构性解决** ✓；剩下的仍是那句话：**写落地 / `uid=0` 仍未出现** ✗；
- 下一步（有据可循 ✓）：`post_groups`/`leak_groups` 的退役对应值核对 ✓ ⇒ 再多次真机跑，看是否落 ✓。
