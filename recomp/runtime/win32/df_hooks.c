/* Delta Force: the native additions at guest addresses (tools/lift.py DF_HOOKS / DF_OVERRIDES), one module per feature:
 *   df_net.c    LAN over TCP/IP, the NovaWorld stand-in
 *   df_frame.c  high frame rates
 *   df_res.c    resolutions, widescreen
 *   df_hw.c     DF1 Full Screen Hardware
 * A feature that has no module yet answers nothing (the weak definitions below). A hook runs before the instruction at
 * its address; an override at a function's first instruction, returning 1 when it did the whole call. */
#include "w32.h"

#ifdef DF_EDITION
#define WEAK __attribute__((weak))
WEAK int df_frame_hook(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
WEAK int df_res_hook(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
WEAK int df_hw_hook(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
WEAK int df_frame_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
WEAK int df_res_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
WEAK int df_hw_override(Ctx *c, uint32_t addr) { (void)c; (void)addr; return 0; }
int df_net_hook(Ctx *c, uint32_t addr);
int df_hook(Ctx *c, uint32_t addr)
{
    return df_net_hook(c, addr) || df_frame_hook(c, addr) || df_res_hook(c, addr) || df_hw_hook(c, addr);
}
int df_override(Ctx *c, uint32_t addr)
{
    return df_frame_override(c, addr) || df_res_override(c, addr) || df_hw_override(c, addr);
}
#endif
