# 43499 · 真 seam 已绑定：不卡（6 s 跑完）；待命中奇偶门（round 204）✓

## 本轮读数（✓）

```
单跑（170 s 上限给足）：INNER_RC=9 ✓ **ELAPSED=6s** ✓ ⇒ **不是卡** ✓
该跑撞奇偶门 ✗（payload-layout-not-accepted ✓）⇒ 未到达 route ✓
随后 6 跑：全部撞奇偶门 ✗（p ≈ 0.75^6 ≈ 18 % ⇒ 属正常抽样损失 ✓）
⇒ 修复后的 route 尚未取到"真 seam 之下"的读数 ✗ ⇒ 如实 ✓。
```

## 抽样成本（本轮量清 ✓，写进纪律 ✓）

```
未被 gate 的跑：约 6 s ✓（快 ✓）⇒ **每次调用可以塞 10-15 跑** ✓
⇒ 下一批不必再 4-6 跑一小批 ✗：一次调用 12 跑，命中概率 1-0.75^12 ≈ 97 % ✓
```

## 下一批的判据（不变 ✓）

```
① main: select route … successes= ⇒ 0 变 ≥1 ✓（真 cuff 的 cmp_requeue_pi ≥1 ✓）；
② main: selinux enforce= ⇒ 1 变 0 ✓；③ main: victim uid= ⇒ 0 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
