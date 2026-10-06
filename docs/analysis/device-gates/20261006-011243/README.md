# M2 门禁 · 第一次尝试（未通过，保留为证据）

> 这次运行**设备在 `CMP_REQUEUE_PI … waiting route_done` 处掉线**（`native exit=unknown`，设备 uptime 归零 ⇒ 重启）。
> **同构建冷机复跑 PASS**（归档 `../20261006-011408/`）⇒ 按 `AGENTS.md` 的 `KERNEL-PANIC-01` 口径定性为**一次性环境/时序问题，不归因代码**。
> 保留本目录用于「同构建复现」证据链：失败与通过两次的 `native-stdout.txt` 并存，便于将来比对掉线点。