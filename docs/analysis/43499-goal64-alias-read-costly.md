# 43499 · 第 64 轮：别名读的代价被契约自己点明（不是"小件"）✓✓

## 一、读到的原文（✓，`contract/kernel_access.hpp` ✓）

```
:13-14  "Everything returns AccessResult<T>: a status and a value… A read hands back the bytes it read…" ✓
:29-34  "Which channel a read or write travels over" + "**A loaded kernel module writes on the caller's behalf**" ✓
:80-81  "The value is the operation's answer: the bytes a read read, the count a write wrote,
         **the word a word read read**" ✓
:101    "**The 43499 path writes early and can only read arbitrarily once the** …" ✗（句子在此截断 ✓）
```

## 二、含义（✓✓）

```
契约自述：43499 路径**先写**（write early ✓），而**任意读**要等到**某个条件之后** ✓；
且通道表里写着 "**A loaded kernel module** writes on the caller's behalf" ✓ ⇒ ⇒
⇒ ⇒ **⇒ 任意读很可能要求"已加载的模块"** ✗ ⇒ ⇒ 即：**它是一条"上模块之后"的能力** ✗✓；
⇒ 因此我第 63 轮把"别名读"当作**近在手边的小件** ✗ —— **低估了代价** ✗（现更正 ✓）。
```

## 三、由这条更正得到的推论（✓✓，重要 ✓）

```
若"任意读"要等模块 ✗ ⇒ 那么**"读回验证写"这条路在 root 之前不通** ✗ ⇒
⇒ ⇒ 于是顺序**反转**：必须先有别的办法让"写生效" ✓，才能读回验证 ✓；
⇒ ⇒ 而"写生效"的判据一直就是 `main: victim uid=0` ✓（⇒ 我已有的唯一硬判据 ✓）。
⇒ ⇒ ⇒ **⇒ 因此下一步不应去建"任意读"，而应回到"让那一轮落上"** ✗✓（⇒ 与第 53 轮的结论一致 ✓）。
```

## 四、下一步（✓）

```
顺着"让写落上"找，按「查旧代码」✓：
  · 退役 `attack_write` 只有"准备 + 路由"两步 ✓（读它对任何**重试粒度**的描述 ✓）；
  · 重点看"**哪一轮之间发生了变化**"✓：退役 15 轮里能落 ✓ ⇒ ⇒ 一定有某个量在**轮间演化** ✓
    （候选：spray 的落点/桩页、`slab_drain` 的效果、`attempts_used` 与 `arm_sequence` 的关系 ✓）⇒
  · 本树是否在**轮间**推进同样的量 ✗ ⇒ 下一读 ✓。
判据：任一轮 `main: victim uid=0` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
