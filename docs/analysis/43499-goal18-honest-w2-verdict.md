# 43499 · 第 18 轮：`w2` 的结论改为具名（读数不再假装成功）✓✓

## 一、问题（✓）

```
步骤跑器用 `refusal == nullptr` ⇒ 打印 `won` ✓；而 `w2` 的真正判据是**受害者自报 uid==0** ✓
（退役 `verify_w2_stage` ✓）。⇒ 于是"路由成功但没 root"也会显示成 `won` ✗ ⇒ **误导** ✗。
```

## 二、改法（✓，3 行 ✓）

```cpp
if (report->refusal == nullptr && step == "w2" && report->victim_uid != 0U) {
    report->refusal = "w2-not-rooted";      // 具名拒绝而非假装成功 ✓（本树纪律 ✓）
}
report_step(step, report->refusal);
```

## 三、真机读数（✓✓）

```
`main: step result w1 => won` ✓
`main: step result w2 => **w2-not-rooted**` ✓✓（此前会打成 `won` ✗）
`main: victim uid=2000` ✗ ⇒ 与具名结论一致 ✓。
```

## 四、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```

## 五、意义（✓）

```
从现在起，任何一轮只要没拿到 root，日志会**直接**写 `w2-not-rooted` ✓ ⇒
  · 我不会再把"路由成功"读成"拿到 root" ✗；
  · 后续切片（预建页接线 ✓）的效果也能一眼分辨 ✓。
```
