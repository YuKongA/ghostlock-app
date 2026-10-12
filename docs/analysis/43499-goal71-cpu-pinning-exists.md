# 43499 · 第 71 轮：收回第 70 轮结论（本树**有** CPU 绑定）⇒ 改为查"配置有没有被读进" ✓✓

## 一、收回（✗✓，与第 31 轮同类错误 ✓）

```
本树**有**绑定 ✓：`prepare_page.hpp:461 Seam::pin_to_processor(geometry.main_cpu)` ✓；
`multicast_route.hpp:456/487 pin_self(config_.main_cpu)` ✓；字段 `:108-109 main_cpu/consumer_cpu = -1` ✓；
⇒ 我第 70 轮的 grep **漏了 `seam/` 与 `memory/` 的部分路径** ✗ ⇒ ⇒ **⇒ 结论收回** ✓：
   「生产路径缺 CPU 绑定」**不成立** ✗（⇒ 教训记一笔：grep 的**范围/词表**要覆盖全 ✓，
   第 31 轮也曾因 `head -10` 截断误判 ✓）。
```

## 二、改为查"配置有没有被读进 config_"（✓✓，这才有意义 ✓）

```
字段默认 **-1** ✗（`multicast_route.hpp:108-109` ✓）⇒ 若构造时未从文档读 ⇒ 就**不绑定** ✗✓；
而夹具/目录**都有键** ✓：`recommended_cpus.main/consumer` ✓（`key_catalog.hpp:118-119` ✓）、
  `main_cpu` ✓、`cpu_core_pair` ✓。
⇒ ⇒ **⇒ 下一查：`MulticastRouteConfig{main_cpu, consumer_cpu}` 是在哪里、用什么键填充的** ✓（一处读 ✓）。
```

## 三、下一步（✓）

```
① 读路由配置的构造点（`build_route_config` / `route_config.cpp` ✓）⇒ 看 cpu 两字段的键 ✓；
② 若来源键=文档里的 `recommended_cpus.*` ✓ ⇒ 真机核对绑定是否生效（⇒ 可打印 `main_cpu/consumer_cpu` ✓）；
③ 判据：任一轮 `main: victim uid=0` ✓；辅助：绑定的 CPU 对与归档一致（main=0 / consumer=1 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
