"""Host checks for OS-mode enumeration and stable resolution preferences."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'native/android/app/veil_native_scaling.c'
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "veil_native_scaling.h"
#include "oa_dynamic_resolution.h"
int main(int argc, char **argv) {
    assert(argc == 4);
    if (!strcmp(argv[3], "dynamic")) {
        veil_native_scaling_percent(argv[1]);
        assert(!veil_dynamic_resolution_enabled());
        assert(!veil_native_scaling_write("DEFAULT", "VeilDynamicResolution", "2"));
        assert(veil_native_scaling_write("DEFAULT", "VeilDynamicResolution", "1"));
        assert(veil_dynamic_resolution_enabled());
        char value[8];
        assert(veil_native_scaling_read("DEFAULT", "VeilDynamicResolution", value));
        assert(!strcmp(value, "1"));
        return 0;
    }
    if (!strcmp(argv[3], "dynamic_reload")) {
        veil_native_scaling_percent(argv[1]);
        assert(veil_dynamic_resolution_enabled());
        assert(veil_native_scaling_write("DEFAULT", "VeilDynamicResolution", "0"));
        assert(!veil_dynamic_resolution_enabled());
        return 0;
    }
    if (!strcmp(argv[3], "heat") || !strcmp(argv[3], "heat_reload")) {
        const char *heat = "VeilResolutionHeatResponse";
        veil_native_scaling_percent(argv[1]);
        char value[8];
        if (!strcmp(argv[3], "heat")) {
            // Upgrade a profile with DRS already on and no heat preference.
            assert(veil_dynamic_resolution_enabled());
            assert(!veil_resolution_heat_response_enabled());
            assert(!veil_native_scaling_write("DEFAULT", heat, "2"));
            assert(veil_native_scaling_write("DEFAULT", heat, "1"));
            assert(veil_native_scaling_write("DEFAULT", "VeilDynamicResolution", "0"));
        }
        assert(!veil_dynamic_resolution_enabled());
        assert(veil_resolution_heat_response_enabled());
        assert(veil_native_scaling_read("DEFAULT", heat, value) && !strcmp(value, "1"));
        if (!strcmp(argv[3], "heat_reload")) {
            assert(veil_native_scaling_write("DEFAULT", "VeilDynamicResolution", "1"));
            assert(veil_native_scaling_write("DEFAULT", heat, "0"));
            assert(veil_dynamic_resolution_enabled());
            assert(!veil_resolution_heat_response_enabled());
        }
        return 0;
    }
    if (!strcmp(argv[3], "budget")) {
        OaResolutionTiming timing = {0};
        uint64_t now = 1;
        unsigned level = OA_RESOLUTION_FULL;
        for (int i=0; i<110; ++i, now+=20000000)
            level = oa_resolution_next(&timing, now, level);
        assert(level == OA_RESOLUTION_LOW);
        assert(oa_resolution_dimension(1890, level) == 1260);
        assert(oa_resolution_dimension(1260, level) == 840);
        now += 3000000000ull;
        for (int i=0; i<160; ++i, now+=12000000)
            level = oa_resolution_next(&timing, now, level);
        assert(level == OA_RESOLUTION_LOW);
        for (int i=0; i<250; ++i, now+=12000000)
            level = oa_resolution_next(&timing, now, level);
        assert(level == OA_RESOLUTION_MEDIUM);
        return 0;
    }
    int modes[] = {1680,2520,1680,2520,1920,1080,1280,720,2560,1440,
                   3840,2160,1600,900,2048,1152,2560,1600,2880,1800,
                   3200,1800,1920,1200,0,0};
    if (!strcmp(argv[2], "dedupe")) {
        int duplicate[] = {1920,1080,1080,1920,0,0};
        veil_display_resolutions(duplicate, 3, 1920,1080);
    } else if (!strcmp(argv[2], "fallback")) {
        veil_display_resolutions(NULL, 0, 2520,1680);
    } else {
        if (!strcmp(argv[2], "reverse")) {
            for (int a=0,b=12;a<b;++a,--b) {
                int x=modes[2*a],y=modes[2*a+1];
                modes[2*a]=modes[2*b];modes[2*a+1]=modes[2*b+1];
                modes[2*b]=x;modes[2*b+1]=y;
            }
        }
        veil_display_resolutions(modes, 13, 2520,1680);
    }
    int w,h; veil_render_resolution(argv[1],2520,1680,&w,&h);
    char value[32];
    assert(veil_native_scaling_read("DEFAULT","VeilResolutionCount",value));
    printf("count=%s active=%dx%d\n",value,w,h);
    if (!strcmp(argv[3], "write")) {
        assert(veil_native_scaling_write("DEFAULT","VeilRenderResolution","10"));
        veil_render_resolution(argv[1],2520,1680,&w,&h);
        printf("pending_active=%dx%d\n",w,h);
        assert(!veil_native_scaling_write("DEFAULT","VeilRenderResolution","12"));
        assert(!veil_native_scaling_write("DEFAULT","VeilRenderResolution","-1"));
        assert(!veil_native_scaling_write("DEFAULT","VeilRenderResolution","999999999999999999999999"));
        assert(!veil_native_scaling_write("DEFAULT","VeilRenderResolution","1junk"));
    }
    return 0;
}
'''

class DisplayResolutions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.path = Path(cls.work.name)
        harness = cls.path / 'check.c'; harness.write_text(HARNESS)
        cls.exe = cls.path / 'check'
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-I',str(SOURCE.parent),str(harness),str(SOURCE),'-o',str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls): cls.work.cleanup()
    def run_case(self, preference, mode='normal', action='read'):
        (self.path/'render-scale').write_text(preference+'\n')
        return subprocess.check_output([self.exe,self.path,mode,action],text=True)
    def test_enumeration(self):
        self.assertIn('count=12 active=2520x1680',self.run_case('native'))
        self.assertIn('count=2',self.run_case('100','dedupe'))
        self.assertIn('count=3 active=2520x1680',self.run_case('native','fallback'))
    def test_legacy_preferences(self):
        self.assertIn('active=1920x1080',self.run_case('100'))
        self.assertIn('active=960x540',self.run_case('50'))
        self.assertIn('active=1920x1080',self.run_case('1234x567'))
        self.assertIn('active=1920x1080',self.run_case('3840x2160bad'))
    def test_selection_survives_mode_reordering(self):
        self.assertIn('pending_active=1920x1080',self.run_case('100',action='write'))
        selected=(self.path/'render-scale').read_text().strip()
        self.assertEqual(selected,'3200x1800')
        self.assertIn('active=3200x1800',self.run_case(selected,'reverse'))

    def test_dynamic_resolution_preference(self):
        preference = self.path / 'veil-dynamic-resolution'
        preference.unlink(missing_ok=True)
        self.run_case('native', action='dynamic')
        self.assertEqual(preference.read_text().strip(), '1')
        self.run_case('native', action='dynamic_reload')
        self.assertEqual(preference.read_text().strip(), '0')

    def test_heat_response_is_independent_and_survives_restart(self):
        (self.path / 'veil-dynamic-resolution').write_text('1\n')
        heat = self.path / 'veil-resolution-heat-response'
        heat.unlink(missing_ok=True)
        self.run_case('native', action='heat')
        self.assertEqual(heat.read_text().strip(), '1')
        self.run_case('native', action='heat_reload')
        self.assertEqual(heat.read_text().strip(), '0')

    def test_dynamic_resolution_hysteresis(self):
        self.run_case('native', action='budget')

if __name__ == '__main__': unittest.main()
