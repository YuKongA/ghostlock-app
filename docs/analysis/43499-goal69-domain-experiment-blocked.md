# 43499 · 第 69 轮：域实验在当前工作树**不可做**（app 侧源码已删）⇒ 线索留档 ✓

## 一、读数（✓）

```
`grep -rn "exec|ProcessBuilder|Runtime.getRuntime" app/src/main` ⇒ **空** ✗；
`grep -rln "assets|payload" app/src/main` ⇒ **空** ✗；
对照第 42 轮的 `git status`（只读 ✓）：`D app/src/main/kotlin/com/ghostlock/app/**` ⇒
⇒ ⇒ **app 侧源码在工作树里是"已删除"状态** ✗（⇒ **非本次会话改动** ✓，按纪律不动 ✓）。
```

## 二、结论（✓，如实 ✓）

```
⇒ 当前树**没有**可接入的 app 侧入口 ✗ ⇒ ⇒ **域复现实验（第 67/68 轮）此刻不可做** ✗；
⇒ 恢复 app 源码属于"改别人已删的东西" ✗ ⇒ 按 §「不动用户既有改动」不做 ✓；
⇒ ⇒ 于是"域/抢占差异"这条假设**只能留档** ✓（⇒ 它是本会话找到的**最可能**解释之一 ✓）。
```

## 三、留档：域实验的**一击即用**形态（✓）

```
前提：app 侧恢复后 ✓（或用**任何**运行在 `u:r:untrusted_app:s0` 的宿主 ✓）；
做法：让该宿主 exec `/data/local/tmp/ghostlock < /data/local/tmp/wire-43499-collide-F.framed` ✓，
      并把 stdout 落到宿主可写的路径 ✓；
对照：`victim uid` ✓ / `clean=1/1` ✓ / `stamp=-1/99` ✓ / `attempts` ✓ 与 shell 域下的读数逐项比 ✓；
判据：任一轮 `main: victim uid=0` ✓；
价值：一次跑即可判定"**域 ⇒ 抢占 ⇒ 拷贝是否落在栈上**"这条假设 ✓（⇒ 本会话最重的一条 ✓）。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
