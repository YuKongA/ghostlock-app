# CPP13b 真机门禁：Multicast（resident 类化 + 编码同源）— PASS（第一次冷机）

- 构建：APK 296；native `66f0a8a3b6450b9c50a02759bcb77162cb3b56af9bb3febcbbac161593184ea6`
  （`fa9c5dd` 步骤 A + `6cb2640` 步骤 C；步骤 B 经 `do_kernel5` +3 实验否决）
- 入口：Direct（干净冷启动；`boot_ms=42309`）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP13b-20260918-multicast-pass.native.log`（192 行）、`.ksu.log`（16 行）
- 结论：**PASS（第一次冷机）**；阶段版本（A+C）行为等价

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`；`prepare_kernel_page ok attempt=1` ×6（零页重试）。
- W1 / W1b / W2 / W2b / W3 TIF_SECCOMP / W3 seccomp mode 全链；
  `Write 1 complete` T+11345ms、`exploit complete` T+31192ms。
- `enforce=1 (enforcing)`、`KernelSU ready`；仅基线 `SELinux enforce unreadable` 告警。
- 形状：vs `ea87d65f` 8/8 攻击函数 strict 一致。

## 判定

- 设计文档要求阶段"至少多次冷机"：`CPP13a`（步骤 A）与本页各一次 PASS；
- **第二次冷机（重连后补跑）PASS**：`boot_ms=151075`、6/6 route、`Write 1 complete` T+10958ms、
  `exploit complete` T+30511ms、`KernelSU ready`（日志 `CPP13b-20260918-multicast-pass-2.native.log`）；
  同一构建期间的一次 panic 记录在 `CPP12y`（PASS/panic/PASS 直接对照）。
