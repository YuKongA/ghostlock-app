import os,re,json,collections
ROOT='/Users/nickji/Documents/Program/Analysis/ghostlock-app'
KROOT=ROOT+'/app/src/main/kotlin'
FILES=[]
for dp,_,fs in os.walk(KROOT):
    for f in fs:
        if f.endswith('.kt'): FILES.append(os.path.join(dp,f))
FILES.sort()
MOD=r'(?:@\w+(?:\([^)]*\))?\s*)*(?:(?:internal|private|public|protected|open|abstract|sealed|data|enum|annotation|value|inline|suspend|operator|infix|tailrec|external|override|actual|expect|const|lateinit|vararg)\s+)*'
DECL_RE=re.compile(r'^\s*'+MOD+r'(class|object|interface|fun)\s+([A-Za-z_][A-Za-z0-9_]*)',re.M)
KW=set('if for while when return catch try is as in else do throw super this null true false break continue object class interface fun val var package import typealias init constructor by get set'.split())

def clean(text):
    out=[]; i=0; n=len(text); in_s=None; in_lc=False; in_bc=False
    while i<n:
        c=text[i]; c2=text[i:i+2]
        if in_lc:
            if c=='\n': in_lc=False; out.append(c)
            else: out.append(' ')
            i+=1; continue
        if in_bc:
            if c2=='*/': in_bc=False; out.append('  '); i+=2
            else: out.append('\n' if c=='\n' else ' '); i+=1
            continue
        if in_s:
            if c=='\\': out.append('  '); i+=2; continue
            if c==in_s: in_s=None
            out.append('\n' if c=='\n' else ' '); i+=1; continue
        if c2=='//': in_lc=True; out.append('  '); i+=2; continue
        if c2=='/*': in_bc=True; out.append('  '); i+=2; continue
        if c in ('"',"'"): in_s=c; out.append(' '); i+=1; continue
        out.append(c); i+=1
    return ''.join(out)

def brace_open_after(code, idx):
    par=0; br=0; ang=0; i=idx; n=len(code)
    while i<n:
        c=code[i]
        if c=='(' : par+=1
        elif c==')': par=max(0,par-1)
        elif c=='[': br+=1
        elif c==']': br=max(0,br-1)
        elif c=='<': ang+=1
        elif c=='>' and ang>0: ang-=1
        elif c=='{':
            if par==0 and br==0 and ang==0: return i
        elif c=='\n' and par==0 and br==0:
            seg=code[idx:i]
            if re.search(r'(^|[={(:,]\s*)\s*=\s*[^=>]',seg): return -1
        i+=1
    return -1

def match_brace(code, open_idx):
    d=0
    for i in range(open_idx,len(code)):
        if code[i]=='{': d+=1
        elif code[i]=='}':
            d-=1
            if d==0: return i
    return -1

functions=[]; classes=[]
for path in FILES:
    rel=path.replace(ROOT+'/','')
    code=clean(open(path,encoding='utf-8',errors='replace').read())
    m=re.search(r'^\s*package\s+([\w.]+)',code,re.M)
    pkg=m.group(1) if m else ''
    class_stack=[]
    for m in DECL_RE.finditer(code):
        kind,name=m.group(1),m.group(2)
        idx=m.start(2)
        line=code.count('\n',0,idx)+1
        while class_stack and idx>class_stack[-1][1]: class_stack.pop()
        owner='.'.join(c[0] for c in class_stack)
        if kind in ('class','object','interface'):
            bo=brace_open_after(code,idx)
            end=match_brace(code,bo) if bo!=-1 else idx
            classes.append(dict(name=name,kind=kind,file=rel,line=line,pkg=pkg,owner=owner))
            if bo!=-1 and end!=-1: class_stack.append((name,end))
        else:
            external = bool(re.search(r'\bexternal\s+fun\b',m.group(0)))
            bo=brace_open_after(code,idx)
            if bo!=-1:
                end=match_brace(code,bo)
                body=(bo,end if end!=-1 else len(code))
            else:
                le=code.find('\n',idx); le=len(code) if le==-1 else le
                body=(idx,le)
            nl=code.find('\n',idx); nl=len(code) if nl==-1 else nl
            functions.append(dict(name=name,kind='fun',file=rel,line=line,pkg=pkg,owner=owner,
                                  sig=re.sub(r'\s+',' ',code[idx:nl]).strip(),external=external,body=body))
# owner normalization: use first top-level class/object/interface per file as owner
top={}
for c in classes:
    if c['owner']=='' and c['file'] not in top:
        top[c['file']]=c['name']
for c in classes:
    if c['owner']!='':
        c['owner']=top.get(c['file'],'')
for f in functions:
    f['owner']=top.get(f['file'],'')
print('parsed files',len(FILES),'classes',len(classes),'funcs',len(functions),flush=True)
cache={}
def getcode(rel):
    if rel not in cache: cache[rel]=clean(open(ROOT+'/'+rel,encoding='utf-8',errors='replace').read())
    return cache[rel]
CALL_RE=re.compile(r'(?:\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^<>{}]*>)?\s*\(|\.([A-Za-z_][A-Za-z0-9_]*)\s*\()')
edges=[]
for f in functions:
    s,e=f['body']
    if e<=s: continue
    code=getcode(f['file']); body=code[s:e]
    for m in CALL_RE.finditer(body):
        name=m.group(1) or m.group(2)
        if not name or name in KW: continue
        pos=s+m.start(); line=code.count('\n',0,pos)+1
        edges.append(dict(caller=f['name'],caller_owner=f['owner'],caller_file=f['file'],caller_line=f['line'],callee=name,line=line))
print('raw edges',len(edges),flush=True)
callables=[]
for c in classes: callables.append(dict(name=c['name'],kind=c['kind'],file=c['file'],line=c['line'],owner=c['owner']))
for f in functions: callables.append(dict(name=f['name'],kind='fun',file=f['file'],line=f['line'],owner=f['owner']))
byname=collections.defaultdict(list)
for c in callables: byname[c['name']].append(c)
def resolve(e):
    cands=byname.get(e['callee'],[])
    if not cands: return None
    same=[c for c in cands if c['file']==e['caller_file'] and c['owner']==e['caller_owner']]
    if len(same)==1: return same[0]
    samefile=[c for c in cands if c['file']==e['caller_file']]
    if len(samefile)==1: return samefile[0]
    if len(cands)==1: return cands[0]
    return None
res=[]
for e in edges:
    t=resolve(e)
    if t: res.append(dict(e,callee_file=t['file'],callee_line=t['line'],callee_owner=t['owner'],callee_kind=t['kind'],resolved=True))
    else: res.append(dict(e,callee_file='',callee_line='',callee_owner='',callee_kind='',resolved=False))
json.dump(dict(functions=functions,classes=classes,calls=res),open('/tmp/glk_analysis/kotlin_graph.json','w'),ensure_ascii=False)
print('classes',len(classes),'funcs',len(functions),'edges',len(res),'resolved',sum(1 for e in res if e['resolved']),'external',sum(1 for f in functions if f['external']))
