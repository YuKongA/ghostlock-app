import json, shlex, subprocess, sys, os, collections

ROOT='/Users/nickji/Documents/Program/Analysis/ghostlock-app'
CC=json.load(open(ROOT+'/app/.cxx/Debug/12l21261/arm64-v8a/compile_commands.json'))

TARGET_SUFFIXES=['/gl43499/jni43499.c','/gl43499/mcast510_probe.c',
 '/gl43499/core/actor.c','/gl43499/core/carrier_multicast.c','/gl43499/core/carrier_pselect.c',
 '/gl43499/core/carrier_tcp.c','/gl43499/core/page.c','/gl43499/core/placement.c',
 '/gl43499/core/perf.c','/gl43499/core/victim_protocol.c',
 '/gl43499/core/victim_helper/victim_helper.c','/common/gl_log.c','/glgeneral/general_jni.c']

def rel(p):
    return p.replace(ROOT+'/','')

def run_ast(entry):
    cmd=shlex.split(entry['command'])+['-Xclang','-ast-dump=json','-fsyntax-only']
    p=subprocess.run(cmd,cwd=entry['directory'],capture_output=True,text=True)
    if p.returncode!=0:
        print('AST_FAIL',entry['file'],p.returncode,p.stderr[-200:],file=sys.stderr)
        return None
    return json.loads(p.stdout)

def walk(node, fn):
    if isinstance(node, dict):
        fn(node)
        for ch in node.get('inner',[]):
            walk(ch, fn)
    elif isinstance(node, list):
        for ch in node: walk(ch, fn)

def extract(tu_file, data):
    funcs=[]; calls=[]; thread_entries=set(); jni=[]
    cur_file=tu_file
    def visit(node):
        nonlocal cur_file
        if not isinstance(node,dict): return
        loc=node.get('loc') or {}
        f=loc.get('file')
        if f: cur_file=f
        kind=node.get('kind')
        if kind=='FunctionDecl' and node.get('inner') is not None:
            name=node.get('name') or ''
            line=(loc.get('line') or (node.get('range',{}).get('begin',{}) or {}).get('line'))
            fname=loc.get('file') or cur_file
            params=[]
            for ch in node.get('inner',[]):
                if ch.get('kind')=='ParmVarDecl':
                    params.append((ch.get('name') or '', ch.get('type',{}).get('qualType','')))
            storage=node.get('storageClass','')
            is_static = storage=='static'
            if name:
                funcs.append(dict(name=name,file=rel(fname),line=line,params=params,static=is_static,tu=rel(tu_file)))
                if name.startswith('Java_com_ghostlock_app_jni_'):
                    jni.append(dict(cname=name,file=rel(fname),line=line,params=params))
                # calls inside body
                def scan(n, depth=0):
                    if not isinstance(n,dict): return
                    k=n.get('kind')
                    if k in ('CallExpr','CXXMemberCallExpr','CXXOperatorCallExpr'):
                        callee=None
                        kids=n.get('inner',[])
                        if kids:
                            callee=resolve_ref(kids[0])
                        if callee:
                            bl=n.get('range',{}).get('begin',{}) or {}
                            calls.append(dict(caller=name,caller_file=rel(fname),caller_line=line,
                                              callee=callee,file=rel(bl.get('file') or fname),line=bl.get('line')))
                            if callee=='pthread_create' and len(kids)>=4:
                                te=find_ref(kids[3])
                                if te: thread_entries.add(te)
                    for ch in n.get('inner',[]):
                        scan(ch,depth+1)
                scan(node)
        for ch in node.get('inner',[]):
            visit(ch)
    def resolve_ref(n):
        if not isinstance(n,dict): return None
        k=n.get('kind')
        if k=='DeclRefExpr':
            rd=n.get('referencedDecl') or {}
            return rd.get('name')
        if k=='MemberExpr':
            return n.get('name')
        for ch in n.get('inner',[]):
            r=resolve_ref(ch)
            if r: return r
        return None
    def find_ref(n):
        if not isinstance(n,dict): return None
        if n.get('kind')=='DeclRefExpr':
            rd=n.get('referencedDecl') or {}
            if rd.get('kind')=='FunctionDecl' and rd.get('name'): return rd['name']
        for ch in n.get('inner',[]):
            r=find_ref(ch)
            if r: return r
        return None
    visit(data)
    return funcs,calls,thread_entries,jni

def main():
    entries=[e for e in CC if any(e['file'].endswith(s) for s in TARGET_SUFFIXES)]
    all_funcs=[]; all_calls=[]; threads=set(); jni=[]
    for e in entries:
        print('AST',rel(e['file']),flush=True)
        data=run_ast(e)
        if data is None: continue
        f,c,t,j=extract(os.path.abspath(e['file']),data)
        all_funcs+=f; all_calls+=c; threads|=t; jni+=j
    # dedupe funcs by file/line/name
    seen=set(); funcs=[]
    for f in all_funcs:
        k=(f['file'],f['line'],f['name'])
        if k in seen: continue
        seen.add(k); funcs.append(f)
    # filter calls that are inside our files only (but keep callee external)
    defs=collections.defaultdict(list)
    for f in funcs: defs[f['name']].append(f)
    edges=[]
    seen=set()
    for c in all_calls:
        callee=c['callee']
        targets=defs.get(callee,[])
        if targets:
            for t in targets:
                key=(c['caller_file'],c['caller_line'],c['caller'],callee,t['file'],t['line'])
                if key in seen: continue
                seen.add(key); edges.append(dict(c,**{'callee_file':t['file'],'callee_line':t['line'],'resolved':True}))
        else:
            key=(c['caller_file'],c['caller_line'],c['caller'],callee,'','')
            if key in seen: continue
            seen.add(key); edges.append(dict(c,**{'callee_file':'','callee_line':'','resolved':False}))
    out=dict(functions=funcs,calls=edges,thread_entries=sorted(threads),jni=jni)
    json.dump(out,open('/tmp/glk_analysis/native_graph.json','w'),ensure_ascii=False)
    print('FUNCS',len(funcs),'EDGES',len(edges),'JNI',len(jni),'THREADS',len(threads))
main()
