#ifndef OA_MAP_SHADOW_DELTA_H
#define OA_MAP_SHADOW_DELTA_H
#include <stddef.h>
#include <string.h>
/* Copy exactly the changed bytes. Unchanged destination bytes may be GPU-owned. */
static inline void oa_shadow_delta(unsigned char*dest,const unsigned char*source,const unsigned char*before,size_t n,int fast){
 size_t i=0;
 while(i<n){size_t end=n;if(fast){end=i+256;if(end>n)end=n;if(!memcmp(source+i,before+i,end-i)){i=end;continue;}}
  while(i<end){while(i<end&&source[i]==before[i])i++;size_t first=i;while(i<end&&source[i]!=before[i])i++;if(i>first)memcpy(dest+first,source+first,i-first);}
 }
}
#endif
