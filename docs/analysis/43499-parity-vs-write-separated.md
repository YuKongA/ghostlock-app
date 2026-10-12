# 43499 · 决定性数据：完整跑里写依然不落（round 147）

## 重试编排（6 次 ✓）

```
try1/2/5/6：奇偶不过 ⇒ refused (payload-layout-not-accepted) ✗（提前停）
try3/try4（**完整跑**）：
   main: step w1/w2/w3 ... accepts=1   ×3 ✓（奇偶合格 ✓）
   main: repair route status=0/0        ×2 ✓（两轮修复都赢 ✓）
   main: victim comm after len=15       ✗（写未落 ✓）
   main: victim uid=2000                ✗
```

## 由此确定的两件事

1. **奇偶与"写不落"是两个独立问题** ✓：奇偶只决定"这一跑能不能走到底" ✓（约 1/3 ~ 1/2 ✓）；
2. **走到底的那些跑里，写依然不落** ✗ ⇒ ⇒ 说明缺的东西在**机制内部** ✗，不在调度/几何/常量上 ✓。

## 下一步：需要"能读回"的仪器（有退役为据 ✓）

- 归档里退役能打印 `SELinux permissive` ✓ ⇒ 它**能看到 W1 的效果** ✓ ⇒ 说明它有一个"写后可读回"的能力 ✓；
- 本树 shell 身份读不到 `/sys/fs/selinux/enforce` ✗（启动时已打印 enforce=unreadable ✓）；
- 但本树**有**内核读原语（kernelsnitch 侧 ✓）⇒ 若能对 `selinux_enforcing` 做一次**定向读回** ✓，
  就能直接判定"W1 的写到底落没落" ✓ —— 这是把"是否落地"从间接（comm 长度 ✗）变成直接（字段值 ✓）的关键 ✓；
- 判据：读回值 = 0（permissive ✓）⇒ 机制通 ✓；仍是 1 ⇒ 机制不通 ✓（此时才有理由动三槽/换 route ✓）。

## 门禁

- android → RC=0，0 警 ✓；check → 108 PASS ✓；lint → 0 ✓。
