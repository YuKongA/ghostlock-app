# 43499 · 第 88 轮：W1b 线索**关闭**（轮数等值、行为等价，只差打印）✓✓✓

## 一、本轮新增读数（✓✓）

```
本树（新补打印 ✓）：`main: W1b: private scratch repair attempt **1/3**` ✓
  ⇒ ⇒ 配置键 `w1_scratch_repair_attempts = **3**` **被读到** ✓（不是 1 ✓）；
归档（逐字 ✓）：`W1b: private scratch repair attempt **1/3**` ✓ ⇒ **同样的 1/3** ✓；
  另两条：`W1: SELinux attempt 1/**1**` ✓（⇒ multicast 下 w1 只试 1 次 ✓，与退役代码一致 ✓）；
          `W2: cred attempt 1/**15**` ✓（⇒ 与夹具 `w2_attempts=15` ✓ 一致 ✓）。
```

## 二、结论（✓✓✓，关闭）

```
归档那轮 W1b **也在第 1 次就成功** ✓ ⇒ ⇒ **⇒ "少跑几次"不成立** ✗✓（⇒ 两边都是"第 1 次即过" ✓）；
⇒ ⇒ **⇒ W1b 与本树的差异只剩"打印"** ✗（⇒ 纯读数差 ✓，行为等价 ✓）；
⇒ ⇒ ⇒ 而打印本轮已补 ✓ ⇒ ⇒ **⇒ 该线索**（第 80 轮列出的第一项 ✓）**关闭** ✓。
```

## 三、回到剩余两条（✓）

```
① **`fast_repair` 的预建/激活**（第 80 轮列出的第二项 ✓）：归档有 `W1 fast repair payload prebuilt` ✓ 与
   `W1b: firing prebuilt init_cred+8 repair` ✓ ✗ ⇒ 本树**两者皆无** ✗ ⇒ ⇒ **⇒ 这才是"确有的缺失"** ✓；
   ⇒ 计划已出（第 12 轮 ✓）、页槽已接（第 30 轮 ✓）⇒ 差的正是"**预建提前 + 5000 µs**" ✓；
② **域/宿主**（归档 `u:r:untrusted_app:s0` ✓ vs 我 `u:r:shell:s0` ✗）⇒ 需 app 侧 ✓（已留档 ✓）。
判据始终一条：`main: victim uid=0` ✓（**仍未取得** ✗）。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
