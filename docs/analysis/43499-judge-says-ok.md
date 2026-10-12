# 43499 · 路由判定"写成功"，而内核状态未变（round 235）✓✓

## 判据本体（✓，逐字 ✓）

```cpp
// multicast_route.hpp:370-391 judge()
report_.successes = shared_.consumer_success.load();
…
if (status.code == Ok && (report_.calls == 0 || report_.successes == 0)) {
    status.code = Retryable; status.step = 62;      // **只有 0 才降级** ✓
}
status.error_number = shared_.stamp_errno;          // = 99 ✓
⇒ ⇒ **不要求 `stamp_result >= 0`** ✗ ⇒ `stamp=-1/99` 是**通过**的读数 ✓；
⇒ ⇒ 且 `99 = EADDRNOTAVAIL` 正是"**拷贝落地**"的口径（早前归档推导 ✓）。
```

## 于是出现并列的两个事实（✓，本晚最尖锐的一处）

| 事实 | 依据 |
| --- | --- |
| 路由判定 = **写成功** ✓ | `status=0/0` ✓ `calls=1` ✓ `successes=1` ✓ `stamp=-1/99` ✓ |
| 内核状态 **未变** ✗ | `main: selinux enforce=1` ✗（多次 ✓）；`victim uid=2000` ✗ |

## 候选（按可核性 ✓）

```
① 擦除式写入**落在了别的位置**（页/别名 ✓）—— 页元组此前做过字节比对（preserve 支 ✓）⇒ 需再核"目标"是否同一 ✓；
② 判定链**本身**不足以证明写入（`successes` 只统计消费者动作 ✓）⇒ 需要**独立的写验证**（读回目标 ✓）；
③ `enforce` 读取的路径与内核实际门控不同（可能性低 ✗）。
```

## 下一批（✓，最直接）

```
加一个**独立的写验证**：在路由跑完后，读回"应当被写到的那个位置"（例如页里的元组槽 ✓），
看它是否与 `write_layout.value` 一致 ✓ ⇒ 可把 ①/② 一刀分开 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
