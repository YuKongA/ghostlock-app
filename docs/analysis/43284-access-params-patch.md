# AccessParams 补齐补丁（待落，2026-10-15）

目标：把工具（tools/pagecache_load_lkm.cpp）那半「调用方清单」的其余字段，在 core/src/device/43284/chain.cpp 的
device_access_initialize 里逐行补进 AccessParams。契约侧已就绪（AccessParams43284 = 18 字段，见提交 857f8696）。

## 补丁（插在 `params.sa = sa_material_of(sa_source);` 这一行之后）

```cpp
        /* 2026-10-15：工具那半调用方清单的其余字段（字符串借用式，全部由本栈帧持有）。 */
        const std::string hook_image = text_of(values.accessHookImagePath);
        const std::string kernel_release_text = text_of(values.accessKernelRelease);
        const std::string success_marker_text = text_of(values.accessSuccessMarkerPath);
        const std::string failure_marker_text = text_of(values.accessFailureMarkerPath);
        const std::string in_flight_marker_text = text_of(values.accessInFlightMarkerPath);
        const std::string module_marker_text = text_of(values.accessModuleMarkerPath);
        params.hook_image = access_path_of(hook_image);
        params.kernel_release = access_path_of(kernel_release_text);
        params.success_marker = access_path_of(success_marker_text);
        params.failure_marker = access_path_of(failure_marker_text);
        params.in_flight_marker = access_path_of(in_flight_marker_text);
        params.module_marker = access_path_of(module_marker_text);
        params.kernel_preempt = values.accessKernelPreempt;
        params.kernel_modversions = values.accessKernelModversions;
        params.kernel_module_force_unload = values.accessKernelModuleForceUnload;
        params.trigger_delay_ms = static_cast<std::uint64_t>(values.accessTriggerDelayMs);
        params.wait_ms = static_cast<std::uint64_t>(values.accessWaitMs);
```

## 注意

1. `text_of` 与 `access_path_of` 都已在该 TU 里（`text_of` 是 device_access_initialize 内的 lambda，
   `access_path_of` 是匿名命名空间里的助手）；空路径走 `access_path_of` 会给 {nullptr, 0}（规则见设计 §9.6）；
2. `hook_plan` 这一轮**不接**（字段类型待确认）—— 不要猜；
3. 同一段也要照抄到 `device_write_image` 与 `device_write_hook` 里（那两处各自重建了 AccessParams）；
4. 判据：宿主 0 错误 + Android 0 错误 + ctest 132/132 + verifyJniLibs PASS，然后真机跑一次 accessInit。

## 教训（我这两轮踩的）

- 用 Python 生成补丁时，多行 list 里别混入未转义的引号（`caller's` 那次把字符串截断）；
- 别在 TypeScript 里使用 Python 的变量名（`NL`）—— 两次失败都在拼串，不在内容；
- 更稳的写法：把补丁这样单独存成文件，落码时按段读取、逐段替换并断言命中数。
