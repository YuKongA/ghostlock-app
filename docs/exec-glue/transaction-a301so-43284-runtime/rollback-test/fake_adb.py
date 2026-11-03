#!/usr/bin/env python3
import pathlib, shutil, sys
root=pathlib.Path(__file__).resolve().parent
installed=root/'device-installed.apk'
testapk=root/'test-installed.apk'
a=sys.argv[1:]
if len(a)>=5 and a[2:4]==['install','-r']:
    shutil.copyfile(a[4],installed)
elif len(a)>=4 and a[2]=='uninstall' and a[3]=='com.ghostlock.app.test':
    testapk.unlink(missing_ok=True)
elif len(a)>=5 and a[2:5]==['shell','pm','path']:
    print('package:/data/app/mock/com.ghostlock.app/base.apk')
elif len(a)>=5 and a[2:4]==['exec-out','cat']:
    sys.stdout.buffer.write(installed.read_bytes())
else:
    raise SystemExit('unexpected fake adb args: '+repr(a))
