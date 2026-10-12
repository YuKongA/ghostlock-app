# 43499 · 新目标第 10 轮：目标地址族被排除（16 次抽样 + 归档对照）✓✓

## 一、抽样（✓，一次跑内的全部数据 ✓）

```
w1 target = 0xffffff802adaad88 ✓（与归档 W1 逐字相同 ✓）
w2 target 族：0xffffff89… / 0xffffff88… / 0xffffff80… ✓ ⇒ **全在 w1 同族** ✓
受害者 task（泄漏值）族统计：0xffffff89 ×8 ✓ / 0xffffff88 ×4 ✓ / 0xffffff80 ×4 ✓ / 0xffffffc0 ×1 ✗
归档对照：`child_task=0xffffff8a19880000` ✓ / `W3: TIF_SECCOMP === target=0xffffff8a19880000` ✓
⇒ ⇒ **我的泄漏 task 与归档同族（0xffffff8x ✓）** ⇒ 16 次里只有 1 次越到 0xffffffc0 ✗（抽样噪声 ✓）。
```

## 二、结论（✓，本轮排除一条）

```
⇒ **目标地址是对的** ✗（不是"写不落"的原因 ✓）；第 9 轮对 w3 目标的怀疑（0xffffffc0 ✗）经抽样判为**个别**，
   不是系统性 ✗ ⇒ 差异必在**写的机制**：页编码 / 元组（pc·right·left ✓）/ stamp 传输 / 路由触发时序 ✓。
```

## 三、下一步（✓，机制侧四件）

| # | 待验 | 出处 |
| --- | --- | --- |
| ① | `slab_drain()`：每阶段**首次**先排空 slab ✓ | 退役 `retry_write_stage` ✓ |
| ② | `discard_prebuilt_page()`：路由**失败**即丢弃预建页 ✓ | 同上 ✓ |
| ③ | W2 的 `w2_fast_repair_prebuild`：**路由前**预建修复页 ✓ | `multicast_waiter_route.cpp:170` ✓ |
| ④ | `in_direct_map` 首行守卫（⇒ 具名拒绝而非硬写 ✓） | `steps.cpp:70` ✓ |

```
判据不变：任一轮 `main: victim uid=0` ✓（唯一硬判据 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
