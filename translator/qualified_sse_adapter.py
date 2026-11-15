"""Reuse Open Amalur's existing SSE emitter/helpers, without importing its CLI.
Only missing SSE operations and approximation-sensitive RCP/RSQRT use this seam.
Both repositories remain unchanged. Helpers retain Runner's memory access rules.
"""
from pathlib import Path
import ast,hashlib,json,re,collections
import capstone
from capstone import x86_const as K

counts=collections.Counter()
def install(lift, include_dir):
    p=Path(__file__).parent
    src=p/'sse'
    text=(src/'recomp.py').read_text()
    tree=ast.parse(text)
    wanted=[]
    for node in tree.body:
        if isinstance(node,(ast.ClassDef,ast.FunctionDef)) and node.name in ('G','xmmop'):wanted.append(node)
        elif isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id in ('GPR','SUB') for t in node.targets):wanted.append(node)
    scope={'K':K,'Unsupported':lift.Unsupported}
    exec(compile(ast.Module(body=wanted,type_ignores=[]),str(src/'recomp.py'),'exec'),scope)
    G,xmmop=scope['G'],scope['xmmop']
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    original=lift.FnLifter.insn
    force={'rcpps','rsqrtps','rcpss','rsqrtss'}
    def emit(self,va,n,txt,kind,tgt):
        start=len(self.lines)
        mnemonic=txt.strip().split(None,1)[0]
        if mnemonic not in force:
            try:return original(self,va,n,txt,kind,tgt)
            except lift.Unsupported as why:
                saved_error=why
                del self.lines[start:]
        else:saved_error=lift.Unsupported('approximation operation')
        off=va-self.L.tva
        ins=next(md.disasm(self.L.code[off:off+n],va,1),None)
        if ins is None or ins.size!=n or not any(o.type==K.X86_OP_REG and ins.reg_name(o.reg).startswith('xmm') for o in ins.operands):raise saved_error
        g=G(ins)
        def xr(op):
            name=ins.reg_name(op.reg)
            if not re.fullmatch(r'xmm[0-7]',name):raise lift.Unsupported('SSE receiver '+name)
            return f'(*(X*)&ctx->xmm[{name[3:]}])'
        g.xr=xr
        g.x=lambda op: xr(op) if g.isx(op) else f'RX({g.addr(op)})'
        try:
            if mnemonic in ('rcpss','rsqrtss'):
                d,s=ins.operands
                helper='p_rcpps' if mnemonic=='rcpss' else 'p_rsqrtps'
                value=f'{xr(s)}.d[0]' if g.isx(s) else f'R32({g.addr(s)})'
                code=f'{{ X a_={{0}};a_.d[0]={value};a_={helper}(a_);{xr(d)}.d[0]=a_.d[0]; }}'
            else:code=xmmop(g,ins.mnemonic,list(ins.operands))
        except (lift.Unsupported,KeyError,IndexError):raise saved_error
        if 'COMI(' in code:code='F.op = FL_EXPL; '+code
        self.e(code);counts[ins.mnemonic]+=1
    lift.FnLifter.insn=emit

    # Copy the existing helper implementation, removing unrelated x87/string
    # state so its ST macro cannot alter Runner's x87 register convention.
    header=(src/'rt.h').read_text().split('/* x87, kept as doubles */')[0]+'\n#endif\n'
    header=header.replace('#define P(a) ((void *)(uintptr_t)(u32)(a))','#define P(a) ((void *)X86_PTR(a))')
    for bits in (8,16,32,64):
        header=re.sub(r'INL u'+str(bits)+r' R'+str(bits)+r'\(u32 a\) \{[^\n]*\}',f'INL u{bits} R{bits}(u32 a) {{ return ld{bits}(a); }}',header)
        header=re.sub(r'INL void W'+str(bits)+r'\(u32 a, u'+str(bits)+r' v\) \{[^\n]*\}',f'INL void W{bits}(u32 a, u{bits} v) {{ st{bits}(a,v); }}',header)
    header=re.sub(r'INL float RF\(u32 a\) \{[^\n]*\}','INL float RF(u32 a) { return ldf(a); }',header)
    header=re.sub(r'INL double RD\(u32 a\) \{[^\n]*\}','INL double RD(u32 a) { return ldd(a); }',header)
    header+='''
_Static_assert(sizeof(X)==sizeof(X86Xmm),"SSE ABI size");
#undef COMI
#define COMI(a,b) do { int un_=(a)!=(a)||(b)!=(b); F.op=FL_EXPL;F.cf=un_||(a)<(b);F.zf=un_||(a)==(b);F.pf=un_;F.sf=0;F.of=0; }while(0)
#define FS (ctx->fs_base)
'''
    dst=Path(include_dir);dst.mkdir(parents=True, exist_ok=True)
    (dst/'oa_qualified_sse.h').write_text(header)
    (dst/'qualified-sse-source-receipt.json').write_text(json.dumps({'source':str(src),'translator_sha256':hashlib.sha256(text.encode()).hexdigest(),'header_sha256':hashlib.sha256((src/'rt.h').read_bytes()).hexdigest(),'adapter_header_sha256':hashlib.sha256(header.encode()).hexdigest(),'scope':'missing vector operations; RCP/RSQRT; default floating control qualified separately','memory':'R8/16/32/64 and writes route to Runner loads/stores; vector RX/WX match its 128-bit access model'},indent=2))
