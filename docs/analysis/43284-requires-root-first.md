# 43284 的硬前提：它需要 root 才能开始（round 245）✓✓

## 夹具自述（✓，逐字 ✓）

```json
"targets": {
  "module": "/data/local/tmp/ghostlock.ko",            // 加载 LKM ✗ 需 root
  "patch1_target": "/apex/com.android.runtime/bin/crash_dump64",   // 写 /apex ✗ 需 root
  "hook_target": "/system/lib64/libc++.so",            // 写 /system ✗ 需 root
  "carrier": "/vendor/lib64/libbinderdebug.so",        // 载体 ✗ 需 root
  "hook_symbol": "_ZNSt3__1…sentryC1ERS3_", "frame": "/data/local/tmp/ghostlock-sa.frame"
}
"steps": ["patch1", "carrier", "hook", "trigger", "wait"]
"markers": { "loaded": "/dev/df0", "failed": "/dev/df1", "hook": "/dev/df", "module": "/sys/module/dirtyfrag" }
"handoff": { "type": "lkm", "data": ["vendor/lib64/libbinderdebug.so"] }
```

## 这解释了真机读数（✓）

```
`wire-43284-steps`: `step result patch1 => step-verify-failed/splice=0` ✗；
⇒ 第一步就要**写 /apex 下的文件** ✗ ⇒ 无 root ⇒ splice 读不出/写不进 ⇒ 校验失败 ✓；
而 `kernelcall`/`lowlevel` 那两支**只做读写原语**（`surface.writeop` / `surface.kernelcall` ✓）
⇒ 不需要那三件 ⇒ 因此能 `EXIT=0` ✓（但 `verdict none` ✗，因为**没有真目标** ✗）。
```

## 由此对目标清单的修订（✓，重要 ✓）

```
43284 的三支里，**只有"读写原语"两条可以无 root 跑** ✓（已实证 ✓）；
真正"执行攻击"的那条（steps ✓）**必须先有 root** ✗ ⇒ ⇒ 于是**顺序被定死**：
    先解决 43499 的 root ✗ ⇒ 才谈 43284 的攻击 ✓。
⇒ ⇒ 因此**下一步只剩 43499 的"写/生效"** ✓（与本晚收束结论一致 ✓）。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
