# 写路径的「有界重试 + 比字节」补丁设计（2026-10-15，待落）

用户裁定：**底层项目不要异步** ⇒ 等待只允许出现在**原语内部**，且有界、具名，budget 由 Kotlin 给。
契约侧已就绪：WriteImage43284.imageBudgetMs（提交 c2ab8d11）。

## 改哪里

core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp 的 write_image_verified：
现在逐块「写 → **立刻**回读比字节」，一次不中即 kVerifyFailed（真机看到 step-verify-failed 正是它）。
写是派发式的（适配器注释：成功只到 Dispatched），落盘要等内核处理 ⇒ 立刻比对必然过早。

## 改成（逐块内）

    1) 写：request.access->write(0U, span, request.channel, *request.params)
       —— 失败照旧具名 step-write-refused:<status>
    2) 有界重试回读：deadline = steady_clock::now() + budget_ms
         loop {
             back = request.access->read(0U, request.width, request.channel, *request.params)
             if (back.ok() && back.value.size() == request.width && memcmp(back.value.data(), in, request.width) == 0) break;  // landed
             if (now >= deadline) { *refusal = kVerifyFailed; return false; }                                                   // budget-exhausted
             ::usleep(poll_us)                                                                                                  // 一个轮询间隔
         }
    3) budget_ms == 0 时退化为旧行为（不重试）—— 保留可对照性

## 数据从哪来

- WriteRequest 加一个 budget_ms 字段（我这边 device_write_image 从 WriteImage43284Values.imageBudgetMs 填）；
- poll_us 用常量（例如 2000 = 2 ms）；
- 具名出口三种：landed（比中）/ kVerifyFailed（预算用尽仍不中）/ kNoAccess、kImageNotAligned、kImageEmpty（照旧）。

## 判据

宿主构建 0 错误 + ctest 全绿（既有测试 + 可能新增"budget 用尽仍不中"的用例）
→ 真机门禁：期望 patch1 wrote=true（第一次真正把我们的字节写进系统文件的页缓存）
→ 若仍 false，看它在哪一层（具名已到最细：dispatch-refused / budget-exhausted）

## 注意

- 只动这一个函数的验证段，不动写段、不动其它步骤；
- 不许引入任何 Kotlin 侧的等待（已被用户否决）；
- budget 由 Kotlin 决定 ⇒ 探针里给一个值（例如 200 ms）。
