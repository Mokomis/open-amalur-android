/* Dispatch platform events to original guest callback methods.
 * Never call a native function with an original guest C++ object/vtable. */
typedef struct{uint32_t guest;int id,active;} Callback;
static Callback callbacks[128];static unsigned ncallbacks;static int manual_dispatch;
typedef struct{int user,id;unsigned char*param;int bytes;} NativeCallback;
static void manual_init(void){if(!manual_dispatch){((void(*)(void))symbol("SteamAPI_ManualDispatch_Init"))();manual_dispatch=1;}}
static void register_callback(X86Ctx*ctx){
 uint32_t g=ARG(0);int id=(int)ARG(1);manual_init();Callback*c=NULL;
 for(unsigned i=0;i<ncallbacks;i++)if(callbacks[i].guest==g)c=&callbacks[i];
 if(!c){if(ncallbacks==128)rt_fatal("Steam callback registry full");c=&callbacks[ncallbacks++];}
 *c=(Callback){g,id,1};st8(g+4,ld8(g+4)|1);st32(g+8,id);
 rt_log("registered original Steam callback id=%d object=%#x against platform dispatcher",id,g);
}
static void unregister_callback(X86Ctx*ctx){uint32_t g=ARG(0);for(unsigned i=0;i<ncallbacks;i++)if(callbacks[i].guest==g)callbacks[i].active=0;st8(g+4,ld8(g+4)&~1u);}
static uint32_t callback_call(X86Ctx*ctx,uint32_t g,unsigned slot,unsigned n,const uint32_t*args){X86Ctx saved=*ctx;ctx->ecx=g;uint32_t r=rt_call(ctx,ld32(ld32(g)+4*slot),n,args);*ctx=saved;return r;}
static void run_callbacks(X86Ctx*ctx){
 if(!manual_dispatch){((void(*)(void))symbol("SteamAPI_RunCallbacks"))();return;}
 int pipe=((int(*)(void))symbol("SteamAPI_GetHSteamPipe"))();
 ((void(*)(int))symbol("SteamAPI_ManualDispatch_RunFrame"))(pipe);
 NativeCallback event;unsigned count=0;
 while(((bool(*)(int,NativeCallback*))symbol("SteamAPI_ManualDispatch_GetNextCallback"))(pipe,&event)){
  if(++count>1024)rt_fatal("Steam event bound exhausted");
  for(unsigned i=0;i<ncallbacks;i++)if(callbacks[i].active&&callbacks[i].id==event.id){
   if(event.id!=1101 && event.id!=1102 && event.id!=1103)rt_fatal("Steam callback %d payload ABI not qualified",event.id);
   uint32_t g=callbacks[i].guest,size=callback_call(ctx,g,2,0,NULL);
   if(event.bytes<0||size!=(unsigned)event.bytes)rt_fatal("Steam callback %d ABI size mismatch: guest=%u native=%d",event.id,size,event.bytes);
   if(size>1048576)rt_fatal("Steam callback size outside probe bound");
   uint32_t payload=rt_heap_alloc(size?size:1,0);memcpy(rt_ptr(payload),event.param,size);
   rt_log("dispatch platform callback id=%d bytes=%u to original code",event.id,size);
   callback_call(ctx,g,0,1,&payload);rt_heap_free(payload);
  }
  ((void(*)(int))symbol("SteamAPI_ManualDispatch_FreeLastCallback"))(pipe);
 }
}
