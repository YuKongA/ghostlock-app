# 43499 · 三步三请求落地后的新发现：Zero 路需要"奇偶合格"的页地址（round 121）

## 真机（本轮）

```
main: layout matches=1 accepts=0 value=0xffff800000007280 mode=1    ← w1 的新请求（Zero + preserve）
main: step result w1 => payload-layout-not-accepted                   ← 本树判据拒绝（**正确** ✓）
```

## 判据与它的含义

```
layout_accepts_page：Zero + preserve 时要求 ((value >> 16) & 1) == 1
   （否则会清掉 selinux_state.initialized，退役注释原话 ✓）
value = page_value = payload_base + 0x100  ⇒ 奇偶完全取决于**页面落在哪** ⇒
   ⇒ **Zero 路需要一个奇偶合格的页面地址**（这正是退役 spray 在挑地址的原因 ✓）。
```

## 我自己的失误（如实）

- 我此前为 mmap 页写过 `payload_for_zero_write`（按 64 KiB 扫描挑奇偶合格页 ✓）；
- 改成 `send_bytes`（页面进内核 ✓）之后把它删了 ✗ —— **那个能力现在正需要** ✓（只是要用在新的地址来源上 ✓）。

## 下一步（两条，选其一，都可验证）

1. **按页地址重试准备**（退役式 ✓）：`prepare` 用新的 `base` ⇒ 若 `layout_accepts_page` 不过就**再准备一次**
   （最多 N 次 ✓，N 走文档 ✓）⇒ 直到奇偶合格 ✓；判据：`w1` 不再 `payload-layout-not-accepted` ✓；
2. **改页面值**：令 `page_value` 取一个**已知奇偶**的字（如 `payload_base | 0x10000` ✓）⇒ 但那是改退役语义 ✗，
   **不优先** ✗。

⇒ 优先选 1 ✓（有退役行为作参照：它也是"挑地址/重试" ✓）。
