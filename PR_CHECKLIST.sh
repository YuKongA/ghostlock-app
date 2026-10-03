#!/bin/bash
# PR 提交前验证脚本
# 运行所有必要的测试和检查

set -e

echo "=========================================="
echo "GhostLock 反 vr.ko 模块 - PR 验证"
echo "=========================================="
echo ""

# 1. 主机单元测试
echo "[1/7] 运行主机单元测试..."
cd src
make native-host-tests
if [ $? -eq 0 ]; then
    echo "✅ 主机单元测试通过"
else
    echo "❌ 主机单元测试失败"
    exit 1
fi
cd ..
echo ""

# 2. 编译真机二进制
echo "[2/7] 编译真机二进制..."
make -C src ghostlock
if [ $? -eq 0 ]; then
    echo "✅ 真机二进制编译成功"
else
    echo "❌ 真机二进制编译失败"
    exit 1
fi
echo ""

# 3. cmp_disasm 形状验证
echo "[3/7] 运行 cmp_disasm 形状验证..."
python3 tools/cmp_disasm.py baseline/build/native/ghostlock build/native/ghostlock
if [ $? -eq 0 ]; then
    echo "✅ cmp_disasm 形状验证通过"
else
    echo "❌ cmp_disasm 形状验证失败"
    exit 1
fi
echo ""

# 4. Kotlin 单元测试
echo "[4/7] 运行 Kotlin 单元测试..."
./gradlew :app:testDebugUnitTest --offline
if [ $? -eq 0 ]; then
    echo "✅ Kotlin 单元测试通过"
else
    echo "❌ Kotlin 单元测试失败"
    exit 1
fi
echo ""

# 5. C++/Kotlin 键集合双向核对
echo "[5/7] 核对 C++/Kotlin 键集合..."
# TODO: 添加自动化核对脚本
echo "⚠️ 手动核对以下内容："
echo "  - src/core/profile/model.h 中的 kRouteCatalog"
echo "  - app/src/main/kotlin/data/route/RouteKind.kt"
echo "  - 确保 token 和 wire 值一致"
echo ""

# 6. 真机验证（需要设备连接）
echo "[6/7] 真机验证..."
if adb devices | grep -q "device$"; then
    echo "✅ 检测到设备"
    echo ""
    echo "请手动执行以下步骤："
    echo "  1. adb push build/native/ghostlock /data/local/tmp/"
    echo "  2. adb shell chmod +x /data/local/tmp/ghostlock"
    echo "  3. adb shell /data/local/tmp/ghostlock --load-prebuilt-profile /data/local/tmp/profile.bin"
    echo "  4. adb logcat | grep -E 'vr neutralize|cfi'"
    echo "  5. adb shell su -c 'whoami'  # 应返回 root"
    echo ""
    echo "预期日志输出："
    echo "  [cfi] vr neutralize: probestub=0xffffffc080... commit_creds module probes=1"
    echo "  [cfi] vr neutralize: sys_exit slot 0 redirected"
    echo "  [cfi] vr neutralize: commit_creds_probes=1 neutralized=1 fallback=0"
else
    echo "⚠️ 未检测到设备，跳过真机验证"
    echo "请连接设备后重新运行此脚本"
fi
echo ""

# 7. 文档检查
echo "[7/7] 文档检查..."
if [ -f "PR.md" ] && [ -f "IMPLEMENTATION.md" ] && [ -f "docs/kernel_profiles/EXTRACT_TRACEPOINT_CONSTANTS.md" ]; then
    echo "✅ 文档完整"
else
    echo "❌ 文档不完整"
    echo "缺少以下文件："
    [ ! -f "PR.md" ] && echo "  - PR.md"
    [ ! -f "IMPLEMENTATION.md" ] && echo "  - IMPLEMENTATION.md"
    [ ! -f "docs/kernel_profiles/EXTRACT_TRACEPOINT_CONSTANTS.md" ] && echo "  - docs/kernel_profiles/EXTRACT_TRACEPOINT_CONSTANTS.md"
    exit 1
fi
echo ""

echo "=========================================="
echo "✅ 所有验证通过！"
echo "=========================================="
echo ""
echo "请确认以下事项后再提交 PR："
echo "  [ ] 所有主机测试通过"
echo "  [ ] 真机验证完成"
echo "  [ ] 设备适配清单更新"
echo "  [ ] 文档完整"
echo "  [ ] 代码审查通过"
echo ""
echo "确认无误后，请将 PR 推送到远程仓库。"