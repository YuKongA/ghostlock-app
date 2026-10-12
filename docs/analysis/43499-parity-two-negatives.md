# 43499 · 奇偶的两次尝试都失败 + 退役的对应物找到了（round 126-127）

## 两次尝试（都记成否定 ✓）

| 尝试 | 做法 | 真机结果 |
| --- | --- | --- |
| 释放再试 | 重试之间关闭 shaping 两端（"放开块"） | **4-8 次全同址**（`0xffff800000010000` ×4 ✗）⇒ 无效 ✗ |
| 扣住不放 | 重试时保留 fd、只清 `base` | **`base=0x0` 到底** ✗ ⇒ `prepare` 交不出结果 ⇒ 更糟 ✗（已回退 ✓） |

⇒ 两条都不通 ⇒ 奇偶不能靠"重放准备"解决 ✓（根因仍是 `align_base(leaked, align_region_size)` ✓）。

## 判据的理由（源码注释逐字 ✓）

```
"A preserving zero write lands across a state field; if the second byte of the value it carries is
 even, that field is cleared and every lookup after it fails. The bit lives in the value, so it is
 checked here."        （memory/write_layout.cpp:34-42 ✓）
⇒ value（= 我们的**页面地址**）= 落进内核状态字段的那个值 ⇒ bit16 必须为奇 ✓ ⇒ 硬约束 ✓
⇒ 页面 64 KiB 之内所有地址 bit16 相同 ⇒ **只能靠放置** ✓
```

## 退役的对应物（下一步读它 ✓）

```
退役 spray.cpp:318   payload_write_layout(…)            ⇒ fake_parent/fake_right/fake_left/fake_fops 全来自它
退役 spray.cpp:363   … *request, write_layout)
退役 spray.cpp:704   payload_write_layout_matches_request(&request, &layout)   ⇒ **同一套判据** ✓
⇒ ⇒ 退役的 page_value（"right"）就在 `payload_write_layout` 里 ⇒ 读它即可知道"放置"是怎么被选中的 ✓
```

## 当前状态

```
树：android RC=0（0 警）✓ ；check 108 PASS ✓ ；lint 0 ✓
三步三请求 ✓、预检算法 ✓、判据 ✓ —— 全部正确；卡点 = **放置的奇偶** ✗
uid=0：仍未 ✗
```
