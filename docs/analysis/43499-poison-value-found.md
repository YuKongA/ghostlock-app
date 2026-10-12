# 43499 · 冒烟的手枪：`route_values.value = 0x4141414141414141`（round 212）✓✓✓

## 打点读数（逐字 ✓）

```
main: rv max_attempts=4 arm_seq=16 arm_hold=20000 park_ms=1000 settle=50000 burst=4 max=4 fb=50000 nice=19
        target=0xffffff881265cfa8 **value=0x4141414141414141** ✗  ft=0xffff8000000b0400 fl=0xffff8…
```

⇒ ⇒ `value = 0x4141414141414141` = **ASCII 的 "AAAAAAAA"** ✗ —— 这是一个**毒值/占位符** ✗ ✓；
⇒ ⇒ 而 route 配置里的 `value` 正是"要写下去的那个 64 位值" ✓ ⇒ **写"A"当然不会有任何效果** ✗；
⇒ ⇒ ⇒ 这是本晚最硬的一条：**写不落地**的一个**直接**原因 ✓（而不是形状/时序 ✓）。

## 同时发现的一处工具性问题（✓，影响判读 ✓）

```
我新加的两处打印在两跑里**缺失** ✗ ⇒ 因为 stdout 重定向到文件 ⇒ **全缓冲** ✓ ⇒ 早退路径**丢尾巴** ✗；
⇒ ⇒ 这解释了此前多次"日志停在某行" ✗（不一定是停在那里 ✓）⇒ 下一次落码**先关缓冲** ✓。
```

## 下一步（下一批，两条都小 ✓）

1. `main` 开头设 `setvbuf(stdout, nullptr, _IONBF, 0)` ✓（或每个早退前 `fflush` ✓）⇒ 日志不再骗人 ✓；
2. 追 `route_values.value` 为何是 `0x41…` ✗：全树 grep `0x4141414141414141` ✓，
   找到赋值点 ⇒ 看它是否本应被"页派生值（page_value ✓）"覆盖 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
