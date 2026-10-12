# 43499 · 0 命中的真相：门按"步的 route"，分支按"available 的 route"（round 205）✓✓

## 对照实验（决定性 ✓）

```
同一夹具，只改 available.cve_2026_43499.route：
  route = multicast_waiter ⇒ **1/6 命中** ✓（main: route status=0/0 … successes=1 ✓）
  route = select_stack     ⇒ **0/24+ 命中** ✗
⇒ 结论：`payload-layout-not-accepted` 这道门**按 route 判** ✓ ⇒ select 分支**从未走到门后** ✓
```

## 代码依据（本轮读到 ✓）

```
main.cpp:825-827：
    if (!cve49::memory::layout_accepts_page(page_request, write_layout)) {
        victim.retire_child();
        return "payload-layout-not-accepted";
    }
而 HIT 日志里那行写着：… backend=cve_2026_43499 **route=multicast_waiter** ✓（步自带的 route ✓）
⇒ ⇒ **步（w1/w2/w3）自带 route 字段** ✓，而我的 select 分支跟的是 `available.<id>.route` ✗
⇒ 两条来源不同的键：门 ⇒ 步的 route（multicast ✓）；分支 ⇒ available 的 route（select ✗）
⇒ select 夹具沿用了 multicast 的步 ⇒ 门过 ✓（在 multicast 的语境里 ✓），但分支走 select ⇒ 之后的准备未按 select 做 ✓
```

## 已排除的三条（本轮 ✓）

| 假设 | 结论 |
| --- | --- |
| `attempts` 4 vs 128 | 排除 ✗（改回 128 仍 0/6 ✓） |
| `step.key` 文案 | 排除 ✗（对齐后仍 0/6 ✓） |
| 设备状态漂移 | 排除 ✗（canonical 夹具 1/4 ✓ / 控制实验 1/6 ✓） |

## 下一步（下一批 ✓）

1. 读清"**步的 route**"与"**available 的 route**"的关系 ✓（谁由谁派生 ✓、两处是否应一致 ✓）；
2. 据此让 select 夹具的步也指向 `select_stack` ✓（或让分支改读步的 route ✓——按谁权威定 ✓，一处权威 ✓）；
3. 之后再谈 `successes`/`enforce` ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
