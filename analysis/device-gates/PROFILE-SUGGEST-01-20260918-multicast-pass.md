# PROFILE-SUGGEST-01 真机功能验证：Multicast（建议值化构建）— PASS（条件运行）

- 构建：APK 334；native `3d7c024d5e941971`（`execution_settings_suggestions` 建议回退 +
  Shizuku 开关建议化）
- 入口：Direct；`boot_ms=2113720`（**非冷机**），启动时 `enforce=0`，KernelSU 已加载
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`PROFILE-SUGGEST-01-20260918-multicast-pass.native.log`（165 行）
- 结论：**条件 PASS**——启动校验链与 W2/W3 无回归

## 覆盖点

- resolved profile 正常加载执行（`validate_offsets_profile` 放宽后标准路径不受影响）。
- W2 / W2b / W3 TIF_SECCOMP / W3 seccomp mode 全链，4/4 route `status=0 clean=1/1`。
- handoff 走 "KernelSU already loaded → restoring enforcing" 分支：`enforce=1 (enforcing)`、
  `KernelSU ready`；`exploit complete` T+19629ms。
- pstore 无 panic（仅 `pmsg-ramoops-0`）。

## 未覆盖 / 限制

- W1 未执行（启动时 SELinux 已 permissive）、非冷机、KSU 已加载——不是标准冷机门禁；
  但攻击函数 shape 未变，本次足以证明启动侧无回归。
- **建议回退逻辑本身**（缺省 execution → shipped suggestions）无法在真机触发：Kotlin 总是生成
  完全解析的 profile；由 `profile_test` 固定向量（建议回退、CPU 修复、arm clamp）覆盖。
- **UI 建议化**（requires_shizuku 只作开关种子）需要 requires 机型或 UI 手动操作验证；A301SO
  不在 `REQUIRES_SHIZUKU` 列表，未覆盖。
- 形状对比基线 `e1153fd3`：8 个攻击关键函数 shape 不变，`run_setup_stage` +160（新回退逻辑）。
