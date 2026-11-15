"""Four explicit SSE2 operations needed by Amalur's actual global initializers.

This experiment qualifies observed initializer outputs at default MXCSR. It does
not claim arbitrary rounding modes, FP exception flags or DAZ/FTZ equivalence.
"""
def install(lift):
    def cvtps2pd(self, ops, prefix):
        d,s=ops
        if d.kind!='xmm' or s.kind not in ('xmm','mem'):
            raise lift.Unsupported('cvtps2pd form')
        v=f'ctx->xmm[{s.idx}].q[0]' if s.kind=='xmm' else f'ld64({self.ea(s)})'
        self.e(f'{{ X86Xmm v={{0}},r={{0}}; v.q[0]={v}; r.d[0]=(double)v.f[0]; r.d[1]=(double)v.f[1]; ctx->xmm[{d.idx}]=r; }}')
    def cvtpd2ps(self, ops, prefix):
        d,s=ops
        if d.kind!='xmm' or s.kind not in ('xmm','mem'):
            raise lift.Unsupported('cvtpd2ps form')
        v=f'ctx->xmm[{s.idx}]' if s.kind=='xmm' else f'ldx({self.ea(s)})'
        self.e(f'{{ X86Xmm v={v},r={{0}}; r.f[0]=(float)v.d[0]; r.f[1]=(float)v.d[1]; ctx->xmm[{d.idx}]=r; }}')
    def mulsd(self,ops,prefix):
        d,s=ops
        if d.kind!='xmm' or s.kind not in ('xmm','mem'):
            raise lift.Unsupported('mulsd form')
        v=f'ctx->xmm[{s.idx}].d[0]' if s.kind=='xmm' else f'ldd({self.ea(s)})'
        self.e(f'{{ double a=ctx->xmm[{d.idx}].d[0],b={v}; ctx->xmm[{d.idx}].d[0]=a*b; }}')
    def movdqa(self,ops,prefix):
        return self._mov128(ops,True)
    def xadd(self,ops,prefix):
        d,s=ops
        bits=self.size(d,s)
        if d.kind!='mem' or s.kind!='reg' or bits!=32:
            raise lift.Unsupported('xadd: qualified memory/register dword form only')
        self.e(f'{{ uint32_t at={self.ea(d)},a=ld32(at),b={self.rd(s,32)},r=a+b; st32(at,r); {self.wr(s,32,"a")} '+self.flags('FL_ADD',32,'a','b','r')+' }')
    def cmpxchg(self,ops,prefix):
        d,s=ops
        bits=self.size(d,s)
        if d.kind!='mem' or s.kind!='reg' or bits!=32:
            raise lift.Unsupported('cmpxchg: memory/register dword form only')
        self.e(f'{{ uint32_t at={self.ea(d)},a=eax,b=ld32(at),v={self.rd(s,32)},r=a-b; if(a==b) st32(at,v); else eax=b; '+self.flags('FL_SUB',32,'a','b','r')+' }')
    for name,fn in [('cvtps2pd',cvtps2pd),('cvtpd2ps',cvtpd2ps),('mulsd',mulsd),('movdqa',movdqa)]:
        setattr(lift.FnLifter,'i_'+name,fn)
    lift.FnLifter.i_xadd=xadd
    lift.FnLifter.i_cmpxchg=cmpxchg
