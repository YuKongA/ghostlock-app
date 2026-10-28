#!/usr/bin/env python3
"""43499 日志调用点静态 gate（task-101）。

规则：
1. 生产 Kotlin 只允许 `Flow43499Impl.kt` 调 `Cve43499RunLog.emit`（唯一发射点）；
2. 每条 emit 的消息前缀必须属于阶段白名单（run start/w1/w1b/w2/w3/victim start/
   finish source/close termination/fail-stop/stage=）；
3. 任意其它生产文件出现 emit、或消息不在白名单、或 emit 与 PI hot-path 标记同段 ⇒ FAIL。

`--self-test`：内嵌变异负例（禁止文件/白名单外消息）必须被检出，否则 EXIT=1；
输出 MUTATION=FAIL→恢复 PASS；不写工作树。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

PREFIXES = (
    "run start",
    "w1",
    "w1b",
    "w2",
    "w3",
    "victim",
    "finish",
    "close",
    "fail-stop",
    "stage=",
)
ALLOWED_REL = "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/flow/Flow43499Impl.kt"
HELPER_REL = "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/flow/Cve43499RunLog.kt"
FORBIDDEN_MARKERS = ("executeActor(", "carrierHotPath", "waiterHotPath")


def extract_messages(lines: list[str], idx: int) -> list[str]:
    """读取 emit 调用附近 4 行的字符串字面量。"""
    window = "\n".join(lines[idx : min(len(lines), idx + 4)])
    return re.findall(r'"((?:[^"\\]|\\.)*)"', window)


def validate(text_by_path: dict[str, str]) -> list[str]:
    problems: list[str] = []
    calls = 0
    for path, text in sorted(text_by_path.items()):
        rel = path.replace("\\", "/")
        is_allowed = rel.endswith(ALLOWED_REL)
        is_helper = rel.endswith(HELPER_REL)
        lines = text.splitlines()
        for i, line in enumerate(lines):
            if not any(key in line for key in ("Cve43499RunLog.emit", "run.info(", "run.debug(", "run.warn(", "run.error(")):
                continue
            if is_helper or ".emit(progress, level, message)" in line:
                continue
            calls += 1
            if not is_allowed:
                problems.append(f"{rel}:{i + 1}: emit outside Flow43499Impl")
                continue
            if any(marker in line for marker in FORBIDDEN_MARKERS):
                problems.append(f"{rel}:{i + 1}: emit near PI/hot-path marker")
            messages = extract_messages(lines, i)
            if not messages:
                problems.append(f"{rel}:{i + 1}: emit without literal message")
                continue
            if not any(any(msg.startswith(p) for p in PREFIXES) for msg in messages):
                problems.append(f"{rel}:{i + 1}: emit message not in stage whitelist {messages[:2]}")
    if calls == 0:
        problems.append("no Cve43499RunLog.emit call found (implementation missing)")
    return problems


def read_sources(root: Path) -> dict[str, str]:
    sources: dict[str, str] = {}
    kotlin_root = root / "app/src/main/kotlin/com/ghostlock/app"
    for path in kotlin_root.rglob("*.kt"):
        sources[str(path)] = path.read_text(encoding="utf-8")
    return sources


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    sources = read_sources(root)
    problems = validate(sources)
    if "--self-test" in sys.argv:
        allowed = str(root / ALLOWED_REL)
        mutated = dict(sources)
        # 负例 1：禁止文件里加 emit
        mutated["/tmp/FakeBackend.kt"] = 'fun f() { Cve43499RunLog.emit(p, l, "pi-window hot") }'
        if not any("outside Flow43499Impl" in p for p in validate(mutated)):
            print("MUTATION=FAIL reason=forbidden-file-not-caught")
            return 1
        # 负例 2：允许文件里加白名单外消息
        mutated2 = dict(sources)
        if allowed not in mutated2:
            print("MUTATION=FAIL reason=allowed-file-missing")
            return 1
        mutated2[allowed] = mutated2[allowed] + '\nfun fake() { Cve43499RunLog.emit(p, l, "pi-window hot") }\n'
        if not any("not in stage whitelist" in p for p in validate(mutated2)):
            print("MUTATION=FAIL reason=whitelist-mutation-not-caught")
            return 1
        print("MUTATION=FAIL caught=forbidden-file,whitelist-message")
        print("RESTORE=PASS")
    if problems:
        for problem in problems:
            print("FAIL " + problem)
        return 1
    total = sum(sum(t.count(k) for k in ('run.info(', 'run.debug(', 'run.warn(', 'run.error(')) for path, t in sources.items() if path.replace('\\', '/').endswith(ALLOWED_REL))
    print(f"PASS 43499 log sites: {total} run log call(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
