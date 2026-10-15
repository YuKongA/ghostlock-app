# 让写路径带上「组帧层的错误码」（2026-10-15，待落）

## 为什么

写失败时我们只看到 step-write-refused:call-failed ✗，而真正的信息在组帧层：
RealSpliceIo::last_error() —— 20/21/22/23 表示**组帧/发送**各步骤失败；为 0 表示报文发出去了。
这一条读数能一次分辨「报文没发出去 ✗」与「发出去但内核没处理 ✗」，是整条链最后一处未具名的地方。

## 现状（相关结构，都在 core/src/jni/api_43284_device.cpp）

- struct Landing43284 { PageCacheDoorLinux door; RealSpliceIo io; PageCacheWrite window; PageCacheLanding landing; bool up; }
- 表：g_surfaces / g_landings（落地对象与拼接面）
- device_make_access(landingId) → g_accesses[slot] = Access(g_landings[landingId-1].get())
- device_write_image(...) → write_image_verified(request, &refusal)

## 补丁（三处，都很小）

1) 记下 access 对应的落地槽：
   namespace 里加   constexpr std::size_t kMaxAccesses = 4U;
                    std::size_t g_access_landing[kMaxAccesses] = {};      // 0 = 未记
   在 device_make_access 里（成功创建 Access 之后）：
                    g_access_landing[index] = slot + 1U;                  // 记下落地槽（1 起）
   在 device_destroy_access 里：g_access_landing[slot] = 0U;

2) 写失败时带出组帧错误：
   device_write_image 里，write_image_verified 返回 false 之后：
                    const std::size_t landing_slot = g_access_landing[slot] - 1U;
                    const int splice_error = (g_access_landing[slot] != 0U && landing_slot < kMaxSurfaces)
                                                 ? g_landings[landing_slot]->io.last_error()
                                                 : -1;
                    静态缓冲里拼名：splice_error == 0 ? "land:dispatched-unverified"
                                                       : "land:splice-error-<n>"
                    *why = 该名（覆盖原来的 step-…；或拼在其前缀之后，二选一，保持门禁期望同步更新）

3) 门禁：这条新名会改变真机日志文本 ⇒ 若被门禁期望覆盖，同批更新期望（不许只改代码不改期望）。

## 判据

宿主构建 0 错误 + ctest 全绿 + 真机门禁 → 看 refusal：
  land:splice-error-20/21/22/23 ⇒ **报文根本没发出去** ⇒ 修组帧（前端/后端字节、IV 位置）
  land:dispatched-unverified   ⇒ 报文发出去了、内核没处理 ⇒ 转核 SA 方向/策略（入站）、或非 ESP 标记
