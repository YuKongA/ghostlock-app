# 43284 的阻塞项解除：app 自己能申请 SPI（2026-10-15 真机）

## 真机结果（A301SO，APK 1010）

```text
typed: ipsec spi allocated=0x7afb293      ← ★ IpSecManager.allocateSecurityParameterIndex 在我们 app 的 uid 下成功
typed: ipsec spi closed
```

（`./gradlew deviceGate` → PASS，38 项不变 ✓）

## 这意味着什么

此前我基于「公开 API 不让指定 SPI、也不把 SPI 还给我们」的推断，得出过两条结论：

1. §11.30：应用权限内自建 SA 不可能 ✗；
2. `spi-origin`：现阶段只有「外部给帧」可行 ✗。

**两条都被真机推翻** ✓ —— 上游 `upstream-dirtyfrag-handling.md:227` 说得对：
**SPI 是我们自己申请的**（`allocateSPI`），不是系统悄悄给的；端口用 `openUdpEncapsulationSocket` 拿 ✓。

## 因此 43284 的接线（全部在 app 自己 uid 内，不需要任何外部材料）

```text
1. Kotlin：allocateSecurityParameterIndex(address)      → spi      ✓ 已实测
2. Kotlin：openUdpEncapsulationSocket(port)             → encap 端口（下一步实测）
3. Kotlin：建传输模式 SA（AES-CBC + HMAC，密钥我们自定）
4. Kotlin：按**冻结的 84 字节格式**拼帧（version/kind/spi/encap_port/sender_port/icv_len/aes_key/hmac_key/trailer）
5. 交 native（写 /data/local/tmp/ghostlock-sa.frame，由既有 read_session_secret_frame 读 ✓）
6. 重跑 patch#1 —— 判据：crash_dump64 的 offset 16 窗口与 embeddedImage(0) 一致
```

## 下一步

探针再加一问：`openUdpEncapsulationSocket` 能不能开（拿到端口号）—— 与 SPI 同一族 API，
拿到它之后帧的六个字段就齐了（spi / encap_port / sender_port / icv_len / 两把密钥）✓。
