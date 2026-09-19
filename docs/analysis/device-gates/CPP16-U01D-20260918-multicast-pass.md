# CPP16+U01-D 真机门禁：Multicast — PASS（第一次冷机）

- 构建：APK 324；native `18fe119c1c20cfd7e39dc562e632677529e49533a053fcf83a81fe30a29714cb`
  （`27924cb` U01-D direct-map payload alias + CPP15 namespace 规范化 + CPP16 零指令清理；
  该构建相对门禁基线 `66f0a8a3…` 触发 LTO 布局重排）
- 设备安装确认：`versionCode=324`，设备内 `libghostlock.so` 解包 SHA-256 与本构建一致
- 入口：Direct（冷启动，`boot_ms=70181`；启动时 `/sys/module` 无 KernelSU）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP16-U01D-20260918-multicast-pass.native.log`（235 行）
- 结论：**PASS（第一次冷机）**；U01-D 生效后首个设备证据

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0`；每次伴随 `mcast ghost disarm ret=-1 errno=110`
  与 `[route] threads joined`（与 `CPP13b` 基线一致，各 6 次）。
- W1 页验收 `prepare_kernel_page ok attempt=4 +13276ms`（本次唯一页重试；其余 5 次 `attempt=1`，
  约 +2.2s each）；W1 / W1b / W2 / W2b / W3 TIF_SECCOMP / W3 seccomp mode 全链。
- `Write 1 complete` T+20066ms、`exploit complete` T+38809ms。
- `KernelSU module loaded`、`enforce=1 (enforcing)`、`KernelSU ready`；`late-load exit=0`。
- `/sys/fs/pstore` 无本次 panic 记录（仅有 09:33 的历史 `dmesg-ramoops-1.enc.z`）。

## 判定

- 本页是布局敏感改动（CPP16 触发 LTO 重排）与 U01-D payload alias 的第一次冷机 PASS；
  全函数 shape 对比：0 个业务函数差异（唯一差异为 compiler-rt `__emutls_get_address` 的 TLS 槽偏移）。
- **用户于 2026-09-18 豁免第二次冷机**（同 `CPP12o`/`CPP12q`/`CPP12w` 先例）：本页单次冷机
  结合主机 shape/大小对比作为生效依据，`18fe119c…` 登记为新 Multicast 基线。
- 温度/环境条件日志不可判定（无温度字段）。
