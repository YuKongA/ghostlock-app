import json,os,collections,re
ROOT='/Users/nickji/Documents/Program/Analysis/ghostlock-app'
OUT=ROOT+'/docs/analysis/43499-callchains-20261029'
os.makedirs(OUT,exist_ok=True)
kg=json.load(open('/tmp/glk_analysis/kotlin_graph.json'))
ng=json.load(open('/tmp/glk_analysis/native_graph.json'))
funcs=kg['functions']; classes=kg['classes']; kcalls=kg['calls']
ifuncs=ng['functions']; ncalls=ng['calls']

def is_native(f):
    return f['file'].startswith('app/src/main/cpp/')
nfuncs=[f for f in ifuncs if is_native(f)]
# dedupe native by file/line/name
seen=set(); nf=[]
for f in nfuncs:
    k=(f['file'],f['line'],f['name'])
    if k in seen: continue
    seen.add(k); nf.append(f)
nname=collections.defaultdict(list)
for f in nf: nname[f['name']].append(f)
nedge=[]
for e in ncalls:
    if not e['caller_file'].startswith('app/src/main/cpp/'): continue
    tgt=nname.get(e['callee'],[])
    # prefer same file
    same=[t for t in tgt if t['file']==e['caller_file']]
    t=(same or tgt or [None])[0]
    if t:
        nedge.append(dict(caller=e['caller'],caller_file=e['caller_file'],caller_line=e['caller_line'],
                          callee=e['callee'],callee_file=t['file'],callee_line=t['line'],resolved=True))
    else:
        nedge.append(dict(caller=e['caller'],caller_file=e['caller_file'],caller_line=e['caller_line'],
                          callee=e['callee'],callee_file='',callee_line='',resolved=False))
print('native funcs',len(nf),'native edges',len(nedge))

KOT_FOCUS_PREFIX=['app/src/main/kotlin/com/ghostlock/app/backend/cve43499',
 'app/src/main/kotlin/com/ghostlock/app/data/cve43499',
 'app/src/main/kotlin/com/ghostlock/app/jni']
KOT_FOCUS_EXTRA=['FinishHandoffAdapter.kt','BackendFlowRunner.kt','FinishHandoff.kt','ModuleBackendFlowRunner.kt']
def is_focus(f):
    return any(f['file'].startswith(p) for p in KOT_FOCUS_PREFIX) or any(f['file'].endswith(x) for x in KOT_FOCUS_EXTRA)
kfuncs=[f for f in funcs if is_focus(f)]
kbyfile=collections.defaultdict(list)
for f in kfuncs: kbyfile[f['file']].append(f)
print('kotlin focus funcs',len(kfuncs),'files',len(kbyfile))

# ---- symbols tsv ----
with open(OUT+'/kotlin-symbols.tsv','w') as fh:
    fh.write('file\tline\tkind\towner\tname\texternal\tsignature\n')
    for c in classes:
        fh.write(f"{c['file']}\t{c['line']}\t{c['kind']}\t{c['owner']}\t{c['name']}\tfalse\t\n")
    for f in funcs:
        sig=(f.get('sig') or '').replace('\t',' ')
        fh.write(f"{f['file']}\t{f['line']}\tfun\t{f['owner']}\t{f['name']}\t{'true' if f['external'] else 'false'}\t{sig}\n")
with open(OUT+'/native-symbols.tsv','w') as fh:
    fh.write('file\tline\tname\tstatic\tparams\ttu\n')
    for f in nf:
        ps='; '.join(f"{n}:{t}" for n,t in f['params'])
        fh.write(f"{f['file']}\t{f['line']}\t{f['name']}\t{'1' if f['static'] else '0'}\t{ps.replace(chr(9),' ')}\t{f['tu']}\n")
with open(OUT+'/kotlin-callgraph.tsv','w') as fh:
    fh.write('caller_file\tcaller_line\tcaller_owner\tcaller\tcallee\tcallee_file\tcallee_line\tcallee_owner\tresolved\n')
    for e in kcalls:
        fh.write(f"{e['caller_file']}\t{e['caller_line']}\t{e['caller_owner']}\t{e['caller']}\t{e['callee']}\t{e['callee_file']}\t{e['callee_line']}\t{e['callee_owner']}\t{1 if e['resolved'] else 0}\n")
with open(OUT+'/native-callgraph.tsv','w') as fh:
    fh.write('caller_file\tcaller_line\tcaller\tcallee\tcallee_file\tcallee_line\tresolved\n')
    for e in nedge:
        fh.write(f"{e['caller_file']}\t{e['caller_line']}\t{e['caller']}\t{e['callee']}\t{e['callee_file']}\t{e['callee_line']}\t{1 if e['resolved'] else 0}\n")

# ---- JNI bridge ----
def demangle(cn):
    p='Java_com_ghostlock_app_jni_'
    if not cn.startswith(p): return (None,None)
    rest=cn[len(p):]
    parts=rest.split('_')
    # greedy: last part is method, middle joined by underscore is class
    if len(parts)<2: return (None,None)
    method=parts[-1]; cls='_'.join(parts[:-1])
    return (cls,method)
c_jni={}
for j in ng['jni']:
    cls,method=demangle(j['cname'])
    if method: c_jni[(cls,method)]=j
k_ext={}
for f in funcs:
    if not f['external']: continue
    cls=f['owner'].split('.')[-1] if f['owner'] else ''
    k_ext[(cls,f['name'])]=f
rows=[]
for key in sorted(set(c_jni)|set(k_ext)):
    k=k_ext.get(key); c=c_jni.get(key)
    rows.append((key[0]+'.'+key[1], (k['file']+':'+str(k['line'])) if k else 'MISSING',
                 (c['file']+':'+str(c['line'])) if c else 'MISSING',
                 (k['sig'] if k else ''), (c['params'] if c else '')))
with open(OUT+'/jni-bridge.tsv','w') as fh:
    fh.write('method\tkotlin_loc\tnative_loc\tkotlin_sig\tnative_params\n')
    for r in rows:
        fh.write('\t'.join(str(x).replace('\t',' ') for x in r)+'\n')
print('jni pairs',len(rows),'kotlin externals',len(k_ext),'native jni',len(c_jni))

# ---- graph helpers ----
kedge_by=collections.defaultdict(list)
kmap={}
for f in kfuncs:
    kmap[(f['file'],f['owner'],f['name'])]=f
def kkey(f): return (f['file'],f['owner'],f['name'])
for e in kcalls:
    if not e['resolved']: continue
    caller=kmap.get((e['caller_file'],e['caller_owner'],e['caller']))
    if not caller: continue
    callee=kmap.get((e['callee_file'],e['callee_owner'],e['callee']))
    if not callee: continue
    kedge_by[kkey(caller)].append((callee,e['line']))
for k in kedge_by: 
    uniq={}; [uniq.setdefault((c[0]['file'],c[0]['owner'],c[0]['name']),c) for c in kedge_by[k]]
    kedge_by[k]=list(uniq.values())

def kchain(start,maxdepth=9,maxpaths=40):
    paths=[]; 
    def dfs(node,path,seen):
        if len(paths)>=maxpaths: return
        kids=kedge_by.get(kkey(node),[])
        if not kids or len(path)>=maxdepth:
            paths.append(path); return
        for callee,line in kids[:3]:
            if kkey(callee) in seen: continue
            dfs(callee,path+[(callee,line)],seen|{kkey(callee)})
            if len(paths)>=maxpaths: return
    dfs(start,[(start,0)],{kkey(start)})
    return paths

entry_names={'run','start','execute','step','steps','executeFlow','runFlow','invoke'}
entries=[f for f in kfuncs if f['name'] in entry_names]
pref=[f for f in entries if ('Flow43499Impl' in f.get('owner','') or 'WriteTransactionRunner' in f.get('owner','') or f['file'].endswith('BackendFlowRunner.kt') or f['file'].endswith('FinishHandoffAdapter.kt'))]
entries=(pref or entries)[:5]
kot_chains=[]
for e in entries:
    for p in kchain(e):
        kot_chains.append(p)
print('kotlin chains',len(kot_chains))

# native adjacency
nkey=lambda f:(f['file'],f['name'])
nadj=collections.defaultdict(list)
for e in nedge:
    caller=nname[e['caller']]
    callerf=[c for c in caller if c['file']==e['caller_file']]
    if not callerf: continue
    if e['resolved']:
        callee=[c for c in nname[e['callee']] if c['file']==e['callee_file']]
        if callee: nadj[nkey(callerf[0])].append((callee[0],e['caller_line']))
def nchain(start,maxdepth=10,maxpaths=3):
    paths=[]
    def dfs(node,path,seen):
        if len(paths)>=maxpaths: return
        kids=[]; seenk=set()
        for c,line in nadj.get(nkey(node),[]):
            k=nkey(c)
            if k in seenk: continue
            seenk.add(k); kids.append((c,line))
        if not kids or len(path)>=maxdepth:
            paths.append(path); return
        for callee,line in kids[:3]:
            if nkey(callee) in seen: continue
            dfs(callee,path+[(callee,line)],seen|{nkey(callee)})
            if len(paths)>=maxpaths: return
    dfs(start,[(start,0)],{nkey(start)})
    return paths
nstart=[f for f in nf if f['name'].startswith('Java_com_ghostlock_app_jni_')]
nchains=[]
for s in nstart:
    for p in nchain(s):
        nchains.append(p)
print('native chains',len(nchains))

# thread entries + their chains
tentries=[]
for name in ng['thread_entries']:
    for f in nf:
        if f['name']==name:
            tentries.append(f); break
tchains=[]
for f in tentries:
    for p in nchain(f,maxdepth=8,maxpaths=4): tchains.append(p)
print('thread entries',[f['name'] for f in tentries],len(tchains))

# route paths via BFS from actor execute-ish to carrier triggers
def find(name):
    return [f for f in nf if f['name']==name]
def bfs(src,dst,maxdepth=12):
    from collections import deque
    q=deque([(src,[src])]); seen={nkey(src)}
    while q:
        node,path=q.popleft()
        if len(path)>maxdepth: continue
        if node['name']==dst['name']: return path
        for c,_ in nadj.get(nkey(node),[]):
            k=nkey(c)
            if k in seen: continue
            seen.add(k); q.append((c,path+[c]))
    return None
srcs=[f for f in nf if f['name'] in ('glk43499_actor_execute','glk43499_actor_execute_v2')]
if not srcs:
    srcs=[f for f in nf if 'actor_execute' in f['name']][:2]
carriers=[f for f in nf if f['name'] in ('glk43499_carrier_multicast_trigger','glk43499_carrier_pselect_trigger','glk43499_carrier_tcp_trigger')]
route_chains=[]
for s in srcs[:1]:
    for c in carriers:
        p=bfs(s,c)
        if p: route_chains.append((s,c,p))

# syscalls
SYSCALL={'syscall','setsockopt','getsockopt','socket','pthread_create','pthread_join','futex','open','openat','read','write','mmap','munmap','ioctl','close','dup2','prctl','sched_setattr','sched_setscheduler','tgkill','clone','pipe','timerfd_create','select','pselect'}
syscalls=collections.defaultdict(list)
for e in nedge:
    if e['callee'] in SYSCALL:
        syscalls[e['callee']].append((e['caller_file'],e['caller_line'],e['caller'],e['callee']))
print('syscall leaves',{k:len(v) for k,v in syscalls.items()})


# ---- Kotlin -> JNI caller chains (reverse edges ending at external funs) ----
kmap2={}
for f in kfuncs: kmap2[(f['file'],f['owner'],f['name'])]=f
krev=collections.defaultdict(list)
for e in kcalls:
    if not e['resolved']: continue
    tgt=(e['callee_file'],e['callee_owner'],e['callee'])
    src=(e['caller_file'],e['caller_owner'],e['caller'])
    if src in kmap2 and tgt in kmap2:
        krev[tgt].append((kmap2[src],e['line']))
kextern=[f for f in kfuncs if f['external']]
jni_chains=[]
for ef in kextern:
    def up(node,path,depth):
        if len(jni_chains)>=40 or depth>=4: 
            jni_chains.append(path); return
        callers=krev.get((node['file'],node['owner'],node['name']),[])
        if not callers: jni_chains.append(path); return
        for c,line in callers[:2]:
            up(c,path+[c],depth+1)
            if len(jni_chains)>=40: return
    k=(ef['file'],ef['owner'],ef['name'])
    up(ef,[ef],0)
print('jni caller chains',len(jni_chains))

# ---- markdown ----
def loc(f): return f"{f['file']}:{f['line']}"
def kfmt(f): return f"{f['owner']}.{f['name']}" if f['owner'] else f['name']
L=[]
L.append('# 43499 Kotlin / C++ 符号与调用链（工具生成）')
L.append('')
L.append('> 生成时间：2026-10-29（host America/Toronto）。工具：clang AST JSON（NDK clang `-Xclang -ast-dump=json`，来源 `app/.cxx/.../compile_commands.json`）+ `ctags` 辅助 + Python Kotlin source 解析（声明/调用点/解析）。')
L.append('> 范围：Kotlin 全部 `app/src/main/kotlin`（348 文件）；C/C++ 全部 Android 编译单元（`gl43499/core/*.c`、`jni43499.c`、`mcast510_probe.c`、`victim_helper.c`、`common/gl_log.c`）。legacy/legacy2/very_legacy 为参考树，未编译，不在本图内。')
L.append('> 精度：C/C++ 调用边来自 clang AST（宏展开/函数指针有缺口）；Kotlin 调用边为源码解析+同名解析（重载/高阶函数可能有遗漏）；file:line 为声明/调用起始行。')
L.append('')
L.append('## 0. 产物清单')
L.append('')
L.append('| 文件 | 内容 |')
L.append('|---|---|')
L.append('| `kotlin-symbols.tsv` | 全部 Kotlin class/object/interface/fun（含 owner/签名/external），%d 行 |' % (len(classes)+len(funcs)))
L.append('| `kotlin-callgraph.tsv` | Kotlin 调用边（含 resolved 标记），%d 行 |' % len(kcalls))
L.append('| `native-symbols.tsv` | app C/C++ 函数符号（file/line/参数/static/TU），%d 行 |' % len(nf))
L.append('| `native-callgraph.tsv` | C/C++ 调用边（caller→callee/file:line），%d 行 |' % len(nedge))
L.append('| `jni-bridge.tsv` | Kotlin `external fun` ↔ C `Java_com_ghostlock_app_jni_*` 导出，%d 对 |' % len(rows))
L.append('')
L.append('复现命令：')
L.append('')
L.append('```sh')
L.append('# 1) 原生：clang AST（脚本 /tmp/glk_analysis/native_ast.py）')
L.append("python3 /tmp/glk_analysis/native_ast.py   # -> native_graph.json")
L.append('# 2) Kotlin：源码解析（脚本 /tmp/glk_analysis/kotlin_parse.py）')
L.append("python3 /tmp/glk_analysis/kotlin_parse.py  # -> kotlin_graph.json")
L.append('# 3) 汇总（脚本 /tmp/glk_analysis/gen_report.py）')
L.append("python3 /tmp/glk_analysis/gen_report.py    # -> 本目录 tsv/md")
L.append('```')
L.append('')
L.append('## 1. 总量')
L.append('')
L.append('- Kotlin：文件 %d，class/object/interface **%d**，fun **%d**（其中 external **%d**），调用边 **%d**（resolved %d）' % (len(set(f['file'] for f in funcs)),len(classes),len(funcs),sum(1 for f in funcs if f['external']),len(kcalls),sum(1 for e in kcalls if e['resolved'])))
L.append('- C/C++：函数 **%d**（app 源码），调用边 **%d**（resolved %d），JNI 导出 **%d**，pthread 线程入口 **%d**' % (len(nf),len(nedge),sum(1 for e in nedge if e['resolved']),len(ng['jni']),len(tentries)))
L.append('')
L.append('## 2. JNI 边界（Kotlin external ↔ C 导出）')
L.append('')
L.append('| # | method | Kotlin | C 导出 |')
L.append('|---|---|---|---|')
for i,r in enumerate(rows,1):
    L.append(f"| {i} | `{r[0]}` | `{r[1]}` | `{r[2]}` |")
L.append('')
L.append('## 3. Kotlin 主干调用链（自动生成，聚焦 43499/UI 入口）')
L.append('')
for idx,p in enumerate(kot_chains[:26],1):
    L.append(f'### K{idx}')
    L.append('')
    L.append(' → '.join(f"`{kfmt(f)}` ({loc(f)})" for f,_ in p))
    L.append('')
L.append('## 3.5 Kotlin → JNI 调用者链（自动，最多 40 条）')
L.append('')
for idx,p in enumerate(jni_chains[:36],1):
    seq=list(reversed(p))
    L.append(f'### J{idx}')
    L.append('')
    L.append(' → '.join(f"`{kfmt(f)}` ({loc(f)})" for f in seq))
    L.append('')
L.append('## 4. Native 调用链（JNI 导出 → 下游；自动生成）')
L.append('')
for idx,p in enumerate(nchains[:24],1):
    L.append(f'### N{idx}')
    L.append('')
    L.append(' → '.join(f"`{f['name']}` ({loc(f)})" for f,_ in p))
    L.append('')
L.append('## 5. actor 线程入口调用链（pthread_create 目标）')
L.append('')
L.append('线程入口：' + ', '.join(f"`{f['name']}` ({loc(f)})" for f in tentries))
L.append('')
for idx,p in enumerate(tchains[:16],1):
    L.append(f'### T{idx}')
    L.append('')
    L.append(' → '.join(f"`{f['name']}` ({loc(f)})" for f,_ in p))
    L.append('')
L.append('## 6. route 触发链（actor → carrier trigger，BFS 最短路）')
L.append('')
for s,c,p in route_chains:
    L.append(f"- `{s['name']}` ({loc(s)}) → `{c['name']}` ({loc(c)})：" + ' → '.join(f"`{f['name']}` ({loc(f)})" for f in p[1:-1]))
L.append('')
L.append('## 7. syscall 叶子（按调用者）')
L.append('')
L.append('| syscall | 调用点数 | 调用者（前 6） |')
L.append('|---|---|---|')
for name in sorted(syscalls):
    items=syscalls[name]
    L.append(f"| `{name}` | {len(items)} | " + '; '.join(f"`{f}:{l}`" for f,l,c,_ in items[:6]) + ' |')
L.append('')
L.append('## 8. 符号索引（文件级）')
L.append('')
L.append('Kotlin 文件（focus 43499/UI）符号数：')
L.append('')
L.append('| file | classes | funs |')
L.append('|---|---|---|')
focus_files=sorted(kbyfile)
for fn in focus_files:
    fl=[f for f in kfuncs if f['file']==fn]
    cl=[c for c in classes if c['file']==fn]
    L.append(f"| `{fn.replace('app/src/main/kotlin/','')}` | {len(cl)} | {len(fl)} |")
L.append('')
L.append('C/C++ 文件符号数：')
L.append('')
L.append('| file | functions |')
L.append('|---|---|')
for fn in sorted(set(f['file'] for f in nf)):
    L.append(f"| `{fn}` | {sum(1 for f in nf if f['file']==fn)} |")
L.append('')
L.append('## 9. 已知缺口/注意')
L.append('')
L.append('- Kotlin 解析为**同名解析**：重载、扩展函数接收者、高阶/lambda、反射调用可能漏边；resolved=0 的边在 `kotlin-callgraph.tsv` 保留可查。')
L.append('- C/C++ clang AST 已展开宏但**函数指针调用**（如 vtable 风格的 carrier->trigger、JNI 注册表）不会出现在 CallExpr 名称中；线程入口单独给出。')
L.append('- actor 内部通过 `pthread_create` 进入的 10 个入口见 §5；动态线程（victim helper 子进程）不在同进程调用图。')
L.append('- legacy/legacy2/very_legacy 未编译（parked），其符号/调用链需要单独分析批次（可复用同一 AST 脚本，但需临时编译单元）。')
L.append('')
open(OUT+'/README.md','w').write('\n'.join(L))
open(OUT+'/../43499-symbol-callchains-20261029.md','w').write('\n'.join(L))
print('WROTE',OUT,'lines',len(L))
