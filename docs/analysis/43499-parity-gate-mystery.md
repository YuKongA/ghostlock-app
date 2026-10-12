# 43499 · 奇偶门之谜：已排除全部已知解释（round 206）✓

## 硬事实（本轮全部有命令支撑 ✓）

```
1. 两个夹具现在**除 route 键外完全相同** ✓（递归 diff 空 ✓、framed 同长 8591 ✓）；
2. 背靠背交替：canonical（route=multicast_waiter）**1/4 命中** ✓；select（route=select_stack）**0/4** ✗；
3. select 连续未命中累计约 **0/30+** ✗（12+6+6+6 ✓），canonical 同期 2/8 ✓；
4. `available.<id>.route` 全树**只在 main.cpp:887 被读** ✓（本轮 grep ✓）；
5. `route_name` 在 main 里只有 :885-894 与 :976 两处用法 ✓；
6. 那道门在 **:825** ✓（`layout_accepts_page` ✓），其输入（page_request/write_layout ✓）在 :760-800 里**确定性**构造 ✓。
```

## 由此得到的结论（诚实 ✓）

```
⇒ 按代码，route 键**不参与**那道门 ✓ ⇒ canonical 与 select **应有相同命中率** ✗；
⇒ 但实测差别巨大 ✗ ⇒ ⇒ 只能是下列之一（按可能性 ✓）：
   (a) 抽样倒霉 ✓（0.75^30 ≈ 2e-4 ✗ 极不可能，但一次晚上两次长 streak ✗ 值得怀疑 ✓）；
   (b) 我漏了某处**间接**依赖 ✗（例如按 route 选择**步骤集** ✓、或按 route 决定**是否跑 prepare** ✓）；
   (c) 设备侧夹具**陈旧** ✗（本批已 push ✓，但需一次"同内容不同名"对照 ✓）。
```

## 下一批（一次性把这三个都排掉 ✓）

```
① 把 select 夹具**改名为 select2.framed 再 push** ✓ ⇒ 排 (c) ✓；
② 连续 12 跑 select、再连续 12 跑 canonical ✓ ⇒ 得到干净的两条命中率 ✓；
③ 若两条率仍差 ⇒ 在 main 里把"门"之前**打点**（打印 route_name + 门的结果 ✓）⇒ 直接看 route 键有没有进过 → 排 (b) ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
