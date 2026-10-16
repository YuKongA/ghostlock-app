# 上游说的：SA 由 app 自己建，帧自产（2026-10-15 用户指路）

## 证据（`docs/analysis/upstream-dirtyfrag-handling.md` 里的原文）

```text
220:    participant K as Kernel(XFRM/ESP)
227:    Note over A: IpSecManager 建 SA(AES-CBC/HMAC)  openUdpEncapsulationSocket + allocateSPI
282:  | session/IPsec | Java IpSecManager 建 SA；native 组装 ESP | 已具备：84B 通道 B 帧 + connect_esp_socket + make_real_chain_ops | 已具备 |
```

⇒ 三步全是我们能用的**公开 API**：

1. `IpSecManager.allocateSecurityParameterIndex(address)` —— **自己分配 SPI**（不是等系统给 ✗）✓；
2. `IpSecManager.openUdpEncapsulationSocket(port)` —— 拿到 **encap 端口**（UDP 封装）✓；
3. `IpSecManager` 建 SA（AES-CBC + HMAC）✓；
4. 之后 native 侧组装 ESP（本树的 `connect_esp_socket` + `make_real_chain_ops` + `RealSpliceIo.splice_one` ✓ 已具备 ✓）。

⇒ 于是 84 字节帧（`spi` / `encap_port` / `sender_port` / `icv_len` / `aes_key` / `hmac_key`）**全部可由 app 自产** ✓✓，
**不需要外部给帧** ✗ —— 我此前「现阶段只有外部给帧可行」的结论**作废** ✓（那是基于「公开 API 不给 SPI」的推断 ✗，
而上游与本文档都表明：SPI 是我们**自己申请**的，不是系统悄悄给的 ✓）。

## 因此 43284 的下一步（在 app 自己 uid 内）

1. Kotlin：`allocateSecurityParameterIndex` + `openUdpEncapsulationSocket` + 建 SA（两把密钥我们自定 ✓）→ 按冻结格式拼 84 字节帧；
2. 把帧交给 native（或写 `/data/local/tmp/ghostlock-sa.frame` 由既有 `read_session_secret_frame` 读 ✓）；
3. 重跑 patch#1：判据 `crash_dump64` 的 offset 16 窗口与 `embeddedImage(0)` 一致；
4. 之后 carrier 写测试 → hook → trigger → wait（终态对齐 B5-9h）。

## 我刚才那个探针问题仍然有用

探针里问 `allocated.spi` 那一句（因 `step` 作用域问题尚未编译通过 ✗）现在可以直接执行 ——
它会把**真实的 SPI 数字**打出来，作为第 1 步的实证 ✓（插在 `step` 声明之后即可，注意这个作用域坑）。
