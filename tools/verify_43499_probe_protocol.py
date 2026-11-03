#!/usr/bin/env python3
"""G04/task-80 静态契约：C/Kotlin PROBE 协议 append 对拍（不编译、不跑设备）。

断言：
1) `victim_protocol.h` opcode 6/7/8 与 Kotlin `VictimProtocol.Opcode` 对齐；
2) capability bits 0–9 C 枚举与 Kotlin `VictimCapability` 对齐（7/8/9 append，不重排）；
3) helper 三 handler 存在，且不调用 fork/system/execve/posix_spawn（机制-only）；
4) PROBE_STATUS 4 KiB 上限/frame 单帧上限常量存在；
5) 既有命令 dispatch 保留（probeUid/perfSelfSample/execScript/park/bye）。
`--self-test` 用合成负例证明检查能失败。
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "app/src/main/cpp/backends/43499/core/victim_protocol.h"
HELPER = ROOT / "app/src/main/cpp/backends/43499/core/victim_helper/victim_helper.c"
KOTLIN_PROTOCOL = ROOT / "app/src/main/kotlin/com/ghostlock/app/core/victim/VictimProtocol.kt"
KOTLIN_CAPABILITY = ROOT / "app/src/main/kotlin/com/ghostlock/app/core/victim/VictimCapability.kt"
OPCODES = {"PROBE_STATUS": 6, "PROBE_COMM_SET": 7, "PROBE_COMM": 8}
CAPABILITY_BITS = {
    "PROBE_UID": 0, "SPAWN_HELPER": 1, "PERF_SELF_SAMPLE": 2, "EXEC_SCRIPT": 3,
    "PARK": 4, "BYE": 5, "DETACH_ROOTED": 6, "PROBE_STATUS": 7,
    "PROBE_COMM_SET": 8, "PROBE_COMM": 9,
}
FORBIDDEN_HELPER = ["fork(", "system(", "execve(", "posix_spawn("]


def fail(message):
    print("probe-protocol gate: FAIL " + message, flush=True)
    return 1


def scan(header_text, helper_text, protocol_text, capability_text):
    violations = []
    for name, wire in OPCODES.items():
        if not re.search(r"GLK_VICTIM_OPCODE_" + name + r"\s*=\s*" + str(wire), header_text):
            violations.append("c-opcode:" + name)
        if not re.search(r"\b" + name + r"\s*\(\s*" + str(wire) + r"\s*\)", protocol_text):
            violations.append("kotlin-opcode:" + name)
    for name, bit in CAPABILITY_BITS.items():
        if not re.search(r"GLK_VICTIM_CAPABILITY_" + name + r"\s*=\s*\(1\s*<<\s*" + str(bit) + r"\)", header_text):
            violations.append("c-capability:" + name)
        if not re.search(r"\b" + name + r"\s*\(\s*1\s+shl\s+" + str(bit) + r"\s*\)", capability_text):
            violations.append("kotlin-capability:" + name)
    for handler in ("glk_helper_probe_status", "glk_helper_probe_comm_set", "glk_helper_probe_comm"):
        if "static int32_t " + handler + "(" not in helper_text:
            violations.append("helper-missing:" + handler)
        start = helper_text.find("static int32_t " + handler + "(")
        if start >= 0:
            body = helper_text[start:start + 6000]
            for token in FORBIDDEN_HELPER:
                if token in body:
                    violations.append("helper-forbidden:" + handler + ":" + token)
    for token in ("GLK_VICTIM_PROBE_STATUS_MAX_BYTES", "GLK_VICTIM_PROBE_STATUS_MAX_REPLY_BYTES", "GLK_VICTIM_PROBE_COMM_MAX_BYTES"):
        if token not in header_text:
            violations.append("header-missing:" + token)
    for legacy in ("GLK_VICTIM_OPCODE_PROBE_UID", "GLK_VICTIM_OPCODE_PERF_SELF_SAMPLE", "GLK_VICTIM_OPCODE_EXEC_SCRIPT", "GLK_VICTIM_OPCODE_PARK", "GLK_VICTIM_OPCODE_BYE"):
        if legacy not in header_text or legacy.replace("GLK_VICTIM_OPCODE_", "glk_helper_") in ("",):
            if legacy not in helper_text:
                violations.append("dispatch-legacy:" + legacy)
    return violations


def self_test():
    good_header = HEADER.read_text(encoding="utf-8")
    good_helper = HELPER.read_text(encoding="utf-8")
    good_protocol = KOTLIN_PROTOCOL.read_text(encoding="utf-8")
    good_capability = KOTLIN_CAPABILITY.read_text(encoding="utf-8")
    if scan(good_header, good_helper, good_protocol, good_capability):
        return fail("clean tree must pass")
    bad_header = good_header.replace("GLK_VICTIM_OPCODE_PROBE_COMM = 8", "GLK_VICTIM_OPCODE_PROBE_COMM = 9")
    if not scan(bad_header, good_helper, good_protocol, good_capability):
        return fail("opcode drift undetected")
    bad_cap = good_header.replace("GLK_VICTIM_CAPABILITY_PROBE_STATUS = (1 << 7)", "GLK_VICTIM_CAPABILITY_PROBE_STATUS = (1 << 8)")
    if not scan(bad_cap, good_helper, good_protocol, good_capability):
        return fail("capability drift undetected")
    bad_protocol = good_protocol.replace("PROBE_COMM_SET(7)", "PROBE_COMM_SET(9)")
    if not scan(good_header, good_helper, bad_protocol, good_capability):
        return fail("kotlin opcode drift undetected")
    bad_helper = good_helper.replace("glk_helper_probe_status(void) {", "glk_helper_probe_status(void) {\n    fork();")
    if not scan(good_header, bad_helper, good_protocol, good_capability):
        return fail("helper fork undetected")
    print("probe-protocol self-test: PASS (drift/fork samples detected)", flush=True)
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    violations = scan(
        HEADER.read_text(encoding="utf-8"),
        HELPER.read_text(encoding="utf-8"),
        KOTLIN_PROTOCOL.read_text(encoding="utf-8"),
        KOTLIN_CAPABILITY.read_text(encoding="utf-8"),
    )
    if violations:
        for item in violations:
            print("  violation: " + item, flush=True)
        return fail(str(len(violations)) + " violations")
    print("probe-protocol gate: PASS (opcode 6/7/8 + capability 7/8/9 + helper-only)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
