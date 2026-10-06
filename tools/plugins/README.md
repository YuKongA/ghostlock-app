# tools/plugins — 已移出（moved out）

参考对策插件原先放在这里，现已是**独立项目**（不在本仓库内），位置是**与本仓库同级**的目录
`../ghostlock-plugin-example/`（即和 `ghostlock-app/` 并列）：

```
ghostlock-plugin-example/        # 独立仓库/目录：include/ + src/ + tools/ + build.sh + README(_ZH)
  include/glk_contract_abi.h     # 内置 ABI 头（接口唯一来源）
  src/glk_probe_plugin.c         # 示例插件 glk.probe（2 params + 1 extract）
  tools/probe_runner.cpp         # 仅主机的探针 runner（部署前看冻结 TSV）
  build.sh                       # android / host / abi-check
```

**为什么移出**：插件作者不应需要 exploit 仓库；独立项目只依赖内置的 ABI 头即可交叉编译 aarch64 产物。

**本仓库保留的部分**（宿主侧契约，不要移走）：
- `src/core/plugin/**`：探针、schema/wire 校验、宿主（宿主实现本身）；
- `src/core/contract/abi/glk_contract_abi.h`：**ABI 权威**（独立项目里那份是 vendored 副本，用 `./build.sh abi-check` 对拍）；
- `docs/analysis/contract-design.md` §3.14.7：冻结的探针 TSV 契约与阶段可用性；
- `app/src/test/resources/*` 与 `profile-manifest-v3.tsv`：跨语言对拍物证（golden / manifest）。

**本目录仅作历史路径指引**，不要再往这里添加文件。
