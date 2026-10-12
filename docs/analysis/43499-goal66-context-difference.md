# 43499 · 第 66 轮：轮间重建候选关闭；浮现**运行上下文**差异（app/Shizuku ↔ adb shell）✓✓

## 一、对照（✓，两处同形 ✓）

```
本树：`build_stamp()` ⇒ memset ✓ → family(+8) ✓ → `encode_multicast_arm` ✓ → 元组 ✓（在 `execute` 内被调用 ✓）；
退役：:60-80 **在循环（:111 起 ✓）之前** ⇒ memset ✓ → `encode_multicast_waiter` ✓ → family(+8) ✓；
⇒ 循环内**只有** `setsockopt(fd, MCAST_BLOCK_SOURCE, stamp, sizeof(stamp))` ✓ ⇒ 同一份 stamp **反复投递** ✓；
⇒ ⇒ 因此 "Re-poison" 指的是**内核栈上的拷贝被反复覆盖** ✓（不是我方缓冲 ✓）⇒
   本树逐轮重建**结果确定性地相同** ⇒ **无害** ✗ ⇒ ⇒ 第 65 轮的候选**关闭** ✓。
```

## 二、浮现的差异：运行上下文（✓✓，此前从未核对 ✓）

```
归档成功那次：`runtime home=/data/user/0/com.ghostlock.app/files` ✓ ⇒ ⇒ **经 app（Shizuku）启动** ✓；
我这边至今：一律 `adb shell` 里直接跑 ✓ ⇒ ⇒ **上下文不同** ✓；
⇒ 影响面（都可能决定"那一轮能否落"✓）：**SELinux 域** ✓（app 域 vs shell 域 ✓）、
  **perf 权限** ✓（`perf_event_open` 的域限制 ✓ ⇒ 泄漏质量 ✓）、**调度/优先级** ✓（⇒ `arm_hold` 的 20000 次 yield ✓）。
⇒ ⇒ ⇒ **⇒ 这是一条"不需要改代码、只需换上下文复跑"的实验** ✓✓ ⇒ 成本低、判据同 ✓。
```

## 三、下一步（✓，一次对照实验 ✓）

```
① 若本树有 app 侧入口/载荷（`payload/root_cmd.sh` ✓ 被打进 app ✓ ⇒ 说明 app 侧存在 ✓）⇒
   按归档那样**经 app** 跑一次同一夹具 ✓；
② 对照读数：`victim uid` ✓ / `clean=1/1` ✓ / `stamp=-1/99` ✓ 是否变化 ✓；
③ 判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
