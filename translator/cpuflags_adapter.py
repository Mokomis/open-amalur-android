"""Preserve guest ID flag per context; do not borrow arithmetic lazy flags."""
def install(lift):
 def pushfd(self,ops,p):
  self.e('esp -= 4; st32(esp, x86_eflags(&F) | (ctx->eflags_id & 0x200000u) | (F.df ? 0x400u : 0));')
 def popfd(self,ops,p):
  self.e('{ uint32_t ef_=ld32(esp);ctx->eflags_id=ef_&0x200000u;F.df=(ef_>>10)&1;x86_set_eflags(&F,ef_,1);esp+=4; }')
 lift.FnLifter.i_pushfd=pushfd
 lift.FnLifter.i_popfd=popfd
