"""Architectural CMPXCHG8B and PAUSE, needed by the original Bink worker."""
def install(lift):
 def cmpxchg8b(self,ops,prefix):
  if len(ops)!=1 or ops[0].kind!='mem':raise lift.Unsupported('cmpxchg8b operand')
  ea=self.ea(ops[0])
  self.e('{ uint32_t a_='+ea+';uint64_t m_=ld64(a_),want_=((uint64_t)edx<<32)|eax;int same_=m_==want_;')
  self.e('int cf_=x86_cc(&F,CC_B),pf_=x86_cc(&F,CC_P),sf_=x86_cc(&F,CC_S),of_=x86_cc(&F,CC_O);')
  self.e('if(same_)st64(a_,((uint64_t)ecx<<32)|ebx);else{eax=(uint32_t)m_;edx=(uint32_t)(m_>>32);}')
  self.e('F.op=FL_EXPL;F.cf=cf_;F.pf=pf_;F.sf=sf_;F.of=of_;F.zf=same_; }')
 def pause(self,ops,prefix):
  self.e('oa_cpu_pause();')
 lift.FnLifter.i_cmpxchg8b=cmpxchg8b
 lift.FnLifter.i_pause=pause
 def psraw(self,ops,p):self._mmx_shift(ops,'oa_mmx_psraw')
 def pmullw(self,ops,p):self._mmx2(ops,'oa_mmx_pmullw')
 lift.FnLifter.i_psraw=psraw
 lift.FnLifter.i_pmullw=pmullw
