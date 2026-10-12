# 43284 · 两条路都有证据（round 106）

## 真机逐字

```
① 纯底层 Lua（procedures/pagecache_load_lkm.lua ✓ 不调封装 step ✓）：
   RC=0   main: cfg fields=69 (skipped 18) payloads=1 calls=7 handoffs=1 steps=0   main: verdict none
   ⇒ 无任何拒绝 ⇒ **整条跑完** ✓（backend/writeop/copyop/settle/handoff/close 六类行都被宿主接住 ✓）

② 封装步骤（surface.step(h, name) ✓，名字来自 backend.cve_2026_43284.steps ✓）：
   RC=9   main: step result patch1 => step-verify-failed/splice=0
   ⇒ 第一站就卡在 splice 写 ✗ = **SA 材料未建立**（诚实失败 ✓）
```

## 与本目标的对应

| 目标原文 | 现状 |
| --- | --- |
| 为 script 提供封装好的步骤（43284 也要 ✓） | ✓ `steps = [patch1, carrier, hook, trigger, wait]` ✓ 且 step 接口可跑（第一站如实报缺陷 ✓） |
| 把 pagecache_load_lkm 封装成 script 接口 ✓ | ✓ 封装路存在并在跑 ✓ |
| 完全重写成 lua、**不调用封装接口**、测底层可访问性 ✓ | ✓ **纯底层 Lua 整条跑完（RC=0）** ✓ |
| 43284 的 wire 侧补 `sa` ✓ | ✓ `required_param=["sa"]` ✓；缺 SA 时如实报缺（不再说 write-path-unbuilt ✓） |

## 两个仍需 root 的点（诚实 ✓）

1. **SA 材料**：封装路的 `splice=0` ✗ 与底层路的 `write-needs-sa` ✓ 都指向同一件事 ✓；
2. ⇒ 而拿到 root 的是 43499 那条链 ✓（本晚已修 14 处，写仍未落地 ✗）。

## 门禁

- check → RC=0，108 PASS ✓；android → RC=0，0 error/0 warning ✓；lint → 0 ✓。
