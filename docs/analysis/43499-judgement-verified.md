# 43499 · 判据可靠性已核（round 248）✓

## 逐字证据（✓）

```cpp
// main.cpp:1220-1224
/* 判据：**受害者自己报**的 uid（这才是拿到 root 的硬证据）。 */
std::uint32_t victim_uid = 0xFFFFFFFFU;
if (cve_victim::victim_report_uid(victim, victim_uid)) {
    std::printf("main: victim uid=%u", static_cast<unsigned>(victim_uid));
}
```

⇒ ⇒ 结论：`main: victim uid=` **不是我们的推测** ✓ 而是**受害者进程自己报出**的 ✓；
   读取失败会打 `0xFFFFFFFF`（可区分 ✓）⇒ 我观测到的 `2000` ✗ 是**真实读数** ✓。

## 这一核对掉了什么（✓）

```
它排除了"判据本身不可靠"这一整类怀疑 ✓：
⇒ 先前所有"未生效"的结论（`enforce=1` ✗ / `uid=2000` ✗ / `verdict none` ✗）**不是观测错误** ✗，
   而是**真实状态** ✓ ⇒ ⇒ 于是问题的定位也就**唯一**地落在"写/生效"这一层 ✓。
```

## 与既定纪律一致（✓）

```
早已登记的纪律：「除非设备日志里出现 `main: victim uid=0`，否则绝不声称拿到 root ✓」——
本轮的核对说明这条纪律**不是形式主义** ✗：它盯的正是**唯一无法自欺的那个读数** ✓。
```

## 门禁（未改码 ✓）

```
check → 108 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
