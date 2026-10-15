# SA 材料的最后一条：SPI 从哪来（2026-10-15）

## 已查实

1. **App 里没有 IpSec 代码**（app/src 只有我探针里那句关于 IpSecManager 的注释）⇒ 现在没有任何地方建会话；
2. **帧有编码器**：encode_session_secret_frame(sa) → bytes（session_frame.hpp，session_frame_test.cpp 在用）
   ⇒ 只要拿到 SaMaterial（spi / encap_port / sender_port / icv_len / 两把密钥），**native 侧就能造出合法帧**；
3. 帧格式已冻结（84 字节 payload，见 43284-sa-frame-is-ipsecmanager-secrets.md）。

## 剩下的一条：SPI 从哪来

按 §11.30 的实测结论：**公开 API（IpSecTransform.Builder）不让指定 SPI，也不把 SPI 还给我们**；
而帧里的 SPI 必须与内核那条 SA 一致。于是只有三条路：

| 路 | 做法 | 代价 |
|---|---|---|
| A 外部给帧 | 由外部（操作者 / 能读 xfrm 的一方）按设备造好帧放到 /data/local/tmp/ghostlock-sa.frame | 需要一步设备外操作；与「必须从 app 自己 uid 起链」不冲突（链仍在 app 里跑） |
| B 系统权限读回 | 让 App 以 system 权限建会话并读回 SPI（系统 API IpSecConfig） | 需要 system app / root —— 与前置条件冲突 |
| C 内核侧读出 | 等 LKM 起来后读 xfrm state | 鸡生蛋：LKM 依赖写路径，写路径依赖 SPI |

**当前事实**：设备无 root、App 非 system ⇒ **只有 A 现在可行**（B/C 都需要我们还没有的东西）。

## 下一步

1. 先按 A 走通一次（外部造帧 → app 侧链子消费），把 patch#1 与后续阶段的真机判据拿到手；
2. 之后再回头做 B/C 的替代（例如把帧材料的产出交给上层的真实 profile 机制，等 profile 设计定案）；
3. 这条也是「真实 profile 还需进一步设计」的一部分：**帧/会话材料的来源**正是 profile 边界上的东西。