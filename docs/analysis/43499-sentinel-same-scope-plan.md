# 43499 · 写后快照的"同域载体"方案（round 238）✓

## 问题重述（✓）

```
before 打点在**内层块**里（`page` / `page_image` 都是那里声明的 ✓）；
after 打点在**路由之后**（更外层 ✓）⇒ 拿不到 `page` ✗ ⇒ 直接"留一份指针"要动块边界 ✗（且我上一轮已因多行锚点自伤一次 ✓）。
```

## 改换个思路：把 before 六值存到**同域**载体（✓，最小）

```
`report` 是**函数级**可用的 ✓（`report.doc` 全程在用 ✓）⇒ 在 before 打点处把六值写进 `report` 的新字段 ✓，
于是 after 打点**无需页指针** ✗ —— 它只要读 `report` 的对应字段即可与 after 值并列打印 ✓；
⇒ ⇒ 更省的做法：before 六值写进 `report` 后，after 打点只需打印"当前六值 + before 六值"两串 ✓，
   比较在**人/归档**侧完成 ✓（也符合"给命令与结果"的纪律 ✓）。
```

## 落地步骤（下一批 ✓，两处小改）

```
① 在 `report` 上加六个 `std::uint32_t` 字段（或一个 `std::array<std::uint32_t,6>` ✓，更省 ✓）；
② before 打点处顺带 `report.* = …`（同一行语句即可 ✓）；
③ after 打点处只打印 `report.*`（不碰 `page` ✗）⇒ 编译无作用域问题 ✓；
④ 判据不变：`enforce` 1→0 ✓ ⇒ `uid=0` ✓。
```

## 门禁（本轮未改码 ✓）

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
