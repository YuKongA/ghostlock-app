# IPsec SA 的原生供给（不经 Kotlin / JNI）

> 目的：`cve_2026_43284` 的页缓存写需要一个**内核里真实存在**的 ESP SA（相同 SPI / 密钥 / UDP 封装端口），
> 入向解密才会把我们的包写进目标文件的页缓存。此前这一步由 app 的 Kotlin `IpsecManager` 完成并把
> 84 字节会话帧交给原生；本文记录**原生自给**的可行路径与从设备实测得到的线格式。

## 1. 为什么不是 JNI

| 想法 | 结论 |
| --- | --- |
| native 里 `JNI_CreateJavaVM` 起 JVM 再调 `IpsecManager` | **不可行**：`IpsecManager` 由 `context.getSystemService(Context.IPSEC_SERVICE)` 取得，依赖 app 的 `Context` 与 `ActivityThread`；裸 VM 里两者都不存在 |
| native attach 到现有 app 进程的 VM | 跨进程，无法 attach |
| **native 直接 binder 到 `android.net.IIpSecService`** | **可行**：`IpsecManager` 底下就是这一个 binder 调用，线格式与语言无关，见 §3 |

## 2. 权限：uid 2000 已实证可行

`app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt:73` 要求 `Process.myUid() == Process.SHELL_UID`（Shizuku UserService 跑在 **uid 2000**），
而 6f7ded87 的真机 PASS 正是由该服务创建 SA（`AndroidIpsecSessionFactory`）后驱动原生完成的。
⇒ `IpSecService` 对 **shell uid** 的校验通过；原生进程（`adb shell` 或 Shizuku 下）以自身 uid 直接调用同样成立。

## 3. 线格式（由设备 `/apex/com.android.tethering/javalib/framework-connectivity-t.jar` 的 dex 反汇编取得）

描述符：`android.net.IIpSecService`。Java AIDL ⇒ 事务码 = FIRST_CALL_TRANSACTION(1) + 声明序。

| 调用 | 事务码 | 参数（Parcel 写入序） | 返回 |
| --- | --- | --- | --- |
| `allocateSecurityParameterIndex` | **1** | writeString(remoteAddress) · writeInt(direction) · writeStrongBinder(token) | `IpSecSpiResponse{status, spi, resourceId}` |
| `openUdpEncapsulationSocket` | **3** | writeInt(port) · writeStrongBinder(token) | `IpSecUdpEncapResponse{status, fd, port, resourceId}` |
| `createTransform` | **4** | writeTypedObject(IpSecConfig) · writeStrongBinder(token) · writeString(callingPackage) | `IpSecTransformResponse{status, resourceId}` |
| `applyTransportModeTransform` | 1（另一重载） | writeTypedObject(ParcelFileDescriptor) · writeInt(direction) · writeInt(resourceId) | void |
| `deleteTransform` / `closeUdpEncapsulationSocket` | — | writeInt(resourceId) | void |

要点（T 的设计）：

- **资源 id 是 int**：创建调用的 IBinder 参数是**客户端提供的令牌**（供服务做死亡监听），服务把分配的 `resourceId` 作为 **int** 返回；后续 apply/delete/close 都用这个 int。原生只需一个最小的 `IResourceId` 令牌 binder（描述符 `android.net.IResourceId`，事务处理器容忍任意码）。
- 每个方法先 `writeInterfaceToken("android.net.IIpSecService")`；回复先 readException（状态头）。
- 设备侧 Kotlin 的参数（照抄即得一致行为，见 `AndroidIpsecSessionFactory`）：`openUdpEncapsulationSocket()` → `allocateSecurityParameterIndex(127.0.0.1)` → `IpSecAlgorithm(CRYPT_AES_CBC, aes32)` + `IpSecAlgorithm(AUTH_HMAC_SHA256, hmac32, 128bit)` → `setIpv4Encapsulation(encapSocket, senderPort)` → `buildTransportModeTransform(127.0.0.1, spi)`。
- 入向解密只需要 SA **存在**；本案不做 `applyTransportModeTransform`（那是给「自己发加密包」用的）。

## 4. 落地计划

1. `backend/cve_2026_43284/ipsec/sa_provision.{hpp,cpp}`（Android only，libbinder_ndk）：令牌 binder 类 + §3 的调用 + `IpSecConfig` / `IpSecAlgorithm` 的 Parcel 写入 + 回复解码 + 析构时 `deleteTransform` / `closeUdpEncapsulationSocket`。
2. 产出与 `SaMaterial` 同构的记录（spi / encapPort / senderPort / icvLen / keys）⇒ 现有 `AccessParams.sa` 直接可用；会话帧退化为跨进程时才需要的传输格式。
3. 设备探针 `tools/sa_provision_probe`：先只做 `openUdpEncapsulationSocket`（决定性问题：shell uid 是否被服务接受），再 `allocateSecurityParameterIndex`，再 `createTransform`，最后接现有示例的写 + 回读 ⇒ **新 kernel access 的真机落盘判据**。
4. 仍需从 dex 取的细节：`IpSecConfig.writeToParcel` 字段序、`IpSecAlgorithm.writeToParcel`（名字 + 密钥 + 截断位数）、`IpSecSpiResponse` / `IpSecTransformResponse` 字段序、`IpSecManager.DIRECTION_OUT/IN` 常量值。

## 5. 与现状的关系

- 原生供给接通后 `main` 不再依赖 app 传帧：`main` 自建 SA → 自建 `SaMaterial` → `KernelAccess` 写块 → 触发 / 等待；app 只提供 uid 2000 的执行环境，或彻底不要（`adb shell` 直接跑）。
- 在那之前，设备上的写会因「没有活动 SA」停在回读校验（已实测：判失败、载体页缓存哈希不变）。

## 6. 免 App 试验台（仓库原文，参数照抄，无需从 dex 挖）

`docs/analysis/43284-production-adb-harness.md`（2026-10-05，已在真机 PASS）给出等价内核对象的造法：

| 组件 | 做法（原文） |
| --- | --- |
| XFRM SA | `ip xfrm state add src 127.0.0.1 dst 127.0.0.1 proto esp spi 0x4a1f0011 mode transport enc 'cbc(aes)' <32B> auth-trunc 'hmac(sha256)' <32B> 128 encap espinudp 4500 4500 0.0.0.0`（**需 root**） |
| UDP 封装 socket | `tools/espencap/espencap.c`：`socket` + `setsockopt(UDP_ENCAP_ESPINUDP)` + `bind 127.0.0.1:4500`，`setsid ... </dev/null >/dev/null 2>&1 &` 启动 |
| 会话帧 | 手工 84B：version/kind/spi/encap_port=4500/sender_port=4501/icv=16/AES32/HMAC32/trailer；stdin = `[u32 len][GLKv3 doc][u32 len][frame][ACK × N]` |
| 算法参数 | AES-CBC-256 + HMAC-SHA256 截断 128 bit（与 `AndroidIpsecSessionFactory` 一致） |

该试验台的前提是**设备已 root**（`su -c`）。冷机（无 `su`）下 root 本身要靠 43284 链跑一次，而链又要 SA —— 所以冷机上的第一份 SA 只能来自 `IpSecService`（app 的 Kotlin，或本文的原生 binder 客户端）。

## 7. 权限实证（2026-10-10，本设备，锁屏、无 root、无 app）

```
$ adb shell service call ipsec 3 i32 0
Result: Parcel( 0x00000000: fffffffc 00000030 0075004e 006c006c  '....0...N.u.l.l.'
  0x00000010: 00420020 006e0069 00650064 00200072  ' .B.i.n.d.e.r. .'
  0x00000020: 00610070 00730073 00640065 00740020  'p.a.s.s.e.d. .t.'
  0x00000030: 0020006f 0070006f 006e0065 00640055  'o. .o.p.e.n.U.d.' )
```

回复文本是 **`Null Binder passed to openUdpEncapsulationSocket`**（`service call` 无法投递令牌 binder），
而不是权限拒绝 ⇒ **shell uid 已通过 `IpSecService` 的权限检查**，参数也被真正读取。编码 `1`（allocateSPI）同样返回 `Null Binder passed to allocateSecurityParameterIndex`。

## 8. 完整线格式（实现用）

| 类型 | Parcel 顺序 |
| --- | --- |
| `IpSecAlgorithm` | writeString(name) · writeByteArray(key) · writeInt(truncLenBits) |
| `IpSecConfig` | writeInt(mode) · writeString(srcAddress) · writeString(dstAddress) · writeParcelable(network) · writeInt(spiResourceId) · writeParcelable(encryption) · writeParcelable(authentication) · writeParcelable(authenticatedEncryption) · writeInt(encapType) · writeInt(encapSocketResourceId) · writeInt(encapRemotePort) · writeInt(nattKeepaliveInterval) · writeInt(markValue) · writeInt(markMask) · writeInt(xfrmInterfaceId) |
| `IpSecSpiResponse` | status(int) · resourceId(int) · spi(int) |
| `IpSecUdpEncapResponse` | status(int) · resourceId(int) · port(int) · fileDescriptor(ParcelFileDescriptor) |
| `IpSecTransformResponse` | status(int) · resourceId(int) |

事务码：`allocateSecurityParameterIndex`=1 · `openUdpEncapsulationSocket`=3 · `createTransform`=4（`IIpSecService$Stub$Proxy` 反汇编）。
常量：`mode` = transport(0)/tunnel(1)；`encapType` = `UDP_ENCAP`(2)；`DIRECTION_OUT`=0 / `DIRECTION_IN`=1（`IpSecManager`）。
