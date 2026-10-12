# 43499 · select_stack：已枚举差全部关闭；快照 `FakePage` 指出 `fake_fops`（round 200）✓

## 本轮落地 ✓

```
三组 fd_set 改为 **prepare 构建一次并保存**（owned_in_/out_/ex_ ✓，快照 build_fdsets + owned_* ✓）；
每轮只在已保存的三组上重装低位 fd ✓；pselect 直接用这三组 ✓ ⇒ 与快照完全同形 ✓。
```

## 读数（HIT2/HIT3 ✓）

```
lock=0 ✓  park=-1/110 ✓  setattr=0 ✓  successes=0  enforce=**1** ✗
⇒ **所有已枚举的快照差异都关闭了** ✓，写仍不落地 ✗ ⇒ 如实 ✓。
```

## 新线索：`FakePage.fake_fops`（本轮读到 ✓）

```
快照 prepare()（:490-494 ✓）：
    const FakePage page{};   // 真实值来自堆喷
    if (page.base == 0 || page.fake_lock == 0 || **page.fake_fops == 0**) return fail(30, 0);
⇒ 快照的假页需要一个 **fake_fops** 字段 ✗；
   我的 payload 页只有 fake_task / fake_lock ✓（fixture 的 payload 段即如此 ✓）⇒ **缺 fops** ✗。
⇒ 这正好属于"(ii) 页面/payload 侧" ✓ ⇒ 下一批先**读快照里 fake_fops 的用途与偏移** ✓，
   再决定是否要在本 route 的页面上补一个字段 ✓（改页面 = L 级 ⇒ 先出计划 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
