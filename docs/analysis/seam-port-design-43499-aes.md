# 附：43284 的 AES 件（判据与依赖更正）

> 本件是 `seam-port-design-43499.md` 的附件 · 2026-10-09

## 17 AES 验收判据已独立验证（不是凭记忆给的）

我给 `aes-ecb` 的判据是 **FIPS-197 附录 C.3** 的 AES-256 单块向量。为免「凭记忆写判据」这种错，我用**本机 OpenSSL 独立算了一遍** ✓：

```
明文（16 B）：00112233445566778899aabbccddeeff
密钥（32 B）：000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
命令：openssl enc -aes-256-ecb -K <key> -nopad -in plaintext
结果：8ea2b7ca516745bfeafc49904b496089        ← 与判据逐字节一致
OpenSSL 版本：3.6.4
```

⇒ 意义：队友的实现若算出别的值 ✗ ⇒ **判据不是嫌疑对象** ✓ ⇒ 应当去查它的 `expand_key` / `sub_bytes` / `mix_columns` ✓（这正是我给它的话 ✓）。

**教训**：判据要**独立验证** ✓ —— 本会话已多次因「口径/判据没核」而得出过强结论 ✗（§13 的 `#if` 猜测 ✓、§16 的 grep 口径 ✓），所以这次先算再说 ✓。

## 18 更正：43284 的 ICV 不只缺 AES，还缺 HMAC（实测）

我说过「AES 是 43284 上楼到真机的**唯一**瓶颈」。**实测不成立**：

- 旧 `ipsec/ipsec.cpp` 文件头自述：「The AES core lives in `aes256.cpp`; **the HMAC core is the shared `support/sha256.cpp`** (S4 R8).」
- 该件的 include 清单里同时有 `ipsec/aes256.hpp` 与 **`support/sha256.hpp`**，且 `esp_compute_icv(...)` 就在其中（旧 `ipsec.cpp:54` 起）。
- 新树核对：**有** `core/src/support/sha256.{hpp,cpp}` ✓，但 `core/src/support/*.hpp` 里 **`hmac` 零命中** ✗；而旧树的 `support/sha256.{hpp,cpp}` 里 **有** `hmac` ✓ ⇒ ⇒ **HMAC 那一半没被搬过来** ✗。

### 因此准确的清单是三件（不是一件）

1. **AES-256**（ECB/CBC）—— `aes-ecb` 正在做 ECB 那一件 ✓；
2. **HMAC-SHA256**（旧 `support/sha256` 里的那部分）—— **新树缺** ✗ ⇒ 要单独交一件 ✓；
3. 然后才能接写体的 **`compute_icv`** ✓。

**教训**：说「唯一瓶颈」这种话之前，必须**把那个函数的依赖读一遍** ✗ —— 我这次是从「`zeroize_bytes` 而已」推断的 ✓，而真实依赖写在**另一个文件的文件头**里 ✓。⇒ 与 §13/§16 同一类错：**推断代替实测** ✓。

## 21 AES-ECB 件：Lead 直接复算的验证记录（2026-10-09）

`aes-ecb` 交出 `core/src/backend/cve_2026_43284/ipsec/aes256.{hpp,cpp}`（73 + 279 行 ✓，公开面**只有**两个常量与 ECB 两个函数 ✓，**无 CBC** ✓ 与范围一致 ✓）+ `core/src/tests/aes256_test.cpp` ✓。

Lead 的独立复核（编译到 `/tmp`，**不碰 `build/`** ✓ 故未与在途门禁争用 ✓）：

```
c++ -std=c++23 -Wall -Wextra -Wconversion -Wsign-conversion -Isrc \
    src/tests/aes256_test.cpp src/backend/cve_2026_43284/ipsec/aes256.cpp -o /tmp/aes_test
issues: 0                      ← 0 告警 0 错误
直跑输出：
  aes-256: ciphertext 8ea2b7ca516745bfeafc49904b496089
  aes-256: decrypted  00112233445566778899aabbccddeeff
  aes-256: PASS (FIPS-197 C.3 vector, round trip, null pointers refused)
EXIT=0
```

⇒ 与**本机 OpenSSL**（3.6.4 ✓）算出的期望值**逐字节一致** ✓ ⇒ 判据与实现**双向确认** ✓（既验证了实现 ✓，也验证了我写下的判据 ✓）。

**未接线的部分**：`core/Makefile` 此刻仍未接（文件清单 / `EVIDENCE_BINS` / rule / check 行 ✓ 四处待落 ✓）⇒ 按我上一轮的澄清 ✓，**Makefile 只有 `aes-ecb` 一个写入者** ✓（`aes-port` 的授权已收回 ✓）⇒ 由它落 ✓ 或由我在它停手后落 ✓。

**仍未交**：HMAC-SHA256（`aes-port` 在做 ✓，判据 RFC 4231 TC1/TC2 已独立验证 ✓）⇒ 两件齐后接 `compute_icv` ✓。

## 22 接 `compute_iv` 与 `compute_icv` 的确切写法（据已量旧代码）

写体的缝（`seam/pagecache_write.hpp` 的 `SpliceOps`）有两处密码学槽 ✓，它们的真实现现在可以写清楚了 ✓：

### ① `compute_iv` —— **今天就能接**（只要 ECB ✓ 已落地 ✓）

旧 `ipsec/ipsec.cpp:39` 实测：`if (!aes256_ecb_decrypt(aes_key, old_content, decrypted.data())) { … }` ⇐ 这就是「**ECB 单块解密 feeds the CBC IV identity**」那句注释的实现 ✓。

⇒ 新写法：`compute_iv(old_block, aes_key, out)` = **对 `old_block` 做一次 `aes256_ecb_decrypt(aes_key, …)`** ✓ ⇒ 返回 false 时**不得继续**（没有 IV 就没有数据包 ✓ 与我写体的 `NoOldBlock` 同一条规矩 ✓）。
⇒ 它**只依赖 ECB** ✓ ⇒ **不依赖 HMAC** ✓ ⇒ **此刻就可以接** ✓。

### ② `compute_icv` —— 等 HMAC 落地后接

旧 `support/sha256.hpp:65-70` 的注释实测：「HMAC-SHA256 (RFC 2104); **the full 32-byte MAC is written and the ESP layer** （截断）」✓。

⇒ 新写法：`compute_icv(datagram, size, hmac_key, icv_len, out)` = 调 `support::hmac_sha256(hmac_key, key_len, datagram, size, full_mac)` ✓ 然后**按 `icv_len` 截断复制**到 `out` ✓ ⇒ 与旧树「先写满 32 字节、由 ESP 层截断」的分工**一致** ✓。

⇒ **顺序**：ECB（已交 ✓）⇒ HMAC（在跑 ✓）⇒ 两件齐即可接 ② 与 ① ✓ ⇒ 然后 43284 的写**第一次是合法 ESP** ✓。

> CBC 判据与真实 syscall 面规格（§23–§24）：见同目录 `seam-port-design-43499-syscall.md`。
