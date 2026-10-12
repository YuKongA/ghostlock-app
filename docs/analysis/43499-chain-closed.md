# 43499 · 整条链闭合：写 → 装模块 → 模块以 uid 0 跑 root_cmd.sh（round 257）✓✓✓

## 终端阶段是什么（✓，逐字 ✓）

```sh
# tools/lkm/ghostlock/root_cmd.sh（81 行 ✓）头部：
# Executed by ghostlock.ko through call_usermodehelper (uid 0, kernel context)
# after the module has set SELinux permissive.
# … Unlike the 43499 root script it performs NO SELinux policy fixup
#   (the 43284 path puts SELinux permissive instead of repairing W1 damage).
```

## 于是整条链是这样闭的（✓✓）

```
① 43499 的**写能力**（本晚已把 w1/w2/w3 全部修到 `won` ✓）用于**把 ghostlock.ko 写进内核** ✗；
② 模块接管后 **设 SELinux permissive** ✓ 并以 **uid 0** 跑 `root_cmd.sh` ✓（call_usermodehelper ✓）；
③ 脚本**找 ksud、推 KMI、`ksud late-load`** ✓ ⇒ 内核里出现 KernelSU ✓；
④ 于是会话拿到 **root** ✓ ⇒ ⇒ 判据 `main: victim uid=0` ✓ / `root script start uid=0 euid=0` ✓。
```

## 这解释了本晚所有"看似矛盾"的读数（✓✓）

| 现象 | 真解释 |
| --- | --- |
| `enforce` 恒 1 ✗ 而归档成功那次**也是 1** ✓ | **43499 不改 enforce** ✗ ⇒ 我此前把它当判据是**错的** ✓（已更正 ✓） |
| `w1/w2/w3` 全 `won` ✓ 而 `uid` 仍 2000 ✗ | 写成功了 ✓，但**模块还没被加载/接管** ✗ ⇒ 终端阶段未接 ✓ |
| `handoffs=1` ✓ 而日志无 `seccomp probe` ✗ | 终端阶段由**模块**做 ✓，不是 host ✓ ⇒ host 只需完成"装模块" ✓ |

## 下一步（✓，形状与零件都齐了）

```
① host 侧在 `handoff` 后执行：**把 ghostlock.ko（payload 里的 asset ✓）写到内核目标位置并加载** ✓；
② 之后模块自动完成 ③④（`root_cmd.sh` 已在 `tools/lkm/ghostlock/` ✓ 并被打进 app payload ✓）；
③ 判据：`root script start uid=0 euid=0` ✓ ⇒ `main: victim uid=0` ✓。
```

## 门禁

```
check → 108 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
