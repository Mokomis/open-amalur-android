#include "oa_ui.h"
enum {MODE_SOLID, MODE_SPRITE, MODE_TEXT, MODE_TITLE};
struct oa_ui {
    float *vertices;
    unsigned count;
    uint16_t sprites[OA_SPR_COUNT][4];
    float atlas_w,atlas_h,field_w,field_h;
    struct {
        float px,ascent,line;
        struct {uint16_t x,y,w,h; int16_t ox,oy; float advance;} glyphs[95];
    } fonts[2];
};
static void vertex(oa_ui *u,float x,float y,float s,float t,const float c[4],float mode){float *v=u->vertices+u->count;v[0]=x;v[1]=y;v[2]=s;v[3]=t;memcpy(v+4,c,16);v[8]=mode;u->count+=9;}
static int room(const oa_ui *u,unsigned vertices){return u->vertices&&u->count+vertices*9u<=OA_PAUSE_VERTEX_FLOATS;}
static void quad(oa_ui *u,float x,float y,float w,float h,float s0,float t0,float s1,float t1,const float a[4],const float b[4],const float c[4],const float d[4],float mode){
 if(!room(u,6))return;vertex(u,x,y,s0,t0,a,mode);vertex(u,x+w,y,s1,t0,b,mode);vertex(u,x+w,y+h,s1,t1,c,mode);vertex(u,x,y,s0,t0,a,mode);vertex(u,x+w,y+h,s1,t1,c,mode);vertex(u,x,y+h,s0,t1,d,mode);}
void oa_ui_rect(oa_ui *u,float x,float y,float w,float h,const float c[4]){quad(u,x,y,w,h,0,0,0,0,c,c,c,c,MODE_SOLID);}
void oa_ui_fade(oa_ui *u,float x,float y,float w,float h,const float l[4],const float r[4]){quad(u,x,y,w,h,0,0,0,0,l,r,r,l,MODE_SOLID);}
void oa_ui_fade_v(oa_ui *u,float x,float y,float w,float h,const float t[4],const float b[4]){quad(u,x,y,w,h,0,0,0,0,t,t,b,b,MODE_SOLID);}
void oa_ui_sprite(oa_ui *u,int id,float x,float y,float w,float h,float fraction,const float c[4]){const uint16_t *s=u->sprites[id];float f=fraction<0?0:fraction>1?1:fraction;
 quad(u,x,y,w*f,h,(s[0]+.5f)/u->atlas_w,(s[1]+.5f)/u->atlas_h,(s[0]+(s[2]-1)*f+.5f)/u->atlas_w,(s[1]+s[3]-.5f)/u->atlas_h,c,c,c,c,MODE_SPRITE);}
void oa_ui_title(oa_ui *u,float x,float y,float w,float h){const float c[4]={1,1,1,1};quad(u,x,y,w,h,0,0,1,1,c,c,c,c,MODE_TITLE);}
void oa_ui_disk(oa_ui *u,float x,float y,float r,const float c[4]){int i;for(i=0;i<48;i++){float a=i*.1308997f,b=(i+1)*.1308997f;if(!room(u,3))return;vertex(u,x,y,0,0,c,0);vertex(u,x+r*cosf(a),y+r*sinf(a),0,0,c,0);vertex(u,x+r*cosf(b),y+r*sinf(b),0,0,c,0);}}
void oa_ui_ring(oa_ui *u,float x,float y,float r,float t,const float c[4]){int i;float q=r-t;for(i=0;i<64;i++){float a=i*.09817477f,b=(i+1)*.09817477f,ca=cosf(a),sa=sinf(a),cb=cosf(b),sb=sinf(b);if(!room(u,6))return;
 vertex(u,x+q*ca,y+q*sa,0,0,c,0);vertex(u,x+r*ca,y+r*sa,0,0,c,0);vertex(u,x+r*cb,y+r*sb,0,0,c,0);vertex(u,x+q*ca,y+q*sa,0,0,c,0);vertex(u,x+r*cb,y+r*sb,0,0,c,0);vertex(u,x+q*cb,y+q*sb,0,0,c,0);}}
float oa_ui_measure(const oa_ui *u,int font,float px,const char *s){float total=0,scale=px/u->fonts[font].px;for(;*s;s++){unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';total+=u->fonts[font].glyphs[c-32].advance*scale;}return total;}
void oa_ui_text(oa_ui *u,int font,float x,float y,float px,int align,const float color[4],const char *s){
 float scale=px/u->fonts[font].px;
 if(align!=OA_ALIGN_LEFT){float m=oa_ui_measure(u,font,px,s);x-=align==OA_ALIGN_CENTER?m*.5f:m;}
 for(;*s;s++){unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';
  {const float gx=x+u->fonts[font].glyphs[c-32].ox*scale,gy=y+u->fonts[font].glyphs[c-32].oy*scale,a=u->fonts[font].glyphs[c-32].x,b=u->fonts[font].glyphs[c-32].y,w=u->fonts[font].glyphs[c-32].w,h=u->fonts[font].glyphs[c-32].h;
   if(c!=32)quad(u,gx,gy,w*scale,h*scale,a/u->field_w,b/u->field_h,(a+w)/u->field_w,(b+h)/u->field_h,color,color,color,color,MODE_TEXT);
   x+=u->fonts[font].glyphs[c-32].advance*scale;}}
}
