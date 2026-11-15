/* Guest32 CRT ABI -> portable services. Unsupported exception/format paths fail.
 * Memory results always point into guest memory, never into native libc. */
#include "rt.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <locale.h>
#include <pthread.h>
#define A(i) rt_arg(ctx,i)
#define RET(v) (ctx->eax=(uint32_t)(v))
static void c_malloc(X86Ctx*ctx){RET(rt_heap_alloc(A(0)?A(0):1,0));}
static void c_free(X86Ctx*ctx){rt_heap_free(A(0));}
static void c_memcpy(X86Ctx*ctx){memcpy(rt_ptr(A(0)),rt_ptr(A(1)),A(2));RET(A(0));}
static void c_memmove(X86Ctx*ctx){memmove(rt_ptr(A(0)),rt_ptr(A(1)),A(2));RET(A(0));}
static void c_memset(X86Ctx*ctx){memset(rt_ptr(A(0)),A(1),A(2));RET(A(0));}
static void c_strdup(X86Ctx*ctx){const char*s=rt_str(A(0));size_t n=strlen(s)+1;uint32_t g=rt_heap_alloc(n,0);memcpy(rt_ptr(g),s,n);RET(g);}
static void c_strlen(X86Ctx*ctx){RET(strlen(rt_str(A(0))));}
static void c_strncmp(X86Ctx*ctx){RET(strncmp(rt_str(A(0)),rt_str(A(1)),A(2)));}
static void c_stricmp(X86Ctx*ctx){RET(strcasecmp(rt_str(A(0)),rt_str(A(1))));}
static void c_strnicmp(X86Ctx*ctx){RET(strncasecmp(rt_str(A(0)),rt_str(A(1)),A(2)));}
static void c_strchr(X86Ctx*ctx){const char*s=rt_str(A(0));char*r=strchr(s,A(1));RET(r?A(0)+(r-s):0);}
static void c_strrchr(X86Ctx*ctx){const char*s=rt_str(A(0));char*r=strrchr(s,A(1));RET(r?A(0)+(r-s):0);}
static void c_strstr(X86Ctx*ctx){const char*s=rt_str(A(0));char*r=strstr(s,rt_str(A(1)));RET(r?A(0)+(r-s):0);}
static void c_atoi(X86Ctx*ctx){RET(atoi(rt_str(A(0))));}
static void c_atof(X86Ctx*ctx){x87_push(ctx,atof(rt_str(A(0))));}
#define CLASSIFY(N,F) static void c_##N(X86Ctx*ctx){RET(F((unsigned char)A(0)));}
CLASSIFY(isxdigit,isxdigit) CLASSIFY(isdigit,isdigit) CLASSIFY(isalpha,isalpha) CLASSIFY(isalnum,isalnum) CLASSIFY(isspace,isspace) CLASSIFY(toupper,toupper) CLASSIFY(tolower,tolower)
static void c_initterm(X86Ctx*ctx){uint32_t b=A(0),e=A(1);if(e<b||e-b>1048576||(b&3)||(e&3))rt_fatal("CRT initializer range invalid");for(uint32_t p=b;p<e;p+=4){uint32_t fn=ld32(p);if(fn){X86Ctx saved=*ctx;rt_call(ctx,fn,0,NULL);*ctx=saved;}}}
static pthread_mutex_t crt_guard=PTHREAD_MUTEX_INITIALIZER;
static HostMutex*crt_locks[128];
static HostMutex*crt_lock(unsigned i){if(i>=128)rt_fatal("CRT lock index outside bound");pthread_mutex_lock(&crt_guard);if(!crt_locks[i])crt_locks[i]=host_mutex_new();HostMutex*m=crt_locks[i];pthread_mutex_unlock(&crt_guard);return m;}
static void c_lock(X86Ctx*ctx){host_lock(crt_lock(A(0)));}
static void c_unlock(X86Ctx*ctx){host_unlock(crt_lock(A(0)));}
static uint32_t onexit_callbacks[256];static unsigned nonexit;
static void c_onexit(X86Ctx*ctx){if(nonexit==256)rt_fatal("CRT exit callback bound");onexit_callbacks[nonexit++]=A(0);RET(A(0));}
static void c_dllonexit(X86Ctx*ctx){uint32_t fn=A(0),bp=A(1),ep=A(2),b=ld32(bp),e=ld32(ep);if(b==UINT32_MAX)b=e=0;if(e<b||e-b>65536)rt_fatal("CRT DLL exit table outside bound");uint32_t bytes=e-b,g=rt_heap_alloc(bytes+4,0);if(bytes)memcpy(rt_ptr(g),rt_ptr(b),bytes);st32(g+bytes,fn);if(b)rt_heap_free(b);st32(bp,g);st32(ep,g+bytes+4);RET(fn);}
static _Thread_local uint32_t guest_errno;
static void c_errno(X86Ctx*ctx){if(!guest_errno)guest_errno=rt_heap_alloc(4,1);RET(guest_errno);}
static void c_setlocale(X86Ctx*ctx){const char*s=A(1)?rt_str(A(1)):NULL;const char*out=setlocale((int)A(0),s);if(!out){RET(0);return;}size_t n=strlen(out)+1;uint32_t g=rt_heap_alloc(n,0);memcpy(rt_ptr(g),out,n);RET(g);}
static void c_getenv(X86Ctx*ctx){X86Ctx saved=*ctx;uint32_t name=A(0),n=0,args[]={name,0,0};n=rt_call(ctx,rt_hle_thunk("kernel32.dll","GetEnvironmentVariableA"),3,args);*ctx=saved;if(!n){RET(0);return;}uint32_t g=rt_heap_alloc(n,0);uint32_t read[]={name,g,n};rt_call(ctx,rt_hle_thunk("kernel32.dll","GetEnvironmentVariableA"),3,read);*ctx=saved;RET(g);}
static void c_not_qualified(X86Ctx*ctx){(void)ctx;extern uint32_t rt_self_last_target(void);extern const char*rt_thunk_name(uint32_t);rt_fatal("CRT path requires qualification: %s",rt_thunk_name(rt_self_last_target()));}
#define DEF(N,F) {"msvcrt.dll",N,c_##F,0}
static const HleDef defs[]={
 DEF("malloc",malloc),DEF("free",free),DEF("??2@YAPAXI@Z",malloc),DEF("??3@YAXPAX@Z",free),DEF("memcpy",memcpy),DEF("memset",memset),DEF("memmove",memmove),DEF("_strdup",strdup),DEF("_mbstrlen",strlen),DEF("strncmp",strncmp),DEF("_stricmp",stricmp),DEF("_strnicmp",strnicmp),DEF("strchr",strchr),DEF("strrchr",strrchr),DEF("strstr",strstr),DEF("atoi",atoi),DEF("atof",atof),DEF("isxdigit",isxdigit),DEF("isdigit",isdigit),DEF("isalpha",isalpha),DEF("isalnum",isalnum),DEF("isspace",isspace),DEF("toupper",toupper),DEF("tolower",tolower),DEF("_initterm",initterm),DEF("_lock",lock),DEF("_unlock",unlock),DEF("_onexit",onexit),DEF("__dllonexit",dllonexit),DEF("_errno",errno),DEF("getenv",getenv),DEF("setlocale",setlocale),
 DEF("_vsnprintf",not_qualified),DEF("qsort",not_qualified),DEF("_controlfp",not_qualified),DEF("_clearfp",not_qualified),DEF("__CxxFrameHandler",not_qualified),DEF("_CxxThrowException",not_qualified),DEF("?terminate@@YAXXZ",not_qualified),DEF("??1type_info@@UAE@XZ",not_qualified),DEF("_XcptFilter",not_qualified),DEF("_amsg_exit",not_qualified),DEF("_purecall",not_qualified)
};
void oa_crt_register(void){rt_hle_register(defs,sizeof(defs)/sizeof(*defs));}
