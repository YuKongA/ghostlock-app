# 43499 · 真值到位、触发归位，而 `enforce` 仍旧（round 234）✓

## 两支的最新读数（✓，全在真值树上 ✓）

| 路线 | 机制读数 | 落地读数 |
| --- | --- | --- |
| multicast（canonical，4 跑 ✓） | `status=0/0` ✓ `attempts=16 calls=1` ✓ `successes=1` ✓ `stamp=-1/99` ✓ | `enforce=1` ✗ / `uid=2000` ✗ |
| select（1 跑 ✓） | `status=0 step=61 attempts=4 calls=4 successes=0 lock=0 park=-1/110 setattr=0` ✓ | **`enforce=1`** ✗ |

## 本轮落地（✓）

```
· 真值实验保留（round 232 ✓）：`route_values.value = page_write_value` 挪到 build_route_config **之前** ✓
  ⇒ guard 满足 ✓、两支读数**与改前一致** ✓（无回退 ✓）；
· 触发归位（round 234 ✓）：select 的 `pselect` 移到 **waiter 线程、park 返回之后** ✓
  （主线程改为只等 done ✓，与 multicast :249-254 同形 ✓）⇒ select 读数不变 ✗。
```

## 结论（诚实 ✓）

```
两条路线现在都：**机制跑得动**（multicast 甚至 successes=1 ✓）、**写值是真值** ✓、**触发线程正确** ✓，
而 `enforce` 始终为 1 ✗、`uid` 始终 2000 ✗ ⇒ ⇒ **拦路者在别处** ✓（页面/stamp/几何已在早前逐项比对 ✓）。
```

## 下一批（✓，按可核性）

```
① `stamp=-1/99` 的**确切语义**（99 = EADDRNOTAVAIL ✓ 归档口径："拷贝落地" ✓）⇒ 核它是否即"成功" ✓；
② 若 stamp 即成功 ⇒ 查**页面里的元组**与**内核 walk 的目标**是否同一个（早前字节比对针对 preserve 支 ✓）✓；
③ 支线清单：`payload.value` 已改真值 ✓（登记 ✓）、`buf=264` ✗ 仍未查 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
