#!/usr/bin/env python3
"""43499 R1 literal token 级扫描器（P2）。

目标：对 `app/src/main/cpp/gl43499` 全部 C/C++ 源/头做**token 级**数值字面量清点，
逐 token 给出 `file:line:token` 与分类理由，输出闭表；`unclassified != 0` 时 exit 1。

口径（与 R1 冻结文档一致）：
- 先剥离 `//` 与 `/* */` 注释、字符串/字符字面量（含转义），注释/字符串内数字不算；
- 数值 token：十进制/十六进制/小数/指数，含单数字、负数（`-` 与数字合并）、后缀
  （`u/U/l/L/f/F`）；不把标识符/字段名里的数字拆出来；
- 允许清单为**逐 token** 表（`relpath:line:token` + 理由），内嵌于本脚本；
- 额外残留检查：`GLK43499_PHYS_` / `GLK43499_STRUCT_` / `GLK43499_WIRE_` / `reserved`
  （R2 目标为四项=0，决定 R2 gate 退出码）。
- R2 magic 逃逸检查：已参数化的目标阈值 token（1h/4MiB/64MiB/1e6/4096/0xff/
  FUTEX_WAITERS/nice 边界等）**不得**再出现；任何出现或落入 numeric-literal
  类别 ⇒ R2 gate FAIL。

用法：
  python3 tools/scan_native_literals.py --emit-allowlist   # 打印待分类 token 表
  python3 tools/scan_native_literals.py                    # 冻结检查（exit 1 if unclassified>0）
  python3 tools/scan_native_literals.py --root <dir>       # 指定扫描根
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from dataclasses import dataclass

DEFAULT_ROOT = "app/src/main/cpp/gl43499"

# 逐 token 允许清单：`relpath:line:token<TAB>理由`（由 --emit-allowlist 生成后冻结）。
# 允许清单 sha256（重生成后必须更新并签核；--regenerate 打印新值）
ALLOWLIST_SHA256 = "f1489bfdff21f6e04c4e03bf80a74e4b48fb60243017a20be946e71c1a60448b"

# 受控类别白名单（allowlist 理由只能取自这些受控类别）
CATEGORY_ALLOWLIST = {
    # 受控类别：只允许与目标参数无关的语义；A 决策后容量/协议值不得以“allow”形式永久保留
    # （它们在 R1 作为 target token 列出，R2 必须进请求字段）。
    "sentinel/loop/index",
    "bit-width/shift",
    "system-header-derived",
    "abi-alignment",
    "jni-marshalling",
    "numeric-literal",
}

# 禁止 target token：A 决策后这些数值必须来自请求字段（出现即 R2 gate FAIL）
TARGET_TOKEN_PATTERNS = [
    (re.compile(r"^0x(fff|ffffff)"), "identity/scan address-space target"),
    (re.compile(r"^0x80000000$"), "FUTEX_WAITERS target flag"),
]

# R2 magic 逃逸闭表：R2b 已参数化的目标/容量/时序阈值 token；出现即 FAIL。
# 允许的派生写法：UINT8_MAX/INT32_MAX/FD_SETSIZE/PRIO_MIN/PRIO_MAX/1<<31 等。
BANNED_MAGIC_TOKENS = {
    "999999999",       # lock_pi_spin_ns 旧上限 → 请求内自行约束
    "1000000",         # poll/settle 旧上限 → us→ns 与请求 timeout 交叉校验
    "3600000000000",   # 1h 超时上限 → deadline 溢出校验
    "0x1000",          # multicast frame 容量 → 请求 max_frame
    "4096",            # TCP/multicast budget、SO_SNDBUF 下限 → 请求字段/INT32_MAX
    "0xff",            # fill_byte 上界 → UINT8_MAX
    "255",             # 同上（十进制写法）
    "0x80000000",      # FUTEX_WAITERS → 请求 futex_waiter_flag（1<<31 域校验）
    "0x400000",        # 64MiB mapping 容量 → 请求 max_mapping
    "4194304",         # 4MiB socket buffer 容量 → 请求 socket_buffer_size
    "22",              # 旧 1<<22 位移上界
    "26",              # 旧 1<<26 位移上界
}

ALLOWLIST_TSV = r"""
core/abi.h:33:4	sentinel/loop/index
core/abi.h:36:0	sentinel/loop/index
core/abi.h:76:8	abi-alignment
core/actor.c:46:0	sentinel/loop/index
core/actor.c:47:1	sentinel/loop/index
core/actor.c:48:2	sentinel/loop/index
core/actor.c:49:3	sentinel/loop/index
core/actor.c:50:4	sentinel/loop/index
core/actor.c:51:5	sentinel/loop/index
core/actor.c:148:0	sentinel/loop/index
core/actor.c:148:-1	sentinel/loop/index
core/actor.c:150:0	sentinel/loop/index
core/actor.c:152:3	sentinel/loop/index
core/actor.c:154:0	sentinel/loop/index
core/actor.c:155:0	sentinel/loop/index
core/actor.c:173:0	sentinel/loop/index
core/actor.c:173:1	sentinel/loop/index
core/actor.c:175:0	sentinel/loop/index
core/actor.c:176:0	sentinel/loop/index
core/actor.c:179:0	sentinel/loop/index
core/actor.c:179:0	sentinel/loop/index
core/actor.c:180:0	sentinel/loop/index
core/actor.c:182:1	sentinel/loop/index
core/actor.c:194:0	sentinel/loop/index
core/actor.c:196:0	sentinel/loop/index
core/actor.c:197:0	sentinel/loop/index
core/actor.c:200:1	sentinel/loop/index
core/actor.c:210:0	sentinel/loop/index
core/actor.c:211:0	sentinel/loop/index
core/actor.c:212:1	sentinel/loop/index
core/actor.c:213:1	sentinel/loop/index
core/actor.c:215:0	sentinel/loop/index
core/actor.c:235:1	sentinel/loop/index
core/actor.c:237:0	sentinel/loop/index
core/actor.c:238:0	sentinel/loop/index
core/actor.c:240:0	sentinel/loop/index
core/actor.c:250:0	sentinel/loop/index
core/actor.c:252:0	sentinel/loop/index
core/actor.c:253:0	sentinel/loop/index
core/actor.c:255:0	sentinel/loop/index
core/actor.c:257:0	sentinel/loop/index
core/actor.c:261:1	sentinel/loop/index
core/actor.c:262:1	sentinel/loop/index
core/actor.c:267:1	sentinel/loop/index
core/actor.c:268:1	sentinel/loop/index
core/actor.c:271:0	sentinel/loop/index
core/actor.c:276:0	sentinel/loop/index
core/actor.c:277:0	sentinel/loop/index
core/actor.c:277:0	sentinel/loop/index
core/actor.c:279:0	sentinel/loop/index
core/actor.c:280:0	sentinel/loop/index
core/actor.c:281:0	sentinel/loop/index
core/actor.c:284:0	sentinel/loop/index
core/actor.c:285:1	sentinel/loop/index
core/actor.c:287:0	sentinel/loop/index
core/actor.c:290:0	sentinel/loop/index
core/actor.c:296:1	sentinel/loop/index
core/actor.c:316:0	sentinel/loop/index
core/actor.c:316:0	sentinel/loop/index
core/actor.c:317:1	sentinel/loop/index
core/actor.c:321:0	sentinel/loop/index
core/actor.c:322:0	sentinel/loop/index
core/actor.c:326:0	sentinel/loop/index
core/actor.c:328:0	sentinel/loop/index
core/actor.c:330:0	sentinel/loop/index
core/actor.c:330:0	sentinel/loop/index
core/actor.c:332:1	sentinel/loop/index
core/actor.c:333:1	sentinel/loop/index
core/actor.c:337:1	sentinel/loop/index
core/actor.c:339:0	sentinel/loop/index
core/actor.c:340:0	sentinel/loop/index
core/actor.c:341:1	sentinel/loop/index
core/actor.c:343:0	sentinel/loop/index
core/actor.c:346:0	sentinel/loop/index
core/actor.c:348:0	sentinel/loop/index
core/actor.c:348:0	sentinel/loop/index
core/actor.c:350:1	sentinel/loop/index
core/actor.c:351:1	sentinel/loop/index
core/actor.c:373:0	sentinel/loop/index
core/actor.c:374:0	sentinel/loop/index
core/actor.c:374:0	sentinel/loop/index
core/actor.c:375:1	sentinel/loop/index
core/actor.c:376:1	sentinel/loop/index
core/actor.c:377:1	sentinel/loop/index
core/actor.c:378:1	sentinel/loop/index
core/actor.c:379:1	sentinel/loop/index
core/actor.c:380:1	sentinel/loop/index
core/actor.c:381:-1	sentinel/loop/index
core/actor.c:383:1	sentinel/loop/index
core/actor.c:387:0	sentinel/loop/index
core/actor.c:388:0	sentinel/loop/index
core/actor.c:395:1	sentinel/loop/index
core/actor.c:396:0	sentinel/loop/index
core/actor.c:398:0	sentinel/loop/index
core/actor.c:399:0	sentinel/loop/index
core/actor.c:401:0	sentinel/loop/index
core/actor.c:403:1	sentinel/loop/index
core/actor.c:404:0	sentinel/loop/index
core/actor.c:413:1	sentinel/loop/index
core/actor.c:414:1	sentinel/loop/index
core/actor.c:415:0	sentinel/loop/index
core/actor.c:416:0	sentinel/loop/index
core/actor.c:419:0	sentinel/loop/index
core/actor.c:423:1	sentinel/loop/index
core/actor.c:436:1	sentinel/loop/index
core/actor.c:437:1	sentinel/loop/index
core/actor.c:441:0	sentinel/loop/index
core/actor.c:447:0	sentinel/loop/index
core/actor.c:450:0	sentinel/loop/index
core/actor.c:450:0	sentinel/loop/index
core/actor.c:451:0	sentinel/loop/index
core/actor.c:453:0	sentinel/loop/index
core/actor.c:454:0	sentinel/loop/index
core/actor.c:475:1	sentinel/loop/index
core/actor.c:476:1	sentinel/loop/index
core/actor.c:480:0	sentinel/loop/index
core/actor.c:493:1	sentinel/loop/index
core/actor.c:494:1	sentinel/loop/index
core/actor.c:498:0	sentinel/loop/index
core/actor.c:518:0	sentinel/loop/index
core/actor.c:524:0	sentinel/loop/index
core/actor.c:540:0	sentinel/loop/index
core/actor.c:546:0	sentinel/loop/index
core/actor.c:553:0	sentinel/loop/index
core/actor.c:562:0	sentinel/loop/index
core/actor.c:569:0	sentinel/loop/index
core/actor.c:597:0	sentinel/loop/index
core/actor.c:598:0	sentinel/loop/index
core/actor.c:598:1	sentinel/loop/index
core/actor.c:598:0	sentinel/loop/index
core/actor.c:621:1	sentinel/loop/index
core/actor.c:640:3	sentinel/loop/index
core/actor.c:640:0	sentinel/loop/index
core/actor.c:647:0	sentinel/loop/index
core/actor.c:650:3	sentinel/loop/index
core/actor.c:654:0	sentinel/loop/index
core/actor.c:654:3	sentinel/loop/index
core/actor.c:656:0	sentinel/loop/index
core/actor.c:659:0	sentinel/loop/index
core/actor.c:659:0	sentinel/loop/index
core/actor.c:661:0	sentinel/loop/index
core/actor.c:664:4	sentinel/loop/index
core/actor.c:669:0	sentinel/loop/index
core/actor.c:669:4	sentinel/loop/index
core/actor.c:670:0	sentinel/loop/index
core/actor.c:672:0	sentinel/loop/index
core/actor.c:676:0	sentinel/loop/index
core/actor.c:677:1000	sentinel/loop/index
core/actor.c:681:0	sentinel/loop/index
core/actor.c:683:0	sentinel/loop/index
core/actor.c:684:1000	sentinel/loop/index
core/actor.c:687:0	sentinel/loop/index
core/actor.c:689:0	sentinel/loop/index
core/actor.c:691:0	sentinel/loop/index
core/actor.c:694:0	sentinel/loop/index
core/actor.c:696:0	sentinel/loop/index
core/actor.c:699:0	sentinel/loop/index
core/actor.c:701:0	sentinel/loop/index
core/actor.c:703:1	sentinel/loop/index
core/actor.c:717:0	sentinel/loop/index
core/actor.c:719:0	sentinel/loop/index
core/actor.c:731:0	sentinel/loop/index
core/actor.c:737:0	sentinel/loop/index
core/actor.c:737:0	sentinel/loop/index
core/actor.c:739:1	sentinel/loop/index
core/actor.c:757:0	sentinel/loop/index
core/actor.c:757:0	sentinel/loop/index
core/actor.c:758:0	sentinel/loop/index
core/actor.c:758:0	sentinel/loop/index
core/actor.c:762:0	sentinel/loop/index
core/actor.c:767:0	sentinel/loop/index
core/actor.c:772:0	sentinel/loop/index
core/actor.c:774:0	sentinel/loop/index
core/actor.c:774:0	sentinel/loop/index
core/actor.c:775:1	sentinel/loop/index
core/actor.c:804:0	sentinel/loop/index
core/actor.c:811:0	sentinel/loop/index
core/actor.c:811:0	sentinel/loop/index
core/actor.c:817:0	sentinel/loop/index
core/actor.c:817:0	sentinel/loop/index
core/actor.c:823:0	sentinel/loop/index
core/actor.c:823:0	sentinel/loop/index
core/actor.c:829:1	sentinel/loop/index
core/actor.c:833:0	sentinel/loop/index
core/actor.c:836:0	sentinel/loop/index
core/actor.c:837:0	sentinel/loop/index
core/actor.c:838:0	sentinel/loop/index
core/actor.c:839:0	sentinel/loop/index
core/actor.c:840:0	sentinel/loop/index
core/actor.c:841:0	sentinel/loop/index
core/actor.c:842:0	sentinel/loop/index
core/actor.c:843:0	sentinel/loop/index
core/actor.c:844:0	sentinel/loop/index
core/actor.c:845:0	sentinel/loop/index
core/actor.c:846:0	sentinel/loop/index
core/actor.c:847:0	sentinel/loop/index
core/actor.c:848:0	sentinel/loop/index
core/actor.c:849:0	sentinel/loop/index
core/actor.c:850:0	sentinel/loop/index
core/actor.c:851:0	sentinel/loop/index
core/actor.c:852:0	sentinel/loop/index
core/actor.c:853:0	sentinel/loop/index
core/actor.c:854:0	sentinel/loop/index
core/actor.c:855:0	sentinel/loop/index
core/actor.c:856:0	sentinel/loop/index
core/actor.c:857:0	sentinel/loop/index
core/actor.c:858:0	sentinel/loop/index
core/actor.c:859:0	sentinel/loop/index
core/actor.c:885:0	sentinel/loop/index
core/actor.c:893:0	sentinel/loop/index
core/actor.c:898:0	sentinel/loop/index
core/actor.c:901:0	sentinel/loop/index
core/actor.c:902:0	sentinel/loop/index
core/actor.c:906:1	sentinel/loop/index
core/actor.c:909:1	sentinel/loop/index
core/actor.c:932:0	sentinel/loop/index
core/actor.c:932:0	sentinel/loop/index
core/actor.c:937:0	sentinel/loop/index
core/actor.c:939:0	sentinel/loop/index
core/actor.c:940:0	sentinel/loop/index
core/actor.c:942:1	sentinel/loop/index
core/actor.c:944:0	sentinel/loop/index
core/actor.c:945:0	sentinel/loop/index
core/actor.c:947:1	sentinel/loop/index
core/actor.c:949:0	sentinel/loop/index
core/actor.c:950:0	sentinel/loop/index
core/actor.c:952:1	sentinel/loop/index
core/actor.c:956:0	sentinel/loop/index
core/actor.c:963:0	sentinel/loop/index
core/actor.c:964:0	sentinel/loop/index
core/actor.c:965:0	sentinel/loop/index
core/actor.c:973:0	sentinel/loop/index
core/actor.c:974:0	sentinel/loop/index
core/actor.c:979:0	sentinel/loop/index
core/actor.c:987:0	sentinel/loop/index
core/actor.c:992:2	sentinel/loop/index
core/actor.c:994:1	sentinel/loop/index
core/actor.c:999:1	sentinel/loop/index
core/actor.c:1003:1	sentinel/loop/index
core/actor.c:1009:2	sentinel/loop/index
core/actor.c:1016:1	sentinel/loop/index
core/actor.c:1017:1	sentinel/loop/index
core/actor.c:1018:1	sentinel/loop/index
core/actor.c:1019:1	sentinel/loop/index
core/actor.c:1020:1	sentinel/loop/index
core/actor.c:1023:0	sentinel/loop/index
core/actor.c:1023:0	sentinel/loop/index
core/actor.c:1044:0	sentinel/loop/index
core/actor.c:1048:0	sentinel/loop/index
core/actor.c:1048:0	sentinel/loop/index
core/actor.c:1053:0	sentinel/loop/index
core/actor.c:1056:0	sentinel/loop/index
core/actor.c:1056:0	sentinel/loop/index
core/actor.c:1064:1	sentinel/loop/index
core/actor.c:1065:0	sentinel/loop/index
core/actor.c:1067:1	sentinel/loop/index
core/actor.c:1068:1	sentinel/loop/index
core/actor.c:1074:0	sentinel/loop/index
core/actor.c:1076:0	sentinel/loop/index
core/actor.c:1077:1	sentinel/loop/index
core/actor.c:1087:1	sentinel/loop/index
core/actor.c:1096:1	sentinel/loop/index
core/actor.c:1099:1	sentinel/loop/index
core/actor.c:1109:0	sentinel/loop/index
core/actor.c:1110:1	sentinel/loop/index
core/actor.c:1111:1	sentinel/loop/index
core/actor.c:1119:1	sentinel/loop/index
core/actor.c:1122:0	sentinel/loop/index
core/actor.c:1122:1	sentinel/loop/index
core/actor.c:1127:0	sentinel/loop/index
core/actor.c:1128:1	sentinel/loop/index
core/actor.c:1129:1	sentinel/loop/index
core/actor.c:1149:0	sentinel/loop/index
core/actor.c:1161:1	sentinel/loop/index
core/actor.c:1162:1	sentinel/loop/index
core/actor.c:1168:0	sentinel/loop/index
core/actor.c:1171:0	sentinel/loop/index
core/actor.c:1171:1	sentinel/loop/index
core/actor.c:1174:0	sentinel/loop/index
core/actor.c:1178:0	sentinel/loop/index
core/actor.c:1182:0	sentinel/loop/index
core/actor.c:1182:0	sentinel/loop/index
core/actor.c:1182:0	sentinel/loop/index
core/actor.c:1186:0	sentinel/loop/index
core/actor.c:1189:0	sentinel/loop/index
core/actor.c:1194:0	sentinel/loop/index
core/actor.c:1194:0	sentinel/loop/index
core/actor.c:1215:0	sentinel/loop/index
core/actor.c:1215:0	sentinel/loop/index
core/actor.c:1215:0	sentinel/loop/index
core/actor.c:1215:0	sentinel/loop/index
core/actor.c:1215:0	sentinel/loop/index
core/actor.c:1230:0	sentinel/loop/index
core/actor.c:1230:1	sentinel/loop/index
core/actor.c:1231:0	sentinel/loop/index
core/actor.c:1231:0	sentinel/loop/index
core/actor.c:1231:0	sentinel/loop/index
core/actor.c:1256:0	sentinel/loop/index
core/actor.c:1256:0	sentinel/loop/index
core/actor.c:1256:0	sentinel/loop/index
core/actor.c:1256:0	sentinel/loop/index
core/actor.c:1256:0	sentinel/loop/index
core/actor.c:1263:0	sentinel/loop/index
core/actor.c:1267:0	sentinel/loop/index
core/actor.c:1267:1	sentinel/loop/index
core/actor.c:1267:0	sentinel/loop/index
core/actor.c:1267:0	sentinel/loop/index
core/actor.c:1268:1	sentinel/loop/index
core/actor.c:1274:0	sentinel/loop/index
core/actor.c:1275:1	sentinel/loop/index
core/actor.c:1278:0	sentinel/loop/index
core/actor.c:1278:1	sentinel/loop/index
core/actor.c:1278:0	sentinel/loop/index
core/actor.c:1278:0	sentinel/loop/index
core/actor.c:1279:1	sentinel/loop/index
core/actor.c:1282:0	sentinel/loop/index
core/actor.c:1283:1	sentinel/loop/index
core/actor.c:1286:0	sentinel/loop/index
core/actor.c:1286:1	sentinel/loop/index
core/actor.c:1286:0	sentinel/loop/index
core/actor.c:1286:0	sentinel/loop/index
core/actor.c:1287:1	sentinel/loop/index
core/actor.c:1292:1	sentinel/loop/index
core/actor.c:1296:0	sentinel/loop/index
core/actor.c:1302:0	sentinel/loop/index
core/actor.c:1308:0	sentinel/loop/index
core/actor.c:1311:1	sentinel/loop/index
core/actor.c:1314:0	sentinel/loop/index
core/actor.c:1314:1	sentinel/loop/index
core/actor.c:1314:0	sentinel/loop/index
core/actor.c:1314:0	sentinel/loop/index
core/actor.c:1315:1	sentinel/loop/index
core/actor.c:1319:0	sentinel/loop/index
core/actor.c:1320:1	sentinel/loop/index
core/actor.c:1323:0	sentinel/loop/index
core/actor.c:1323:1	sentinel/loop/index
core/actor.c:1323:1	sentinel/loop/index
core/actor.c:1323:0	sentinel/loop/index
core/actor.c:1324:1	sentinel/loop/index
core/actor.c:1330:0	sentinel/loop/index
core/actor.c:1331:1	sentinel/loop/index
core/actor.c:1334:0	sentinel/loop/index
core/actor.c:1334:1	sentinel/loop/index
core/actor.c:1334:1	sentinel/loop/index
core/actor.c:1334:0	sentinel/loop/index
core/actor.c:1335:1	sentinel/loop/index
core/actor.c:1342:0	sentinel/loop/index
core/actor.c:1342:1	sentinel/loop/index
core/actor.c:1342:1	sentinel/loop/index
core/actor.c:1342:1	sentinel/loop/index
core/actor.c:1342:0	sentinel/loop/index
core/actor.h:146:128	abi-alignment
core/actor.h:148:40	abi-alignment
core/actor.h:150:88	abi-alignment
core/actor.h:152:72	abi-alignment
core/actor.h:154:120	abi-alignment
core/carrier_internal.h:99:3	sentinel/loop/index
core/carrier_internal.h:191:0	sentinel/loop/index
core/carrier_internal.h:192:1	sentinel/loop/index
core/carrier_internal.h:193:2	sentinel/loop/index
core/carrier_internal.h:194:3	sentinel/loop/index
core/carrier_internal.h:195:4	sentinel/loop/index
core/carrier_internal.h:196:5	sentinel/loop/index
core/carrier_internal.h:197:6	sentinel/loop/index
core/carrier_multicast.c:33:0	sentinel/loop/index
core/carrier_multicast.c:34:0	sentinel/loop/index
core/carrier_multicast.c:35:0	sentinel/loop/index
core/carrier_multicast.c:43:0	sentinel/loop/index
core/carrier_multicast.c:44:-1	sentinel/loop/index
core/carrier_multicast.c:48:0	sentinel/loop/index
core/carrier_multicast.c:48:0	sentinel/loop/index
core/carrier_multicast.c:52:0	sentinel/loop/index
core/carrier_multicast.c:55:0	sentinel/loop/index
core/carrier_multicast.c:58:0	sentinel/loop/index
core/carrier_multicast.c:63:0	sentinel/loop/index
core/carrier_multicast.c:68:0	sentinel/loop/index
core/carrier_multicast.c:68:0	sentinel/loop/index
core/carrier_multicast.c:74:0	sentinel/loop/index
core/carrier_multicast.c:77:1	sentinel/loop/index
core/carrier_multicast.c:78:0	sentinel/loop/index
core/carrier_multicast.c:78:0	sentinel/loop/index
core/carrier_multicast.c:95:0	sentinel/loop/index
core/carrier_multicast.c:96:0	sentinel/loop/index
core/carrier_multicast.c:100:0	sentinel/loop/index
core/carrier_multicast.c:103:1	sentinel/loop/index
core/carrier_multicast.c:104:0	sentinel/loop/index
core/carrier_multicast.c:120:0	sentinel/loop/index
core/carrier_multicast.c:121:0	sentinel/loop/index
core/carrier_multicast.c:122:0	sentinel/loop/index
core/carrier_multicast.c:123:0	sentinel/loop/index
core/carrier_multicast.c:124:0	sentinel/loop/index
core/carrier_multicast.c:128:0	sentinel/loop/index
core/carrier_multicast.c:129:-1	sentinel/loop/index
core/carrier_multicast.c:130:0	sentinel/loop/index
core/carrier_multicast.c:136:1	sentinel/loop/index
core/carrier_multicast.c:140:0	sentinel/loop/index
core/carrier_multicast.c:148:0	sentinel/loop/index
core/carrier_multicast.c:152:0	sentinel/loop/index
core/carrier_multicast.c:154:0	sentinel/loop/index
core/carrier_multicast.c:158:0	sentinel/loop/index
core/carrier_multicast.c:159:0	sentinel/loop/index
core/carrier_multicast.c:162:0	sentinel/loop/index
core/carrier_multicast.c:163:1	sentinel/loop/index
core/carrier_multicast.c:168:0	sentinel/loop/index
core/carrier_multicast.c:169:0	sentinel/loop/index
core/carrier_multicast.c:170:1	sentinel/loop/index
core/carrier_multicast.c:176:0	sentinel/loop/index
core/carrier_multicast.c:176:0	sentinel/loop/index
core/carrier_multicast.c:188:0	sentinel/loop/index
core/carrier_multicast.c:192:0	sentinel/loop/index
core/carrier_multicast.c:193:0	sentinel/loop/index
core/carrier_multicast.c:193:1	sentinel/loop/index
core/carrier_multicast.c:196:0	sentinel/loop/index
core/carrier_multicast.c:196:1	sentinel/loop/index
core/carrier_multicast.c:196:0	sentinel/loop/index
core/carrier_multicast.c:201:1	sentinel/loop/index
core/carrier_multicast.c:202:0	sentinel/loop/index
core/carrier_multicast.c:203:0	sentinel/loop/index
core/carrier_multicast.c:203:0	sentinel/loop/index
core/carrier_multicast.c:204:-1	sentinel/loop/index
core/carrier_multicast.c:208:0	sentinel/loop/index
core/carrier_multicast.c:209:0	sentinel/loop/index
core/carrier_multicast.c:210:0	sentinel/loop/index
core/carrier_multicast.c:211:0	sentinel/loop/index
core/carrier_multicast.c:212:0	sentinel/loop/index
core/carrier_multicast.c:213:0	sentinel/loop/index
core/carrier_pselect.c:40:8	abi-alignment
core/carrier_pselect.c:48:63	bit-width/shift
core/carrier_pselect.c:48:64	bit-width/shift
core/carrier_pselect.c:68:2	sentinel/loop/index
core/carrier_pselect.c:69:2	sentinel/loop/index
core/carrier_pselect.c:74:0	sentinel/loop/index
core/carrier_pselect.c:95:2	sentinel/loop/index
core/carrier_pselect.c:97:1	sentinel/loop/index
core/carrier_pselect.c:118:0	sentinel/loop/index
core/carrier_pselect.c:119:0	sentinel/loop/index
core/carrier_pselect.c:120:-1	sentinel/loop/index
core/carrier_pselect.c:122:0	sentinel/loop/index
core/carrier_pselect.c:125:0	sentinel/loop/index
core/carrier_pselect.c:126:0	sentinel/loop/index
core/carrier_pselect.c:127:-1	sentinel/loop/index
core/carrier_pselect.c:129:0	sentinel/loop/index
core/carrier_pselect.c:132:0	sentinel/loop/index
core/carrier_pselect.c:133:0	sentinel/loop/index
core/carrier_pselect.c:134:-1	sentinel/loop/index
core/carrier_pselect.c:136:0	sentinel/loop/index
core/carrier_pselect.c:150:2	sentinel/loop/index
core/carrier_pselect.c:151:0	sentinel/loop/index
core/carrier_pselect.c:153:0	sentinel/loop/index
core/carrier_pselect.c:159:0	sentinel/loop/index
core/carrier_pselect.c:160:1	sentinel/loop/index
core/carrier_pselect.c:160:-1	sentinel/loop/index
core/carrier_pselect.c:164:0	sentinel/loop/index
core/carrier_pselect.c:164:1	sentinel/loop/index
core/carrier_pselect.c:164:0	sentinel/loop/index
core/carrier_pselect.c:164:1	sentinel/loop/index
core/carrier_pselect.c:165:0	sentinel/loop/index
core/carrier_pselect.c:165:1	sentinel/loop/index
core/carrier_pselect.c:166:1	sentinel/loop/index
core/carrier_pselect.c:166:-1	sentinel/loop/index
core/carrier_pselect.c:184:2	sentinel/loop/index
core/carrier_pselect.c:186:1	sentinel/loop/index
core/carrier_pselect.c:187:0	sentinel/loop/index
core/carrier_pselect.c:188:0	sentinel/loop/index
core/carrier_pselect.c:189:1	sentinel/loop/index
core/carrier_pselect.c:190:0	sentinel/loop/index
core/carrier_pselect.c:191:0	sentinel/loop/index
core/carrier_pselect.c:192:0	sentinel/loop/index
core/carrier_pselect.c:192:0	sentinel/loop/index
core/carrier_pselect.c:193:0	sentinel/loop/index
core/carrier_pselect.c:193:0	sentinel/loop/index
core/carrier_pselect.c:196:0	sentinel/loop/index
core/carrier_pselect.c:196:0	sentinel/loop/index
core/carrier_pselect.c:198:0	sentinel/loop/index
core/carrier_pselect.c:198:-1	sentinel/loop/index
core/carrier_pselect.c:199:1	sentinel/loop/index
core/carrier_pselect.c:199:-1	sentinel/loop/index
core/carrier_pselect.c:212:2	sentinel/loop/index
core/carrier_pselect.c:214:0	sentinel/loop/index
core/carrier_pselect.c:215:0	sentinel/loop/index
core/carrier_pselect.c:221:0	sentinel/loop/index
core/carrier_pselect.c:224:1	sentinel/loop/index
core/carrier_pselect.c:224:-1	sentinel/loop/index
core/carrier_pselect.c:228:0	sentinel/loop/index
core/carrier_pselect.c:229:1	sentinel/loop/index
core/carrier_pselect.c:229:0	sentinel/loop/index
core/carrier_pselect.c:230:1	sentinel/loop/index
core/carrier_pselect.c:231:1	sentinel/loop/index
core/carrier_pselect.c:231:-1	sentinel/loop/index
core/carrier_pselect.c:234:0	sentinel/loop/index
core/carrier_pselect.c:236:1	sentinel/loop/index
core/carrier_pselect.c:238:1	sentinel/loop/index
core/carrier_pselect.c:249:2	sentinel/loop/index
core/carrier_pselect.c:256:0	sentinel/loop/index
core/carrier_pselect.c:256:0	sentinel/loop/index
core/carrier_pselect.c:257:1	sentinel/loop/index
core/carrier_pselect.c:258:0	sentinel/loop/index
core/carrier_pselect.c:259:0	sentinel/loop/index
core/carrier_pselect.c:260:0	sentinel/loop/index
core/carrier_pselect.c:261:0	sentinel/loop/index
core/carrier_pselect.c:262:0	sentinel/loop/index
core/carrier_pselect.c:267:0	sentinel/loop/index
core/carrier_pselect.c:279:1	sentinel/loop/index
core/carrier_pselect.c:280:0	sentinel/loop/index
core/carrier_pselect.c:280:0	sentinel/loop/index
core/carrier_pselect.c:283:0	sentinel/loop/index
core/carrier_pselect.c:285:1	sentinel/loop/index
core/carrier_pselect.c:286:0	sentinel/loop/index
core/carrier_pselect.c:286:0	sentinel/loop/index
core/carrier_pselect.c:287:0	sentinel/loop/index
core/carrier_pselect.c:288:0	sentinel/loop/index
core/carrier_pselect.c:288:0	sentinel/loop/index
core/carrier_pselect.c:290:0	sentinel/loop/index
core/carrier_pselect.c:290:0	sentinel/loop/index
core/carrier_pselect.c:293:0	sentinel/loop/index
core/carrier_pselect.c:294:0	sentinel/loop/index
core/carrier_pselect.c:295:0	sentinel/loop/index
core/carrier_pselect.c:296:0	sentinel/loop/index
core/carrier_pselect.c:297:0	sentinel/loop/index
core/carrier_pselect.c:298:0	sentinel/loop/index
core/carrier_pselect.c:299:0	sentinel/loop/index
core/carrier_pselect.c:300:0	sentinel/loop/index
core/carrier_pselect.c:301:1	sentinel/loop/index
core/carrier_pselect.c:315:0	sentinel/loop/index
core/carrier_pselect.c:316:-1	sentinel/loop/index
core/carrier_pselect.c:317:-1	sentinel/loop/index
core/carrier_pselect.c:318:-1	sentinel/loop/index
core/carrier_pselect.c:319:-1	sentinel/loop/index
core/carrier_pselect.c:320:0	sentinel/loop/index
core/carrier_pselect.c:320:-1	sentinel/loop/index
core/carrier_pselect.c:321:1	sentinel/loop/index
core/carrier_pselect.c:321:-1	sentinel/loop/index
core/carrier_pselect.c:322:2	sentinel/loop/index
core/carrier_pselect.c:322:-1	sentinel/loop/index
core/carrier_pselect.c:324:0	sentinel/loop/index
core/carrier_pselect.c:325:0	sentinel/loop/index
core/carrier_pselect.c:326:0	sentinel/loop/index
core/carrier_pselect.c:331:2	sentinel/loop/index
core/carrier_pselect.c:333:2	sentinel/loop/index
core/carrier_pselect.c:338:0	sentinel/loop/index
core/carrier_pselect.c:341:0	sentinel/loop/index
core/carrier_pselect.c:348:1	sentinel/loop/index
core/carrier_pselect.c:348:0	sentinel/loop/index
core/carrier_pselect.c:352:0	sentinel/loop/index
core/carrier_pselect.c:363:0	sentinel/loop/index
core/carrier_pselect.c:364:0	sentinel/loop/index
core/carrier_pselect.c:365:0	sentinel/loop/index
core/carrier_pselect.c:368:3	sentinel/loop/index
core/carrier_pselect.c:370:8	abi-alignment
core/carrier_pselect.c:377:1	sentinel/loop/index
core/carrier_pselect.c:390:2	sentinel/loop/index
core/carrier_pselect.c:390:-1	sentinel/loop/index
core/carrier_pselect.c:390:-1	sentinel/loop/index
core/carrier_pselect.c:391:0	sentinel/loop/index
core/carrier_pselect.c:395:0	sentinel/loop/index
core/carrier_pselect.c:398:0	sentinel/loop/index
core/carrier_pselect.c:399:1	sentinel/loop/index
core/carrier_pselect.c:404:0	sentinel/loop/index
core/carrier_pselect.c:405:-1	sentinel/loop/index
core/carrier_pselect.c:406:1	sentinel/loop/index
core/carrier_pselect.c:413:0	sentinel/loop/index
core/carrier_pselect.c:414:1	sentinel/loop/index
core/carrier_pselect.c:416:0	sentinel/loop/index
core/carrier_pselect.c:418:0	sentinel/loop/index
core/carrier_pselect.c:419:-1	sentinel/loop/index
core/carrier_pselect.c:420:-1	sentinel/loop/index
core/carrier_pselect.c:421:-1	sentinel/loop/index
core/carrier_pselect.c:422:-1	sentinel/loop/index
core/carrier_pselect.c:425:1	sentinel/loop/index
core/carrier_pselect.c:428:0	sentinel/loop/index
core/carrier_pselect.c:431:0	sentinel/loop/index
core/carrier_pselect.c:433:0	sentinel/loop/index
core/carrier_pselect.c:434:-1	sentinel/loop/index
core/carrier_pselect.c:435:-1	sentinel/loop/index
core/carrier_pselect.c:436:-1	sentinel/loop/index
core/carrier_pselect.c:437:-1	sentinel/loop/index
core/carrier_pselect.c:439:1	sentinel/loop/index
core/carrier_pselect.c:443:1	sentinel/loop/index
core/carrier_pselect.c:444:1	sentinel/loop/index
core/carrier_pselect.c:445:0	sentinel/loop/index
core/carrier_pselect.c:464:0	sentinel/loop/index
core/carrier_pselect.c:465:0	sentinel/loop/index
core/carrier_pselect.c:466:0	sentinel/loop/index
core/carrier_pselect.c:467:0	sentinel/loop/index
core/carrier_pselect.c:468:0	sentinel/loop/index
core/carrier_pselect.c:471:1000	sentinel/loop/index
core/carrier_pselect.c:481:0	sentinel/loop/index
core/carrier_pselect.c:483:0	sentinel/loop/index
core/carrier_pselect.c:485:1	sentinel/loop/index
core/carrier_pselect.c:489:1	sentinel/loop/index
core/carrier_pselect.c:489:0	sentinel/loop/index
core/carrier_pselect.c:503:0	sentinel/loop/index
core/carrier_pselect.c:508:0	sentinel/loop/index
core/carrier_pselect.c:509:0	sentinel/loop/index
core/carrier_pselect.c:510:0	sentinel/loop/index
core/carrier_pselect.c:516:1000	sentinel/loop/index
core/carrier_pselect.c:528:0	sentinel/loop/index
core/carrier_pselect.c:528:0	sentinel/loop/index
core/carrier_pselect.c:528:0	sentinel/loop/index
core/carrier_pselect.c:531:0	sentinel/loop/index
core/carrier_pselect.c:532:0	sentinel/loop/index
core/carrier_pselect.c:535:0	sentinel/loop/index
core/carrier_pselect.c:535:0	sentinel/loop/index
core/carrier_pselect.c:536:1	sentinel/loop/index
core/carrier_pselect.c:544:0	sentinel/loop/index
core/carrier_pselect.c:544:0	sentinel/loop/index
core/carrier_pselect.c:544:0	sentinel/loop/index
core/carrier_pselect.c:554:0	sentinel/loop/index
core/carrier_pselect.c:555:0	sentinel/loop/index
core/carrier_pselect.c:556:0	sentinel/loop/index
core/carrier_pselect.c:556:1	sentinel/loop/index
core/carrier_pselect.c:559:0	sentinel/loop/index
core/carrier_pselect.c:562:1	sentinel/loop/index
core/carrier_pselect.c:563:0	sentinel/loop/index
core/carrier_pselect.c:565:1	sentinel/loop/index
core/carrier_pselect.c:571:1	sentinel/loop/index
core/carrier_tcp.c:37:2	sentinel/loop/index
core/carrier_tcp.c:48:0	sentinel/loop/index
core/carrier_tcp.c:49:0	sentinel/loop/index
core/carrier_tcp.c:52:0	sentinel/loop/index
core/carrier_tcp.c:53:0	sentinel/loop/index
core/carrier_tcp.c:54:0	sentinel/loop/index
core/carrier_tcp.c:54:0	sentinel/loop/index
core/carrier_tcp.c:54:0	sentinel/loop/index
core/carrier_tcp.c:55:0	sentinel/loop/index
core/carrier_tcp.c:58:1	sentinel/loop/index
core/carrier_tcp.c:59:0	sentinel/loop/index
core/carrier_tcp.c:63:0	sentinel/loop/index
core/carrier_tcp.c:65:0	sentinel/loop/index
core/carrier_tcp.c:67:0	sentinel/loop/index
core/carrier_tcp.c:68:0	sentinel/loop/index
core/carrier_tcp.c:70:1	sentinel/loop/index
core/carrier_tcp.c:82:0	sentinel/loop/index
core/carrier_tcp.c:83:0	sentinel/loop/index
core/carrier_tcp.c:84:0	sentinel/loop/index
core/carrier_tcp.c:85:1	sentinel/loop/index
core/carrier_tcp.c:88:0	sentinel/loop/index
core/carrier_tcp.c:91:0	sentinel/loop/index
core/carrier_tcp.c:92:0	sentinel/loop/index
core/carrier_tcp.c:93:1	sentinel/loop/index
core/carrier_tcp.c:93:0	sentinel/loop/index
core/carrier_tcp.c:95:0	sentinel/loop/index
core/carrier_tcp.c:100:0	sentinel/loop/index
core/carrier_tcp.c:102:0	sentinel/loop/index
core/carrier_tcp.c:105:0	sentinel/loop/index
core/carrier_tcp.c:106:0	sentinel/loop/index
core/carrier_tcp.c:108:0	sentinel/loop/index
core/carrier_tcp.c:112:0	sentinel/loop/index
core/carrier_tcp.c:114:0	sentinel/loop/index
core/carrier_tcp.c:115:-1	sentinel/loop/index
core/carrier_tcp.c:119:0	sentinel/loop/index
core/carrier_tcp.c:123:0	sentinel/loop/index
core/carrier_tcp.c:125:0	sentinel/loop/index
core/carrier_tcp.c:126:-1	sentinel/loop/index
core/carrier_tcp.c:130:0	sentinel/loop/index
core/carrier_tcp.c:133:0	sentinel/loop/index
core/carrier_tcp.c:144:1	sentinel/loop/index
core/carrier_tcp.c:145:0	sentinel/loop/index
core/carrier_tcp.c:146:1	sentinel/loop/index
core/carrier_tcp.c:147:0	sentinel/loop/index
core/carrier_tcp.c:148:0	sentinel/loop/index
core/carrier_tcp.c:150:0	sentinel/loop/index
core/carrier_tcp.c:153:0	sentinel/loop/index
core/carrier_tcp.c:154:0	sentinel/loop/index
core/carrier_tcp.c:157:0	sentinel/loop/index
core/carrier_tcp.c:160:0	sentinel/loop/index
core/carrier_tcp.c:161:0	sentinel/loop/index
core/carrier_tcp.c:162:0	sentinel/loop/index
core/carrier_tcp.c:163:-1	sentinel/loop/index
core/carrier_tcp.c:165:0	sentinel/loop/index
core/carrier_tcp.c:168:0	sentinel/loop/index
core/carrier_tcp.c:168:0	sentinel/loop/index
core/carrier_tcp.c:169:0	sentinel/loop/index
core/carrier_tcp.c:170:-1	sentinel/loop/index
core/carrier_tcp.c:172:0	sentinel/loop/index
core/carrier_tcp.c:175:0	sentinel/loop/index
core/carrier_tcp.c:175:0	sentinel/loop/index
core/carrier_tcp.c:176:0	sentinel/loop/index
core/carrier_tcp.c:177:-1	sentinel/loop/index
core/carrier_tcp.c:179:0	sentinel/loop/index
core/carrier_tcp.c:183:0	sentinel/loop/index
core/carrier_tcp.c:205:4	sentinel/loop/index
core/carrier_tcp.c:211:4	sentinel/loop/index
core/carrier_tcp.c:217:4	sentinel/loop/index
core/carrier_tcp.c:217:2	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:217:0	sentinel/loop/index
core/carrier_tcp.c:218:0	sentinel/loop/index
core/carrier_tcp.c:218:4	sentinel/loop/index
core/carrier_tcp.c:219:0	sentinel/loop/index
core/carrier_tcp.c:219:0	sentinel/loop/index
core/carrier_tcp.c:220:0	sentinel/loop/index
core/carrier_tcp.c:223:0	sentinel/loop/index
core/carrier_tcp.c:225:0	sentinel/loop/index
core/carrier_tcp.c:226:1	sentinel/loop/index
core/carrier_tcp.c:227:0	sentinel/loop/index
core/carrier_tcp.c:228:0	sentinel/loop/index
core/carrier_tcp.c:231:0	sentinel/loop/index
core/carrier_tcp.c:232:1	sentinel/loop/index
core/carrier_tcp.c:234:1	sentinel/loop/index
core/carrier_tcp.c:249:0	sentinel/loop/index
core/carrier_tcp.c:251:0	sentinel/loop/index
core/carrier_tcp.c:254:1	sentinel/loop/index
core/carrier_tcp.c:261:0	sentinel/loop/index
core/carrier_tcp.c:262:-1	sentinel/loop/index
core/carrier_tcp.c:263:-1	sentinel/loop/index
core/carrier_tcp.c:264:-1	sentinel/loop/index
core/carrier_tcp.c:265:0	sentinel/loop/index
core/carrier_tcp.c:266:0	sentinel/loop/index
core/carrier_tcp.c:267:0	sentinel/loop/index
core/carrier_tcp.c:268:0	sentinel/loop/index
core/carrier_tcp.c:269:0	sentinel/loop/index
core/carrier_tcp.c:271:0	sentinel/loop/index
core/carrier_tcp.c:272:0	sentinel/loop/index
core/carrier_tcp.c:273:0	sentinel/loop/index
core/carrier_tcp.c:276:0	sentinel/loop/index
core/carrier_tcp.c:279:0	sentinel/loop/index
core/carrier_tcp.c:284:0	sentinel/loop/index
core/carrier_tcp.c:289:0	sentinel/loop/index
core/carrier_tcp.c:290:0	sentinel/loop/index
core/carrier_tcp.c:294:0	sentinel/loop/index
core/carrier_tcp.c:302:0	sentinel/loop/index
core/carrier_tcp.c:310:0	sentinel/loop/index
core/carrier_tcp.c:310:0	sentinel/loop/index
core/carrier_tcp.c:314:0	sentinel/loop/index
core/carrier_tcp.c:316:2	sentinel/loop/index
core/carrier_tcp.c:329:0	sentinel/loop/index
core/carrier_tcp.c:329:0	sentinel/loop/index
core/carrier_tcp.c:330:0	sentinel/loop/index
core/carrier_tcp.c:330:0	sentinel/loop/index
core/carrier_tcp.c:377:0	sentinel/loop/index
core/carrier_tcp.c:382:0	sentinel/loop/index
core/carrier_tcp.c:389:0	sentinel/loop/index
core/carrier_tcp.c:394:0	sentinel/loop/index
core/carrier_tcp.c:394:0	sentinel/loop/index
core/carrier_tcp.c:394:0	sentinel/loop/index
core/carrier_tcp.c:400:0	sentinel/loop/index
core/carrier_tcp.c:410:0	sentinel/loop/index
core/carrier_tcp.c:417:0	sentinel/loop/index
core/carrier_tcp.c:418:0	sentinel/loop/index
core/carrier_tcp.c:422:1	sentinel/loop/index
core/carrier_tcp.c:423:1	sentinel/loop/index
core/carrier_tcp.c:424:0	sentinel/loop/index
core/carrier_tcp.c:442:0	sentinel/loop/index
core/carrier_tcp.c:443:0	sentinel/loop/index
core/carrier_tcp.c:444:0	sentinel/loop/index
core/carrier_tcp.c:445:0	sentinel/loop/index
core/carrier_tcp.c:446:0	sentinel/loop/index
core/carrier_tcp.c:447:1	sentinel/loop/index
core/carrier_tcp.c:454:0	sentinel/loop/index
core/carrier_tcp.c:455:0	sentinel/loop/index
core/carrier_tcp.c:457:1	sentinel/loop/index
core/carrier_tcp.c:457:0	sentinel/loop/index
core/carrier_tcp.c:464:0	sentinel/loop/index
core/carrier_tcp.c:471:0	sentinel/loop/index
core/carrier_tcp.c:473:0	sentinel/loop/index
core/carrier_tcp.c:477:0	sentinel/loop/index
core/carrier_tcp.c:479:0	sentinel/loop/index
core/carrier_tcp.c:480:0	sentinel/loop/index
core/carrier_tcp.c:484:0	sentinel/loop/index
core/carrier_tcp.c:492:0	sentinel/loop/index
core/carrier_tcp.c:517:0	sentinel/loop/index
core/carrier_tcp.c:521:0	sentinel/loop/index
core/carrier_tcp.c:522:0	sentinel/loop/index
core/carrier_tcp.c:527:0	sentinel/loop/index
core/carrier_tcp.c:530:0	sentinel/loop/index
core/carrier_tcp.c:532:0	sentinel/loop/index
core/carrier_tcp.c:534:0	sentinel/loop/index
core/carrier_tcp.c:542:1	sentinel/loop/index
core/carrier_tcp.c:550:0	sentinel/loop/index
core/carrier_tcp.c:550:0	sentinel/loop/index
core/carrier_tcp.c:551:0	sentinel/loop/index
core/carrier_tcp.c:563:0	sentinel/loop/index
core/carrier_tcp.c:564:0	sentinel/loop/index
core/carrier_tcp.c:565:1	sentinel/loop/index
core/carrier_tcp.c:566:0	sentinel/loop/index
core/carrier_tcp.c:567:0	sentinel/loop/index
core/carrier_tcp.c:567:1	sentinel/loop/index
core/carrier_tcp.c:570:0	sentinel/loop/index
core/carrier_tcp.c:570:1	sentinel/loop/index
core/carrier_tcp.c:570:0	sentinel/loop/index
core/carrier_tcp.c:576:0	sentinel/loop/index
core/carrier_tcp.c:577:1	sentinel/loop/index
core/carrier_tcp.c:580:0	sentinel/loop/index
core/carrier_tcp.c:583:0	sentinel/loop/index
core/carrier_tcp.c:587:0	sentinel/loop/index
core/carrier_tcp.c:587:0	sentinel/loop/index
core/carrier_tcp.c:588:0	sentinel/loop/index
core/carrier_tcp.c:591:0	sentinel/loop/index
core/carrier_tcp.c:593:0	sentinel/loop/index
core/carrier_tcp.c:597:1	sentinel/loop/index
core/carrier_tcp.c:598:0	sentinel/loop/index
core/carrier_tcp.c:599:0	sentinel/loop/index
core/carrier_tcp.c:600:-1	sentinel/loop/index
core/carrier_tcp.c:602:0	sentinel/loop/index
core/carrier_tcp.c:605:0	sentinel/loop/index
core/carrier_tcp.c:605:0	sentinel/loop/index
core/carrier_tcp.c:606:0	sentinel/loop/index
core/carrier_tcp.c:607:-1	sentinel/loop/index
core/carrier_tcp.c:609:0	sentinel/loop/index
core/carrier_tcp.c:612:0	sentinel/loop/index
core/carrier_tcp.c:612:0	sentinel/loop/index
core/carrier_tcp.c:613:0	sentinel/loop/index
core/carrier_tcp.c:614:-1	sentinel/loop/index
core/carrier_tcp.c:616:0	sentinel/loop/index
core/carrier_tcp.c:619:0	sentinel/loop/index
core/carrier_tcp.c:621:0	sentinel/loop/index
core/carrier_tcp.c:629:0	sentinel/loop/index
core/carrier_tcp.c:630:0	sentinel/loop/index
core/carrier_tcp.c:631:0	sentinel/loop/index
core/carrier_tcp.c:632:0	sentinel/loop/index
core/carrier_tcp.c:633:0	sentinel/loop/index
core/carrier_tcp.c:634:0	sentinel/loop/index
core/carrier_tcp.c:635:0	sentinel/loop/index
core/carrier_tcp.c:636:0	sentinel/loop/index
core/carrier_tcp.c:637:0	sentinel/loop/index
core/carrier_tcp.c:638:0	sentinel/loop/index
core/carrier_tcp.c:639:0	sentinel/loop/index
core/carrier_tcp.c:640:0	sentinel/loop/index
core/carrier_tcp.c:641:0	sentinel/loop/index
core/carrier_tcp.c:642:0	sentinel/loop/index
core/carrier_tcp.c:643:0	sentinel/loop/index
core/carrier_tcp.c:644:1	sentinel/loop/index
core/core_internal.h:54:0	sentinel/loop/index
core/core_internal.h:54:1	sentinel/loop/index
core/core_internal.h:54:0	sentinel/loop/index
core/core_internal.h:55:0	sentinel/loop/index
core/core_internal.h:56:0	sentinel/loop/index
core/core_internal.h:56:0	sentinel/loop/index
core/core_internal.h:58:1	sentinel/loop/index
core/core_internal.h:117:1	sentinel/loop/index
core/core_internal.h:117:0	sentinel/loop/index
core/core_internal.h:151:0	sentinel/loop/index
core/core_internal.h:151:0	sentinel/loop/index
core/core_internal.h:152:1000000000	system-header-derived
core/core_internal.h:171:0	sentinel/loop/index
core/core_internal.h:171:1	sentinel/loop/index
core/core_internal.h:172:1	sentinel/loop/index
core/core_internal.h:172:0	sentinel/loop/index
core/core_internal.h:183:1000000000	system-header-derived
core/core_internal.h:184:1000000000	system-header-derived
core/core_internal.h:188:1024	system-header-derived
core/core_internal.h:197:0	sentinel/loop/index
core/core_internal.h:199:0	sentinel/loop/index
core/core_internal.h:199:0	sentinel/loop/index
core/core_internal.h:199:1	sentinel/loop/index
core/core_internal.h:199:0	sentinel/loop/index
core/core_internal.h:200:1	sentinel/loop/index
core/core_internal.h:200:0	sentinel/loop/index
core/core_internal.h:213:8	bit-width/shift
core/core_internal.h:214:0	sentinel/loop/index
core/core_internal.h:215:8	bit-width/shift
core/core_internal.h:215:1	sentinel/loop/index
core/core_internal.h:216:8	bit-width/shift
core/core_internal.h:217:0	sentinel/loop/index
core/core_internal.h:229:0	sentinel/loop/index
core/core_internal.h:229:1	sentinel/loop/index
core/core_internal.h:229:31	bit-width/shift
core/core_internal.h:229:1	sentinel/loop/index
core/core_internal.h:229:0	sentinel/loop/index
core/core_internal.h:238:0	sentinel/loop/index
core/core_internal.h:238:1	sentinel/loop/index
core/core_internal.h:238:0	sentinel/loop/index
core/core_internal.h:238:1	sentinel/loop/index
core/core_internal.h:238:0	sentinel/loop/index
core/core_internal.h:251:0	sentinel/loop/index
core/core_internal.h:251:1	sentinel/loop/index
core/core_internal.h:265:0	sentinel/loop/index
core/core_internal.h:266:0	sentinel/loop/index
core/core_internal.h:266:1	sentinel/loop/index
core/core_internal.h:278:0	sentinel/loop/index
core/core_internal.h:278:0	sentinel/loop/index
core/core_internal.h:280:1	sentinel/loop/index
core/cpu_pin_internal.h:24:0	sentinel/loop/index
core/cpu_pin_internal.h:33:1	sentinel/loop/index
core/cpu_pin_internal.h:33:0	sentinel/loop/index
core/cpu_pin_internal.h:43:0	sentinel/loop/index
core/cpu_pin_internal.h:43:1	sentinel/loop/index
core/page.c:58:0	sentinel/loop/index
core/page.c:64:0	sentinel/loop/index
core/page.c:68:0	sentinel/loop/index
core/page.c:69:2	sentinel/loop/index
core/page.c:73:0	sentinel/loop/index
core/page.c:76:0	sentinel/loop/index
core/page.c:80:1	sentinel/loop/index
core/page.c:81:0	sentinel/loop/index
core/page.c:82:64	bit-width/shift
core/page.c:83:0	sentinel/loop/index
core/page.c:86:0	sentinel/loop/index
core/page.c:88:0	sentinel/loop/index
core/page.c:90:0	sentinel/loop/index
core/page.c:92:0	sentinel/loop/index
core/page.c:95:0	sentinel/loop/index
core/page.c:97:0	sentinel/loop/index
core/page.c:99:1	sentinel/loop/index
core/page.c:107:2	sentinel/loop/index
core/page.c:108:1	sentinel/loop/index
core/page.c:109:0	sentinel/loop/index
core/page.c:109:2	sentinel/loop/index
core/page.c:110:0	sentinel/loop/index
core/page.c:111:0	sentinel/loop/index
core/page.c:112:0	sentinel/loop/index
core/page.c:115:-1	sentinel/loop/index
core/page.c:129:1	sentinel/loop/index
core/page.c:130:4	sentinel/loop/index
core/page.c:134:0	sentinel/loop/index
core/page.c:134:0	sentinel/loop/index
core/page.c:136:0	sentinel/loop/index
core/page.c:138:0	sentinel/loop/index
core/page.c:139:0	sentinel/loop/index
core/page.c:140:0	sentinel/loop/index
core/page.c:142:0	sentinel/loop/index
core/page.c:144:0	sentinel/loop/index
core/page.c:146:0	sentinel/loop/index
core/page.c:147:0	sentinel/loop/index
core/page.c:148:0	sentinel/loop/index
core/page.c:152:0	sentinel/loop/index
core/page.c:152:-1	sentinel/loop/index
core/page.c:154:0	sentinel/loop/index
core/page.c:155:0	sentinel/loop/index
core/page.c:156:-1	sentinel/loop/index
core/page.c:158:0	sentinel/loop/index
core/page.c:161:0	sentinel/loop/index
core/page.c:165:1	sentinel/loop/index
core/page.c:183:2	sentinel/loop/index
core/page.c:183:2	sentinel/loop/index
core/page.c:186:1	sentinel/loop/index
core/page.c:187:0	sentinel/loop/index
core/page.c:187:0	sentinel/loop/index
core/page.c:188:0	sentinel/loop/index
core/page.c:188:0	sentinel/loop/index
core/page.c:189:0	sentinel/loop/index
core/page.c:193:0	sentinel/loop/index
core/page.c:196:1	sentinel/loop/index
core/page.c:199:1	sentinel/loop/index
core/page.c:223:0	sentinel/loop/index
core/page.c:230:0	sentinel/loop/index
core/page.c:230:0	sentinel/loop/index
core/page.c:236:0	sentinel/loop/index
core/page.c:236:0	sentinel/loop/index
core/page.c:245:0	sentinel/loop/index
core/page.c:245:0	sentinel/loop/index
core/page.c:252:0	sentinel/loop/index
core/page.c:252:0	sentinel/loop/index
core/page.c:263:0	sentinel/loop/index
core/page.c:264:1	sentinel/loop/index
core/page.c:265:0	sentinel/loop/index
core/page.c:266:1	sentinel/loop/index
core/page.c:270:0	sentinel/loop/index
core/page.c:272:1	sentinel/loop/index
core/page.c:272:0	sentinel/loop/index
core/page.c:273:0	sentinel/loop/index
core/page.c:276:0	sentinel/loop/index
core/page.c:278:0	sentinel/loop/index
core/page.c:281:0	sentinel/loop/index
core/page.c:281:0	sentinel/loop/index
core/page.c:288:1	sentinel/loop/index
core/page.c:292:0	sentinel/loop/index
core/page.c:295:2	sentinel/loop/index
core/page.c:295:-1	sentinel/loop/index
core/page.c:295:-1	sentinel/loop/index
core/page.c:296:2	sentinel/loop/index
core/page.c:296:-1	sentinel/loop/index
core/page.c:296:-1	sentinel/loop/index
core/page.c:297:-1	sentinel/loop/index
core/page.c:298:-1	sentinel/loop/index
core/page.c:301:0	sentinel/loop/index
core/page.c:301:0	sentinel/loop/index
core/page.c:310:1	sentinel/loop/index
core/page.c:311:0	sentinel/loop/index
core/page.c:317:1	sentinel/loop/index
core/page.c:317:0	sentinel/loop/index
core/page.c:318:0	sentinel/loop/index
core/page.c:319:1	sentinel/loop/index
core/page.c:319:0	sentinel/loop/index
core/page.c:326:0	sentinel/loop/index
core/page.c:326:0	sentinel/loop/index
core/page.c:336:0	sentinel/loop/index
core/page.c:337:1	sentinel/loop/index
core/page.c:345:0	sentinel/loop/index
core/page.c:348:0	sentinel/loop/index
core/page.c:352:0	sentinel/loop/index
core/page.c:354:1	sentinel/loop/index
core/page.c:357:0	sentinel/loop/index
core/page.c:359:0	sentinel/loop/index
core/page.c:360:0	sentinel/loop/index
core/page.c:361:0	sentinel/loop/index
core/page.c:366:1	sentinel/loop/index
core/page.c:367:1	sentinel/loop/index
core/page.c:372:0	sentinel/loop/index
core/page.c:373:0	sentinel/loop/index
core/page.c:373:1	sentinel/loop/index
core/page.c:374:0	sentinel/loop/index
core/page.c:374:4	sentinel/loop/index
core/page.c:376:1	sentinel/loop/index
core/page.c:381:0	sentinel/loop/index
core/page.c:382:1	sentinel/loop/index
core/page.c:384:0	sentinel/loop/index
core/page.c:389:0	sentinel/loop/index
core/page.c:389:0	sentinel/loop/index
core/page.c:390:0	sentinel/loop/index
core/page.c:390:4	sentinel/loop/index
core/page.c:391:0	sentinel/loop/index
core/page.c:392:0	sentinel/loop/index
core/page.c:393:-1	sentinel/loop/index
core/page.c:395:0	sentinel/loop/index
core/page.c:398:0	sentinel/loop/index
core/page.c:400:0	sentinel/loop/index
core/page.c:404:0	sentinel/loop/index
core/page.c:411:0	sentinel/loop/index
core/page.c:413:1	sentinel/loop/index
core/page.c:415:0	sentinel/loop/index
core/page.c:417:1	sentinel/loop/index
core/page.c:422:1	sentinel/loop/index
core/page.c:423:1	sentinel/loop/index
core/page.c:424:1	sentinel/loop/index
core/page.c:427:0	sentinel/loop/index
core/page.c:427:1	sentinel/loop/index
core/page.c:432:0	sentinel/loop/index
core/page.c:433:1	sentinel/loop/index
core/page.c:434:1	sentinel/loop/index
core/page.c:437:0	sentinel/loop/index
core/page.c:437:1	sentinel/loop/index
core/page.c:442:0	sentinel/loop/index
core/page.c:442:0	sentinel/loop/index
core/page.c:465:0	sentinel/loop/index
core/page.c:465:0	sentinel/loop/index
core/page.c:465:0	sentinel/loop/index
core/page.c:465:0	sentinel/loop/index
core/page.c:465:0	sentinel/loop/index
core/page.c:469:1	sentinel/loop/index
core/page.c:470:0	sentinel/loop/index
core/page.c:473:0	sentinel/loop/index
core/page.c:473:1	sentinel/loop/index
core/page.c:473:0	sentinel/loop/index
core/page.c:473:1	sentinel/loop/index
core/page.c:473:0	sentinel/loop/index
core/page.c:479:0	sentinel/loop/index
core/page.c:479:1	sentinel/loop/index
core/page.c:479:0	sentinel/loop/index
core/page.c:479:0	sentinel/loop/index
core/page.c:479:1	sentinel/loop/index
core/page.c:484:1	sentinel/loop/index
core/page.c:485:1	sentinel/loop/index
core/page.c:486:0	sentinel/loop/index
core/page.c:487:0	sentinel/loop/index
core/page.c:488:-1	sentinel/loop/index
core/page.c:490:0	sentinel/loop/index
core/page.c:493:0	sentinel/loop/index
core/page.c:494:0	sentinel/loop/index
core/page.c:495:-1	sentinel/loop/index
core/page.c:497:0	sentinel/loop/index
core/page.c:500:0	sentinel/loop/index
core/page.c:501:1	sentinel/loop/index
core/page.c:502:0	sentinel/loop/index
core/page.c:504:2	sentinel/loop/index
core/page.c:506:1	sentinel/loop/index
core/page.h:112:32	abi-alignment
core/page.h:114:104	abi-alignment
core/page.h:116:88	abi-alignment
core/page.h:118:120	abi-alignment
core/perf.c:70:1	sentinel/loop/index
core/perf.c:72:0	sentinel/loop/index
core/perf.c:75:0	sentinel/loop/index
core/perf.c:76:0	sentinel/loop/index
core/perf.c:79:0	sentinel/loop/index
core/perf.c:80:0	sentinel/loop/index
core/perf.c:81:-1	sentinel/loop/index
core/perf.c:83:0	sentinel/loop/index
core/perf.c:84:0	sentinel/loop/index
core/perf.c:101:0	sentinel/loop/index
core/perf.c:101:0	sentinel/loop/index
core/perf.c:102:0	sentinel/loop/index
core/perf.c:103:0	sentinel/loop/index
core/perf.c:104:1	sentinel/loop/index
core/perf.c:104:0	sentinel/loop/index
core/perf.c:105:0	sentinel/loop/index
core/perf.c:106:0	sentinel/loop/index
core/perf.c:106:0	sentinel/loop/index
core/perf.c:107:0	sentinel/loop/index
core/perf.c:108:1	sentinel/loop/index
core/perf.c:109:0	sentinel/loop/index
core/perf.c:112:0	sentinel/loop/index
core/perf.c:114:0	sentinel/loop/index
core/perf.c:114:0	sentinel/loop/index
core/perf.c:116:1	sentinel/loop/index
core/perf.c:131:0	sentinel/loop/index
core/perf.c:132:0	sentinel/loop/index
core/perf.c:132:0	sentinel/loop/index
core/perf.c:133:0	sentinel/loop/index
core/perf.c:134:0	sentinel/loop/index
core/perf.c:135:0	sentinel/loop/index
core/perf.c:138:0	sentinel/loop/index
core/perf.c:140:0	sentinel/loop/index
core/perf.c:143:1	sentinel/loop/index
core/perf.c:166:0	sentinel/loop/index
core/perf.c:166:0	sentinel/loop/index
core/perf.c:169:0	sentinel/loop/index
core/perf.c:174:0	sentinel/loop/index
core/perf.c:174:0	sentinel/loop/index
core/perf.c:179:1	sentinel/loop/index
core/perf.c:183:0	sentinel/loop/index
core/perf.c:186:-1	sentinel/loop/index
core/perf.c:190:0	sentinel/loop/index
core/perf.c:202:0	sentinel/loop/index
core/perf.c:204:-1	sentinel/loop/index
core/perf.c:204:-1	sentinel/loop/index
core/perf.c:206:0	sentinel/loop/index
core/perf.c:211:0	sentinel/loop/index
core/perf.c:220:0	sentinel/loop/index
core/perf.c:227:0	sentinel/loop/index
core/perf.c:227:0	sentinel/loop/index
core/perf.c:227:1	sentinel/loop/index
core/perf.c:228:0	sentinel/loop/index
core/perf.c:232:0	sentinel/loop/index
core/perf.c:234:0	sentinel/loop/index
core/perf.c:240:0	sentinel/loop/index
core/perf.c:242:0	sentinel/loop/index
core/perf.c:242:0	sentinel/loop/index
core/perf.c:242:1	sentinel/loop/index
core/perf.c:243:0	sentinel/loop/index
core/perf.c:246:0	sentinel/loop/index
core/perf.c:248:0	sentinel/loop/index
core/perf.c:250:0	sentinel/loop/index
core/perf.c:255:0	sentinel/loop/index
core/perf.c:256:0	sentinel/loop/index
core/perf.c:257:0	sentinel/loop/index
core/perf.c:257:0	sentinel/loop/index
core/perf.c:262:0	sentinel/loop/index
core/perf.c:262:0	sentinel/loop/index
core/perf.c:262:1	sentinel/loop/index
core/perf.c:263:0	sentinel/loop/index
core/perf.c:266:0	sentinel/loop/index
core/perf.c:268:0	sentinel/loop/index
core/perf.c:273:1	sentinel/loop/index
core/perf.c:277:0	sentinel/loop/index
core/perf.c:277:0	sentinel/loop/index
core/perf.c:298:0	sentinel/loop/index
core/perf.c:303:0	sentinel/loop/index
core/perf.c:303:0	sentinel/loop/index
core/perf.c:311:0	sentinel/loop/index
core/perf.c:311:0	sentinel/loop/index
core/perf.c:317:0	sentinel/loop/index
core/perf.c:318:0	sentinel/loop/index
core/perf.c:319:0	sentinel/loop/index
core/perf.c:319:0	sentinel/loop/index
core/perf.c:324:0	sentinel/loop/index
core/perf.c:327:0	sentinel/loop/index
core/perf.c:339:1	sentinel/loop/index
core/perf.c:342:0	sentinel/loop/index
core/perf.c:342:0	sentinel/loop/index
core/perf.c:364:0	sentinel/loop/index
core/perf.c:364:0	sentinel/loop/index
core/perf.c:364:0	sentinel/loop/index
core/perf.c:364:0	sentinel/loop/index
core/perf.c:364:0	sentinel/loop/index
core/perf.c:368:0	sentinel/loop/index
core/perf.c:369:0	sentinel/loop/index
core/perf.c:370:0	sentinel/loop/index
core/perf.c:371:0	sentinel/loop/index
core/perf.c:372:0	sentinel/loop/index
core/perf.c:372:0	sentinel/loop/index
core/perf.c:374:1	sentinel/loop/index
core/perf.c:376:0	sentinel/loop/index
core/perf.c:380:0	sentinel/loop/index
core/perf.c:382:0	sentinel/loop/index
core/perf.c:386:1	sentinel/loop/index
core/perf.c:387:0	sentinel/loop/index
core/perf.c:387:1	sentinel/loop/index
core/perf.c:387:0	sentinel/loop/index
core/perf.c:392:0	sentinel/loop/index
core/perf.c:392:1	sentinel/loop/index
core/perf.c:392:0	sentinel/loop/index
core/perf.c:392:1	sentinel/loop/index
core/perf.c:392:0	sentinel/loop/index
core/perf.c:397:1	sentinel/loop/index
core/perf.c:398:0	sentinel/loop/index
core/perf.c:398:0	sentinel/loop/index
core/perf.c:398:1	sentinel/loop/index
core/perf.h:133:128	abi-alignment
core/perf.h:135:32	abi-alignment
core/perf.h:137:48	abi-alignment
core/perf.h:139:80	abi-alignment
core/placement.c:22:1	sentinel/loop/index
core/placement.c:54:0	sentinel/loop/index
core/placement.c:56:0	sentinel/loop/index
core/placement.c:56:1	sentinel/loop/index
core/placement.c:62:0	sentinel/loop/index
core/placement.c:64:0	sentinel/loop/index
core/placement.c:66:0	sentinel/loop/index
core/placement.c:66:-1	sentinel/loop/index
core/placement.c:67:1	sentinel/loop/index
core/placement.c:79:0	sentinel/loop/index
core/placement.c:92:0	sentinel/loop/index
core/placement.c:93:0	sentinel/loop/index
core/placement.c:95:1	sentinel/loop/index
core/placement.c:95:0	sentinel/loop/index
core/placement.c:110:0	sentinel/loop/index
core/placement.c:111:0	sentinel/loop/index
core/placement.c:113:1	sentinel/loop/index
core/placement.c:113:0	sentinel/loop/index
core/placement.c:115:0	sentinel/loop/index
core/placement.c:126:64	bit-width/shift
core/placement.c:129:0	sentinel/loop/index
core/placement.c:131:-1	sentinel/loop/index
core/placement.c:143:0	sentinel/loop/index
core/placement.c:143:1	sentinel/loop/index
core/placement.c:144:0	sentinel/loop/index
core/placement.c:144:0	sentinel/loop/index
core/placement.c:146:0	sentinel/loop/index
core/placement.c:147:1	sentinel/loop/index
core/placement.c:148:0	sentinel/loop/index
core/placement.c:149:0	sentinel/loop/index
core/placement.c:149:1	sentinel/loop/index
core/placement.c:150:0	sentinel/loop/index
core/placement.c:150:0	sentinel/loop/index
core/placement.c:160:0	sentinel/loop/index
core/placement.c:161:0	sentinel/loop/index
core/placement.c:162:0	sentinel/loop/index
core/placement.c:163:0	sentinel/loop/index
core/placement.c:164:0	sentinel/loop/index
core/placement.c:166:0	sentinel/loop/index
core/placement.c:169:-1	sentinel/loop/index
core/placement.c:171:0	sentinel/loop/index
core/placement.c:180:0	sentinel/loop/index
core/placement.c:180:0	sentinel/loop/index
core/placement.c:181:0	sentinel/loop/index
core/placement.c:182:0	sentinel/loop/index
core/placement.c:183:0	sentinel/loop/index
core/placement.c:184:0	sentinel/loop/index
core/placement.c:185:0	sentinel/loop/index
core/placement.c:188:-1	sentinel/loop/index
core/placement.c:190:0	sentinel/loop/index
core/placement.c:198:0	sentinel/loop/index
core/placement.c:199:0	sentinel/loop/index
core/placement.c:200:0	sentinel/loop/index
core/placement.c:201:0	sentinel/loop/index
core/placement.c:202:0	sentinel/loop/index
core/placement.c:203:0	sentinel/loop/index
core/placement.c:204:0	sentinel/loop/index
core/placement.c:207:-1	sentinel/loop/index
core/placement.c:209:0	sentinel/loop/index
core/placement.c:217:0	sentinel/loop/index
core/placement.c:218:0	sentinel/loop/index
core/placement.c:224:0	sentinel/loop/index
core/placement.c:225:0	sentinel/loop/index
core/placement.c:226:0	sentinel/loop/index
core/placement.c:227:0	sentinel/loop/index
core/placement.c:232:0	sentinel/loop/index
core/placement.c:233:0	sentinel/loop/index
core/placement.c:236:-1	sentinel/loop/index
core/placement.c:237:0	sentinel/loop/index
core/placement.c:239:0	sentinel/loop/index
core/placement.c:250:0	sentinel/loop/index
core/placement.c:264:0	sentinel/loop/index
core/placement.c:265:0	sentinel/loop/index
core/placement.c:266:0	sentinel/loop/index
core/placement.c:267:0	sentinel/loop/index
core/placement.c:278:0	sentinel/loop/index
core/placement.c:279:0	sentinel/loop/index
core/placement.c:280:4	sentinel/loop/index
core/placement.c:284:0	sentinel/loop/index
core/placement.c:284:0	sentinel/loop/index
core/placement.c:288:0	sentinel/loop/index
core/placement.c:289:0	sentinel/loop/index
core/placement.c:310:1	sentinel/loop/index
core/placement.c:312:0	sentinel/loop/index
core/placement.c:314:-1	sentinel/loop/index
core/placement.c:316:0	sentinel/loop/index
core/placement.c:319:0	sentinel/loop/index
core/placement.c:320:0	sentinel/loop/index
core/placement.c:321:-1	sentinel/loop/index
core/placement.c:323:0	sentinel/loop/index
core/placement.c:326:4	sentinel/loop/index
core/placement.c:330:0	sentinel/loop/index
core/placement.c:330:0	sentinel/loop/index
core/placement.c:332:0	sentinel/loop/index
core/placement.c:366:0	sentinel/loop/index
core/placement.c:367:0	sentinel/loop/index
core/placement.c:373:1	sentinel/loop/index
core/placement.c:374:1	sentinel/loop/index
core/placement.c:377:0	sentinel/loop/index
core/placement.c:377:0	sentinel/loop/index
core/placement.c:378:0	sentinel/loop/index
core/placement.c:378:0	sentinel/loop/index
core/placement.c:381:0	sentinel/loop/index
core/placement.c:394:0	sentinel/loop/index
core/placement.c:399:0	sentinel/loop/index
core/placement.c:402:0	sentinel/loop/index
core/placement.c:409:0	sentinel/loop/index
core/placement.c:416:0	sentinel/loop/index
core/placement.c:419:0	sentinel/loop/index
core/placement.c:426:0	sentinel/loop/index
core/placement.c:436:0	sentinel/loop/index
core/placement.c:436:1	sentinel/loop/index
core/placement.c:440:0	sentinel/loop/index
core/placement.c:449:0	sentinel/loop/index
core/placement.c:454:0	sentinel/loop/index
core/placement.c:457:0	sentinel/loop/index
core/placement.c:465:0	sentinel/loop/index
core/placement.c:469:0	sentinel/loop/index
core/placement.c:472:0	sentinel/loop/index
core/placement.c:480:0	sentinel/loop/index
core/placement.c:483:0	sentinel/loop/index
core/placement.c:492:0	sentinel/loop/index
core/placement.c:496:0	sentinel/loop/index
core/placement.c:499:0	sentinel/loop/index
core/placement.c:509:3	sentinel/loop/index
core/placement.c:512:0	sentinel/loop/index
core/placement.c:512:0	sentinel/loop/index
core/placement.c:515:0	sentinel/loop/index
core/placement.c:517:0	sentinel/loop/index
core/placement.c:519:-1	sentinel/loop/index
core/placement.c:531:0	sentinel/loop/index
core/placement.c:534:-1	sentinel/loop/index
core/placement.c:535:0	sentinel/loop/index
core/placement.c:541:0	sentinel/loop/index
core/placement.c:548:-1	sentinel/loop/index
core/placement.c:556:0	sentinel/loop/index
core/placement.c:568:-1	sentinel/loop/index
core/placement.c:583:1	sentinel/loop/index
core/placement.c:584:0	sentinel/loop/index
core/placement.c:585:0	sentinel/loop/index
core/placement.c:593:1	sentinel/loop/index
core/placement.c:601:0	sentinel/loop/index
core/placement.c:603:1	sentinel/loop/index
core/placement.c:629:0	sentinel/loop/index
core/placement.c:632:0	sentinel/loop/index
core/placement.c:634:0	sentinel/loop/index
core/placement.c:636:0	sentinel/loop/index
core/placement.c:637:0	sentinel/loop/index
core/placement.c:638:0	sentinel/loop/index
core/placement.c:644:64	bit-width/shift
core/placement.c:645:0	sentinel/loop/index
core/placement.c:649:0	sentinel/loop/index
core/placement.c:656:0	sentinel/loop/index
core/placement.c:662:0	sentinel/loop/index
core/placement.c:664:0	sentinel/loop/index
core/placement.c:664:0	sentinel/loop/index
core/placement.c:665:2	sentinel/loop/index
core/placement.c:668:0	sentinel/loop/index
core/placement.c:671:0	sentinel/loop/index
core/placement.c:672:2	sentinel/loop/index
core/placement.c:675:0	sentinel/loop/index
core/placement.c:679:0	sentinel/loop/index
core/placement.c:680:0	sentinel/loop/index
core/placement.c:684:0	sentinel/loop/index
core/placement.c:686:0	sentinel/loop/index
core/placement.c:687:0	sentinel/loop/index
core/placement.c:691:1	sentinel/loop/index
core/placement.c:693:0	sentinel/loop/index
core/placement.c:695:0	sentinel/loop/index
core/placement.c:697:0	sentinel/loop/index
core/placement.c:703:0	sentinel/loop/index
core/placement.c:706:16	bit-width/shift
core/placement.c:706:64	bit-width/shift
core/placement.c:707:0	sentinel/loop/index
core/placement.c:711:0	sentinel/loop/index
core/placement.c:713:0	sentinel/loop/index
core/placement.c:716:0	sentinel/loop/index
core/placement.c:717:1000	sentinel/loop/index
core/placement.c:720:0	sentinel/loop/index
core/placement.c:724:0	sentinel/loop/index
core/placement.c:728:0	sentinel/loop/index
core/placement.c:729:0	sentinel/loop/index
core/placement.c:732:1	sentinel/loop/index
core/placement.c:734:0	sentinel/loop/index
core/placement.c:738:1	sentinel/loop/index
core/placement.c:740:0	sentinel/loop/index
core/placement.c:746:0	sentinel/loop/index
core/placement.c:766:1	sentinel/loop/index
core/placement.c:791:0	sentinel/loop/index
core/placement.c:797:0	sentinel/loop/index
core/placement.c:797:0	sentinel/loop/index
core/placement.c:803:1	sentinel/loop/index
core/placement.c:807:0	sentinel/loop/index
core/placement.c:810:-1	sentinel/loop/index
core/placement.c:811:-1	sentinel/loop/index
core/placement.c:818:0	sentinel/loop/index
core/placement.c:818:0	sentinel/loop/index
core/placement.c:826:0	sentinel/loop/index
core/placement.c:826:0	sentinel/loop/index
core/placement.c:834:0	sentinel/loop/index
core/placement.c:847:0	sentinel/loop/index
core/placement.c:848:0	sentinel/loop/index
core/placement.c:849:1	sentinel/loop/index
core/placement.c:857:1	sentinel/loop/index
core/placement.c:865:0	sentinel/loop/index
core/placement.c:884:0	sentinel/loop/index
core/placement.c:899:0	sentinel/loop/index
core/placement.c:899:0	sentinel/loop/index
core/placement.c:900:0	sentinel/loop/index
core/placement.c:923:0	sentinel/loop/index
core/placement.c:923:0	sentinel/loop/index
core/placement.c:923:0	sentinel/loop/index
core/placement.c:923:0	sentinel/loop/index
core/placement.c:923:0	sentinel/loop/index
core/placement.c:929:0	sentinel/loop/index
core/placement.c:929:1	sentinel/loop/index
core/placement.c:929:0	sentinel/loop/index
core/placement.c:929:0	sentinel/loop/index
core/placement.c:929:0	sentinel/loop/index
core/placement.c:954:0	sentinel/loop/index
core/placement.c:954:0	sentinel/loop/index
core/placement.c:954:0	sentinel/loop/index
core/placement.c:954:0	sentinel/loop/index
core/placement.c:954:0	sentinel/loop/index
core/placement.c:959:1	sentinel/loop/index
core/placement.c:962:1	sentinel/loop/index
core/placement.c:963:0	sentinel/loop/index
core/placement.c:966:0	sentinel/loop/index
core/placement.c:969:-1	sentinel/loop/index
core/placement.c:971:0	sentinel/loop/index
core/placement.c:974:0	sentinel/loop/index
core/placement.c:975:0	sentinel/loop/index
core/placement.c:976:-1	sentinel/loop/index
core/placement.c:978:0	sentinel/loop/index
core/placement.c:981:4	sentinel/loop/index
core/placement.c:984:0	sentinel/loop/index
core/placement.c:984:0	sentinel/loop/index
core/placement.c:989:0	sentinel/loop/index
core/placement.c:991:0	sentinel/loop/index
core/placement.c:992:0	sentinel/loop/index
core/placement.c:995:1	sentinel/loop/index
core/placement.c:998:0	sentinel/loop/index
core/placement.c:998:1	sentinel/loop/index
core/placement.c:998:0	sentinel/loop/index
core/placement.c:998:0	sentinel/loop/index
core/placement.c:999:1	sentinel/loop/index
core/placement.c:1005:0	sentinel/loop/index
core/placement.c:1005:0	sentinel/loop/index
core/placement.c:1009:2	sentinel/loop/index
core/placement.c:1013:0	sentinel/loop/index
core/placement.c:1013:1	sentinel/loop/index
core/placement.c:1013:1	sentinel/loop/index
core/placement.c:1013:1	sentinel/loop/index
core/placement.c:1013:0	sentinel/loop/index
core/placement.c:1024:0	sentinel/loop/index
core/placement.c:1029:1	sentinel/loop/index
core/placement.h:33:0	sentinel/loop/index
core/placement.h:34:0	sentinel/loop/index
core/placement.h:35:0	sentinel/loop/index
core/placement.h:38:0	sentinel/loop/index
core/placement.h:38:0	sentinel/loop/index
core/placement.h:40:1	sentinel/loop/index
core/placement.h:140:216	abi-alignment
core/placement.h:142:64	bit-width/shift
core/placement.h:144:96	abi-alignment
core/placement_internal.h:101:0	sentinel/loop/index
core/primitive.h:23:0	sentinel/loop/index
core/primitive.h:24:1	sentinel/loop/index
core/primitive.h:25:2	sentinel/loop/index
core/primitive.h:26:3	sentinel/loop/index
core/primitive.h:126:24	abi-alignment
core/primitive.h:128:112	abi-alignment
core/primitive.h:130:104	abi-alignment
core/primitive.h:132:176	abi-alignment
core/primitive.h:134:192	abi-alignment
core/pselect_timeout_internal.h:35:0	sentinel/loop/index
core/pselect_timeout_internal.h:35:1	sentinel/loop/index
core/result.h:23:0	sentinel/loop/index
core/result.h:24:1	sentinel/loop/index
core/result.h:25:2	sentinel/loop/index
core/result.h:27:3	sentinel/loop/index
core/result.h:29:4	sentinel/loop/index
core/result.h:30:5	numeric-literal
core/result.h:31:6	numeric-literal
core/result.h:32:7	numeric-literal
core/result.h:33:8	numeric-literal
core/result.h:34:9	numeric-literal
core/result.h:35:10	numeric-literal
core/result.h:41:0	sentinel/loop/index
core/result.h:42:1	sentinel/loop/index
core/result.h:43:2	sentinel/loop/index
core/result.h:44:3	sentinel/loop/index
core/result.h:45:4	sentinel/loop/index
core/result.h:46:5	numeric-literal
core/result.h:52:0	sentinel/loop/index
core/result.h:53:1	sentinel/loop/index
core/result.h:54:2	sentinel/loop/index
core/result.h:55:3	sentinel/loop/index
core/result.h:56:4	sentinel/loop/index
core/result.h:58:5	numeric-literal
core/result.h:59:6	numeric-literal
core/result.h:60:7	numeric-literal
core/result.h:61:8	numeric-literal
core/result.h:62:9	numeric-literal
core/result.h:63:10	numeric-literal
core/result.h:90:32	abi-alignment
core/result.h:92:48	abi-alignment
core/victim_helper/victim_helper.c:39:1024	sentinel/loop/index
core/victim_helper/victim_helper.c:53:0	sentinel/loop/index
core/victim_helper/victim_helper.c:53:1	sentinel/loop/index
core/victim_helper/victim_helper.c:53:8	bit-width/shift
core/victim_helper/victim_helper.c:62:0	sentinel/loop/index
core/victim_helper/victim_helper.c:62:1	sentinel/loop/index
core/victim_helper/victim_helper.c:62:8	bit-width/shift
core/victim_helper/victim_helper.c:63:2	sentinel/loop/index
core/victim_helper/victim_helper.c:63:16	bit-width/shift
core/victim_helper/victim_helper.c:63:3	sentinel/loop/index
core/victim_helper/victim_helper.c:63:24	abi-alignment
core/victim_helper/victim_helper.c:72:0	sentinel/loop/index
core/victim_helper/victim_helper.c:73:7	sentinel/loop/index
core/victim_helper/victim_helper.c:73:0	sentinel/loop/index
core/victim_helper/victim_helper.c:74:8	bit-width/shift
core/victim_helper/victim_helper.c:85:0	sentinel/loop/index
core/victim_helper/victim_helper.c:86:1	sentinel/loop/index
core/victim_helper/victim_helper.c:86:8	bit-width/shift
core/victim_helper/victim_helper.c:95:0	sentinel/loop/index
core/victim_helper/victim_helper.c:96:1	sentinel/loop/index
core/victim_helper/victim_helper.c:96:8	bit-width/shift
core/victim_helper/victim_helper.c:97:2	sentinel/loop/index
core/victim_helper/victim_helper.c:97:16	bit-width/shift
core/victim_helper/victim_helper.c:98:3	sentinel/loop/index
core/victim_helper/victim_helper.c:98:24	abi-alignment
core/victim_helper/victim_helper.c:107:0	sentinel/loop/index
core/victim_helper/victim_helper.c:107:8	bit-width/shift
core/victim_helper/victim_helper.c:108:8	bit-width/shift
core/victim_helper/victim_helper.c:119:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:119:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:119:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:128:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:129:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:129:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:129:1000	sentinel/loop/index
core/victim_helper/victim_helper.c:140:128	abi-alignment
core/victim_helper/victim_helper.c:141:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:159:0	sentinel/loop/index
core/victim_helper/victim_helper.c:165:0	sentinel/loop/index
core/victim_helper/victim_helper.c:166:1	sentinel/loop/index
core/victim_helper/victim_helper.c:167:2	sentinel/loop/index
core/victim_helper/victim_helper.c:168:0	sentinel/loop/index
core/victim_helper/victim_helper.c:168:4	sentinel/loop/index
core/victim_helper/victim_helper.c:184:1	sentinel/loop/index
core/victim_helper/victim_helper.c:185:0	sentinel/loop/index
core/victim_helper/victim_helper.c:186:0	sentinel/loop/index
core/victim_helper/victim_helper.c:186:1	sentinel/loop/index
core/victim_helper/victim_helper.c:187:1	sentinel/loop/index
core/victim_helper/victim_helper.c:199:6	sentinel/loop/index
core/victim_helper/victim_helper.c:200:0	sentinel/loop/index
core/victim_helper/victim_helper.c:201:1	sentinel/loop/index
core/victim_helper/victim_helper.c:202:2	sentinel/loop/index
core/victim_helper/victim_helper.c:215:5	sentinel/loop/index
core/victim_helper/victim_helper.c:215:0	sentinel/loop/index
core/victim_helper/victim_helper.c:215:0	sentinel/loop/index
core/victim_helper/victim_helper.c:215:0	sentinel/loop/index
core/victim_helper/victim_helper.c:217:0	sentinel/loop/index
core/victim_helper/victim_helper.c:218:5	sentinel/loop/index
core/victim_helper/victim_helper.c:218:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:218:0	sentinel/loop/index
core/victim_helper/victim_helper.c:218:1	sentinel/loop/index
core/victim_helper/victim_helper.c:222:0	sentinel/loop/index
core/victim_helper/victim_helper.c:222:65536	sentinel/loop/index
core/victim_helper/victim_helper.c:222:65536	sentinel/loop/index
core/victim_helper/victim_helper.c:223:5	sentinel/loop/index
core/victim_helper/victim_helper.c:226:0	sentinel/loop/index
core/victim_helper/victim_helper.c:227:5	sentinel/loop/index
core/victim_helper/victim_helper.c:227:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:227:1	sentinel/loop/index
core/victim_helper/victim_helper.c:228:0	sentinel/loop/index
core/victim_helper/victim_helper.c:228:1	sentinel/loop/index
core/victim_helper/victim_helper.c:236:0	sentinel/loop/index
core/victim_helper/victim_helper.c:236:1	sentinel/loop/index
core/victim_helper/victim_helper.c:237:0	sentinel/loop/index
core/victim_helper/victim_helper.c:237:1	sentinel/loop/index
core/victim_helper/victim_helper.c:238:0	sentinel/loop/index
core/victim_helper/victim_helper.c:254:4	sentinel/loop/index
core/victim_helper/victim_helper.c:255:6	sentinel/loop/index
core/victim_helper/victim_helper.c:256:0	sentinel/loop/index
core/victim_helper/victim_helper.c:256:0	sentinel/loop/index
core/victim_helper/victim_helper.c:257:8	bit-width/shift
core/victim_helper/victim_helper.c:258:0	sentinel/loop/index
core/victim_helper/victim_helper.c:263:0	sentinel/loop/index
core/victim_helper/victim_helper.c:269:0	sentinel/loop/index
core/victim_helper/victim_helper.c:274:0	sentinel/loop/index
core/victim_helper/victim_helper.c:280:0	sentinel/loop/index
core/victim_helper/victim_helper.c:288:0	sentinel/loop/index
core/victim_helper/victim_helper.c:288:0	sentinel/loop/index
core/victim_helper/victim_helper.c:297:4	sentinel/loop/index
core/victim_helper/victim_helper.c:311:10	sentinel/loop/index
core/victim_helper/victim_helper.c:311:8	bit-width/shift
core/victim_helper/victim_helper.c:313:0	sentinel/loop/index
core/victim_helper/victim_helper.c:314:8	bit-width/shift
core/victim_helper/victim_helper.c:315:16	bit-width/shift
core/victim_helper/victim_helper.c:316:24	abi-alignment
core/victim_helper/victim_helper.c:317:32	abi-alignment
core/victim_helper/victim_helper.c:318:40	abi-alignment
core/victim_helper/victim_helper.c:319:48	abi-alignment
core/victim_helper/victim_helper.c:320:56	abi-alignment
core/victim_helper/victim_helper.c:321:64	bit-width/shift
core/victim_helper/victim_helper.c:322:72	abi-alignment
core/victim_helper/victim_helper.c:324:0	sentinel/loop/index
core/victim_helper/victim_helper.c:324:0	sentinel/loop/index
core/victim_helper/victim_helper.c:324:0	sentinel/loop/index
core/victim_helper/victim_helper.c:326:0	sentinel/loop/index
core/victim_helper/victim_helper.c:333:0	sentinel/loop/index
core/victim_helper/victim_helper.c:340:1	sentinel/loop/index
core/victim_helper/victim_helper.c:343:0	sentinel/loop/index
core/victim_helper/victim_helper.c:343:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:343:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:345:0	sentinel/loop/index
core/victim_helper/victim_helper.c:349:0	sentinel/loop/index
core/victim_helper/victim_helper.c:355:0	sentinel/loop/index
core/victim_helper/victim_helper.c:356:0	sentinel/loop/index
core/victim_helper/victim_helper.c:356:0	sentinel/loop/index
core/victim_helper/victim_helper.c:357:0	sentinel/loop/index
core/victim_helper/victim_helper.c:357:0	sentinel/loop/index
core/victim_helper/victim_helper.c:360:0	sentinel/loop/index
core/victim_helper/victim_helper.c:361:0	sentinel/loop/index
core/victim_helper/victim_helper.c:365:0	sentinel/loop/index
core/victim_helper/victim_helper.c:365:0	sentinel/loop/index
core/victim_helper/victim_helper.c:365:0	sentinel/loop/index
core/victim_helper/victim_helper.c:368:0	sentinel/loop/index
core/victim_helper/victim_helper.c:376:0	sentinel/loop/index
core/victim_helper/victim_helper.c:381:1	sentinel/loop/index
core/victim_helper/victim_helper.c:381:0	sentinel/loop/index
core/victim_helper/victim_helper.c:390:1	sentinel/loop/index
core/victim_helper/victim_helper.c:396:0	sentinel/loop/index
core/victim_helper/victim_helper.c:399:32	abi-alignment
core/victim_helper/victim_helper.c:400:0	sentinel/loop/index
core/victim_helper/victim_helper.c:401:8	bit-width/shift
core/victim_helper/victim_helper.c:402:16	bit-width/shift
core/victim_helper/victim_helper.c:403:24	abi-alignment
core/victim_helper/victim_helper.c:403:0	sentinel/loop/index
core/victim_helper/victim_helper.c:404:28	sentinel/loop/index
core/victim_helper/victim_helper.c:405:0	sentinel/loop/index
core/victim_helper/victim_helper.c:405:0	sentinel/loop/index
core/victim_helper/victim_helper.c:429:2	sentinel/loop/index
core/victim_helper/victim_helper.c:429:1	sentinel/loop/index
core/victim_helper/victim_helper.c:431:2	sentinel/loop/index
core/victim_helper/victim_helper.c:432:1	sentinel/loop/index
core/victim_helper/victim_helper.c:433:1	sentinel/loop/index
core/victim_helper/victim_helper.c:434:1	sentinel/loop/index
core/victim_helper/victim_helper.c:440:0	sentinel/loop/index
core/victim_helper/victim_helper.c:449:0	sentinel/loop/index
core/victim_helper/victim_helper.c:455:0	sentinel/loop/index
core/victim_helper/victim_helper.c:469:0	sentinel/loop/index
core/victim_helper/victim_helper.c:470:0	sentinel/loop/index
core/victim_helper/victim_helper.c:471:0	sentinel/loop/index
core/victim_helper/victim_helper.c:471:0	sentinel/loop/index
core/victim_helper/victim_helper.c:475:2	sentinel/loop/index
core/victim_helper/victim_helper.c:480:2	sentinel/loop/index
core/victim_helper/victim_helper.c:488:0	sentinel/loop/index
core/victim_helper/victim_helper.c:488:1	sentinel/loop/index
core/victim_helper/victim_helper.c:488:0	sentinel/loop/index
core/victim_helper/victim_helper.c:489:0	sentinel/loop/index
core/victim_helper/victim_helper.c:489:0	sentinel/loop/index
core/victim_helper/victim_helper.c:491:0	sentinel/loop/index
core/victim_helper/victim_helper.c:492:0	sentinel/loop/index
core/victim_helper/victim_helper.c:493:1	sentinel/loop/index
core/victim_helper/victim_helper.c:496:0	sentinel/loop/index
core/victim_helper/victim_helper.c:497:0	sentinel/loop/index
core/victim_helper/victim_helper.c:499:0	sentinel/loop/index
core/victim_helper/victim_helper.c:500:0	sentinel/loop/index
core/victim_helper/victim_helper.c:501:2	sentinel/loop/index
core/victim_helper/victim_helper.c:502:1	sentinel/loop/index
core/victim_helper/victim_helper.c:505:2	sentinel/loop/index
core/victim_helper/victim_helper.c:506:1	sentinel/loop/index
core/victim_helper/victim_helper.c:512:0	sentinel/loop/index
core/victim_helper/victim_helper.c:512:0	sentinel/loop/index
core/victim_helper/victim_helper.c:513:1	sentinel/loop/index
core/victim_helper/victim_helper.c:515:0	sentinel/loop/index
core/victim_helper/victim_helper.c:515:0	sentinel/loop/index
core/victim_helper/victim_helper.c:517:0	sentinel/loop/index
core/victim_helper/victim_helper.c:518:0	sentinel/loop/index
core/victim_helper/victim_helper.c:519:1	sentinel/loop/index
core/victim_helper/victim_helper.c:522:0	sentinel/loop/index
core/victim_helper/victim_helper.c:523:0	sentinel/loop/index
core/victim_helper/victim_helper.c:525:0	sentinel/loop/index
core/victim_helper/victim_helper.c:526:0	sentinel/loop/index
core/victim_helper/victim_helper.c:527:8	bit-width/shift
core/victim_helper/victim_helper.c:528:1	sentinel/loop/index
core/victim_helper/victim_helper.c:531:0	sentinel/loop/index
core/victim_helper/victim_helper.c:531:1	sentinel/loop/index
core/victim_helper/victim_helper.c:534:0	sentinel/loop/index
core/victim_helper/victim_helper.c:541:2	sentinel/loop/index
core/victim_helper/victim_helper.c:542:1	sentinel/loop/index
core/victim_helper/victim_helper.c:549:0	sentinel/loop/index
core/victim_helper/victim_helper.c:550:1	sentinel/loop/index
core/victim_helper/victim_helper.c:551:0	sentinel/loop/index
core/victim_helper/victim_helper.c:552:2	sentinel/loop/index
core/victim_helper/victim_helper.c:554:0	sentinel/loop/index
core/victim_helper/victim_helper.c:555:2	sentinel/loop/index
core/victim_helper/victim_helper.c:555:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:555:-1	sentinel/loop/index
core/victim_helper/victim_helper.c:556:0	sentinel/loop/index
core/victim_helper/victim_helper.c:557:0	sentinel/loop/index
core/victim_helper/victim_helper.c:558:0	sentinel/loop/index
core/victim_helper/victim_helper.c:562:0	sentinel/loop/index
core/victim_helper/victim_helper.c:563:0	sentinel/loop/index
core/victim_helper/victim_helper.c:565:0	sentinel/loop/index
core/victim_helper/victim_helper.c:567:0	sentinel/loop/index
core/victim_helper/victim_helper.c:568:1	sentinel/loop/index
core/victim_helper/victim_helper.c:569:0	sentinel/loop/index
core/victim_helper/victim_helper.c:570:1	sentinel/loop/index
core/victim_helper/victim_helper.c:570:1	sentinel/loop/index
core/victim_helper/victim_helper.c:572:0	sentinel/loop/index
core/victim_helper/victim_helper.c:573:1	sentinel/loop/index
core/victim_helper/victim_helper.c:573:2	sentinel/loop/index
core/victim_helper/victim_helper.c:575:0	sentinel/loop/index
core/victim_helper/victim_helper.c:576:0	sentinel/loop/index
core/victim_helper/victim_helper.c:578:0	sentinel/loop/index
core/victim_helper/victim_helper.c:579:1	sentinel/loop/index
core/victim_helper/victim_helper.c:581:0	sentinel/loop/index
core/victim_helper/victim_helper.c:585:0	sentinel/loop/index
core/victim_helper/victim_helper.c:589:0	sentinel/loop/index
core/victim_helper/victim_helper.c:593:0	sentinel/loop/index
core/victim_helper/victim_helper.c:594:0	sentinel/loop/index
core/victim_helper/victim_helper.c:599:1	sentinel/loop/index
core/victim_helper/victim_helper.c:604:0	sentinel/loop/index
core/victim_helper/victim_helper.c:606:0	sentinel/loop/index
core/victim_helper/victim_helper.c:607:0	sentinel/loop/index
core/victim_helper/victim_helper.c:608:0	sentinel/loop/index
core/victim_helper/victim_helper.c:609:0	sentinel/loop/index
core/victim_helper/victim_helper.c:610:0	sentinel/loop/index
core/victim_helper/victim_helper.c:611:0	sentinel/loop/index
core/victim_helper/victim_helper.c:611:1	sentinel/loop/index
core/victim_helper/victim_helper.c:617:0	sentinel/loop/index
core/victim_helper/victim_helper.c:619:0	sentinel/loop/index
core/victim_helper/victim_helper.c:621:0	sentinel/loop/index
core/victim_helper/victim_helper.c:623:1	sentinel/loop/index
core/victim_helper/victim_helper.c:625:0	sentinel/loop/index
core/victim_helper/victim_helper.c:627:0	sentinel/loop/index
core/victim_helper/victim_helper.c:628:0	sentinel/loop/index
core/victim_helper/victim_helper.c:632:0	sentinel/loop/index
core/victim_helper/victim_helper.c:632:0	sentinel/loop/index
core/victim_helper/victim_helper.c:632:100	sentinel/loop/index
core/victim_helper/victim_helper.c:632:100	sentinel/loop/index
core/victim_helper/victim_helper.c:633:1	sentinel/loop/index
core/victim_helper/victim_helper.c:634:0	sentinel/loop/index
core/victim_helper/victim_helper.c:638:0	sentinel/loop/index
core/victim_helper/victim_helper.c:639:0	sentinel/loop/index
core/victim_helper/victim_helper.c:640:0	sentinel/loop/index
core/victim_helper/victim_helper.c:645:1	sentinel/loop/index
core/victim_helper/victim_helper.c:652:1	sentinel/loop/index
core/victim_helper/victim_helper.c:653:0	sentinel/loop/index
core/victim_helper/victim_helper.c:658:1	sentinel/loop/index
core/victim_helper/victim_helper.c:663:0	sentinel/loop/index
core/victim_helper/victim_helper.c:664:0	sentinel/loop/index
core/victim_helper/victim_helper.c:666:1	sentinel/loop/index
core/victim_helper/victim_helper.c:666:0	sentinel/loop/index
core/victim_helper/victim_helper.c:666:0	sentinel/loop/index
core/victim_helper/victim_helper.c:669:0	sentinel/loop/index
core/victim_helper/victim_helper.c:670:0	sentinel/loop/index
core/victim_helper/victim_helper.c:671:0	sentinel/loop/index
core/victim_helper/victim_helper.c:676:1	sentinel/loop/index
core/victim_helper/victim_helper.c:684:0	sentinel/loop/index
core/victim_helper/victim_helper.c:684:0	sentinel/loop/index
core/victim_helper/victim_helper.c:684:0	sentinel/loop/index
core/victim_helper/victim_helper.c:685:0	sentinel/loop/index
core/victim_helper/victim_helper.c:687:0	sentinel/loop/index
core/victim_helper/victim_helper.c:688:0	sentinel/loop/index
core/victim_helper/victim_helper.c:694:0	sentinel/loop/index
core/victim_helper/victim_helper.c:695:0	sentinel/loop/index
core/victim_helper/victim_helper.c:697:0	sentinel/loop/index
core/victim_helper/victim_helper.c:705:24	abi-alignment
core/victim_helper/victim_helper.c:707:0	sentinel/loop/index
core/victim_helper/victim_helper.c:708:4	sentinel/loop/index
core/victim_helper/victim_helper.c:709:12	sentinel/loop/index
core/victim_helper/victim_helper.c:709:0	sentinel/loop/index
core/victim_helper/victim_helper.c:709:1	sentinel/loop/index
core/victim_helper/victim_helper.c:709:0	sentinel/loop/index
core/victim_helper/victim_helper.c:710:16	bit-width/shift
core/victim_helper/victim_helper.c:710:0	sentinel/loop/index
core/victim_helper/victim_helper.c:710:1	sentinel/loop/index
core/victim_helper/victim_helper.c:710:0	sentinel/loop/index
core/victim_helper/victim_helper.c:711:20	sentinel/loop/index
core/victim_helper/victim_helper.c:711:0	sentinel/loop/index
core/victim_helper/victim_helper.c:724:0	sentinel/loop/index
core/victim_helper/victim_helper.c:725:0	sentinel/loop/index
core/victim_helper/victim_helper.c:726:0	sentinel/loop/index
core/victim_helper/victim_helper.c:728:0	sentinel/loop/index
core/victim_helper/victim_helper.c:728:0	sentinel/loop/index
core/victim_helper/victim_helper.c:739:8	bit-width/shift
core/victim_helper/victim_helper.c:741:4	sentinel/loop/index
core/victim_helper/victim_helper.c:743:0	sentinel/loop/index
core/victim_helper/victim_helper.c:744:0	sentinel/loop/index
core/victim_helper/victim_helper.c:762:1	sentinel/loop/index
core/victim_helper/victim_helper.c:771:0	sentinel/loop/index
core/victim_helper/victim_helper.c:772:1	sentinel/loop/index
core/victim_helper/victim_helper.c:775:2	sentinel/loop/index
core/victim_helper/victim_helper.c:777:0	sentinel/loop/index
core/victim_helper/victim_helper.c:785:1	sentinel/loop/index
core/victim_helper/victim_helper.c:786:1	sentinel/loop/index
core/victim_helper/victim_helper.c:797:0	sentinel/loop/index
core/victim_helper/victim_helper.c:801:0	sentinel/loop/index
core/victim_helper/victim_helper.c:803:0	sentinel/loop/index
core/victim_helper/victim_helper.c:804:2	sentinel/loop/index
core/victim_helper/victim_helper.c:806:0	sentinel/loop/index
core/victim_helper/victim_helper.c:808:0	sentinel/loop/index
core/victim_helper/victim_helper.c:811:0	sentinel/loop/index
core/victim_helper/victim_helper.c:814:0	sentinel/loop/index
core/victim_helper/victim_helper.c:832:0	sentinel/loop/index
core/victim_helper/victim_helper.c:835:3	sentinel/loop/index
core/victim_helper/victim_helper.c:845:0	sentinel/loop/index
core/victim_helper/victim_helper.c:845:1	sentinel/loop/index
core/victim_helper/victim_helper.c:846:0	sentinel/loop/index
core/victim_helper/victim_helper.c:846:1	sentinel/loop/index
core/victim_protocol.c:64:1000	sentinel/loop/index
core/victim_protocol.c:64:1000	sentinel/loop/index
core/victim_protocol.c:64:1000	sentinel/loop/index
core/victim_protocol.c:73:1000	sentinel/loop/index
core/victim_protocol.c:73:1000	sentinel/loop/index
core/victim_protocol.c:74:1000	sentinel/loop/index
core/victim_protocol.c:74:1000	sentinel/loop/index
core/victim_protocol.c:74:1000	sentinel/loop/index
core/victim_protocol.c:98:0	sentinel/loop/index
core/victim_protocol.c:98:0	sentinel/loop/index
core/victim_protocol.c:100:0	sentinel/loop/index
core/victim_protocol.c:104:1	sentinel/loop/index
core/victim_protocol.c:105:0	sentinel/loop/index
core/victim_protocol.c:107:-1	sentinel/loop/index
core/victim_protocol.c:109:0	sentinel/loop/index
core/victim_protocol.c:109:0	sentinel/loop/index
core/victim_protocol.c:110:1	sentinel/loop/index
core/victim_protocol.c:125:0	sentinel/loop/index
core/victim_protocol.c:128:0	sentinel/loop/index
core/victim_protocol.c:129:0	sentinel/loop/index
core/victim_protocol.c:131:0	sentinel/loop/index
core/victim_protocol.c:132:0	sentinel/loop/index
core/victim_protocol.c:150:0	sentinel/loop/index
core/victim_protocol.c:153:0	sentinel/loop/index
core/victim_protocol.c:154:0	sentinel/loop/index
core/victim_protocol.c:174:0	sentinel/loop/index
core/victim_protocol.c:177:0	sentinel/loop/index
core/victim_protocol.c:178:0	sentinel/loop/index
core/victim_protocol.c:180:0	sentinel/loop/index
core/victim_protocol.c:196:0	sentinel/loop/index
core/victim_protocol.c:196:1	sentinel/loop/index
core/victim_protocol.c:196:8	bit-width/shift
core/victim_protocol.c:197:2	sentinel/loop/index
core/victim_protocol.c:197:16	bit-width/shift
core/victim_protocol.c:197:3	sentinel/loop/index
core/victim_protocol.c:197:24	abi-alignment
core/victim_protocol.c:206:0	sentinel/loop/index
core/victim_protocol.c:207:1	sentinel/loop/index
core/victim_protocol.c:207:8	bit-width/shift
core/victim_protocol.c:208:2	sentinel/loop/index
core/victim_protocol.c:208:16	bit-width/shift
core/victim_protocol.c:209:3	sentinel/loop/index
core/victim_protocol.c:209:24	abi-alignment
core/victim_protocol.c:221:0	sentinel/loop/index
core/victim_protocol.c:226:0	sentinel/loop/index
core/victim_protocol.c:242:0	sentinel/loop/index
core/victim_protocol.c:244:0	sentinel/loop/index
core/victim_protocol.c:252:1	sentinel/loop/index
core/victim_protocol.c:258:1	sentinel/loop/index
core/victim_protocol.c:268:0	sentinel/loop/index
core/victim_protocol.c:276:0	sentinel/loop/index
core/victim_protocol.c:282:0	sentinel/loop/index
core/victim_protocol.c:283:0	sentinel/loop/index
core/victim_protocol.c:289:0	sentinel/loop/index
core/victim_protocol.c:289:0	sentinel/loop/index
core/victim_protocol.c:290:0	sentinel/loop/index
core/victim_protocol.c:292:0	sentinel/loop/index
core/victim_protocol.c:292:8	bit-width/shift
core/victim_protocol.c:293:1	sentinel/loop/index
core/victim_protocol.c:294:1	sentinel/loop/index
core/victim_protocol.c:294:0xEDB88320	sentinel/loop/index
core/victim_protocol.c:302:0	sentinel/loop/index
core/victim_protocol.c:308:0	sentinel/loop/index
core/victim_protocol.c:312:0	sentinel/loop/index
core/victim_protocol.c:319:0	sentinel/loop/index
core/victim_protocol.c:324:0	sentinel/loop/index
core/victim_protocol.c:336:0	sentinel/loop/index
core/victim_protocol.c:359:0	sentinel/loop/index
core/victim_protocol.c:360:0	sentinel/loop/index
core/victim_protocol.c:363:0	sentinel/loop/index
core/victim_protocol.c:364:0	sentinel/loop/index
core/victim_protocol.c:365:0	sentinel/loop/index
core/victim_protocol.c:369:0	sentinel/loop/index
core/victim_protocol.c:371:2	sentinel/loop/index
core/victim_protocol.c:371:-1	sentinel/loop/index
core/victim_protocol.c:371:-1	sentinel/loop/index
core/victim_protocol.c:374:0	sentinel/loop/index
core/victim_protocol.c:374:0	sentinel/loop/index
core/victim_protocol.c:376:0	sentinel/loop/index
core/victim_protocol.c:380:0	sentinel/loop/index
core/victim_protocol.c:382:0	sentinel/loop/index
core/victim_protocol.c:383:1	sentinel/loop/index
core/victim_protocol.c:384:0	sentinel/loop/index
core/victim_protocol.c:386:0	sentinel/loop/index
core/victim_protocol.c:389:0	sentinel/loop/index
core/victim_protocol.c:390:1	sentinel/loop/index
core/victim_protocol.c:391:0	sentinel/loop/index
core/victim_protocol.c:395:0	sentinel/loop/index
core/victim_protocol.c:396:0	sentinel/loop/index
core/victim_protocol.c:398:0	sentinel/loop/index
core/victim_protocol.c:399:1	sentinel/loop/index
core/victim_protocol.c:401:0	sentinel/loop/index
core/victim_protocol.c:401:1	sentinel/loop/index
core/victim_protocol.c:403:0	sentinel/loop/index
core/victim_protocol.c:413:0	sentinel/loop/index
core/victim_protocol.c:413:1	sentinel/loop/index
core/victim_protocol.c:414:1	sentinel/loop/index
core/victim_protocol.c:416:0	sentinel/loop/index
core/victim_protocol.c:416:0	sentinel/loop/index
core/victim_protocol.c:417:0	sentinel/loop/index
core/victim_protocol.c:419:0	sentinel/loop/index
core/victim_protocol.c:422:0	sentinel/loop/index
core/victim_protocol.c:425:0	sentinel/loop/index
core/victim_protocol.c:427:0	sentinel/loop/index
core/victim_protocol.c:431:0	sentinel/loop/index
core/victim_protocol.c:432:0	sentinel/loop/index
core/victim_protocol.c:438:0	sentinel/loop/index
core/victim_protocol.c:439:0	sentinel/loop/index
core/victim_protocol.c:440:0	sentinel/loop/index
core/victim_protocol.c:443:0	sentinel/loop/index
core/victim_protocol.c:444:1	sentinel/loop/index
core/victim_protocol.c:445:0	sentinel/loop/index
core/victim_protocol.c:448:0	sentinel/loop/index
core/victim_protocol.c:450:1	sentinel/loop/index
core/victim_protocol.c:453:0	sentinel/loop/index
core/victim_protocol.c:455:1	sentinel/loop/index
core/victim_protocol.c:457:0	sentinel/loop/index
core/victim_protocol.c:459:1	sentinel/loop/index
core/victim_protocol.c:462:0	sentinel/loop/index
core/victim_protocol.c:463:-1	sentinel/loop/index
core/victim_protocol.c:472:0	sentinel/loop/index
core/victim_protocol.c:472:0	sentinel/loop/index
core/victim_protocol.c:484:0	sentinel/loop/index
core/victim_protocol.c:485:0	sentinel/loop/index
core/victim_protocol.c:514:0	sentinel/loop/index
core/victim_protocol.c:514:0	sentinel/loop/index
core/victim_protocol.c:521:0	sentinel/loop/index
core/victim_protocol.c:526:0	sentinel/loop/index
core/victim_protocol.c:530:0	sentinel/loop/index
core/victim_protocol.c:557:128	abi-alignment
core/victim_protocol.c:558:-1	sentinel/loop/index
core/victim_protocol.c:570:0	sentinel/loop/index
core/victim_protocol.c:574:0	sentinel/loop/index
core/victim_protocol.c:576:0	sentinel/loop/index
core/victim_protocol.c:579:1	sentinel/loop/index
core/victim_protocol.c:584:0	sentinel/loop/index
core/victim_protocol.c:587:1	sentinel/loop/index
core/victim_protocol.c:594:1000	sentinel/loop/index
core/victim_protocol.c:601:0	sentinel/loop/index
core/victim_protocol.c:610:0	sentinel/loop/index
core/victim_protocol.c:610:0	sentinel/loop/index
core/victim_protocol.c:611:0	sentinel/loop/index
core/victim_protocol.c:615:2000	sentinel/loop/index
core/victim_protocol.c:628:0	sentinel/loop/index
core/victim_protocol.c:630:-1	sentinel/loop/index
core/victim_protocol.c:632:0	sentinel/loop/index
core/victim_protocol.c:634:-1	sentinel/loop/index
core/victim_protocol.c:650:0	sentinel/loop/index
core/victim_protocol.c:650:0	sentinel/loop/index
core/victim_protocol.h:39:1	sentinel/loop/index
core/victim_protocol.h:42:3	sentinel/loop/index
core/victim_protocol.h:45:4	sentinel/loop/index
core/victim_protocol.h:59:2	sentinel/loop/index
core/victim_protocol.h:64:1	sentinel/loop/index
core/victim_protocol.h:64:12	sentinel/loop/index
core/victim_protocol.h:67:4	sentinel/loop/index
core/victim_protocol.h:70:4	sentinel/loop/index
core/victim_protocol.h:73:4	sentinel/loop/index
core/victim_protocol.h:84:16	bit-width/shift
core/victim_protocol.h:87:0x31524156	sentinel/loop/index
core/victim_protocol.h:90:1	sentinel/loop/index
core/victim_protocol.h:93:1	sentinel/loop/index
core/victim_protocol.h:96:1	sentinel/loop/index
core/victim_protocol.h:99:2	sentinel/loop/index
core/victim_protocol.h:103:1	sentinel/loop/index
core/victim_protocol.h:104:2	sentinel/loop/index
core/victim_protocol.h:105:3	sentinel/loop/index
core/victim_protocol.h:106:4	sentinel/loop/index
core/victim_protocol.h:107:5	sentinel/loop/index
core/victim_protocol.h:112:0	sentinel/loop/index
core/victim_protocol.h:113:1	sentinel/loop/index
core/victim_protocol.h:118:1	sentinel/loop/index
core/victim_protocol.h:119:2	sentinel/loop/index
core/victim_protocol.h:120:3	sentinel/loop/index
core/victim_protocol.h:121:4	sentinel/loop/index
core/victim_protocol.h:122:5	sentinel/loop/index
core/victim_protocol.h:123:6	sentinel/loop/index
core/victim_protocol.h:124:7	sentinel/loop/index
core/victim_protocol.h:125:8	bit-width/shift
core/victim_protocol.h:129:512	sentinel/loop/index
core/victim_protocol.h:132:512	sentinel/loop/index
core/victim_protocol.h:135:16	bit-width/shift
core/victim_protocol.h:142:0	sentinel/loop/index
core/victim_protocol.h:145:-1001	sentinel/loop/index
core/victim_protocol.h:148:-1002	sentinel/loop/index
core/victim_protocol.h:151:-1003	sentinel/loop/index
core/victim_protocol.h:154:-1004	sentinel/loop/index
core/victim_protocol.h:157:-1005	sentinel/loop/index
jni43499.c:37:-2	sentinel/loop/index
jni43499.c:39:-2	sentinel/loop/index
jni43499.c:40:-1	sentinel/loop/index
jni43499.c:65:1	sentinel/loop/index
jni43499.c:65:12	sentinel/loop/index
jni43499.c:78:0	sentinel/loop/index
jni43499.c:81:0	sentinel/loop/index
jni43499.c:86:0	sentinel/loop/index
jni43499.c:90:1	sentinel/loop/index
jni43499.c:90:0	sentinel/loop/index
jni43499.c:117:0	sentinel/loop/index
jni43499.c:118:0	sentinel/loop/index
jni43499.c:142:0	sentinel/loop/index
jni43499.c:152:0	sentinel/loop/index
jni43499.c:161:0	sentinel/loop/index
jni43499.c:178:0	sentinel/loop/index
jni43499.c:187:0	sentinel/loop/index
jni43499.c:196:0	sentinel/loop/index
jni43499.c:197:0	sentinel/loop/index
jni43499.c:223:0	sentinel/loop/index
jni43499.c:256:0	sentinel/loop/index
jni43499.c:257:0	sentinel/loop/index
jni43499.c:267:0	sentinel/loop/index
jni43499.c:268:0	sentinel/loop/index
jni43499.c:275:0	sentinel/loop/index
jni43499.c:290:0	sentinel/loop/index
jni43499.c:292:0	sentinel/loop/index
jni43499.c:294:0	sentinel/loop/index
jni43499.c:296:0	sentinel/loop/index
jni43499.c:298:1	sentinel/loop/index
jni43499.c:301:0	sentinel/loop/index
jni43499.c:307:1	sentinel/loop/index
jni43499.c:325:0	sentinel/loop/index
jni43499.c:327:0	sentinel/loop/index
jni43499.c:328:1	sentinel/loop/index
jni43499.c:330:0	sentinel/loop/index
jni43499.c:330:0	sentinel/loop/index
jni43499.c:331:1	sentinel/loop/index
jni43499.c:332:0	sentinel/loop/index
jni43499.c:333:0	sentinel/loop/index
jni43499.c:339:0	sentinel/loop/index
jni43499.c:341:0	sentinel/loop/index
jni43499.c:347:1	sentinel/loop/index
jni43499.c:357:0	sentinel/loop/index
jni43499.c:364:0	sentinel/loop/index
jni43499.c:394:0	sentinel/loop/index
jni43499.c:395:0	sentinel/loop/index
jni43499.c:408:0	sentinel/loop/index
jni43499.c:425:0	sentinel/loop/index
jni43499.c:426:0	sentinel/loop/index
jni43499.c:426:1	sentinel/loop/index
jni43499.c:427:0	sentinel/loop/index
jni43499.c:428:0	sentinel/loop/index
jni43499.c:443:0	sentinel/loop/index
jni43499.c:444:1	sentinel/loop/index
jni43499.c:445:2	sentinel/loop/index
jni43499.c:451:0	sentinel/loop/index
jni43499.c:454:0	sentinel/loop/index
jni43499.c:459:0	sentinel/loop/index
jni43499.c:461:0	sentinel/loop/index
jni43499.c:474:0	sentinel/loop/index
jni43499.c:474:0	sentinel/loop/index
jni43499.c:481:0	sentinel/loop/index
jni43499.c:527:0	sentinel/loop/index
jni43499.c:552:0	sentinel/loop/index
jni43499.c:554:0	sentinel/loop/index
jni43499.c:555:0	sentinel/loop/index
jni43499.c:556:0	sentinel/loop/index
jni43499.c:578:0	sentinel/loop/index
jni43499.c:583:1	sentinel/loop/index
jni43499.c:602:0	sentinel/loop/index
jni43499.c:604:0	sentinel/loop/index
jni43499.c:606:0	sentinel/loop/index
jni43499.c:610:0	sentinel/loop/index
jni43499.c:624:0	sentinel/loop/index
jni43499.c:632:0	sentinel/loop/index
jni43499.c:633:0	sentinel/loop/index
jni43499.c:655:0	sentinel/loop/index
jni43499.c:658:0	sentinel/loop/index
jni43499.c:661:1	sentinel/loop/index
jni43499.c:724:0	sentinel/loop/index
jni43499.c:893:0	sentinel/loop/index
jni43499.c:901:0	sentinel/loop/index
jni43499.c:915:0	sentinel/loop/index
jni43499.c:922:1	sentinel/loop/index
jni43499.c:930:0	sentinel/loop/index
jni43499.c:939:0	sentinel/loop/index
jni43499.c:941:0	sentinel/loop/index
jni43499.c:971:0	sentinel/loop/index
jni43499.c:992:0	sentinel/loop/index
jni43499.c:1001:0	sentinel/loop/index
jni43499.c:1026:0	sentinel/loop/index
jni43499.c:1046:0	sentinel/loop/index
jni43499.c:1054:0	sentinel/loop/index
jni43499.c:1068:0	sentinel/loop/index
jni43499.c:1075:1	sentinel/loop/index
jni43499.c:1114:0	sentinel/loop/index
jni43499.c:1120:0	sentinel/loop/index
jni43499.c:1133:1	sentinel/loop/index
jni43499.c:1134:0	sentinel/loop/index
jni43499.c:1139:0	sentinel/loop/index
jni43499.c:1154:0	sentinel/loop/index
jni43499.c:1193:0	sentinel/loop/index
jni43499.c:1236:0	sentinel/loop/index
jni43499.c:1237:0	sentinel/loop/index
jni43499.c:1238:0	sentinel/loop/index
jni43499.c:1239:0	sentinel/loop/index
jni43499.c:1240:0	sentinel/loop/index
jni43499.c:1241:0	sentinel/loop/index
jni43499.c:1242:0	sentinel/loop/index
jni43499.c:1243:0	sentinel/loop/index
jni43499.c:1244:0	sentinel/loop/index
jni43499.c:1247:0	sentinel/loop/index
jni43499.c:1255:0	sentinel/loop/index
jni43499.c:1257:0	sentinel/loop/index
jni43499.c:1258:0	sentinel/loop/index
jni43499.c:1260:0	sentinel/loop/index
jni43499.c:1268:0	sentinel/loop/index
jni43499.c:1270:0	sentinel/loop/index
jni43499.c:1276:0	sentinel/loop/index
jni43499.c:1278:0	sentinel/loop/index
jni43499.c:1279:0	sentinel/loop/index
jni43499.c:1281:0	sentinel/loop/index
jni43499.c:1289:0	sentinel/loop/index
jni43499.c:1289:0	sentinel/loop/index
jni43499.c:1329:0	sentinel/loop/index
jni43499.c:1330:0	sentinel/loop/index
jni43499.c:1337:0	sentinel/loop/index
jni43499.c:1349:0	sentinel/loop/index
jni43499.c:1352:0	sentinel/loop/index
jni43499.c:1355:0	sentinel/loop/index
jni43499.c:1363:0	sentinel/loop/index
jni43499.c:1371:0	sentinel/loop/index
jni43499.c:1376:0	sentinel/loop/index
jni43499.c:1379:0	sentinel/loop/index
jni43499.c:1383:0	sentinel/loop/index
jni43499.c:1384:0	sentinel/loop/index
jni43499.c:1396:0	sentinel/loop/index
jni43499.c:1396:0	sentinel/loop/index
jni43499.c:1411:0	sentinel/loop/index
jni43499.c:1428:0	sentinel/loop/index
jni43499.c:1428:0	sentinel/loop/index
jni43499.c:1441:0	sentinel/loop/index
jni43499.c:1453:0	sentinel/loop/index
jni43499.c:1470:0	sentinel/loop/index
jni43499.c:1470:0	sentinel/loop/index
jni43499.c:1470:0	sentinel/loop/index
jni43499.c:1485:0	sentinel/loop/index
jni43499.c:1502:0	sentinel/loop/index
jni43499.c:1503:0	sentinel/loop/index
jni43499.c:1514:0	sentinel/loop/index
jni43499.c:1514:0	sentinel/loop/index
jni43499.c:1518:-1	sentinel/loop/index
jni43499.c:1529:0	sentinel/loop/index
jni43499.c:1530:-1	sentinel/loop/index
jni43499.c:1541:0	sentinel/loop/index
jni43499.c:1549:0	sentinel/loop/index
jni43499.c:1562:1	sentinel/loop/index
jni43499.c:1563:1	sentinel/loop/index
jni43499.c:1564:1	sentinel/loop/index
jni43499.c:1588:32	abi-alignment
jni43499.c:1597:0	sentinel/loop/index
jni43499.c:1611:0	sentinel/loop/index
jni43499.c:1611:0	sentinel/loop/index
jni43499.c:1612:0	sentinel/loop/index
jni43499.c:1613:0	sentinel/loop/index
jni43499.c:1614:0	sentinel/loop/index
jni43499.c:1615:0	sentinel/loop/index
"""

TOKEN_RE = re.compile(
    r"(?<![\w.])(-?(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d+)?(?:[eE][-+]?\d+)?))([uUlLfF]*)"
)


@dataclass(frozen=True)
class Occurrence:
    path: str
    line: int
    col: int
    token: str
    suffix: str

    @property
    def key(self) -> str:
        return f"{self.path}:{self.line}:{self.token}"


def strip_comments_and_strings(text: str) -> str:
    """把注释/字符串/字符字面量替换为等长空白（保留行号/列号）。"""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n - 1 and not (text[i] == "*" and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                out.append(" ")
                i += 2
        elif c in ('"', "'"):
            quote = c
            out.append(" ")
            i += 1
            while i < n:
                ch = text[i]
                if ch == "\\" and i + 1 < n:
                    out.append("\n" if text[i + 1] == "\n" else " ")
                    out.append(" ")
                    i += 2
                    continue
                out.append("\n" if ch == "\n" else " ")
                i += 1
                if ch == quote:
                    break
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan_file(relpath: str, text: str) -> list[Occurrence]:
    stripped = strip_comments_and_strings(text)
    result: list[Occurrence] = []
    for lineno, line in enumerate(stripped.splitlines(), start=1):
        for match in TOKEN_RE.finditer(line):
            result.append(
                Occurrence(relpath, lineno, match.start(1) + 1, match.group(1), match.group(2))
            )
    return result


def walk_sources(root: str) -> list[str]:
    """扫描范围=生产参数化路径：root/core/** 与 root/jni43499.c（不含 legacy/）。"""
    files: list[str] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in {"legacy", "very_legacy"})
        for name in sorted(filenames):
            if not name.endswith((".c", ".h", ".cc", ".cpp", ".hpp")):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            if rel.startswith("core/") or rel == "jni43499.c":
                files.append(rel)
    return sorted(files)


def classify_reason(occ: Occurrence) -> str:
    """启发式受控类别（仅 --emit-allowlist 用；冻结后以 TSV 逐 token 为准）。

    只输出 CATEGORY_ALLOWLIST 内的类别；ABI sizeof 断言/位宽/单位换算分别归入
    abi-alignment / bit-width/shift / system-header-derived，其余数字默认
    sentinel/loop/index；无法安全归类的一律 numeric-literal（重生成后须人工复核）。
    """
    token = occ.token.lower()
    try:
        value = int(token, 16) if "0x" in token else int(float(token))
    except ValueError:
        return "numeric-literal"
    abi_sizes = {24, 32, 40, 48, 56, 72, 80, 88, 96, 104, 112, 120, 128, 136, 160, 176, 192, 216}
    if occ.path == "core/result.h" and 5 <= value <= 10:
        return "numeric-literal"  # 公开状态/效果枚举值
    if occ.path == "core/placement.c" and value == 64 and "path" in token:
        return "numeric-literal"
    if occ.path == "core/core_internal.h" and value in (1000, 1000000000, 1024):
        return "system-header-derived"  # timespec ns 换算 / CPU_SETSIZE 镜像
    if occ.path == "core/carrier_pselect.c" and value == 8:
        return "abi-alignment"  # 备份表 8B 头部/每项 8B
    if occ.path == "core/abi.h" and value == 8:
        return "abi-alignment"  # abi_header sizeof
    if value in abi_sizes:
        return "abi-alignment"  # _Static_assert(sizeof)==ABI 矩阵值
    if value in (8, 16, 31, 63, 64):
        return "bit-width/shift"  # 位移位宽 / nibble 域 / slab_order 域
    return "sentinel/loop/index"


def load_allowlist() -> dict[str, str]:
    allowed: dict[str, str] = {}
    for raw in ALLOWLIST_TSV.splitlines():
        line = raw.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            continue
        key, reason = line.split("\t", 1)
        allowed[key] = reason
    return allowed


def allowlist_sha256() -> str:
    return hashlib.sha256(ALLOWLIST_TSV.encode("utf-8")).hexdigest()


def target_definition_lines(root: str) -> set[tuple[str, int]]:
    """收集 PHYS_/STRUCT_/WIRE_ 宏定义行（A 决策后必须 0 处；出现即 target）。"""
    lines: set[tuple[str, int]] = set()
    define_re = re.compile(r"^\s*#\s*define\s+GLK43499_(PHYS|STRUCT|WIRE)_")
    for rel in walk_sources(root):
        stripped = strip_comments_and_strings(
            open(os.path.join(root, rel), encoding="utf-8", errors="replace").read())
        for index, line in enumerate(stripped.splitlines(), start=1):
            if define_re.match(line):
                lines.add((rel, index))
    return lines


def is_target_definition(occ: Occurrence, definitions: set[tuple[str, int]]) -> bool:
    """该 token 是否落在 PHYS_/STRUCT_/WIRE_ 宏定义行（A 决策后必须删除）。"""
    return (occ.path, occ.line) in definitions


def forbidden_target(occ: Occurrence) -> str | None:
    for pattern, label in TARGET_TOKEN_PATTERNS:
        if pattern.search(occ.token.lower()):
            return label
    return None


def residue_report(root: str) -> dict[str, int]:
    """声明级残留计数（先剥离注释/字符串）。"""
    counts = {"GLK43499_PHYS_": 0, "GLK43499_STRUCT_": 0, "GLK43499_WIRE_": 0, "reserved": 0}
    define_re = re.compile(r"^\s*#\s*define\s+GLK43499_(PHYS|STRUCT|WIRE)_", re.MULTILINE)
    reserved_re = re.compile(r"(?:uint(?:8|32|64)_t|int32_t|glk43499_[a-z0-9_]+)\s+reserved(?:32|_resource_serial)?(?:\[[0-9]+\])?\s*;")
    for rel in walk_sources(root):
        text = strip_comments_and_strings(
            open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()
        )
        for m in define_re.finditer(text):
            counts["GLK43499_" + m.group(1) + "_"] += 1
        counts["reserved"] += len(reserved_re.findall(text))
    return counts


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="43499 token-level native literal scanner")
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--emit-allowlist", action="store_true",
                        help="打印 file:line:token<TAB>理由 的待冻结清单")
    parser.add_argument("--gate", choices=("r1", "r2"), default="r1",
                        help="r1=当前半成品 gate（8 常量待删）；r2=R2 目标 gate（必须 0 残留/0 target）")
    parser.add_argument("--signature", action="store_true", help="打印 allowlist sha256 并退出")
    args = parser.parse_args(argv)
    root = args.root
    files = walk_sources(root)
    occurrences: list[Occurrence] = []
    for rel in files:
        path = os.path.join(root, rel)
        occurrences.extend(scan_file(rel, open(path, encoding="utf-8", errors="replace").read()))

    if args.emit_allowlist:
        for occ in occurrences:
            print(f"{occ.key}\t{classify_reason(occ)}")
        print(f"# emitted={len(occurrences)} files={len(files)}", file=sys.stderr)
        return 0

    if args.signature:
        print(f"allowlist_sha256={allowlist_sha256()}")
        print(f"script_sha256={hashlib.sha256(open(__file__, 'rb').read()).hexdigest()}")
        return 0

    current_sha = allowlist_sha256()
    if ALLOWLIST_SHA256 != "SIGNATURE_PLACEHOLDER" and current_sha != ALLOWLIST_SHA256:
        print(f"allowlist signature mismatch: recorded={ALLOWLIST_SHA256} actual={current_sha}")
        return 2

    allowed = load_allowlist()
    for key, reason in allowed.items():
        if reason not in CATEGORY_ALLOWLIST:
            print(f"uncontrolled allowlist category: {key} -> {reason}")
            return 2

    definitions = target_definition_lines(root)
    unclassified: list[Occurrence] = []
    target_tokens: list[str] = []
    magic_escapes: list[str] = []
    for occ in occurrences:
        if occ.token.lower() in BANNED_MAGIC_TOKENS:
            magic_escapes.append(f"{occ.key} (R2 magic literal must come from request)")
        if occ.key in allowed:
            continue
        bad = forbidden_target(occ)
        if bad is not None or is_target_definition(occ, definitions):
            target_tokens.append(f"{occ.key} ({bad or 'A: STRUCT_/WIRE_ constant must be parameterized'})")
        else:
            unclassified.append(occ)
    residue = residue_report(root)
    print(
        f"scan[{args.gate}]: root={root} files={len(files)} literals={len(occurrences)} "
        f"allowed={len(occurrences) - len(unclassified) - len(target_tokens)} "
        f"unclassified={len(unclassified)} target_tokens={len(target_tokens)}"
    )
    print(f"allowlist_sha256={current_sha}")
    for need, count in residue.items():
        print(f"residue[{args.gate}]: {need} count={count}")
    for item in target_tokens[:40]:
        print(f"  target: {item}")
    for item in magic_escapes[:40]:
        print(f"  magic: {item}")
    if unclassified:
        print("unclassified tokens (file:line:token):")
        for occ in unclassified[:200]:
            print(f"  {occ.key}")
        return 1
    if args.gate == "r2":
        if target_tokens or magic_escapes or any(v != 0 for v in residue.values()):
            print("R2 target gate: FAIL (A 决策要求 8 常量/reserved 全部删除且参数化)")
            return 1
        print("R2 target gate: CLOSED (unclassified=0, target=0, residue=0)")
        return 0
    print(f"R1 gate: CLOSED except {len(target_tokens)} target tokens pending R2 removal "
          f"(residue 供 R2 gate 归零；R1 不要求归零)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
