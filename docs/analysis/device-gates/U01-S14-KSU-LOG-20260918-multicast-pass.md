# U01-S14 真机门禁：per-run KernelSU 日志路径（Multicast）— PASS（非冷机）

- 构建：APK 332；native `e1153fd36d743b45`（Kotlin 每次运行生成
  `ghostlock-ksu-<millis>.log` 并经 `GHOSTLOCK_KSU_LOG` 传给 Native；root script 的 `LOG=`
  与 `handoff_probe_run` 共用该路径，固定名仅作 CLI 回退）
- 入口：Direct；`boot_ms=488833`（**非冷机**：设备已运行约 8 分钟）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`U01-S14-KSU-LOG-20260918-multicast-pass.native.log`（236 行）
- 结论：**PASS（功能验证）**

## 覆盖点

- 6/6 route `status=0 clean=1/1`；`Write 1 complete` T+21044ms、`exploit complete` T+40940ms。
- `KernelSU module loaded`、`enforce=1 (enforcing)`、`KernelSU ready`。

## per-run 路径验证（设备实测）

- root script：`LOG='/data/user/0/com.ghostlock.app/files/ghostlock-ksu-1789744294265.log'`
  （本次运行毫秒时间戳；旧固定名 `.ghostlock_ksu.log` 不再被本次写入）。
- 该文件由 root 生成（`-rw-r--r-- root root`），内容含 `late-load exit=0`、
  `KernelSU module loaded`——handoff 判定读到的就是本次运行的文件。
- 旧的 `.ghostlock_ksu.log`（10:54，上一构建遗留）仍在设备上，但**本次 handoff 未读取**，
  证明"上一次运行的标记不能污染本次判定"的语义成立。

## 限制

- 本次为非冷机运行；改动含 8 个函数的形状变化（`run_setup_stage`/`run_exploit`/session
  构造析构与 std::string 相关）。**用户于 2026-09-18 豁免冷机复跑**：以功能验证
  （设备实测 per-run 文件生成、旧标记未被读取）与 8 个攻击关键函数 shape 不变作为生效依据。
- 形状对比基线：`88390be7`（Kotlin/路径改动前的 retirement 构建）。
