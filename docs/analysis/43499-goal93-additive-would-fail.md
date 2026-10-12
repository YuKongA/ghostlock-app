# 43499 · 第 93 轮：为什么"纯加法"不可行（⇒ 必须改旧块）✓

## 一、细节（✓✓，来自我第 30 轮自己写的回退 ✓）

```cpp
// 修复轮（现行 :1244-1249 ✓）
if (!repair_slot.stash(repair_image.data(), kPayloadAreaBytes) ||
    !repair_slot.activate(repair_page, repair_length)) {
    std::printf("main: repair-slot-refused");
    repair_page = repair_image.data();            // ★ **回退到"当场重建的页"** ✗
    repair_length = repair_image.size();
}
```

## 二、推论（✓✓）

```
若**只在主路由之前**新增一次"预建"（先 `stash` ✓）⇒ 旧块随后再 `stash` 必**失败**（槽已占 ✓）；
⇒ ⇒ **⇒ 它走回退、使用重建的页** ✗ ⇒ ⇒ **⇒ 新加的预建被完全忽略** ✗✓；
⇒ ⇒ ⇒ **⇒ 「纯加法」路线不可行** ✗ ⇒ **⇒ 必须改旧块本体**（⇒ 这正是"一次搬移/改写" ✓）。
```

## 三、对清单的修订（✓，一处 ✓）

```
搬移清单（goal91 ✓）不变 ✓，但**执行方式**明确为：
  ① 把「prepare + 建页」从修复轮**移到主路由之前**（⇒ 一次搬移 ✓）；
  ② 修复轮只保留「`activate` + send + route」（⇒ 去掉重建与回退 ✗）；
  ③ 出口补 `discard` ✓ ⇒ ⇒ **⇒ 没有"只加不改"的捷径** ✓（⇒ 我第 92 轮若照加法做会白做一遍 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
