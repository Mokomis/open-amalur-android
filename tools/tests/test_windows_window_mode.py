"""Check Windows mode preferences on a host C compiler (Win32 file rename shim)."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'native/experiments/ff1850-threading/O67-ORIGINAL-UI/veil-apk-update/veil_native_scaling.c'
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "veil_native_scaling.h"
int host_rename(const char *a, const char *b) { return rename(a,b)==0; }
int main(int argc, char **argv) {
    assert(argc==3);
    int active=veil_windows_mode(argv[1]);
    char value[8];
    assert(veil_native_scaling_read("DEFAULT","VeilWindowMode",value));
    printf("active=%d requested=%s\n",active,value);
    if (!strcmp(argv[2],"write")) {
        assert(veil_native_scaling_write("DEFAULT","VeilWindowMode","1"));
        assert(veil_native_scaling_write("DEFAULT","VeilWindowMode","2"));
        assert(veil_windows_mode(argv[1])==active);
        assert(veil_native_scaling_read("DEFAULT","VeilWindowModeActive",value));
        assert(value[0]=='0'+active);
        assert(!veil_native_scaling_write("DEFAULT","VeilWindowMode","3"));
        assert(!veil_native_scaling_write("DEFAULT","VeilWindowMode","-1"));
        assert(!veil_native_scaling_write("DEFAULT","VeilWindowMode","1bad"));
        assert(veil_native_scaling_write("DEFAULT","VeilNativeHardwareScaling","1")==-1);
    }
    return 0;
}
'''
class WindowMode(unittest.TestCase):
    def test_preferences(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'rt.h').write_text('int host_rename(const char *,const char *);\n')
            (root/'check.c').write_text(HARNESS)
            (root/'scaling.c').write_bytes(SOURCE.read_bytes())
            exe=root/'check'
            subprocess.run(['cc','-D_WIN32','-Wall','-Wextra','-Werror','-I',str(root),'-I',str(SOURCE.parent),str(root/'check.c'),str(root/'scaling.c'),'-o',str(exe)],check=True)
            def run(action='read'):
                return subprocess.check_output([exe,root,action],text=True)
            self.assertIn('active=0 requested=0',run('write'))
            self.assertEqual((root/'window-mode').read_text().strip(),'fullscreen')
            self.assertIn('active=2 requested=2',run())
            for value, expected in [('borderless',1),('windowed',0),('garbage',0),('fullscreen junk',0)]:
                (root/'window-mode').write_text(value+'\n')
                self.assertIn(f'active={expected} requested={expected}',run())
if __name__=='__main__': unittest.main()
