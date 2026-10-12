# 43499 · 第 72 轮：CPU 绑定链路完整 ⇒ 第 70/71 轮的线索彻底关闭 ✓

## 一、链路（✓，逐处 ✓）

```
`route_config.cpp:84-85`  config.main_cpu = values.main_cpu ✓；config.consumer_cpu = values.consumer_cpu ✓；
`route_config.hpp:43-44`  values 默认 **-1** ✗（⇒ 若缺键则不绑定 ✗）；
`prepare_mapping.hpp:41`  **具名拒绝** `prepare-value-missing:main_cpu` ✓ ⇒ 该键**必需** ✓；
夹具：`execution.recommended_cpus = { consumer: 1, main: 0 }` ✓ ⇒ 与归档 `cpu pair: main=0 consumer=1` **一致** ✓；
消费端：`multicast_route.hpp:456/487 pin_self(config_.main_cpu)` ✓；`prepare_page.hpp:461` 亦有 ✓。
⇒ ⇒ **⇒ 绑定会生效** ✓ ⇒ ⇒ 「生产路径缺 CPU 绑定」线索**彻底关闭** ✗ ✓（第 70 轮收回 ✓、第 71 轮细化 ✓、本轮闭环 ✓）。
```

## 二、便宜复核（✓，从现有日志即可 ✓）

```
若键缺失 ⇒ 会打 `prepare-value-missing:main_cpu` ✓ ⇒ 在既有日志里 grep 该串：
  · **未出现** ⇒ 键都读到了 ✓ ⇒ 绑定生效 ✓（⇒ 该维度与归档一致 ✓）；
  ⇒ 于是"调度/抢占条件"的**可做的那一半**也排除 ✗ ⇒ 剩下的仍是**域**那一半 ✗（⇒ 需 app 宿主 ✓，第 69 轮已留档 ✓）。
```

## 三、下一步（✓，两条）

```
① 若 `prepare-value-missing` 未出现 ⇒ 关闭"绑定/键"这条 ✓ ⇒ 回到"域"那条（需宿主 ✓，已留档 ✓）；
② 或继续按「查旧代码」找**其它可做**的维度（⇒ 每找到一条即按"归档基线"逐项比 ✓）。
判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
