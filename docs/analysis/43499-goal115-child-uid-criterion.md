# 43499 · 第 115 轮：**判据对象问题** —— 归档读 `child uid`（修复路由里的 child），我读 `victim uid` ✓✓✓

## 一、决定性证据（✓✓✓，两处对照 ✓）

```
归档（唯一身份读数）：`[*] **child uid = 0**` ✓ —— 且位置在 `:142` ✓，**紧接** `W2b: firing prebuilt init_cred+8 repair`
  之后的**第三次路由**（`:134-141` ✓）⇒ ⇒ **⇒ root 是"修复路由那次 child"的属性** ✓；
本树：`main.cpp:1380 printf("main: victim uid=%u", victim_uid)` ✗ —— **本树自造**（归档无此行 ✗）；
  且归档的 `child uid` 在本树**无对应** ✗（`grep child uid` 全空 ✓）。
```

## 二、这意味着什么（✓✓✓，可能推翻整条判据链 ✓）

```
① 归档的判据对象 = **修复路由（`W2b` 那次）里的 child** ✓ ⇒ 它才是**被写 cred 的目标** ✓；
② 本树的判据对象 = `victim_process` 的 `C` 通道读数 ✓（⇒ **另一个进程** ✗，其 uid 由 fork 继承 ✓）；
③ ⇒ ⇒ **⇒ 若二者不是同一进程** ⇒ **⇒ 那么"写是否落到目标"与 `victim uid` 根本无关** ✓✓；
   ⇒ 这解释了贯穿本会话的怪相：**机制读数全对 ✓（`clean=1/1`/`stamp=-1/99`/`disarm`/`prebuilt`/`firing`）
     而 `victim uid` 恒为 2000** ✗✓ —— 因为**它可能根本不是判据** ✗✓。
④ ⇒ ⇒ ⇒ **⇒ 因此下一步是最短的一条** ✓：**打印"修复路由那个 child 的 uid"** ✓（⇒ 与归档同形 ✓），
   ⇒ 若它 = 0 ✓ ⇒ ⇒ **⇒ 说明 root 早已取得，只是此前测错了对象** ✓✓✓。
```

## 三、落地（✓，一处打印 ✓）

```
在"修复路由"那次（`if (repair_prebuilt)` ✓ 体内、路由返回之后 ✓）加一行：
  `std::printf("main: child uid = %u", <该 child 的 uid>);` ✓（⇒ 与归档同形 ✓）；
⇒ 该值从哪来：路由/child 侧已有 ✓（⇒ 与 `victim_uid` 同源与否**由打印本身回答** ✓）；
判据：**真机出现 `main: child uid = 0`** ✓ ⇒ ⇒ 若出现非 0 ⇒ 那才是真正要追的 ✗。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
