# CPP08 Shizuku 入口门禁通过（日志通路修复后）

- APK：`versionCode=211`（提交 `bfe50ee`），native `02743c88…`
- 设备/内核：5.15.189-android13-8-00016-g51bba4309aac-ab14546557
- 入口：Shizuku（UserService，`uid=2000` shell、`Seccomp=0`）；CPU main=0、consumer=1
- 温度：日志不可判定
- 结论：通过

## 验证要点

- tailer 修复生效：日志完整（160 行），`Shizuku ready: uid=2000 Seccomp=0`；
- 4 次 route（W1、W1b、W2、W2b）全部 `route_done status=0 clean=1/1`；
- **W3 按设计跳过**：shell 进程无 seccomp 过滤器（`Seccomp=0`），无需绕过；
- `child is root`、`KernelSU ready`；无卡死、无 kernel panic。

## 与失败对照

- 修复前：native stdout 走**管道**且每行一次 binder 回调 → 背压阻塞竞态窗口 → route 卡住 → `kernel_panic`
  （`CPP08-20260917-shizuku-panic`，两次）；
- 修复后：`redirectOutput(文件)` + 独立 tailer 异步转发，native 永不因日志阻塞。

原始日志：[`CPP08-20260917-shizuku-pass.native.log`](CPP08-20260917-shizuku-pass.native.log)
