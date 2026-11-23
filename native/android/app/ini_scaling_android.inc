#include "veil_native_scaling.h"
#ifndef _WIN32
#include "../veil-dialogue-circle/veil_dialogue_touch.h"
#endif
#include "veil_environment.h"
#include "../veil-waterfall/waterfall_state.h"
/* UTF-16 guest profile API. Reads retail files; writes only the existing overlay. */
#include <pthread.h>
#include <unistd.h>
__attribute__((visibility("hidden"))) int veil_shadow_ini_read(const char *, const char *, char[2]);
__attribute__((visibility("hidden"))) int veil_shadow_ini_write(const char *, const char *, const char *);
__attribute__((visibility("hidden"))) int veil_transmog_command(X86Ctx *, const char *, const char *, const char *);
__attribute__((visibility("hidden"))) int veil_transmog_read(const char *, const char *, char[2]);
__attribute__((visibility("hidden"))) int veil_tablet_read(const char *, const char *, char *);
__attribute__((visibility("hidden"))) int veil_tablet_command(const char *, const char *, const char *);
__attribute__((visibility("hidden"))) void veil_intro_cart_camera(int);
static pthread_mutex_t ini_lock=PTHREAD_MUTEX_INITIALIZER;
static char*ini_wide_string(uint32_t g){if(!g)return NULL;size_t n=0;while(ld16(g+2*n)){if(++n>32768)rt_fatal("INI string bound");}char*out=malloc(4*n+1);size_t at=0;for(size_t i=0;i<n;i++){uint32_t c=ld16(g+2*i);if(c>=0xd800&&c<=0xdbff&&i+1<n){uint32_t d=ld16(g+2*(i+1));if(d>=0xdc00&&d<=0xdfff){c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i++;}}if(c<128)out[at++]=c;else if(c<2048){out[at++]=0xc0|(c>>6);out[at++]=0x80|(c&63);}else if(c<65536){out[at++]=0xe0|(c>>12);out[at++]=0x80|((c>>6)&63);out[at++]=0x80|(c&63);}else{out[at++]=0xf0|(c>>18);out[at++]=0x80|((c>>12)&63);out[at++]=0x80|((c>>6)&63);out[at++]=0x80|(c&63);}}out[at]=0;return out;}
static uint32_t ini_next(const unsigned char**p){uint32_t c=*(*p)++;if(c<128)return c;unsigned more=c<0xe0?1:c<0xf0?2:3;c&=more==1?31:more==2?15:7;while(more--){if(!**p)return 0xfffd;c=(c<<6)|(*(*p)++&63);}return c;}
static size_t ini_put_wide(uint32_t g,unsigned cap,const char*s){size_t n=0;const unsigned char*p=(const unsigned char*)s;while(*p){uint32_t c=ini_next(&p);if(c<65536){if(n+1>=cap)break;st16(g+2*n++,c);}else{if(n+2>=cap)break;c-=65536;st16(g+2*n++,0xd800+(c>>10));st16(g+2*n++,0xdc00+(c&1023));}}if(cap)st16(g+2*n,0);return n;}
static char*ini_read_text(const char*path){FILE*f=fopen(path,"rb");if(!f)return strdup("");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0||n>2097152)rt_fatal("INI file bound");unsigned char*b=calloc(n+4,1);if(fread(b,1,n,f)!=(size_t)n)rt_fatal("INI file read");fclose(f);if(n>=2&&b[0]==0xff&&b[1]==0xfe){char*out=malloc((size_t)n*2+1);size_t at=0;for(long i=2;i+1<n;i+=2){uint32_t c=b[i]|(b[i+1]<<8);if(!c)break;if(c<128)out[at++]=c;else if(c<2048){out[at++]=0xc0|(c>>6);out[at++]=0x80|(c&63);}else{out[at++]=0xe0|(c>>12);out[at++]=0x80|((c>>6)&63);out[at++]=0x80|(c&63);}}out[at]=0;free(b);return out;}if(n>=3&&!memcmp(b,"\xef\xbb\xbf",3))memmove(b,b+3,n-2);return (char*)b;}
static char*ini_trim(char*s){while(*s==' '||*s=='\t'||*s=='\r')s++;size_t n=strlen(s);while(n&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'))s[--n]=0;return s;}
static void ini_path(uint32_t name,int write,char*out){char path[1024],canon[1024];if(!name)rt_fatal("INI default system path not qualified");guest_w(name,path,sizeof path);canonical(path,canon,sizeof canon);if(write){overlay_path(canon,out,2048);mkdirs_for(out);}else read_path(canon,out,2048);}
static void ini_read(X86Ctx*ctx){char*section=ini_wide_string(ARG(0)),*key=ini_wide_string(ARG(1)),*fallback=ini_wide_string(ARG(2));if(!section||!key)rt_fatal("INI section/key enumeration not yet qualified");
    char shadow_value[8];
    if (
        veil_native_scaling_read(section, key, shadow_value) ||
#ifndef _WIN32
        veil_dialogue_read(section, key, shadow_value, sizeof shadow_value) ||
#endif
        veil_tablet_read(section, key, shadow_value) ||
        veil_transmog_read(section, key, shadow_value) || veil_shadow_ini_read(section, key, shadow_value) || veil_environment_read(section, key, shadow_value)) {
        size_t length = ini_put_wide(ARG(3), ARG(4), shadow_value);
        free(section);
        free(key);
        free(fallback);
        RET(length);
        return;
    }
    char path[2048];ini_path(ARG(5),0,path);pthread_mutex_lock(&ini_lock);char*text=ini_read_text(path),*value=NULL,*save=NULL;int in_section=0;for(char*line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){char*t=ini_trim(line);if(*t=='['){char*e=strchr(t+1,']');if(e){*e=0;in_section=!strcasecmp(ini_trim(t+1),section);}continue;}if(in_section&&*t!=';'&&*t!='#'){char*eq=strchr(t,'=');if(eq){*eq=0;if(!strcasecmp(ini_trim(t),key)){char*v=ini_trim(eq+1);size_t n=strlen(v);if(n>=2&&((v[0]=='"'&&v[n-1]=='"')||(v[0]=='\''&&v[n-1]=='\''))){v[n-1]=0;v++;}free(value);value=strdup(v);}}}}size_t n=ini_put_wide(ARG(3),ARG(4),value?value:fallback?fallback:"");free(value);free(text);pthread_mutex_unlock(&ini_lock);free(section);free(key);free(fallback);RET(n);}
static void ini_write_utf16(FILE*f,const char*s){fputc(0xff,f);fputc(0xfe,f);const unsigned char*p=(const unsigned char*)s;while(*p){uint32_t c=ini_next(&p);if(c>65535){c-=65536;uint16_t a=0xd800+(c>>10),b=0xdc00+(c&1023);fputc(a&255,f);fputc(a>>8,f);fputc(b&255,f);fputc(b>>8,f);}else{fputc(c&255,f);fputc(c>>8,f);}}}
static int ini_replace(const char *path, const char *text)
{
    char temporary[2080];
    if (snprintf(temporary, sizeof temporary, "%s.XXXXXX", path) >= (int)sizeof temporary)
        return 0;
    int fd = mkstemp(temporary);
    if (fd < 0) return 0;
    FILE *file = fdopen(fd, "wb");
    int ok = 0;
    if (file) {
        ini_write_utf16(file, text);
        ok = !ferror(file);
        if (fclose(file) != 0) ok = 0;
    } else close(fd);
    if (ok && host_rename(temporary, path)) return 1;
    unlink(temporary);
    return 0;
}
static void ini_write(X86Ctx*ctx){char*section=ini_wide_string(ARG(0)),*key=ini_wide_string(ARG(1)),*value=ini_wide_string(ARG(2));if(!section&&!key&&!value){RET(1);return;}if(!section){free(key);free(value);RET(0);return;}
    int handled = veil_native_scaling_write(section, key, value);
    if (handled < 0 && key && value && !strcasecmp(section, "DEFAULT") && !strcasecmp(key, "VeilIntroCart")) {
        veil_intro_cart_camera(!strcmp(value, "1"));
        handled = 1;
    }
    if (handled < 0) handled = veil_waterfall_command(section, key, value);
#ifndef _WIN32
    if (handled < 0) handled = veil_dialogue_command(section, key, value);
#endif
    if (handled < 0) handled = veil_tablet_command(section, key, value);
    if (handled < 0)
        handled = veil_transmog_command(ctx, section, key, value);
    if (handled < 0)
        handled = veil_shadow_ini_write(section, key, value);
    if (handled < 0)
        handled = veil_environment_write(section, key, value);
    if (handled >= 0) {
        free(section);
        free(key);
        free(value);
        RET(handled);
        return;
    }
    char path[2048];ini_path(ARG(3),1,path);pthread_mutex_lock(&ini_lock);char original_path[2048];ini_path(ARG(3),0,original_path);char*text=ini_read_text(original_path);size_t cap=strlen(text)+(key?strlen(key):0)+(value?strlen(value):0)+strlen(section)+128;char*out=calloc(cap,1);size_t used=0;int target=0,found=0,inserted=0;char*save=NULL;
#define INI_APPEND(...) do{int nn=snprintf(out+used,cap-used,__VA_ARGS__);if(nn<0||(size_t)nn>=cap-used)rt_fatal("INI update output bound");used+=nn;}while(0)
for(char*line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){char*copy=strdup(line),*t=ini_trim(copy);if(*t=='['){if(target&&key&&value&&!inserted){INI_APPEND("%s=%s\n",key,value);inserted=1;}char*e=strchr(t+1,']');target=0;if(e){*e=0;target=!strcasecmp(ini_trim(t+1),section);if(target)found=1;}if(!(target&&!key))INI_APPEND("%s\n",line);}else if(target&&!key){/* delete section */}else if(target&&key){char*eq=strchr(t,'=');int match=0;if(eq){*eq=0;match=!strcasecmp(ini_trim(t),key);}if(match){if(value&&!inserted){INI_APPEND("%s=%s\n",key,value);inserted=1;}}else INI_APPEND("%s\n",line);}else INI_APPEND("%s\n",line);free(copy);}if(key&&value&&!inserted){if(!found)INI_APPEND("\n[%s]\n",section);INI_APPEND("%s=%s\n",key,value);}int ok=ini_replace(path,out);free(out);free(text);pthread_mutex_unlock(&ini_lock);free(section);free(key);free(value);RET(ok);
#undef INI_APPEND
}
static const HleDef ini_defs[]={{"kernel32.dll","GetPrivateProfileStringW",ini_read,6},{"kernel32.dll","WritePrivateProfileStringW",ini_write,4}};
void oa_ini_register(void){rt_hle_register(ini_defs,2);}
