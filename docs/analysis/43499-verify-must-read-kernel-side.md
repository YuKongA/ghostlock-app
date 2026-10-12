# 43499 · 独立写验证的"载体"已就位；而验证对象在**内核侧**（round 239）✓

## 本轮落地（✓）  

```
`report`（main.cpp:110 的局部结构 ✓）新增 `std::array<std::uint32_t,6> sentinel_before` ✓；
before 打点处**顺带记录**六值 ✓（同域 ✓，不依赖块作用域 ✗）；
路由结束后按同一格式打印 ✓（`main: sentinel-after2 …` ✓）⇒ 载体通路**编译并运行** ✓（android 0 警 ✓）。
```

## 由此看清的一件事（关键 ✓）

```
我此前默认"读回页缓冲即可看到写" ✗ —— **不成立** ✗：
  写发生在**内核侧的那一份页**上（经 skb 进内核 ✓），而我们手里的是**用户态缓冲** `page_image` ✓；
⇒ ⇒ 要看到写，必须读**内核侧**的那份 ⇒ 两条途径：
   ① 经**别名**读（`alias_delta` + page 偏移 ✓，本树有 `kernel_access` 可用 ✓）；
   ② 读**内核**里那份 skb 页（需另外的原语 ✗）。
⇒ ⇒ 因此"独立写验证"的正确形态 = **用 ① 在写后读回目标字** ✓，而不是读本地缓冲 ✗。
```

## 下一批（✓，两选一即可推进）

```
① 用既有 `kernel_access` 的读接口 + `alias_delta`，在路由后读回"页内元组槽"与"目标字" ✓；
   与 `report.sentinel_before` / `write_layout.value` 比对 ✓ ⇒ 判定"写没发生 / 发生在别处" ✓；
② 或先用**最省**的旁证：路由后再次打印 `main: selinux enforce=` 与本轮 before 值并列 ✓（已有 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
