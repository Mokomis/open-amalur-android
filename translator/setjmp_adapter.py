"""Add original MSVC _setjmp3 call continuations to native resume frames.

The original CRT still saves/restores the 32-bit context. The host longjmp is
only the control-flow bridge back to the corresponding generated C frame.
"""
from pathlib import Path
import struct,bisect,json

def install(lift, report_dir):
 old_init=lift.Lifter.__init__
 def init(self,*args,**kwargs):
  old_init(self,*args,**kwargs)
  setjmp=0x11c7760
  expected=bytes.fromhex('8b542404892a895a04897a0889720c8962108b0424894214')
  if self.pe.read(setjmp,len(expected))!=expected:raise RuntimeError('original CRT setjmp context-save signature changed')
  sites=[];code=self.code;va=self.tva;at=0
  while True:
   at=code.find(b'\xe8',at)
   if at<0:break
   if at+5<=len(code) and va+at+5+struct.unpack_from('<i',code,at+1)[0]==setjmp:
    n,txt=self.dis.decode(va+at)
    if n!=5 or not txt.strip().startswith('call'):raise RuntimeError('setjmp call-site decode mismatch')
    sites.append(va+at)
   at+=1
  for s in sites:
   self.functions.pop(s+5,None);self.entries.discard(s+5)
  entries=sorted(self.entries);records=[]
  for s in sites:
   i=bisect.bisect_right(entries,s)-1
   if i<0 or s-entries[i]>1048576:raise RuntimeError('setjmp caller ownership unresolved')
   parent=entries[i];self.resume[parent]=sorted(set(self.resume.get(parent,()))|{s+5})
   records.append({'call':hex(s),'original_setjmp':hex(setjmp),'continuation':hex(s+5),'generated_parent':hex(parent)})
  (report_dir/'setjmp-evidence.json').write_text(json.dumps({'signature':expected.hex(),'sites':records,'all_general_windows_exception_paths_qualified':False},indent=2))
 lift.Lifter.__init__=init
