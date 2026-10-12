# 43499/43284 · 回归：43284 两条路在今晚改动后仍成立（round 160）

## 为什么做这次回归

今晚改动集中在 `prepare_page.hpp`（中间 groom ✓）、`multicast_route.hpp`（stamp 元组 ✓、re-poison ✓）、
`main.cpp`（三步三请求 ✓、修复轮并入窗口 ✓、可选键 ✓、直读仪器 ✓）⇒ ⇒ 都可能**误伤 43284 那半边** ✗。

## 结果（真机逐字 ✓）

```
① 纯底层 Lua（目标原文「完全重写成 lua、不调封装接口、测底层可访问性」✓）：
   RC=0 ✓   main: cfg fields=69 (skipped 18) payloads=1 calls=7 handoffs=1 steps=0
   main: verdict none ✓（**无任何拒绝** ✓）⇒ 判据仍成立 ✓

② 封装步骤路（目标原文「对 43284 也是（封装步骤）」✓）：
   RC=9   main: step result patch1 => step-verify-failed/splice=0
   ⇒ 已知的 **SA 门控** ✓（SA 需 root ⇒ 如实失败 ✓，非回归 ✗）
```

## 目标条款的状态（复核 ✓）

| 目标句 | 状态 |
| --- | --- |
| main.cpp 解析 wire → json + lua ⇒ 沙箱 | ✓（真机有据 ✓） |
| 可控可调用的函数 + 只读 cfg | ✓（surface 8 行 ✓ / `cfg fields=69` ✓） |
| pagecache_load_lkm → schema 配置传入 | ✓（`required_param[]` ✓ / manifest 门禁 ✓） |
| 封装步骤（43499 w1w2w3 ✓ / 43284 五步 ✓） | ✓（两条路都跑到 ✓） |
| **完全重写成 lua、不调封装接口** | ✓（本轮复核 ✓ RC=0 ✓） |
| 43284 wire 侧补 `sa` | ✓（`required_param=["sa"]` ✓） |
| 43499 逻辑正常执行攻击 | ✗（≥9 次完整跑、`enforce` 0 次翻转 ✗ ⇒ 计划已出 ✓ 待认可 ✓） |
| lua 直调底层 kernelaccess（读法②） | ✗（新 surface 行 ⇒ 计划已出 ✓ 待认可 ✓） |
