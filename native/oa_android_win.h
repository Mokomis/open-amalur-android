#ifndef OA_ANDROID_WIN_H
#define OA_ANDROID_WIN_H
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <pthread.h>
#include <dlfcn.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#define __cdecl
#define __declspec(x)
#ifndef MAX_PATH
#define MAX_PATH 1024
#endif
#define INVALID_FILE_ATTRIBUTES 0xffffffffu
typedef pthread_mutex_t CRITICAL_SECTION;
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
static inline void InitializeCriticalSection(CRITICAL_SECTION*m){pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(m,&a);pthread_mutexattr_destroy(&a);}
static inline void EnterCriticalSection(CRITICAL_SECTION*m){pthread_mutex_lock(m);}
static inline void LeaveCriticalSection(CRITICAL_SECTION*m){pthread_mutex_unlock(m);}
static inline void AcquireSRWLockExclusive(SRWLOCK*m){pthread_mutex_lock(m);}
static inline void ReleaseSRWLockExclusive(SRWLOCK*m){pthread_mutex_unlock(m);}
static inline LONG InterlockedIncrement(volatile LONG*p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
static inline LONG InterlockedDecrement(volatile LONG*p){return __atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST);}
static inline LONG InterlockedExchange(volatile LONG*p,LONG v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
static inline uint64_t oa_clock_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER*p){p->QuadPart=oa_clock_ns();return TRUE;}
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER*p){p->QuadPart=1000000000;return TRUE;}
static inline DWORD GetTickCount(void){return oa_clock_ns()/1000000;}
static inline void*_aligned_malloc(size_t n,size_t a){void*p=NULL;return posix_memalign(&p,a,n)?NULL:p;}
#define _aligned_free free
static inline HMODULE LoadLibraryA(const char*n){return !strcmp(n,"vulkan-1.dll")?dlopen("libvulkan.so",RTLD_NOW|RTLD_LOCAL):NULL;}
#define GetProcAddress dlsym
static inline DWORD GetFileAttributesA(const char*p){struct stat s;return stat(p,&s)?INVALID_FILE_ATTRIBUTES:S_ISDIR(s.st_mode)?16:0;}
static inline int IsEqualGUID(const GUID*a,const GUID*b){return !memcmp(a,b,sizeof(*a));}
#ifndef IUnknown_QueryInterface
#define IUnknown_QueryInterface(p,...) ((p)->lpVtbl->QueryInterface(p, ##__VA_ARGS__))
#endif
#endif
