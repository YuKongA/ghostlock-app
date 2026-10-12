# 43499 · 第 114 轮：**域实验完成 ⇒ 域假设被否证** ✓✓✓（决定性）

## 一、怎么做到的（✓，一条此前没走过的路 ✓）

```
① 装机版 app **可启动** ✓（`com.ghostlock.app/.ui.MainActivity` ✓）；
② 且它 **debuggable** ✓（`flags=[ DEBUGGABLE … ]` ✓）⇒ ⇒ **`run-as com.ghostlock.app` 可用** ✓；
③ 其域 = **`u:r:runas_app:s0:c23,c258,c512,c768`** ✓（⇒ **应用侧域** ✓，与归档的 `untrusted_app` **同族** ✓）；
④ 但它**读不了** `/data/local/tmp` ✗ 与 `/sdcard/Download` ✗（SELinux ✓）⇒ ⇒ **⇒ 用管道过桥** ✓：
   `cat /data/local/tmp/ghostlock | run-as … sh -c "cat > files/ghostlock"` ✓ ⇒ 落到 `/data/user/0/com.ghostlock.app/files/` ✓
   （★ 这正是**归档那次运行的路径形态** ✓ —— `docs/archive/…multicast-pass.native.log` ✓）。
```

## 二、读数（✓✓，与 shell 域**逐字相同** ✓）

```
`main: route status=0/0 clean=1/1 attempts=16 calls=1 successes=1 stamp=-1/99` ✓（同 ✓）；
`main: victim uid=**10535**` ✗（= app uid ✓，因 victim 继承宿主 ✓）；`uid=0` 计数 **0** ✗；
`main: snitch real verdict=ok probes=77 rounds=0 found=0xffff800000013000` ✓；
`main: prepare parity try=1 base=0xffff800000010000 delta=0xfffffffffffff180` ✓（同 ✓）。
⇒ ⇒ **⇒ 输出规模 54820 字节 ⇒ 完整跑完（非被拒）✓** ⇒ ⇒ **⇒ 结论：域切换**不改变**结论** ✗。
```

## 三、意义（✓✓✓，一条假设被干净地否证 ✓）

```
第 67 轮提出的"域/宿主差异"假设 ✓（⇒ 理由是归档成功那次跑在 `u:r:untrusted_app:s0` ✓），
⇒ 本轮用**同族域**（`runas_app` ✓）跑同一夹具 ✓ ⇒ **读数与 shell 域一致** ✓ ⇒ ⇒ **⇒ 假设**否证** ✓；
⇒ ⇒ **⇒ 于是"环境/域"这一类也被排除** ✗ ⇒ ⇒ 剩下的只能是**尚未发现的机制环节** ✗（或"内核侧语义" ✗）。
```

## 四、门禁与状态（✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓；
夹具已恢复 `setup_settle_us = 50000` ✓（第 113 轮 ✓）；**`main: victim uid=0` 仍未出现** ✗。
```
