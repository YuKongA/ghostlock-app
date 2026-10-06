# Device gate FAIL - the real token conflict

release: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557
binary_sha256: 5731450a9030...
verdict: FAIL - plan_error reason=queue-and-token-both-present backend=cve_2026_43499; cannot load profile; native exit=255

This is the failure that exposed an incomplete M3 migration: available had been rewritten to
the object form while backend.cve_2026_43499.steps still carried the token. Native rejects
both forms in the same backend section by design (glkv3_parse.cpp 407-419). Fixed in 6a3d60c6
by removing the token from all 62 migrated assets; the passing run is 20261006-141317.