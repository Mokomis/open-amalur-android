"""Recognize one binary-proven noreturn wrapper before its embedded jump table.

0x01083C10 always reaches CRT longjmp at 0x011C6EC0. Its latter restores
ESP/nonvolatile registers and jumps to [saved_context+0x14], never the caller.
No game instructions or results are substituted. The normal code is emitted;
if the purported noreturn call returns, execution fails explicitly.
"""
import inspect,textwrap,json,hashlib,struct
from pathlib import Path

def install(lift, report_dir):
 old_init=lift.Lifter.__init__
 def init(self,*args,**kwargs):
  old_init(self,*args,**kwargs)
  wrapper=self.pe.read(0x1083c10,64)
  tail=self.pe.read(0x11c6f4f,30)
  call=self.pe.read(0x1083c46,6)
  if call[0]!=0xe8 or call[5]!=0xcc or 0x1083c4b+struct.unpack('<i',call[1:5])[0]!=0x11c6ec0:
   raise RuntimeError('noreturn profile call bytes changed')
  if self.pe.read(0x11c6f64,9)!=bytes.fromhex('8b621083c404ff6214'):
   raise RuntimeError('longjmp restore and jump bytes changed')
  self.oa_noreturn_functions={0x1083c10}
  p=report_dir/'noreturn-evidence.json'
  p.write_text(json.dumps({'kind':'static machine-code evidence, not a runtime exception test','wrapper':'0x01083c10','noreturn_callee':'0x011c6ec0','wrapper_call':'0x01083c46','longjmp_restore':'0x011c6f64','preserved_jump_table':'0x010a70bc','table_entries':9,'wrapper_bytes_sha256':hashlib.sha256(wrapper).hexdigest(),'saved_context_restore_bytes':self.pe.read(0x11c6f4f,30).hex(),'full_exception_unwind_qualified':False},indent=2))
 lift.Lifter.__init__=init
 source=textwrap.dedent(inspect.getsource(lift.FnLifter.lift))
 needle='kind == X.K_CALL_IAT and tgt in L.noreturn_slots)'
 assert source.count(needle)==1
 source=source.replace(needle,'kind == X.K_CALL_IAT and tgt in L.noreturn_slots) or (kind == X.K_CALL and tgt in L.oa_noreturn_functions)')
 needle='if kind == X.K_CALL_IAT and tgt in L.noreturn_slots:'
 assert source.count(needle)==1
 source=source.replace(needle,'if (kind == X.K_CALL_IAT and tgt in L.noreturn_slots) or (kind == X.K_CALL and tgt in L.oa_noreturn_functions):')
 scope=dict(vars(lift));exec(source,scope);lift.FnLifter.lift=scope['lift']
 old_insn=lift.FnLifter.insn
 def insn(self,va,n,txt,kind,tgt):
  result=old_insn(self,va,n,txt,kind,tgt)
  if kind==lift.X.K_CALL and tgt in self.L.oa_noreturn_functions:
   self.e(f'x86_trap(ctx,0x{va:X}u,"binary-proven noreturn routine returned");')
  return result
 lift.FnLifter.insn=insn
