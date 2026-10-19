# 日志调用点清单（2026-10-18）

## 1. Kotlin 侧调用点（TypedBackendProbe）

| 行 | 当前 helper | 当前级别 | 建议 | 调用 |
|---|---|---|---|---|
| 56 | vstep() | verbose |  | `private fun vstep(line: String) {` |
| 61 | dstep() | debug |  | `private fun dstep(line: String) {` |
| 66 | wstep() | warn |  | `private fun wstep(line: String) {` |
| 71 | estep() | error |  | `private fun estep(line: String) {` |
| 226 | step() | info | 保留 Info（成功里程碑） | `fun step(line: String) = emit(com.ghostlock.app.log.LogLevel.Info, line)` |
| 239 | step() | info | 保留 Info（成功里程碑） | `deployUmhPayload(context) { line -> step(line) }` |
| 251 | wstep() | warn |  | `wstep("typed: ipsec spi skipped no-ipsec-service")` |
| 257 | step() | info | 保留 Info（成功里程碑） | `step("typed: ipsec spi allocated=0x" + Integer.toHexString(allocated.spi))` |
| 261 | step() | info | 保留 Info（成功里程碑） | `step("typed: ipsec spi closed")` |
| 264 | estep() | error |  | `estep("typed: ipsec spi refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 275 | wstep() | warn |  | `wstep("typed: ipsec encap skipped no-ipsec-service")` |
| 280 | step() | info | 可保留 Info（成功+参数），或拆一条纯 Info | `step("typed: ipsec encap opened port=4500")` |
| 284 | step() | info | 保留 Info（成功里程碑） | `step("typed: ipsec encap closed")` |
| 287 | estep() | error |  | `estep("typed: ipsec encap refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 320 | step() | info | 可保留 Info（成功+参数），或拆一条纯 Info | `step( "typed: sa frame written=" + written + " spi=0x" + Integer.toHexString(allocated.spi) + " sha256=" + digest.take(16), )` |
| 328 | estep() | error |  | `estep("typed: sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 360 | estep() | error |  | `estep("typed: ipsec sa refused no-ipsec-service")` |
| 370 | step() | info | 保留 Info（成功里程碑） | `step("typed: ipsec sa encapPort=" + encapPort)` |
| 379 | step() | info | 保留 Info（成功里程碑） | `step("typed: ipsec sa senderPort=" + saSenderPort)` |
| 387 | step() | info | 应降为 Debug（分支/参数） | `step("typed: ipsec sa spi=0x" + Integer.toHexString(spi))` |
| 411 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: ipsec sa built spi=0x" + Integer.toHexString(spi) + " encap=" + encapPort + " sender=" + saSenderPort + " aes=" + retainedAes + " hmac="` |
| 420 | estep() | error |  | `estep("typed: ipsec sa refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 441 | step() | info | 应降为 Debug（分支/参数） | `step("typed: ipsec sa frame=" + written + " (SA 已建、帧按同一材料落盘)")` |
| 443 | estep() | error |  | `estep("typed: ipsec sa frame refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 470 | step() | info | 保留 Info（成功里程碑） | `refusal.forEach { line -> step(line) }` |
| 855 | estep() | error |  | `estep("typed: 43499 library missing (lib" + Native43499.LIBRARY + ".so)")` |
| 858 | step() | info | 保留 Info（成功里程碑） | `step("typed: 43499 library loaded")` |
| 865 | estep() | error |  | `estep("typed: 43499 create failed")` |
| 868 | step() | info | 保留 Info（成功里程碑） | `step("typed: 43499 create ok")` |
| 877 | dstep() | debug |  | `dstep("typed: 43499 initialize code=" + code + " refusal=" + refusal[0])` |
| 882 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43499 snapshot code=" + code + " " + names.indices.joinToString(",") { names[it] + "=" + snapshot[it] }, )` |
| 891 | dstep() | debug |  | `dstep("typed: 43499 prepare code=" + code + " refusal=" + refusal[0])` |
| 896 | dstep() | debug |  | `dstep("typed: 43499 writeWord code=" + code + " refusal=" + refusal[0])` |
| 901 | dstep() | debug |  | `dstep("typed: 43499 finishVictim code=" + code + " refusal=" + refusal[0])` |
| 906 | dstep() | debug |  | `dstep("typed: 43499 shutdown code=" + code + " refusal=" + refusal[0])` |
| 910 | dstep() | debug |  | `dstep("typed: 43499 destroy code=" + code + " refusal=" + refusal[0])` |
| 914 | dstep() | debug |  | `dstep("typed: 43499 shutdown-after-destroy code=" + code + " refusal=" + refusal[0])` |
| 941 | step() | info | 保留 Info（成功里程碑） | `finish.emit { line -> step(line) }` |
| 954 | estep() | error |  | `estep("typed: 43284 library missing (lib" + Native43284.LIBRARY + ".so)")` |
| 958 | step() | info | 保留 Info（成功里程碑） | `step("typed: 43284 library loaded")` |
| 965 | estep() | error |  | `estep("typed: 43284 create failed")` |
| 969 | step() | info | 保留 Info（成功里程碑） | `step("typed: 43284 create ok")` |
| 974 | dstep() | debug |  | `dstep("typed: 43284 initialize code=" + code + " refusal=" + refusal[0])` |
| 980 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 snapshot code=" + code + " " + Native43284.snapshotFields().indices.joinToString(",") { index -> Native43284.snapshotFields()[inde` |
| 991 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 embeddedImage code=" + (if (image == null) "null" else "ok") + " size=" + (image?.size ?: -1) + " refusal=" + refusal[0], )` |
| 1007 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 hookPlan code=" + code + " words=" + hook.joinToString(",") + " guard=" + Assembled43284.guardPolicyName(Assembled43284.GUARD_POLI` |
| 1021 | dstep() | debug |  | `dstep("typed: 43284 moduleCheck code=" + code + " refusal=" + refusal[0])` |
| 1051 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 waitTerminal code=" + code + " state=" + terminal[0] + " refusal=" + refusal[0], )` |
| 1081 | dstep() | debug |  | `dstep("typed: 43284 resolveTarget code=" + code + " ticket=" + ticket[0] + " refusal=" + refusal[0])` |
| 1097 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: 43284 readBlock code=" + code + " bytes=" + block.joinToString("") { byte -> "%02x".format(byte) } + " refusal=" + refusal[0], )` |
| 1108 | dstep() | debug |  | `dstep("typed: 43284 patch1-via-kotlin no-helper refusal=" + refusal[0])` |
| 1131 | estep() | error |  | `estep("typed: 43284 patch1-via-kotlin blocks=" + written + " refusedAt=" + refusedAt)` |
| 1148 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 patch1-readback code=" + code + " first16=" + back.joinToString("") { byte -> "%02x".format(byte) } + " writes=" + snapshot[4] + "` |
| 1165 | vstep() | verbose |  | `vstep("typed: 43284 writeBlock code=" + code + " refusal=" + refusal[0])` |
| 1169 | step() | info | 保留 Info（成功里程碑） | `step( "typed: 43284 snapshot-after-write code=" + code + " " + Native43284.snapshotFields().indices.joinToString(",") { index -> Native43284.snapshotF` |
| 1187 | dstep() | debug |  | `dstep("typed: 43284 resolveTarget(bad) code=" + code + " refusal=" + refusal[0])` |
| 1205 | dstep() | debug |  | `dstep("typed: 43284 trigger code=" + code + " refusal=" + refusal[0])` |
| 1210 | dstep() | debug |  | `dstep("typed: 43284 shutdown code=" + code + " refusal=" + refusal[0])` |
| 1214 | dstep() | debug |  | `dstep("typed: 43284 destroy code=" + code + " refusal=" + refusal[0])` |
| 1225 | estep() | error |  | `estep("typed: assembled 43499 create failed")` |
| 1232 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: assembled 43499 route=" + route + " access=" + access43499 + " refusal=" + assembled43499.lastRefusal, )` |
| 1250 | dstep() | debug |  | `dstep("typed: assembled 43499 init=" + inited + " refusal=" + assembled43499.lastRefusal)` |
| 1255 | dstep() | debug |  | `dstep("typed: assembled 43499 write=" + wrote + " refusal=" + assembled43499.lastRefusal)` |
| 1260 | step() | info | 保留 Info（成功里程碑） | `step( "typed: assembled 43499 read=" + (back?.size ?: -1) + " same=" + same + " refusal=" + assembled43499.lastRefusal, )` |
| 1267 | dstep() | debug |  | `dstep("typed: assembled 43499 shutdown=" + shut + " refusal=" + assembled43499.lastRefusal)` |
| 1272 | step() | info | 保留 Info（成功里程碑） | `step("typed: assembled 43499 close=" + closed43499)` |
| 1445 | step() | info | 应降为 Debug（分支/参数） | `step( "write result=failed i=" + regionIndex + " block=" + index + " off=0x" + java.lang.Long.toHexString(index.toLong() * Assembled43284.BLOCK.toLong` |
| 1452 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: verify sampled label=" + label + " planned=" + sample.planned + " read=" + sample.returned + " verified=" + sample.verified + " mismatch` |
| 1484 | step() | info | 保留 Info（成功里程碑） | `step( "write outcome i=" + label + " wrote=" + wrote + " written=" + outcome[0] + " verified=" + outcome[1] + " rolled=" + outcome[2] + " overflow=" +` |
| 1522 | estep() | error |  | `estep("typed: assembled 43284 library missing")` |
| 1560 | step() | info | 保留 Info（成功里程碑） | `step(carrier.line())` |
| 1575 | estep() | error |  | `estep("typed: assembled 43284 create failed")` |
| 1583 | step() | info | 保留 Info（成功里程碑） | `step("typed: assembled 43284 init code=" + session.lastCode)` |
| 1587 | step() | info | 应降为 Debug（分支/参数） | `step("typed: assembled 43284 open code=" + session.lastCode + " ticket=" + ticket)` |
| 1591 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: assembled 43284 read bytes=" + (block?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") + " refusal=" + session.lastRefusal, ` |
| 1607 | dstep() | debug |  | `dstep("typed: stepwise 43284 open target=" + stepTarget + " refusal=" + stepwise.lastRefusal)` |
| 1619 | dstep() | debug |  | `dstep("typed: probe pattern wrote=" + wrote + " at=" + hex(atSame))` |
| 1623 | step() | info | 应降为 Debug（分支/参数） | `step("typed: probe pattern below=" + hex(atBelow))` |
| 1627 | step() | info | 应降为 Debug（分支/参数） | `step("typed: probe pattern above=" + hex(atAbove))` |
| 1628 | step() | info | 应降为 Debug（分支/参数） | `}.onFailure { problem -> step("typed: probe pattern refused " + problem.javaClass.simpleName) }` |
| 1632 | dstep() | debug |  | `dstep("typed: stepwise 43284 socket=" + stepSocket + " refusal=" + stepwise.lastRefusal)` |
| 1636 | dstep() | debug |  | `dstep("typed: stepwise 43284 surface=" + stepSurface + " refusal=" + stepwise.lastRefusal)` |
| 1640 | dstep() | debug |  | `dstep("typed: stepwise 43284 landing=" + stepLanding + " refusal=" + stepwise.lastRefusal)` |
| 1642 | wstep() | warn |  | `wstep("typed: stepwise 43284 bringUp=skipped-path-b")` |
| 1650 | step() | info | 保留 Info（成功里程碑） | `step( "typed: stepwise 43284 landed=" + landed + " back=" + (back?.joinToString("") { byte -> "%02x".format(byte) } ?: "none") + " refusal=" + stepwis` |
| 1659 | dstep() | debug |  | `dstep("typed: stepwise 43284 access=" + stepAccess + " refusal=" + stepwise.lastRefusal)` |
| 1690 | step() | info | 保留 Info（成功里程碑） | `step( "typed: readable " + path + " exists=" + exists + " canRead=" + canRead + " osAccess=" + osAccess.getOrElse { "throw" } + " " + opened + " size=` |
| 1701 | dstep() | debug |  | `dstep("typed: stepwise 43284 accessInit=" + accessInit + " refusal=" + stepwise.lastRefusal)` |
| 1717 | dstep() | debug |  | `dstep("typed: stepwise 43284 patch1 wrote=" + patch1Wrote + " refusal=" + stepwise.lastRefusal)` |
| 1721 | step() | info | 保留 Info（成功里程碑） | `step("typed: stepwise 43284 accessDestroy=" + stepAccessDestroyed)` |
| 1746 | step() | info | 保留 Info（成功里程碑） | `step("typed: patcher reuses socket=" + stepwise.socketId)` |
| 1750 | dstep() | debug |  | `dstep("typed: patcher surfaceId=" + madeSurface + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)` |
| 1754 | dstep() | debug |  | `dstep("typed: patcher landingId=" + madeLanding + " code=" + patcher.lastCode + " refusal=" + patcher.lastRefusal)` |
| 1755 | wstep() | warn |  | `wstep("typed: marker-before-ids")` |
| 1756 | dstep() | debug |  | `dstep("typed: patch1 door=" + patchDoor + " refusal=" + patcher.lastRefusal)` |
| 1760 | step() | info | 保留 Info（成功里程碑） | `step("typed: patch1 access=" + patchAccess)` |
| 1765 | step() | info | 保留 Info（成功里程碑） | `step( "typed: ids stepwise target=" + stepwise.targetId + " landing=" + stepwise.landingId + " access=" + stepwise.accessId + " || patcher target=" + ` |
| 1771 | estep() | error |  | `estep("typed: ids refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 1777 | dstep() | debug |  | `dstep("typed: patch1 init=" + patchInit + " refusal=" + patcher.lastRefusal)` |
| 1783 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: sa material spi=0x" + Integer.toHexString(patchMaterial.spi) + " encap=" + patchMaterial.encapPort + " aesNonZero=" + patchMaterial.aesK` |
| 1815 | step() | info | 保留 Info（成功里程碑） | `step("typed: old-page direct=" + directHex + " viaChain=" + chainHex + " same=" + (viaChain != null && direct.contentEquals(viaChain)))` |
| 1818 | estep() | error |  | `estep("typed: old-page refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 1826 | dstep() | debug |  | `dstep("typed: next-sequence=" + patchNextSeq + " refusal=" + patcher.lastRefusal)` |
| 1828 | estep() | error |  | `estep("typed: next-sequence refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 1837 | step() | info | 保留 Info（成功里程碑） | `step("typed: xfrm-stat=" + stat)` |
| 1839 | estep() | error |  | `estep("typed: xfrm-stat refused " + problem.javaClass.simpleName)` |
| 1845 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: encap state port=" + (encapSocket?.getPort() ?: -1) + " saEncapPort=" + saEncapPort + " held=" + (encapSocket != null), )` |
| 1851 | estep() | error |  | `estep("typed: encap state refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 1872 | step() | info | 应降为 Debug（分支/参数） | `step("typed: direct udp to encap port=" + saEncapPort + " => " + directResult)` |
| 1876 | step() | info | 保留 Info（成功里程碑） | `step("typed: network active=" + (cm?.activeNetwork != null))` |
| 1878 | estep() | error |  | `estep("typed: network refused " + problem.javaClass.simpleName)` |
| 1919 | vstep() | verbose |  | `vstep("typed: nextSequence before hook=" + seq.value + " patcherLanding=" + patcher.landingId)` |
| 1923 | dstep() | debug |  | `dstep("typed: hook init=" + hookInit + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)` |
| 1931 | vstep() | verbose |  | `vstep("typed: reader wrote=" + wrote + " same=" + hex(hook.readBlock(hookDoor, 0xFE600L)))` |
| 1932 | vstep() | verbose |  | `vstep("typed: reader below=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " above=" + hex(hook.readBlock(hookDoor, 0xFE610L)))` |
| 1933 | vstep() | verbose |  | `vstep("typed: reader skew -8=" + hex(hook.readBlock(hookDoor, 0xFE5F8L)) + " +8=" + hex(hook.readBlock(hookDoor, 0xFE608L)))` |
| 1934 | vstep() | verbose |  | `vstep("typed: reader skew -16=" + hex(hook.readBlock(hookDoor, 0xFE5F0L)) + " +16=" + hex(hook.readBlock(hookDoor, 0xFE610L)))` |
| 1935 | vstep() | verbose |  | `vstep("typed: reader wide -32=" + hex(hook.readBlock(hookDoor, 0xFE5E0L)) + " +32=" + hex(hook.readBlock(hookDoor, 0xFE620L)))` |
| 1936 | vstep() | verbose |  | `vstep("typed: reader wide -64=" + hex(hook.readBlock(hookDoor, 0xFE5C0L)) + " +64=" + hex(hook.readBlock(hookDoor, 0xFE640L)))` |
| 1937 | vstep() | verbose |  | `vstep("typed: reader wide +128=" + hex(hook.readBlock(hookDoor, 0xFE680L)) + " +256=" + hex(hook.readBlock(hookDoor, 0xFE700L)))` |
| 1938 | step() | info | 保留 Info（成功里程碑） | `}.onFailure { problem -> step("typed: reader refused " + problem.javaClass.simpleName) }` |
| 1993 | step() | info | 保留 Info（成功里程碑） | `step(checked.summary)` |
| 1995 | estep() | error |  | `estep("typed: " + checked.refusal)` |
| 2000 | estep() | error |  | `estep("typed: plan:check-refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 2014 | step() | info | 应降为 Debug（分支/参数） | `step("typed: hook rewrite plan site=" + site + " branch=" + brAt + " block=" + blk0 + " within=" + within + " imm=0x" + imm.toString(16))` |
| 2015 | step() | info | 保留 Info（成功里程碑） | `step("typed: hook rewrite word=0x" + word.toString(16))` |
| 2023 | dstep() | debug |  | `dstep("typed: hook branch rewrite=" + branchFix + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)` |
| 2030 | vstep() | verbose |  | `vstep("typed: hook site after=" + (blk?.joinToString(" ") { b -> "%02x".format(b) } ?: "unreadable"))` |
| 2056 | dstep() | debug |  | `dstep("typed: hook site image wrote=" + imWrote + " word=0x" + word.toString(16) + " block=" + blk0)` |
| 2058 | wstep() | warn |  | `wstep("typed: hook site image skipped: block unreadable")` |
| 2060 | step() | info | 保留 Info（成功里程碑） | `}.onFailure { problem -> step("typed: hook site image refused " + problem.javaClass.simpleName) }` |
| 2070 | vstep() | verbose |  | `vstep("typed: hook adopt=" + hookAdopt2 + " seq=" + seq.value)` |
| 2079 | dstep() | debug |  | `dstep("typed: hook wrote=" + hookWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)` |
| 2087 | vstep() | verbose |  | `vstep("typed: hook payload bytes=" + (payload?.size ?: -1))` |
| 2091 | vstep() | verbose |  | `vstep("typed: hook landing=" + hookLanding)` |
| 2100 | vstep() | verbose |  | `vstep("typed: hook payload slots=" + payloadSlots)` |
| 2109 | dstep() | debug |  | `dstep("typed: hook payload wrote=" + payloadWrote + " code=" + hook.lastCode + " refusal=" + hook.lastRefusal)` |
| 2131 | step() | info | 应降为 Debug（分支/参数） | `step("typed: hook site block before=" + before.joinToString(" ") { b -> "%02x".format(b) } + " word=0x" + word2.toString(16))` |
| 2163 | dstep() | debug |  | `dstep("typed: hook site block wrote=" + siteWrote + " afterWord=0x" + afterWord.toString(16) + " verified=" + (afterWord == word2))` |
| 2165 | step() | info | 应降为 Debug（分支/参数） | `step("typed: hook site block unreadable")` |
| 2167 | step() | info | 应降为 Debug（分支/参数） | `}.onFailure { problem -> step("typed: hook site block refused " + problem.javaClass.simpleName) }` |
| 2171 | estep() | error |  | `estep("typed: hook payload refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 2192 | dstep() | debug |  | `dstep("module precheck: name=" + pre.name + " vermagic=" + pre.vermagic)` |
| 2194 | dstep() | debug |  | `dstep("module precheck: refused " + pre)` |
| 2205 | step() | info | 保留 Info（成功里程碑） | `val moduleBytes = retagVermagic(moduleBytesRaw) { line -> step(line) }` |
| 2206 | vstep() | verbose |  | `vstep("typed: module bytes=" + moduleBytes.size)` |
| 2239 | dstep() | debug |  | `dstep("typed: module init=" + moduleInit + " code=" + module.lastCode + " refusal=" + module.lastRefusal)` |
| 2245 | step() | info | 保留 Info（成功里程碑） | `step( "write stage=region i=1 of=1 off=0x0 len=" + moduleBytes.size + " verify=1 rollback=0 label=module", )` |
| 2265 | wstep() | warn |  | `wstep("module write: skipped (carrier already holds this module)")` |
| 2276 | dstep() | debug |  | `dstep("typed: module wrote=" + moduleWrote + " code=" + module.lastCode + " refusal=" + module.lastRefusal)` |
| 2288 | step() | info | 应降为 Debug（分支/参数） | `step( "write result=ok i=1 off=0x0 blocks=" + finish.moduleBlocks + " bytes=" + moduleBytes.size, )` |
| 2314 | vstep() | verbose |  | `vstep("typed: carrier scan off=" + probeOffset + " head=" + (blk?.take(4)?.joinToString(" ") { b -> "%02x".format(b) } ?: "null"))` |
| 2316 | vstep() | verbose |  | `}.onFailure { problem -> vstep("typed: carrier scan refused " + problem.javaClass.simpleName) }` |
| 2318 | estep() | error |  | `estep("typed: module refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 2342 | dstep() | debug |  | `dstep("typed: trigger-time apply wrote=" + triggerApplyWrote)` |
| 2344 | dstep() | debug |  | `}.onFailure { problem -> dstep("typed: trigger-time apply refused " + problem.javaClass.simpleName) }` |
| 2358 | step() | info | 应降为 Debug（分支/参数） | `step( "typed: hook trigger code=" + triggerCode + " delayMs=" + triggerSpec.delayMs + " igniter=" + triggerSpec.igniterPath + " retries=" + triggerSpe` |
| 2388 | step() | info | 保留 Info（成功里程碑） | `// step("typed: detonate exit=" + exit)` |
| 2390 | step() | info | 保留 Info（成功里程碑） | `// step("typed: detonate refused " + problem.javaClass.simpleName)` |
| 2393 | step() | info | 保留 Info（成功里程碑） | `step("typed: detonate disabled (init-reap only, ref exec_path=nullptr)")` |
| 2426 | dstep() | debug |  | `dstep("typed: hook wait terminal=" + terminal + " refusal=" + hook.lastRefusal)` |
| 2439 | step() | info | 保留 Info（成功里程碑） | `step( "typed: wait success=" + (terminal == WaitPolicy43284.TERMINAL_SUCCESS) + " moduleLive=" + (if (moduleLiveAfterWait) 1 else 0) + " markerFresh="` |
| 2467 | vstep() | verbose |  | `vstep("typed: retry " + attempt + " mutex=" + mutex + " slots=" + slots + " payload=" + wrote)` |
| 2507 | step() | info | 保留 Info（成功里程碑） | `step( "typed: retry " + attempt + " trigger=" + triggerCode2 + " terminal=" + state2 + " moduleLive=" + (if (moduleLive) 1 else 0), )` |
| 2513 | vstep() | verbose |  | `}.onFailure { problem -> vstep("typed: retry " + attempt + " refused " + problem.javaClass.simpleName) }` |
| 2543 | estep() | error |  | `estep("typed: ksud status sysfsModule=" + (if (moduleDir) "1" else "0") + " umh=" + (if (umh) "1" else "0") + " lkmOk=" + (if (lkmOk) "1" else "0") + ` |
| 2544 | wstep() | warn |  | `wstep("typed: ksud status markers=" + markers)` |
| 2545 | step() | info | 应降为 Debug（分支/参数） | `step("typed: ksud status rootlog=" + logTail)` |
| 2547 | estep() | error |  | `estep("typed: ksud status refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 2556 | estep() | error |  | `estep("typed: hook refused " + problem.javaClass.simpleName + ":" + (problem.message ?: ""))` |
| 2583 | dstep() | debug |  | `dstep("typed: hook restore=false refusal=" + absent)` |
| 2600 | vstep() | verbose |  | `vstep("typed: restore site before=" + (before?.joinToString(" ") { b -> "%02x".format(b) } ?: "null"))` |
| 2608 | vstep() | verbose |  | `vstep("typed: restore site after=" + (after?.joinToString(" ") { b -> "%02x".format(b) } ?: "null"))` |
| 2614 | dstep() | debug |  | `dstep("typed: hook restore=" + restored + " refusal=" + restoreSession.lastRefusal)` |
| 2619 | step() | info | 保留 Info（成功里程碑） | `step( "typed: hook restore=false refusal=" + problem.javaClass.simpleName + ":" + (problem.message ?: ""), )` |
| 2632 | dstep() | debug |  | `dstep("typed: patch1 wrote=" + patchWrote + " refusal=" + patcher.lastRefusal + " helperBytes=" + (helperForPatch?.size ?: -1))` |
| 2637 | step() | info | 保留 Info（成功里程碑） | `step("typed: patch1 accessDestroy=" + patchAccessDestroy)` |
| 2641 | step() | info | 保留 Info（成功里程碑） | `step("typed: patch1 teardown=" + patchTorn)` |
| 2645 | step() | info | 应降为 Debug（分支/参数） | `step("typed: stepwise 43284 sequence=" + stepSeq)` |
| 2649 | dstep() | debug |  | `dstep("typed: stepwise 43284 tearDown=" + stepTorn + " refusal=" + stepwise.lastRefusal)` |
| 2653 | step() | info | 保留 Info（成功里程碑） | `step("typed: stepwise 43284 destroy=" + stepDestroyed)` |
| 2657 | step() | info | 保留 Info（成功里程碑） | `step("typed: assembled 43284 close ok=" + closed)` |

## 2. 其它 Kotlin 日志点（log.*）

| 文件:行 | 级别 | 调用 |
|---|---|---|
| app/src/main/kotlin/com/ghostlock/app/AppProcess.kt:22 | warn | `log.warn("app: 日志文件不可写（只走 logcat + 内存环）")` |
| app/src/main/kotlin/com/ghostlock/app/AppProcess.kt:25 | info | `log.info(NativeLogBridge.attach(LogFile.fd) + " path=" + opened)` |
| app/src/main/kotlin/com/ghostlock/app/log/log.kt:36 | info | `*  - `log.info("…")` / `log.debug` / `log.verbose` / `log.warn` / `log.error`；` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:93 | info | `lines.forEach { line -> log.info(line) }` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:103 | info | `lines.forEach { line -> log.info(line) }` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:109 | info | `log.info("tempui: run the declared plans")` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:124 | info | `log.info(line)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:173 | info | `log.info("tempui: full 43284 attack start (streaming)")` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:179 | info | `log.info("tempui: full 43284 attack start")` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:182 | info | `log.info("tempui: full 43284 attack done lines=" + lines.size)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:189 | info | `log.info("tempui: run the device gate")` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:224 | error | `log.error(report)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:239 | error | `log.error(report)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:262 | error | `log.error(report)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:267 | info | `log.info(report)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/TempUiActivity.kt:275 | info | `log.info("tempui: 临时界面已启动 fd=" + LogFile.fd + " path=" + (LogFile.path ?: "-"))` |
| app/src/main/kotlin/com/ghostlock/app/tempui/PlanRunner.kt:157 | debug | `val engine = StepEngine(JniOpsInvoker(session)) { entry -> log.debug(entry) }` |
| app/src/main/kotlin/com/ghostlock/app/tempui/PlanRunner.kt:183 | info | `log.info(text)` |
| app/src/main/kotlin/com/ghostlock/app/tempui/DeviceGate.kt:32 | info | `log.info(line)` |

## 3. native 侧调用点（按宏统计）

| 文件 | verbose | debug | info | warn | error |
|---|---|---|---|---|---|
| core/src/backend/cve_2026_43284/hook/sentry_hook.cpp | 4 | 7 | 0 | 0 | 0 |
| core/src/backend/cve_2026_43284/landing/bridge_session.cpp | 4 | 0 | 0 | 3 | 2 |
| core/src/backend/cve_2026_43284/landing/crash_dump_bridge.cpp | 7 | 2 | 0 | 0 | 5 |
| core/src/backend/cve_2026_43284/landing/encap_peer.hpp | 0 | 0 | 0 | 0 | 1 |
| core/src/backend/cve_2026_43284/landing/pagecache_landing.hpp | 0 | 0 | 0 | 2 | 0 |
| core/src/backend/cve_2026_43284/landing/pagecache_write.hpp | 14 | 0 | 0 | 0 | 1 |
| core/src/backend/cve_2026_43284/landing/real_splice_io.cpp | 17 | 0 | 0 | 4 | 0 |
| core/src/backend/cve_2026_43284/steps/pagecache_step_runner.cpp | 0 | 4 | 0 | 0 | 0 |
| core/src/backend/cve_2026_43284/steps/pagecache_steps.cpp | 17 | 8 | 0 | 0 | 1 |
| core/src/backend/cve_2026_43499/landing/multicast_route.hpp | 0 | 4 | 0 | 0 | 0 |
| core/src/backend/cve_2026_43499/steps/window_attempt.cpp | 0 | 1 | 0 | 0 | 0 |
| core/src/device/43284/chain.cpp | 13 | 36 | 0 | 0 | 2 |
| core/src/device/43499/chain.cpp | 0 | 4 | 0 | 0 | 0 |
| core/src/support/gl_log.h | 1 | 1 | 2 | 1 | 1 |

## 4. shell 侧调用点（收尾脚本生成器）

| FinishScript.kt 行 | 内容 |
|---|---|
| 18 | `appendLine("log '[ghostlock] finish start'")` |
| 21 | `appendLine("log '[ghostlock] start root manager: " + spec.name + "'")` |
| 31 | `appendLine("if [ ! -x \"\$BIN\" ]; then log '[ghostlock] manager loader not found'; else")` |
| 33 | `appendLine("  \"\$BIN\" " + spec.args.joinToString(" ") + " --kmi \"\$KMI\" >>\"\$LOG\" 2>&1 || log '[ghostlock] manager late-load failed'")` |
| 37 | `appendLine("log '[ghostlock] load custom lkm'")` |
| 38 | `appendLine("insmod \"" + stagedLkm + "\" " + lkmParams + " && log '[ghostlock] lkm loaded' || log '[ghostlock] lkm insmod failed'")` |
| 51 | `appendLine("  log '[ghostlock] kernel access connected; wait for close'")` |
| 53 | `appendLine("  log '[ghostlock] kernel access closed'")` |
| 55 | `appendLine("  log '[ghostlock] kernel access timeout; continue'")` |
| 61 | `appendLine("log '[ghostlock] run command'")` |
| 65 | `appendLine("log '[ghostlock] run script: " + stagedScript + "'")` |
| 66 | `appendLine("sh \"" + stagedScript + "\" || log '[ghostlock] script failed'")` |
| 70 | `appendLine("log '[ghostlock] launch app: " + appPackage + "'")` |
| 74 | `appendLine("log '[ghostlock] restore selinux enforcing'")` |
| 79 | `appendLine("log '[ghostlock] finish done'")` |
