# 真机发现：perf 采样的寄存器组在这台设备上不投递（2026-10-08）

> 级别：**攻击关键路径相关**（43499 写原语 `perf_find_task` 依赖它）⇒ 按 AGENTS 归档 ✓
> 设备 `QV770MFGJ1` · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
> 探针：`tools/perf_seam_probe.cpp`（`make -C core perf-seam-probe`）

## 1 事实（原始输出）

```
perf seam probe
  perf_open            fd=3 errno=0        <- 采样可以开（未被 paranoid 挡住）
  perf_map             ok
  ring                 offset=4096 size=32768
  data_head after sampling 32760           <- 采样器确实写了满环记录
  rec[0] type=9 size=24 bytes=09 00 00 00 01 00 18 00
        abi=0 payload=                     <- ABI=0 且无载荷
  ring walk            samples=0 kept=0    <- 因此按规则不产生候选（拒绝正确）
```

**解读**：记录 = 头(8) + IP(8) + ABI(8) = 24 字节 ⇒ **请求的寄存器组没有随样本投递**（
`sample_type` 已含 `PERF_SAMPLE_REGS_INTR`、`sample_regs_intr = (1<<32)-1`，与退休树完全同款）。

## 2 后果（写给下一位）

- **退休树的 `perf_find_task` 在这台设备上不会成功**（它要的就是寄存器组）⇒ 移植时必须**具名报出**
  这一点（`no-samples` / `geometry-missing`），不得静默返回 0 让人误以为「没找到」；
- 本轮的**读取器与投票规则仍然正确**：记录里没有可用寄存器 ⇒ `samples=0` ⇒ 投票 0 ⇒
  **拒绝路径在真机上被验证了一次**；
- **未核**：是内核缺该采样类型、还是 attr 某字段要改（`exclude_*` 组合、`precise_ip` 等）⇒
  若要走通 perf 路，这是第一个实验点；另一条路是**碰撞搜索**那条（契约里两个实现之一）。

## 3 复现

```sh
make -C core perf-seam-probe
adb -s QV770MFGJ1 push build/native/perf_seam_probe /data/local/tmp/
adb -s QV770MFGJ1 shell 'timeout 25 /data/local/tmp/perf_seam_probe'
```
