# 43499 · 真值实验的完整读数链（round 232）✓✓

## guard 是设计，不是 bug（有码 ✓）

```cpp
// multicast_route.hpp:226-230
/* The page already holds the tuple, so this call has to be the one the page was built for:
 *  a different target or value would write the page's pair while reporting this one. */
if (address != config_.target || value != config_.value) {
    report_.status = refusal(route::RouteResultCode::Unsupported, 59);   // = 观测到的 4/59 ✓
    return false;
}
```

## 三段读数（✓，逐段都有真机证据 ✓）

| 阶段 | 读数 | 结论 |
| --- | --- | --- |
| 只改参数 ✗ | `4/59 Unsupported` ✗（`cfg=0x4141…` ✓） | guard 生效：配置仍是毒值 ✓ |
| 参数+配置同改 ✓ | `0/0` ✓ attempts=16 ✓ successes=1 ✓ | **guard 满足** ✓ ⇒ 真值全程流通 ✓ |
| 写落地 | `enforce=1` ✗（4/4 ✓） | **仍不落地** ✗ ⇒ 值不是唯一拦路者 ✓ |

## 定位过程中的关键一步（✓）

```
首次"参数+配置同改"仍 `4/59` ✗ ⇒ 在 guard 处打点 ⇒ 读出：
  address=0x…3da8 **target=0x…3da8** ✓（这一半相等 ✓）；value=真值 ✗ vs **cfg=0x4141…** ✗；
⇒ ⇒ 原因：我把 `route_values.value = page_write_value` 加在 **stage 处（:1156 ✓）**，
   而 `build_route_config(route_values)` 在 **:889 ✓** ⇒ **赋值晚于配置构造** ✗ ✓；
⇒ 挪到 :886（配置构造之前 ✓）⇒ guard 通过 ✓（`0/0` ✓）。
```

## 结论（✓，诚实 ✓）

```
1. 写值此前确实"算而未用" ✗ ⇒ 现已改为真值 ✓（guard 满足 ✓、行为与改前一致 ✓ ⇒ 无回退 ✓）；
2. 但**写仍未落地**（`enforce=1` ✗ ×4）⇒ ⇒ **值不是唯一拦路者** ✓；
3. 因此下一步应转向**写本身的落点**：页元组（已字节比对 ✓）、stamp（-1/99 ✓）、requeue（退役亦 EAGAIN ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
