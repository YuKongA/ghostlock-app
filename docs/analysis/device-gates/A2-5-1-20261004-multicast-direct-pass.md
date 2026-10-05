# A2-5-1/2 真机门禁：entry 产出中性 Document + 绑定移入 backend（43499 multicast）— PASS

对应提交 `504583d`（A2-5-1/2）。候选二进制 `build/native/ghostlock`（1669296B）；profile 为 GLKv3 v3（5.15.189）。

## 冷机与入口

- A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；USB serial `QV770MFGJ1`（同时无线 transport 在线）。
- 冷机：reboot → `sys.boot_completed=1`；post-boot `su` 不可用、无 kernelsu（干净启动）。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp setsid timeout 60 /data/local/tmp/glk-a251 --load-prebuilt-profile /data/local/tmp/p0-v3.bin`。

## 结果：PASS

```
[*] [route] route_done status=0 clean=1/1 step=0 errno=0 calls=1 success=1
[*] child uid = 0
[+] child is root!
[+] no app seccomp filter (adb/shell flow); skipping W3
[*] [T+17731ms] exploit complete
[*] handoff: root script path=/data/local/tmp/.ghostlock_root.sh
[+] KernelSU ready
```

- `multicast route status=0 clean=1/1` 命中 **4 次**；事后 `su -c id` = `uid=0 … context=u:r:ksu:s0`。
- 无 panic；完整日志见 `.native.log`。

## 判定意义

- 新入口链路（`entry → 中性 Document → 选择 backend → B::state_from → Pipeline::run`）在真机可用：
  GLKv3 文档被正确 framing、backend 完成 owner 绑定、route 命中并完成 W1/W2（shell 流无 seccomp 故跳过 W3）、root child + KernelSU 就绪。
- `cmp_disasm`：`--reviewed` PASS；6 函数指令数不变（69/892/177/417/224/126），差异仅为 LTO 只读数据地址重排
  （`OPERAND-SHIFT` + `RELOC-SHIFT`，工具逐条证明 same-readonly-datum）；批前 HEAD（`80c6cf3`）worktree 构建 vs B0 为 6/6 IDENTICAL。
  按 ADR-0004 §9「允许有理由的机器码变化并记录理由」接受。
