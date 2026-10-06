> 注（2026-10-06）：本文件中的路径引用已随本轮 profile 目录与 Gradle 任务改名更新（旧名见 git 历史）；原始日志文本未改，证据内容不变。

# T4 真机门禁：`Cve2026_43499Backend<StepSet>`（multicast_waiter）— PASS

对应提交 `91723e7`（T4）。候选二进制 `build/native/ghostlock` sha256
`8f2e31a0bb4bcc24fd7b7adf28109a260b1b99301d136b311d4df77289ee9dcd`；profile `build/profile/5.15.189-…ab14546557.bin` sha256 `8aa3200035b3768100cdf660be3b5a5a62b18936c4f653c35ee8a9243bb1b7af`。

## 设备与入口

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；serial `QV770MFGJ1`；route = multicast_waiter。
- 冷机：reboot → `sys.boot_completed=1` → sleep 8；post-boot 无 kernelsu。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-t4 --load-prebuilt-profile /data/local/tmp/p0.bin`。

## 结果

PASS（run-1，一次冷机通过；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+20331ms] exploit complete
[+] KernelSU ready
```

无 `Kernel panic`/`BUG:`；事后 `su -c id` = root。

## 变更说明

43499 步骤序列改为 backend 模板实参 `Cve2026_43499Backend<StepSet>`：`W1W3Steps`（原序列）与 `W1W2Steps`（无 W3）；
`attack_write`/`zero_word` 移入非模板基类 `Cve43499Primitives`（机器码不变，符号改为 `Cve43499Primitives::*`，cmp 加拼写）；
catalog 加 `{43499, W1W2, RootChild}` 三元组。`cmp_disasm --reviewed`：`do_one_write` OPERAND-SHIFT 无形状变化；
`consumer_thread` 为既有 `REVIEWED_SHAPE`。profile 默认 `w1_w3`，故真机走 W1W3，行为与 T3c 一致。

注：门禁由子智能体执行；其正式汇总未回传，本记录以 run-1 日志与其 `do_one_write` 无形状变化为准。
