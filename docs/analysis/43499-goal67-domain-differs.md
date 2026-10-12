# 43499 · 第 67 轮：**上下文差异确证（域不同）** ⇒ 可能是"落不落"的分水岭 ✓✓✓

## 一、真机读数（✓✓，逐字 ✓）

```
我这边（一直）：`u:r:**shell:s0**` ✓（uid=2000(shell) ✓，groups 里含 adb/sdcard_rw/readproc 等 ✓）；
归档成功那次：`u:r:**untrusted_app:s0**` ✓（device-gates 日志里逐字 ✓）；
⇒ ⇒ **两次跑在不同 SELinux 域** ✓ ⇒ 而这正是退役注释写明的环境 ✓：
   `primitives.cpp:31`「Keep this light in **untrusted_app**. Aggressive fork storms trip LMK/OOM」✓；
   归档日志另有 `seccomp=Seccomp: 0 Seccomp_filters: 0` ✓（⇒ 域的 seccomp 状态亦不同 ✓）。
```

## 二、为什么这可能就是分水岭（✓✓）

```
域一变，随之变化的至少有：**perf 权限** ✓（⇒ 泄漏质量 ✓）、**内存/LMK 策略** ✓、
**调度与抢占** ✓（⇒ `arm_hold` 的 20000 次 yield 在两个域下被抢占地程度不同 ✓）、
**seccomp 过滤** ✓；⇒ 而本路线的**核心机制**恰恰是"**拷贝落在调用者的栈上**"✗ ——
一个**依赖抢占/时序**的事件 ✓ ⇒ ⇒ **⇒ 域不同 ⇒ 抢占行为不同 ⇒ 落不落可能就不同** ✓✓。
⇒ 且它**不需要改一行代码** ✓ ⇒ 属于"换环境复跑"级实验 ✓（成本最低、判据同 ✓）。
```

## 三、下一步（✓）

```
① 找到本树的 **app 侧入口**（⇒ `payload/root_cmd.sh` 被打进 app ✓ ⇒ app 侧存在 ✓；
   或直接用既有 app 跑同一夹具 ✓）；② 在 **untrusted_app 域**下复跑同一夹具 ✓；
③ 对照读数：`victim uid` ✓ / `clean=1/1` ✓ / `stamp=-1/99` ✓ 是否变化 ✓；
④ 判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
