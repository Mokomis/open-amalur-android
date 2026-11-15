#include "map_shadow_delta.h"
extern int oa_fast_mapping;
extern int oa_fast_map_registry;
int oa_vk_guest_constant_map(void*,const void*,size_t,uint32_t*);
int oa_vk_guest_discard_upload(void*,void*,uint32_t,uint32_t*,uint32_t*);
int oa_vk_finish_discard_upload(void*,void*);
void oa_vk_restore_constant_shadow(void*);
extern int oa_direct_constant_upload;

/* koa.exe build cs:15032, draw-list function 00C961E0. Each listed
   Map is followed synchronously by a single copy or zero fill and Unmap. */
static uint32_t draw_discard_size(X86Ctx *ctx)
{
    uint32_t field;
    switch (lds32(ctx->esp)) {
    case 0x00C9650B: field = 0x18; break;  /* copy returns at 00C96533 */
    case 0x00C96677: field = 0x20; break;  /* copy returns at 00C9669F */
    case 0x00C96736: field = 0x24; break;  /* copy or zero fill */
    case 0x00C965DD: return 0x90;         /* zero fill returns at 00C965FF */
    case 0x00C96F4F: {
        uint32_t count = ld32(ctx->edi + 0x34);
        return count <= UINT32_MAX / 48 ? count * 48 : 0;
    }
    default: return 0;
    }
    return (uint32_t)ld16(ld32(ctx->edi + field)) * 16;
}

/* Native D3D mappings become guest32 shadows. Preserve the native pitches.
 * Write-discard never reads native write-combined memory. Other write modes
 * copy only bytes the guest changed, retaining untouched/no-overwrite regions. */
typedef struct{int used,fast,direct;uint32_t capacity;void*context,*resource;UINT sub,type;D3D11_MAPPED_SUBRESOURCE mapped;uint32_t guest,allocation;size_t rowbytes,rows,depth,slice,bytes;unsigned char*before;} ResourceMap;
static ResourceMap resource_maps[256];static unsigned resource_map_limit;static SRWLOCK map_lock=SRWLOCK_INIT;
/* Bounded scratch reuse: at most eight 4-MiB shadows and their baselines.
   Pool entries contain no resource references; Map exclusively owns a checkout. */
typedef struct{uint32_t allocation,capacity;unsigned char*before;} MapScratch;
static MapScratch map_scratch[8];static unsigned map_scratch_n;
static void scratch_take(ResourceMap*m){
 if(!m->fast)return;AcquireSRWLockExclusive(&map_lock);unsigned best=8;
 for(unsigned i=0;i<map_scratch_n;i++)if(map_scratch[i].capacity>=m->bytes&&(best==8||map_scratch[i].capacity<map_scratch[best].capacity))best=i;
 if(best!=8){MapScratch*s=&map_scratch[best];m->allocation=s->allocation;m->capacity=s->capacity;m->before=s->before;*s=map_scratch[--map_scratch_n];}
 ReleaseSRWLockExclusive(&map_lock);
}
static void scratch_put_locked(ResourceMap*m){
 if(m->fast&&m->capacity<=4u*1024u*1024u&&map_scratch_n<8){map_scratch[map_scratch_n++]=(MapScratch){m->allocation,m->capacity,m->before};return;}
 rt_heap_free(m->allocation);free(m->before);
}

static size_t format_rowbytes(DXGI_FORMAT fmt,UINT width,size_t*rows){
 unsigned f=(unsigned)fmt;
 if((f>=70&&f<=72)||(f>=79&&f<=81)){*rows=(*rows+3)/4;return ((size_t)width+3)/4*8;}
 if((f>=73&&f<=78)||(f>=82&&f<=84)||(f>=94&&f<=99)){*rows=(*rows+3)/4;return ((size_t)width+3)/4*16;}
 if(f>=1&&f<=4)return (size_t)width*16;
 if(f>=5&&f<=8)return (size_t)width*12;
 if(f>=9&&f<=22)return (size_t)width*8;
 if(f>=23&&f<=47)return (size_t)width*4;
 if(f>=48&&f<=59)return (size_t)width*2;
 if(f>=60&&f<=65)return width;
 if(f==66)return ((size_t)width+7)/8;
 if(f==67)return (size_t)width*4;
 if(f==68||f==69)return ((size_t)width+1)/2*4;
 if(f==85||f==86||f==115)return (size_t)width*2;
 if(f>=87&&f<=93)return (size_t)width*4;
 rt_fatal("mapped DXGI format %u needs explicit row/plane layout",f);
}
static UINT mip_dimension(UINT x,UINT mip){return mip>=32?1:((x>>mip)?(x>>mip):1);}
static void mapping_geometry(ResourceMap*m,Object*resource){
 m->rows=1;m->depth=1;UINT w=0,level;
 if(resource->kind==BUFFER){D3D11_BUFFER_DESC d;ID3D11Buffer_GetDesc((ID3D11Buffer*)resource->host,&d);if(m->sub)rt_fatal("mapped buffer subresource must be zero");m->rowbytes=d.ByteWidth;}
 else if(resource->kind==TEX2D){D3D11_TEXTURE2D_DESC d;ID3D11Texture2D_GetDesc((ID3D11Texture2D*)resource->host,&d);if(!d.MipLevels||m->sub>=d.MipLevels*d.ArraySize)rt_fatal("texture2D mapped subresource range");level=m->sub%d.MipLevels;w=mip_dimension(d.Width,level);m->rows=mip_dimension(d.Height,level);m->rowbytes=format_rowbytes(d.Format,w,&m->rows);}
 else if(resource->kind==TEX1D){D3D11_TEXTURE1D_DESC d;ID3D11Texture1D_GetDesc((ID3D11Texture1D*)resource->host,&d);if(!d.MipLevels||m->sub>=d.MipLevels*d.ArraySize)rt_fatal("texture1D mapped subresource range");level=m->sub%d.MipLevels;w=mip_dimension(d.Width,level);m->rowbytes=format_rowbytes(d.Format,w,&m->rows);}
 else if(resource->kind==TEX3D){D3D11_TEXTURE3D_DESC d;ID3D11Texture3D_GetDesc((ID3D11Texture3D*)resource->host,&d);if(!d.MipLevels||m->sub>=d.MipLevels)rt_fatal("texture3D mapped subresource range");level=m->sub;w=mip_dimension(d.Width,level);m->rows=mip_dimension(d.Height,level);m->depth=mip_dimension(d.Depth,level);m->rowbytes=format_rowbytes(d.Format,w,&m->rows);}
 else rt_fatal("Map unsupported resource class %s",kind_names[resource->kind]);
 if(m->rowbytes>0x10000000u||m->rows>65536||m->depth>65536)rt_fatal("resource map geometry bound");
}
static void context_map(X86Ctx*ctx){
 Object *r = object(A(0));
 void *context = self(ctx)->host;
 const int fast_mapping = __atomic_load_n(&oa_fast_mapping, __ATOMIC_RELAXED);
 const UINT sub = A(1), type = A(2), flags = A(3);
 const uint32_t out = A(4);
 if (r->kind == BUFFER && fast_mapping && out && !sub && !flags &&
     type == D3D11_MAP_WRITE_DISCARD &&
     __atomic_load_n(&oa_direct_constant_upload, __ATOMIC_RELAXED)) {
  const uint32_t written = draw_discard_size(ctx);
  uint32_t guest, bytes;
  if (written && oa_vk_guest_discard_upload(context, r->host, written, &guest, &bytes)) {
   st32(out, guest);
   st32(out+4, bytes);
   st32(out+8, bytes);
   RET(S_OK);
   return;
  }
 }
 ResourceMap temp={0};temp.fast=fast_mapping;temp.context=context;temp.resource=r->host;temp.sub=sub;temp.type=type;mapping_geometry(&temp,r);
 if(temp.type<D3D11_MAP_READ||temp.type>D3D11_MAP_WRITE_NO_OVERWRITE)rt_fatal("unqualified D3D Map mode %u",temp.type);
 HRESULT hr=ID3D11DeviceContext_Map((ID3D11DeviceContext*)temp.context,(ID3D11Resource*)temp.resource,temp.sub,temp.type,flags,out?&temp.mapped:NULL);
 static unsigned maps;if(maps++<6)rt_log("real Map %s sub=%u mode=%u rowbytes=%zu rows=%zu depth=%zu nativepitch=%u HRESULT=%#lx",kind_names[r->kind],temp.sub,temp.type,temp.rowbytes,temp.rows,temp.depth,temp.mapped.RowPitch,(unsigned long)hr);
 if(FAILED(hr)||!out){RET(hr);return;}
 size_t pitch=temp.mapped.RowPitch?temp.mapped.RowPitch:temp.rowbytes;
 if(pitch<temp.rowbytes)rt_fatal("native map pitch smaller than logical row");
 temp.slice=temp.mapped.DepthPitch?temp.mapped.DepthPitch:pitch*temp.rows;
 if(temp.depth>1&&temp.slice<pitch*(temp.rows-1)+temp.rowbytes)rt_fatal("native map slice overlaps rows");
 temp.bytes=temp.slice*(temp.depth-1)+pitch*(temp.rows-1)+temp.rowbytes;
 if(!temp.bytes||temp.bytes>0x10000000u)rt_fatal("map shadow exceeds 256 MiB probe bound");
 if (r->kind == BUFFER) {
  oa_vk_restore_constant_shadow(temp.resource);
  if (temp.fast && temp.type != D3D11_MAP_READ)
   temp.direct = oa_vk_guest_constant_map(temp.resource, temp.mapped.pData, temp.bytes, &temp.guest);
 }
 if(temp.direct){static unsigned logged;if(__atomic_load_n(&logged,__ATOMIC_RELAXED)<6){unsigned n=__atomic_fetch_add(&logged,1,__ATOMIC_RELAXED);if(n<6)rt_log("PERF direct guest constant Map bytes=%zu guest=%#x mode=%u",temp.bytes,temp.guest,temp.type);}}
 if(!temp.direct){
 scratch_take(&temp);if(!temp.allocation){temp.capacity=(uint32_t)temp.bytes;temp.allocation=rt_heap_alloc(temp.capacity+15,0);}temp.guest=(temp.allocation+15u)&~15u;if(!temp.allocation)rt_fatal("map shadow allocation failed");
 if(temp.type!=D3D11_MAP_WRITE_DISCARD){
  for(size_t z=0;z<temp.depth;z++)for(size_t y=0;y<temp.rows;y++){size_t offset=z*temp.slice+y*pitch;memcpy(rt_ptr(temp.guest+(uint32_t)offset),(unsigned char*)temp.mapped.pData+offset,temp.rowbytes);}
  if(temp.type!=D3D11_MAP_READ){if(!temp.before)temp.before=malloc(temp.capacity);if(!temp.before)rt_fatal("map baseline allocation failed");memcpy(temp.before,rt_ptr(temp.guest),temp.bytes);}
 }
 } /* CPU-only guest constant buffer maps require no copy or scratch checkout. */
 temp.used=1;AcquireSRWLockExclusive(&map_lock);unsigned slot=256;
 unsigned scan=__atomic_load_n(&oa_fast_map_registry,__ATOMIC_RELAXED)?resource_map_limit:256;
 for(unsigned i=0;i<scan;i++){ResourceMap*m=&resource_maps[i];if(!m->used){if(slot==256)slot=i;}else if(m->resource==temp.resource&&m->sub==temp.sub&&m->context==temp.context)rt_fatal("resource mapped twice without Unmap");}
 if(slot==256&&resource_map_limit<256)slot=resource_map_limit;
 if(slot==256)rt_fatal("concurrent resource map bound");resource_maps[slot]=temp;if(slot>=resource_map_limit)resource_map_limit=slot+1;ReleaseSRWLockExclusive(&map_lock);
 st32(out,temp.guest);st32(out+4,temp.mapped.RowPitch);st32(out+8,temp.mapped.DepthPitch);RET(hr);
}
static void context_unmap(X86Ctx*ctx){
 void*context=self(ctx)->host;void*resource=host_object(A(0));UINT sub=A(1);
 if (!sub && oa_vk_finish_discard_upload(context,resource)) return;
 AcquireSRWLockExclusive(&map_lock);ResourceMap*m=NULL;
 unsigned scan=__atomic_load_n(&oa_fast_map_registry,__ATOMIC_RELAXED)?resource_map_limit:256;
 for(unsigned i=0;i<scan;i++)if(resource_maps[i].used&&resource_maps[i].context==context&&resource_maps[i].resource==resource&&resource_maps[i].sub==sub){m=&resource_maps[i];break;}
 if(!m)rt_fatal("Unmap without a successful guest Map");
 size_t pitch=m->mapped.RowPitch?m->mapped.RowPitch:m->rowbytes;
 if(!m->direct&&m->type!=D3D11_MAP_READ){
  for(size_t z=0;z<m->depth;z++)for(size_t y=0;y<m->rows;y++){
   size_t offset=z*m->slice+y*pitch;unsigned char*source=rt_ptr(m->guest+(uint32_t)offset),*dest=(unsigned char*)m->mapped.pData+offset;
   if(m->type==D3D11_MAP_WRITE_DISCARD)memcpy(dest,source,m->rowbytes);
   else oa_shadow_delta(dest,source,m->before+offset,m->rowbytes,m->fast);
  }
 }
 ID3D11DeviceContext_Unmap((ID3D11DeviceContext*)context,(ID3D11Resource*)resource,sub);if(!m->direct)scratch_put_locked(m);memset(m,0,sizeof(*m));while(resource_map_limit&&!resource_maps[resource_map_limit-1].used)resource_map_limit--;ReleaseSRWLockExclusive(&map_lock);
}
