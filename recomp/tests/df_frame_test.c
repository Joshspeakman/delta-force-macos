/* Real interpolation hooks: immediate mouse look, stable simulation, arbitrary cadence. */
#define DS_PLATFORM_H
#include <stdint.h>
#include <unistd.h>
static uint64_t test_now=1000000000;
static uint64_t ds_clock_ns(void) { return test_now; }
#include "../runtime/win32/df_frame.c"
#include <assert.h>
uint8_t *G_MEM;int w32_deterministic;
uint64_t dsr_cap_waited_us;double dsr_display_hz;
static int32_t rawx,rawy;
static uint64_t next_present;
void w32_raw_mouse_sample(int32_t *x,int32_t *y,int consume) { *x=rawx;*y=rawy;if(consume)rawx=rawy=0; }
uint64_t dsr_next_present_us(void) {return next_present;}
void dsr_cap_divide(int k) {}
uint32_t w32_callback(Ctx *c,uint32_t fn,int n,const uint32_t *args) {return 0;}
int main(void) {
 G_MEM=calloc(1,0x1000000);assert(G_MEM);Ctx c={0};
 uint32_t p=arrs[0].base;wr32(A_PLAYER,p);wr32(A_STATE,GAME_LOOP);wr32(A_CAM_TARGET,p);
 wr32(p,123);wr32(p+0x10,456);wr32(arrs[0].count,1);wr32(A_SENS,128);
#ifdef DF2_EDITION
 wr32(0xa6de88,1);wr32(0xe10384,1);
#else
 wr32(0xcc0884,1);
#endif
 ticks=1;mouse_tick=1;tick_starts();rawx=4;rawy=-2;mouse_sampled(&c);
 int32_t dy,dp;assert(turn(&c,4,-2,&dy,&dp));wr32(p+0x30,(uint32_t)dy);wr32(p+0x34,(uint32_t)dp);
 const int rates[]={60,120,144,240,360,777,1000};
 for(unsigned r=0;r<sizeof rates/sizeof *rates;r++) {
  for(int i=1;i<16;i++) {
   test_now=1000000000ull+i*1000000ull;sim_ms=1000;sim_known=1;rawx=i;rawy=0;
   uint32_t yaw=r32(p+0x30),pitch=r32(p+0x34);int32_t pending,unused;assert(turn(&c,i,0,&pending,&unused));
   assert(!render_starts(&c));assert(r32(p+0x30)==yaw+(uint32_t)pending);
   undo_all();assert(r32(p+0x30)==yaw&&r32(p+0x34)==pitch);assert(rawx==i);
   next_present=now_us()+1000000/rates[r];est_us=100;
   uint64_t start=start_time(now_us());assert(start>=now_us());assert(start<=next_present);
   next_present=0;
  }
 }
 /* A fast high-DPI swipe crosses signed-angle boundaries without undefined
  * negation; the engine uses wrapping 32-bit binary angles. */
 int32_t yaw,pitch;assert(turn(&c,1024,0,&yaw,&pitch));assert((uint32_t)yaw==0x80000000u);
 rawx=1024;rawy=-1023;assert(!render_starts(&c));assert(r32(p+0x34)==PITCH_HI);undo_all();
 assert(r32(p+0x30)==(uint32_t)dy&&r32(p+0x34)==(uint32_t)dp);
 free(G_MEM);puts("DF frame hooks: immediate per-frame mouse, restored simulation state, 60-1000 FPS deadlines passed");
}
