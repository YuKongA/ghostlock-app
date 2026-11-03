#!/usr/bin/env python3
from pathlib import Path
import os, shutil, sys
args=sys.argv[1:]
state=Path(os.environ['ADB_STATE']); state.mkdir(parents=True,exist_ok=True)
app=state/'base.apk'; test=state/'test-installed'
op=args[2:] if len(args)>=3 and args[0]=='-s' else args
if len(op)>=3 and op[:2]==['install','-r']:
 shutil.copyfile(op[2], app); print('Success'); raise SystemExit(0)
if len(op)>=2 and op[0]=='uninstall' and op[1]=='com.ghostlock.app.test':
 test.unlink(missing_ok=True); print('Success'); raise SystemExit(0)
if len(op)>=4 and op[:2]==['shell','pm'] and op[2]=='path':
 print('package:/fake/data/app/com.ghostlock.app/base.apk'); raise SystemExit(0)
if len(op)>=3 and op[:2]==['exec-out','cat']:
 sys.stdout.buffer.write(app.read_bytes()); raise SystemExit(0)
print('FAKE_ADB_UNHANDLED',args,file=sys.stderr); raise SystemExit(2)
