#!/usr/bin/env python3
"""Build a standard wire profile whose steps are the Lua procedure.

Template is core/fixtures/wire-43499-collide-F.json: it already carries the
standard keys (step, code, handoff, device, budgets, ...). The base64 of
procedures/43499_window.lua is written into BOTH places the code lives:

  * `step.code` -- the one the HOST actually runs (core/src/main.cpp reads
    doc.child(*step, "code"), never the top-level key);
  * top-level `code` -- kept because the profile carries it as the document's
    own copy, and dropping a field would be a wire change this tool has no
    mandate to make.

The two MUST stay byte-identical: they are the same script, and a generator
that updates only one of them is exactly the bug this comment records (the host
silently kept running the template's stale script while every edit went to the
copy nobody read). main() asserts the equality before writing anything.
"""
import base64
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "core/fixtures/wire-43499-collide-F.json"
LUA = ROOT / "procedures/43499_window.lua"
OUT = ROOT / "core/fixtures/wire-43499-lua-steps.json"


def main() -> int:
    doc = json.loads(SRC.read_text())
    lua = LUA.read_bytes()
    print("template keys:", sorted(doc.keys()))
    print("step  =", json.dumps(doc.get("step"))[:160])
    print("steps =", json.dumps(doc.get("steps"))[:160])
    print("code  length (before) =", len(str(doc.get("code", ""))))
    encoded = base64.b64encode(lua).decode("ascii")
    doc["code"] = encoded
    # The host reads step.code (core/src/main.cpp: doc.child(*step, "code")), so the same
    # script goes there too: write one of them only and the sandbox keeps running the
    # template's stale code while every edit lands in the copy nothing reads.
    step = doc.get("step")
    if not isinstance(step, dict):
        raise SystemExit("template's step is not an object; cannot mirror code into step.code")
    step["code"] = encoded
    doc["lua_source"] = "procedures/43499_window.lua"
    # The logging seam reads these; the level and path live in the profile, never in code.
    doc["log"] = {"level": "debug", "path": "/data/local/tmp/ghostlock.log"}
    OUT.write_text(json.dumps(doc, indent=2, sort_keys=True) + chr(10))
    print("code  length (top)    =", len(doc["code"]))
    print("step.code length      =", len(step["code"]))
    if doc["code"] != step["code"]:
        raise SystemExit("refusing to write: top-level code and step.code differ")
    print("codes identical       = True")
    framed = OUT.with_suffix(".framed")
    body = OUT.read_bytes()
    framed.write_bytes(len(body).to_bytes(4, "big") + body)
    print("wrote", OUT.relative_to(ROOT), len(body), "bytes")
    print("wrote", framed.relative_to(ROOT), framed.stat().st_size, "bytes (4-byte big-endian length + body)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())