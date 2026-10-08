/* Delta Force 1 and 2: widescreen and other screen shapes (tools/lift.py: the 'res' entries of DF_HOOKS and DF_MOVED).
 *
 * The games offer six or seven 4:3 sizes. Here their biggest ordinary choice, "1024x768", renders at the shape of the
 * screen instead: 768 lines and as many columns as the screen's aspect gives (1184x768 on a 16:10 MacBook screen,
 * 1360x768 at 16:9, 1792x768 at 21:9), which the host scales to the screen like any other mode. DF_RES=WxH picks
 * another size (one taller than 768 lines is scaled down to 768, keeping its shape), DF_RES=off keeps 1024x768. The
 * other choices (640x480, 800x600, ...) and the menus (640x480) are as they were; the host shows them with bars.
 *
 * Wider than 4:3 sees more to the sides (Hor+): the 3D view keeps the vertical field of view of the 4:3 screen of
 * the same height, its horizontal angle grows (90 degrees at 4:3, 106 at 16:9; the scope's 22 degrees likewise).
 * Narrower than 4:3 (5:4) keeps the horizontal angle and sees more above and below. Limits: 768 lines (row tables
 * inside the renderers' structures, at fixed offsets in hundreds of instructions) and 2560 columns (the per-column
 * arrays the lifter moved to bigger blocks: DF_MOVED; DF2 keeps one per column pair with room for 2560). Widths are
 * multiples of 16 (DF1: its present copies 16-byte blocks of 8-bit pixels, the terrain draws column pairs) or 8 (DF2:
 * the hardware driver's copy of the CPU-drawn terrain goes 8 pixels at a time).
 *
 * DF1 (Watcom, software renderer at 8 or 32 bits):
 *   0x496510  set a video mode (w, h, windowed): 1024x768 becomes the custom size (every path: settings, mission
 *             start, the in-mission resolution keys)
 *   0x407af0  after the settings are read: max_res (df.cfg; 4 = 800x600 as shipped) is raised to 5 so the custom
 *             size can be chosen in the game's own Settings screen
 *   0x44a8fc  the projection set-up (0x44a820): ST(0) holds the half angle of the horizontal field of view, edx the
 *             view's width, ebx the request (+0x28 the screen height); the angle is widened so the focal length stays
 *             that of a 4:3 screen of the same height. The frustum planes follow from the same angle.
 *   0x428810, 0x402d50  HUD pictures (weap%i/pos%i.pcx) and the small font (f_sml%i.pcx) exist only for the 4:3 widths:
 *             the custom size uses the set of the 4:3 screen of its height (1024 at 768 lines, whose bars are H/8)
 *   0x473c9c  the end of "reset the 2D clip" (0x473c70, fixed 1023x767): HUD lines and boxes reach the right edge
 *   The z buffer (2 bytes per pixel below the top bar) and the terrain's per-column arrays are moved: DF_MOVED.
 *   The 640x480 windowed mode is unchanged (the game turns 800x600 and 1024x768 into 640x480 when windowed).
 *
 * DF2 (MSVC; Full Screen Software, Hardware (Direct3D, Hw_d3d.dll) and Windowed):
 *   0x47a2d0  set the software display (w, h, windowed): 1024x768 becomes the custom size (all three drivers)
 *   0x425d50, 0x426000  Hardware: the driver's set-mode call (w, h, 32, 1), likewise
 *   0x4260d4  Hardware: the size its copy of the CPU-drawn terrain and depth uses (0xa6df74/0xa6df78)
 *   0x46678e  the z buffer is allocated by the size choice (4 bytes per pixel): 4*W*H for the custom size
 *   0x4661da  the main view's call of the projection set-up (0x4048b0): the request's field of view (+0x48, degrees
 *             in 16.16; 90 or the scope's 22.5) widened as in DF1. Direct3D draws vertices the game projected itself,
 *             so this covers all three drivers.
 *   0x4665e0, 0x4672c0  the HUD's bitmaps and HUD.DEF layout (0x44a290) exist only for the 4:3 widths: the custom size
 *             loads the set of the 4:3 screen of its height, and after it (0x4665e5, 0x4672c5) the layout's positions
 *             move to that set's place on the screen: centred, the bottom bar's items to the bottom
 *   0x44878e, 0x44864d  the top and bottom bars are one linear copy each (bitmap width == screen width): drawn here
 *             row by row, centred, the sides filled with a strip of the bars' frame repeated
 *   0x4487a1, 0x448e68  the message lines in the top bar and the recent-messages list are spaced by the screen width
 *             (W*3/160): by the bitmap set's width instead, as the font is the set's. */
#include "w32.h"
#include <math.h>

#ifdef DF_EDITION
extern int w32_screen_w, w32_screen_h;
#define RD(a)    rt_r32(G_MEM, (uint32_t)(a))
#define WR(a, v) rt_w32(G_MEM, (uint32_t)(a), (uint32_t)(v))
#define RES_MAX_W 2560                           /* the moved per-column arrays (tools/lift.py DF_MOVED) */
#define RES_MAX_H 768                            /* the renderers' row tables */
#ifdef DF1_EDITION
#define RES_ALIGN 16
#else
#define RES_ALIGN 8
#endif
static int res_w, res_h;                         /* what the 1024x768 choice renders at; 0: 1024x768 */
static void res_init(void)
{
    static int done; if (done) return; done = 1;
    const char *e = getenv("DF_RES"); double w, h; int ew, eh;
    if (e && !strcmp(e, "off")) return;
    if (e && sscanf(e, "%dx%d", &ew, &eh) == 2 && ew >= 320 && eh >= 240) { w = ew; h = eh; }
    else {
        if (e) fprintf(stderr, "df: DF_RES=%s: not WxH or off; using the screen's shape\n", e);
        if (w32_screen_w <= 0 || w32_screen_h <= 0) return;
        w = 768.0 * w32_screen_w / w32_screen_h; h = 768;
    }
    if (h > RES_MAX_H) { w = w * RES_MAX_H / h; h = RES_MAX_H; }
    if (w > RES_MAX_W) { h = h * RES_MAX_W / w; w = RES_MAX_W; }
    int W = (int)(w / RES_ALIGN + 0.5) * RES_ALIGN, H = (int)(h + 0.5) & ~1;
    if (W < 320) W = 320;
    if (W == 1024 && H == 768) return;
    res_w = W; res_h = H;
    fprintf(stderr, "df: the 1024x768 choice renders at %dx%d (%s; Hor+)\n", W, H, e ? "DF_RES" : "the screen's shape");
}
/* ddraw_pal.c: the custom size is one of the display modes DirectDraw lists (DF2's Direct3D driver sets only a listed
 * one; the games' own lists take the 4:3 sizes by name and pass it over) */
int df_res_mode(uint32_t *w, uint32_t *h)
{
    res_init(); *w = (uint32_t)res_w; *h = (uint32_t)res_h;
    return res_w != 0;
}
/* the bitmap set (and HUD.DEF section) for a w x h screen: the biggest 4:3 width that fits */
static int art_width(int w, int h)
{
    static const int sets[] = {1024, 800, 640, 512, 400, 320};
    for (int k = 0; k < 6; k++) if (sets[k] <= w && sets[k] * 3 <= h * 4) return sets[k];
    return 320;
}
/* the half angle (radians) that keeps a 4:3 screen's focal length at this width: atan(tan(a) * w / (h * 4/3)) */
static double hor_plus(double half, double w, double h)
{
    double w43 = h * 4.0 / 3.0;
    return w > w43 + 0.5 ? atan(tan(half) * w / w43) : half;
}

#ifdef DF1_EDITION
static int custom_mode(void) { return res_w && (int)RD(0xd092c0) == res_w && (int)RD(0xd092c4) == res_h; }   /* view size */
int df_res_hook(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case 0x496510:                                       /* set a video mode (w, h, windowed) */
        res_init();
        if (res_w && RD(c->esp + 4) == 1024 && RD(c->esp + 8) == 768) { WR(c->esp + 4, res_w); WR(c->esp + 8, res_h); }
        return 1;
    case 0x407af0:                                       /* max_res was just read from df.cfg */
        res_init();
        if (res_w && (int)RD(0x67f4f4) < 5) WR(0x67f4f4, 5);
        return 1;
    case 0x44a8fc:                                       /* projection: ST(0) = half angle, edx = view width */
        if (custom_mode()) ST(0) = hor_plus(ST(0), (double)c->edx, (double)(int)RD(c->ebx + 0x28));
        return 1;
    case 0x428810:                                       /* HUD pictures for this width */
        if (custom_mode()) WR(c->esp + 4, art_width(res_w, res_h));
        return 1;
    case 0x402d50:                                       /* the small font: 320/400/640/800 */
        if (custom_mode()) { int a = art_width(res_w, res_h); WR(c->esp + 4, a == 512 ? 400 : a > 800 ? 800 : a); }
        return 1;
    case 0x473c9c:                                       /* 2D clip reset to 1023x767: to the screen's edges */
        if (custom_mode()) {
            uint32_t ctx = c->eax; int w = (int)RD(ctx + 0x1808), h = (int)RD(ctx + 0x180c);
            if (w > 1024) WR(ctx + 0x181c, w - 1);
            if (h > 768) WR(ctx + 0x1820, h - 1);
        }
        return 1;
    }
    return 0;
}
#endif

#ifdef DF2_EDITION
static int custom_mode(void) { return res_w && (int)RD(0xe0f9e0) == res_w && (int)RD(0xe0f9e4) == res_h; }   /* render size */
/* HUD.DEF, parsed for the bitmap set's width: x of item k at 0xa161d8 + 4k, y at 0xa165d8 + 4k. Item 6 is the map's
 * radius (no position), item 9 is never set. Every position moves right by the margin beside the centred bars; the
 * bottom bar's (the lower half) down to the bottom when the screen is taller than the set's 4:3 screen. */
#define HUD_X 0xa161d8u
#define HUD_Y 0xa165d8u
#define HUD_N 27
static void hud_layout(void)
{
    int a = art_width(res_w, res_h), ah = a * 3 / 4, dx = (res_w - a) / 2, dy = res_h - ah;
    if (getenv("DF_RESLOG")) {
        fprintf(stderr, "df: HUD.DEF %d (+%d, +%d below):", a, dx, dy);
        for (int k = 0; k < HUD_N; k++) fprintf(stderr, " %d,%d", (int)RD(HUD_X + 4 * k), (int)RD(HUD_Y + 4 * k));
        fprintf(stderr, "\n");
    }
    for (int k = 0; k < HUD_N; k++) {
        if (k == 6 || k == 9) continue;
        WR(HUD_X + 4 * k, (int)RD(HUD_X + 4 * k) + dx);
        if ((int)RD(HUD_Y + 4 * k) >= ah / 2) WR(HUD_Y + 4 * k, (int)RD(HUD_Y + 4 * k) + dy);
    }
}
/* A HUD bar: the bitmap (aw x ah, 32-bit) centred on its rows; each side is filled with a strip of the bar's frame
 * (8 columns just inside its outer edge) repeated, so the frame's top and bottom edges run on to the screen's. */
static void hud_bar(Ctx *c, uint32_t canvas, int top)
{
    int aw = (int)RD(top ? 0x86ada0 : 0x86adac), ah = (int)RD(top ? 0x86ada4 : 0x86adb0);
    int w = (int)RD(canvas + 0x2008), h = (int)RD(canvas + 0x200c);
    uint32_t src = RD(c->esp);
    if (aw < 32 || ah <= 0 || aw >= w || ah > h || RD(c->esp + 8) != (uint32_t)(aw * ah * 4)) return;
    int x0 = (w - aw) / 2, y0 = top ? 0 : h - ah;
    for (int r = 0; r < ah; r++) {
        uint32_t *d = (uint32_t *)GP(RD(canvas + 4 + 4 * (y0 + r))); const uint32_t *s = (const uint32_t *)GP(src + 4u * aw * r);
        for (int x = 0; x < x0; x++) d[x] = s[3 + ((x - x0) % 8 + 8) % 8];
        memcpy(d + x0, s, 4u * aw);
        for (int x = x0 + aw; x < w; x++) d[x] = s[aw - 11 + (x - x0 - aw) % 8];
    }
    WR(c->esp + 8, 0);                                   /* the original copy: nothing left to do */
}
int df_res_hook(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case 0x47a2d0:                                       /* software display (w, h, windowed) */
        res_init();
        if (res_w && RD(c->esp + 4) == 1024 && RD(c->esp + 8) == 768) { WR(c->esp + 4, res_w); WR(c->esp + 8, res_h); }
        return 1;
    case 0x425d50: case 0x426000:                        /* Hardware: the driver's set mode (w, h, 32, 1) */
        if (res_w && RD(c->esp) == 1024 && RD(c->esp + 4) == 768) { WR(c->esp, res_w); WR(c->esp + 4, res_h); }
        return 1;
    case 0x4260d4:                                       /* Hardware: its terrain copy's size (the 1024x768 case) */
        if (res_w) { WR(0xa6df74, res_w); WR(0xa6df78, res_h); }
        return 1;
    case 0x46678e:                                       /* z buffer by size choice: esi = the choice, [esp] = bytes */
        if (res_w && c->esi == 6) WR(c->esp, 4u * res_w * res_h);
        return 1;
    case 0x4661da: {                                     /* main view: [esp+4] = the request, +0x48 the angle */
        if (!custom_mode()) return 1;
        uint32_t req = RD(c->esp + 4); int fov = (int)RD(req + 0x48);
        double half = hor_plus(fov / 2.0 / 65536.0 * M_PI / 180.0, (double)(int)RD(req + 0x24), (double)(int)RD(req + 0x28));
        WR(req + 0x48, (uint32_t)(int)(2.0 * half * 180.0 / M_PI * 65536.0 + 0.5));
        return 1;
    }
    case 0x4665e0: case 0x4672c0:                        /* HUD bitmaps and layout for this width */
        if (custom_mode()) WR(c->esp, art_width(res_w, res_h));
        return 1;
    case 0x4665e5: case 0x4672c5:
        if (custom_mode()) hud_layout();
        return 1;
    case 0x4487a1: if (custom_mode()) c->eax = (uint32_t)art_width(res_w, res_h); return 1;   /* message lines: */
    case 0x448e68: if (custom_mode()) c->ecx = (uint32_t)art_width(res_w, res_h); return 1;   /* spaced by width */
    case 0x44878e: if (custom_mode()) hud_bar(c, c->edi, 1); return 1;   /* top bar; edi = the canvas */
    case 0x44864d: if (custom_mode()) hud_bar(c, c->esi, 0); return 1;   /* bottom bar; esi = the canvas */
    }
    return 0;
}
#endif
#endif
