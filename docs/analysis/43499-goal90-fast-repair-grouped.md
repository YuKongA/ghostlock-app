# 43499 · 第 90 轮：`fast_repair` 一族的两项都确认"成组"（预建 + 5000 µs）✓

## 一、读数（✓✓）

```
夹具：`execution.consumer.delay_us = **0**` ✓；`execution.race.*` = { route_wait_ms 1000 ✓ /
      route_done_timeout_ms 300000 ✓ / setup_settle_us 50000 ✓ / state_poll_interval_us 1000 ✓ /
      post_requeue_settle_us 200000 ✓ / post_adjust_settle_us 100000 ✓ }；
归档：`race.setup_settle_us=50000` ✓ / `state_poll_interval_us=1000` ✓ ⇒ **与夹具一致** ✓；
本树读取点：`main.cpp:491 num("…execution.consumer.delay_us", route_values.consumer_delay_us)` ✓
      ⇒ **单一值** ⇒ 主轮与修复轮**同值** ✗（⇒ 无"激活那次压到 5000 µs" ✓）。
```

## 二、结论（✓✓）

```
5000 µs 那一项**不在 consumer 侧** ✗ ⇒ 它属退役的 `race.reset(fast_repair ? 5000 : select_enter_delay_us)` ✓，
⇒ 而**本树 race 层无该入口** ✗（第 27 轮已查明 ✓）⇒ ⇒ **⇒ 它与"预建"同属 `fast_repair` 一族** ✓
⇒ ⇒ 因此**剩余工作确为"一组两件"** ✓：① 预建（把 prepare+建页提前、页入槽 ✓）；② 激活（用槽 + 该次 5000 µs ✓）。
```

## 三、下一步（✓，一次性做完 ✓）

```
① 预建：`main.cpp:1143-1167` 的 prepare+建页**上移**到主路由（`:1112`）之前 ✓ ⇒ 结果 `PageSlot::stash` ✓；
② 激活：`:1167-1169` 的 send 改取槽 ✓ ⇒ 并把该次路由的延迟键改为 5000 µs（数值走 profile/wire ✓）；
③ 出口全覆盖 `discard` ✓（守则 §5 ✓）；④ 门禁三线**同批**跑 ✓（含 host 构建 ✓ —— 第 85 轮教训 ✓）。
判据：真机出现归档同形 `fast repair payload prebuilt` / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
