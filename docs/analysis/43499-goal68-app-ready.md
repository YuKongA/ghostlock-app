# 43499 · 第 68 轮：app 侧就绪 ⇒ 域复现实验可行（中等成本）✓✓

## 一、读数（✓✓）

```
`pm list packages` ⇒ **package:com.ghostlock.app** ✓（⇒ app 已安装 ✓）；
`ls /data/user/0/com.ghostlock.app/files/` ⇒ **Permission denied** ✓ ⇒ ⇒ 域隔离成立 ✓（与第 67 轮一致 ✓）；
归档日志头部：`ksud ready` ✓ / `resolved profile loaded: 5.15.189-…` ✓ ⇒ 那次确为**app 内运行** ✓。
```

## 二、由第 67 轮推出的实验，现在的形态（✓）

```
目标：在 **u:r:untrusted_app:s0** 域里跑同一夹具 ✓（⇒ 对照 `victim uid` / `clean=1/1` / `stamp` ✓）；
所需：**app 侧入口**（⇒ 用既有 app 的调试/攻击入口 ✓，或让 app 去 exec 我们的二进制 ✓）；
成本：中等 ✓（⇒ 涉及 app 侧构建/启动 ✓，不是一行改动 ✗）；
⇒ 但它是目前**唯一**能检验"域/抢占差异"这条假设的路径 ✓，且**与既有机制零冲突** ✓。
```

## 三、下一步（✓，两条都可先做 ✓）

```
① 查 app 侧是否已有"跑夹具/跑二进制"的入口 ✓（⇒ `app/src/main/**` 里找 exec/asset 解包 ✓）；
② 若无 ⇒ 最小形态：让 app 在**自己的域**里 exec `/data/local/tmp/ghostlock < 夹具` ✓
   （⇒ 只需一个按钮/一条 Intent ✓，逻辑仍在 native ✓ ⇒ 不动攻击代码 ✓）；
③ 判据：任一轮 `main: victim uid=0` ✓；对照：域不同而其余全同 ⇒ 一次跑即可判该假设 ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
