# 43499 · 第 137 轮（终·2）：**"极性相反"的推理有一条未验证前提** ✓✓✓

## 一、本树的推理（✓，逐字 ✓，`:795-803` ✓）

```
观测：`preserve=false` ⇒ Credential 写的 **value 变 0** ✓（真机量到 `main: step=w2 … value=0x0` ✓）；
规则：`write_layout.hpp:12-16`「**preserve 的写必须带 value**」✓；
⇒ 结论（第 34 轮 ✓）："本树 `preserve_child` 与退役 `leaf` **极性相反**"✗ ⇒ w2 取 **true** ✓。
```

## 二、未验证的前提（✓✓✓）

```
该推理**隐含**假设："**归档那次的写值非零**"✗ —— 只有它成立，"value=0"才是异常 ✓；
而归档自己的读数说：`[*] === W2: cred === … mode=2 **leaf=0**` ✓ ⇒ ⇒ **⇒ `leaf=0` ⇒ 归档该次的写**就是 0** ✗；
⇒ ⇒ ⇒ **⇒ 于是"value=0"**不是异常**，而是**与归档一致** ✓ ⇒ ⇒ **⇒ "极性相反"结论**不成立** ✗；
⇒ ⇒ ⇒ ⇒ **⇒ 本树与退役**同极性** ✓ ⇒ **w2 主写应取 `preserve_child = false`**（= `leaf=0` ✓）。
```

## 三、改动与判据（✓，一行 ✓）

```
改：`main.cpp:802` `page_request.preserve_child = true` ⇒ **`false`** ✓（仅 w2 主写 ✓，不动 w3 ✓、不动修复轮 ✓）；
预期：真机 `main: accept step=w2 … **preserve=0**` ✓ 且 `value` 变 **0x0** ✓（⇒ 与归档同形 ✓）；
判据：★★ **`main: victim uid=0`** ✓（⇒ 这是唯一硬判据 ✓）；
回退：改回 `true` 即可 ✓（一行 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
