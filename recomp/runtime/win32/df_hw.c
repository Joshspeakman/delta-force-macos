/* Delta Force 1: Full Screen Hardware, a renderer built like Delta Force 2's Direct3D driver.
 *
 * The voxel terrain and its depth stay on the CPU, exactly as in the software renderer. Everything else 3D (soldiers,
 * buildings, trees, vehicles, effects, the first-person weapon) is a 3DI model, and every model polygon reaches one
 * function, 0x462df0(flags, verts, material) (cdecl), or its span-buffer twin 0x46cac0 with the same arguments (the
 * objects queued in span mode). Both are taken over here while the "object part" of the 3D frame runs, and their
 * triangles go to the GPU with filtered textures, depth-tested against the terrain's depth:
 *   0x4856f8  (in 0x485610, the 3D frame: after the terrain and the occlusion test 0x4854d0, before the queued
 *             polygon lists are flushed) the segment opens. Nothing is sent until the first polygon: then the CPU's
 *             colour and 16-bit depth in the view's clip rectangle go up (DSR_SURFACE_UPLOAD, DSR_DEPTH_UPLOAD).
 *   0x485739  (after the tracers, before the returns) the segment closes: the GPU picture comes back into the game's
 *             own colour rows (DSR_READBACK), so the HUD, the 65K conversion and the present run unchanged.
 * Inside the segment, a CPU pass that must see the polygons (the water planes 0x48d040 / 0x48d570, lines, drawing
 * kinds not handled here) first brings colour and depth back (sync), and the next polygon uploads them again.
 *
 * The game's polygon: three 0x20-byte vertices {x, y 16.16 screen; z: eye depth 16.16 << 6, of which
 * z >> 16 is the 16-bit depth the terrain shares; u, v 16.16 in texture widths; shade 16.16, a row 0..63 of the
 * palette's 64 x 256 colour table (row 32 = the plain colours, 0..31 darker by a gain, 33..63 towards a haze colour)}.
 * The drawing kind is flags & 0x73c (0x4683b0, the 32-bit table filled by 0x4686d0):
 *   0x00          one colour, the material's +0x14 as a pixel            z test (strictly less) and write
 *   0x04 / 0x08   palette colour +0x14 at the shade row, flat / gouraud    z test and write
 *   0x14 / 0x18   texture through the shade row, flat / gouraud            z test and write; +0x20: texel 0 is a hole
 *   0x21x         the material's colour over the scene, alpha = texel/255  z test, no write
 *   0x41x / 0x43x half texture colour (row 32) + half the scene, texel 0 a hole; z test, no write
 *   0x10, 0x30, 0x10x, 0x20x: nothing in 32-bit colour.
 * The shade is reproduced with Direct3D's fixed function: texture = the row-32 colours, diffuse = the row's gain over
 * row 32 (rows <= 32 are a per-channel multiply of it, within 2/255) and, for the haze rows, stage 1 blends towards
 * the haze colour (TFACTOR) by diffuse alpha (rows 33..63 are row 32 lerped towards one colour, within ~10/255).
 *
 * Guest state is only ever reached through the current view, [0x8ee1e0]: +0x34..0x40 the clip rectangle (inclusive),
 * +0x50 bits per pixel, +0x68 the colour row pointers, +0xc68 the depth row pointers (so a moved or resized buffer
 * keeps working). 256 colours (8-bit views) are left to the CPU.
 *
 * On: the Colors setting "Hardware" (renderer = 3 in df.cfg; see colors_hook), or DF_HARDWARE=1 in any 32-bit mode.
 * Development switches: DF_HWLOG=1 a line a second (polygons, draws, textures, syncs and their cost, the object part's
 * time with the GPU or without) and each new kind of polygon; DF_HW_DUAL=n (with DF_HW_DUAL_DIR, DF_HW_DUAL_FROM) the
 * CPU draws everything too and every n-th frame both pictures are written out; DF_HW_POINT=1 nearest, affine texturing
 * as the CPU samples; DF_HW_MIPS=0 no mipmaps; DF_HW_FLOOD=level raises a mission's water (the plane-pass sync). */
#include "w32.h"
#include "dsr_producer.h"
#include "../../../src/dsr/dsr_proto.h"
#include <string.h>
#include <time.h>

#if defined(DF_EDITION) && defined(DF1_EDITION)

#define VIEW_PTR   0x008ee1e0u                       /* the view being drawn */
#define PAL_BASE   0x00bfc100u                       /* palettes: +4 the 32-bit colour table (64 rows x 256), stride 0x614 */
#define PAL_STRIDE 0x614u
#define NPAL       16

static int hw_log = -1;
static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
static uint32_t r32(uint32_t a) { return rt_r32(G_MEM, a); }
static void w32(uint32_t a, uint32_t v) { rt_w32(G_MEM, a, v); }

/* ---- on or off ---- */
int df_hw_setting;                                   /* the Colors value is "Hardware" (set by the menu/cfg hooks) */
static int hw_wanted(void)
{
    static int env = -1; if (env < 0) { const char *e = getenv("DF_HARDWARE"); env = e && *e == '1'; }
    return env || df_hw_setting;
}

/* ---- render state, sent only when it changes ---- */
static uint32_t rs_cur[256], tss_cur[2][32], tex_cur;
static void state_forget(void) { memset(rs_cur, 0xa5, sizeof rs_cur); memset(tss_cur, 0xa5, sizeof tss_cur); tex_cur = 0xa5a5a5a5u; }
static void rs(uint32_t st, uint32_t v)
{
    if (rs_cur[st] == v) return;
    rs_cur[st] = v; uint32_t *p = cmd_begin(DSR_RENDER_STATE, 8); p[0] = st; p[1] = v; cmd_end();
}
static void tss(uint32_t stage, uint32_t t, uint32_t v)
{
    if (tss_cur[stage][t] == v) return;
    tss_cur[stage][t] = v; uint32_t *p = cmd_begin(DSR_TSS, 12); p[0] = stage; p[1] = t; p[2] = v; cmd_end();
}
static void texture(uint32_t id)
{
    if (tex_cur == id) return;
    tex_cur = id; uint32_t *p = cmd_begin(DSR_TEXTURE, 8); p[0] = 0; p[1] = id; cmd_end();
}

/* ---- the frame segment ---- */
static struct {
    int open, gpu;                                   /* inside the object part; the GPU holds the newest picture */
    uint32_t view, x0, y0, w, h;                     /* the clip rectangle being drawn */
    uint32_t rt, ds, rw, rh;                         /* the GPU colour and depth surfaces (the clip rectangle's size) */
    uint32_t frame;
    uint32_t *keep; size_t nkeep;                    /* the CPU picture as uploaded (pixels the GPU left alone keep it) */
    float *zf; size_t nzf;
} S;
static struct { uint32_t polys, draws, tex_new, tex_up, syncs, zsyncs, planes, skipped, lines, frames; uint64_t rb_us, up_us; } st_;

static void destroy(uint32_t id) { if (!id) return; uint32_t *p = cmd_begin(DSR_SURFACE_DESTROY, 4); p[0] = id; cmd_end(); surface_id_free(id); }
/* colour and depth surfaces for a clip size; a few sizes are kept, as more than one view can be drawn a frame (the
 * picture-in-picture view) */
static void surfaces(uint32_t w, uint32_t h)
{
    static struct { uint32_t rt, ds, w, h, used; } pool[4];
    int k, lru = 0;
    for (k = 0; k < 4; k++) if (pool[k].rt && pool[k].w == w && pool[k].h == h) break;
    if (k == 4) {
        for (k = 1; k < 4; k++) if (pool[k].used < pool[lru].used) lru = k;
        k = lru; destroy(pool[k].rt); destroy(pool[k].ds);
        pool[k].rt = next_object_id(); pool[k].ds = next_object_id(); pool[k].w = w; pool[k].h = h;
        uint32_t *p = cmd_begin(DSR_SURFACE_CREATE, 40); memset(p, 0, 40); p[0] = pool[k].rt; p[1] = w; p[2] = h; p[3] = 1; p[4] = 2; p[7] = 1; cmd_end();
        p = cmd_begin(DSR_SURFACE_CREATE, 40); memset(p, 0, 40); p[0] = pool[k].ds; p[1] = w; p[2] = h; p[3] = 2; p[4] = 3; p[7] = 1; cmd_end();
    }
    pool[k].used = S.frame; S.rt = pool[k].rt; S.ds = pool[k].ds; S.rw = w; S.rh = h;
}
static uint32_t crow(uint32_t y) { return r32(S.view + 0x68 + 4 * y) + 4 * S.x0; }     /* the colour row, at the clip's left */
static uint32_t zrow(uint32_t y) { return r32(S.view + 0xc68 + 4 * y) + 2 * S.x0; }

static void batch_flush(void);
/* the GPU takes over the picture: the CPU's colour and depth go up, and the fixed state is set */
static void gpu_take(void)
{
    if (S.gpu) return;
    uint64_t t0 = now_us();
    surfaces(S.w, S.h);
    size_t n = (size_t)S.w * S.h;
    if (S.nkeep < n) { free(S.keep); S.keep = malloc(n * 4); S.nkeep = n; }
    uint32_t *p = cmd_begin(DSR_SET_RT, 8); p[0] = S.rt; p[1] = S.ds; cmd_end();
    p = cmd_begin(DSR_SURFACE_UPLOAD, 24 + (uint32_t)n * 4);
    p[0] = S.rt; p[1] = 0; p[2] = 0; p[3] = S.w; p[4] = S.h; p[5] = S.w * 4;
    for (uint32_t y = 0; y < S.h; y++) memcpy(S.keep + (size_t)y * S.w, G_MEM + crow(S.y0 + y), S.w * 4);
    memcpy(p + 6, S.keep, n * 4); cmd_end();
    p = cmd_begin(DSR_DEPTH_UPLOAD, 12 + (uint32_t)n * 4); p[0] = S.ds; p[1] = S.w; p[2] = S.h;
    float *z = (float *)(p + 3);
    for (uint32_t y = 0; y < S.h; y++) { const uint16_t *r = (const uint16_t *)(G_MEM + zrow(S.y0 + y));
        for (uint32_t x = 0; x < S.w; x++) z[(size_t)y * S.w + x] = (float)r[x] * (1.0f / 65535.0f); }
    cmd_end();
    p = cmd_begin(DSR_VIEWPORT, 24); p[0] = 0; p[1] = 0; p[2] = S.w; p[3] = S.h; { float mz[2] = {0, 1}; memcpy(p + 4, mz, 8); } cmd_end();
    state_forget();                                  /* everything is sent again once a segment */
    rs(7, 1); rs(23, 2); rs(22, 1); rs(28, 0); rs(29, 0); rs(52, 0); rs(168, 15); rs(8, 3); rs(9, 2);     /* z LESS, no culling, no fog */
    tss(0, 11, 0); tss(0, 24, 0); tss(0, 13, 1); tss(0, 14, 1);                                          /* coordinates as given, wrapped */
    tss(1, 11, 0); tss(1, 24, 0);
    S.gpu = 1; st_.up_us += now_us() - t0;
}
/* DF_HW_DUAL=<n> (development): the CPU draws every polygon as well and its picture stays on screen; every n-th
 * segment both pictures of that same frame go to DF_HW_DUAL_DIR as <frame>_cpu.ppm / <frame>_gpu.ppm */
static int dual_mode(void) { static int v = -1; if (v < 0) { const char *e = getenv("DF_HW_DUAL"); v = e ? atoi(e) : 0; } return v; }
static void ppm(const char *path, const uint32_t *px, size_t pitch, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) { uint32_t c = px[y * pitch + x]; uint8_t rgb[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
static void dual_dump(const uint32_t *gpu)
{
    static int from = -1; if (from < 0) { const char *e = getenv("DF_HW_DUAL_FROM"); from = e ? atoi(e) : 0; }   /* (from that segment on) */
    if (S.frame % (uint32_t)dual_mode() || (int)S.frame < from) return;
    const char *dir = getenv("DF_HW_DUAL_DIR"); if (!dir) return;
    char path[1024]; uint32_t *cpu = malloc((size_t)S.w * S.h * 4);
    for (uint32_t y = 0; y < S.h; y++) memcpy(cpu + (size_t)y * S.w, G_MEM + crow(S.y0 + y), S.w * 4);
    snprintf(path, sizeof path, "%s/%05u_cpu.ppm", dir, S.frame); ppm(path, cpu, S.w, S.w, S.h);
    snprintf(path, sizeof path, "%s/%05u_gpu.ppm", dir, S.frame); ppm(path, gpu, S.w, S.w, S.h);
    free(cpu); fprintf(stderr, "df_hw: frame %u dumped\n", S.frame);
}
/* the CPU takes the picture back (colour, and depth when a CPU pass will test against it) */
static void cpu_take(int depth)
{
    if (!S.gpu) return;
    batch_flush();
    uint64_t t0 = now_us();
    size_t n = (size_t)S.w * S.h; static uint32_t *tmp; static size_t ntmp;
    if (ntmp < n) { free(tmp); tmp = malloc(n * 4); ntmp = n; }
    int32_t r[4] = {0, 0, (int32_t)S.w, (int32_t)S.h};
    int ok = cmd_readback(S.rt, r, (uint8_t *)tmp, S.w * 4, 4);
    if (ok && dual_mode()) dual_dump(tmp);
    else if (ok) {
        /* a pixel whose colour the GPU left as uploaded keeps the CPU's own value; a drawn one gets the top byte the
         * game's colour tables carry, (r + 2g + b) / 4 (checked on every entry of the tables) */
        for (uint32_t y = 0; y < S.h; y++) {
            uint32_t *d = (uint32_t *)(G_MEM + crow(S.y0 + y)); const uint32_t *s = tmp + (size_t)y * S.w, *k = S.keep + (size_t)y * S.w;
            for (uint32_t x = 0; x < S.w; x++) {
                uint32_t c = s[x] & 0xffffffu;
                if (c == (k[x] & 0xffffffu)) { d[x] = k[x]; continue; }
                d[x] = c | (((c >> 16 & 255) + 2 * (c >> 8 & 255) + (c & 255)) >> 2) << 24;
            }
        }
    } else if (!ok && hw_log) fprintf(stderr, "df_hw: readback failed\n");
    if (depth && !dual_mode()) {                     /* the depth, back to 16 bits (the polygons' depth truncated, as the CPU stores it) */
        if (S.nzf < n) { free(S.zf); S.zf = malloc(n * 4); S.nzf = n; }
        if (cmd_depth_readback(S.ds, r, S.zf, S.w * 4))
            for (uint32_t y = 0; y < S.h; y++) {
                uint16_t *d = (uint16_t *)(G_MEM + zrow(S.y0 + y)); const float *z = S.zf + (size_t)y * S.w;
                for (uint32_t x = 0; x < S.w; x++) { float v = z[x] * 65535.0f + 0.01f; d[x] = v <= 0 ? 0 : v >= 65535.0f ? 0xffff : (uint16_t)v; }
            }
        else if (hw_log) fprintf(stderr, "df_hw: depth readback failed\n");
        st_.zsyncs++;
    }
    S.gpu = 0; st_.rb_us += now_us() - t0; st_.syncs++;
}

/* ---- palettes: the gain of each shade row over row 32, and the haze ---- */
typedef struct { uint32_t table, hash; uint8_t gain[64][3], alpha[64]; uint32_t haze; uint32_t frame; } Pal;
static Pal pals[NPAL];
static uint32_t hash_words(uint32_t a, uint32_t nwords, uint32_t step)
{
    uint32_t h = 2166136261u;
    for (uint32_t k = 0; k < nwords; k += step) h = (h ^ r32(a + 4 * k)) * 16777619u;
    return h;
}
static Pal *pal_get(uint32_t pal)
{
    if (pal >= NPAL) return 0;
    Pal *p = &pals[pal]; uint32_t table = r32(PAL_BASE + pal * PAL_STRIDE + 4);
    if (!table) return 0;
    if (p->frame == S.frame && p->table == table) return p;
    uint32_t h = hash_words(table, 64 * 256, 7); p->frame = S.frame;
    if (p->table == table && p->hash == h) return p;
    p->table = table; p->hash = h;
    const uint8_t *t = G_MEM + table;                /* entries 0xTTRRGGBB, little-endian: b g r top */
    uint32_t hz = r32(table + 63 * 1024 + 4); p->haze = hz & 0xffffffu;
    for (int row = 0; row < 64; row++) {
        const uint8_t *e = t + row * 1024, *b = t + 32 * 1024;
        if (row <= 32) {                             /* a multiply of row 32, per channel */
            for (int ch = 0; ch < 3; ch++) { double num = 0, den = 0;
                for (int i = 1; i < 256; i++) { double c = e[4 * i + 2 - ch], c0 = b[4 * i + 2 - ch]; num += c * c0; den += c0 * c0; }
                double g = den > 0 ? num / den : 1; p->gain[row][ch] = (uint8_t)(g >= 1 ? 255 : g * 255 + 0.5); }
            p->alpha[row] = 255;
        } else {                                     /* row 32 lerped towards the haze: the fraction */
            double num = 0, den = 0;
            for (int i = 1; i < 256; i++) for (int ch = 0; ch < 3; ch++) {
                double c = e[4 * i + ch], c0 = b[4 * i + ch], hh = (hz >> (8 * ch)) & 255; num += (c - c0) * (hh - c0); den += (hh - c0) * (hh - c0); }
            double f = den > 0 ? num / den : 0; if (f < 0) f = 0; if (f > 1) f = 1;
            p->gain[row][0] = p->gain[row][1] = p->gain[row][2] = 255; p->alpha[row] = (uint8_t)((1 - f) * 255 + 0.5);
        }
    }
    return p;
}

/* ---- textures: the material's texels through palette row 32, by mode ---- */
enum { TM_OPAQUE, TM_KEY, TM_ALPHA };                /* texel 0 a colour / a hole / the texel as alpha (white) */
typedef struct { uint32_t texels, table, lw, lh, mode, gid, hash, frame, levels; uint32_t ids[12]; } Tex;
#define NTEX 4096
static Tex texs[NTEX]; static int ntex;
static int point_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DF_HW_POINT"); v = e && *e == '1'; } return v; }   /* as the CPU samples: nearest, affine */
static int mips_on(void) { static int v = -1; if (v < 0) { const char *e = getenv("DF_HW_MIPS"); v = !(e && *e == '0') && !point_on(); } return v; }
static void bleed(uint32_t *px, uint32_t w, uint32_t h)          /* a hole takes its visible neighbours' colour (filtering) */
{
    uint8_t *done = malloc((size_t)w * h);
    for (uint32_t k = 0; k < w * h; k++) done[k] = px[k] >> 24 != 0;
    for (int pass = 1; pass <= 3; pass++)
        for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
            uint32_t k = y * w + x; if (done[k]) continue;
            uint32_t r = 0, g = 0, b = 0, n = 0; const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
            for (int j = 0; j < 4; j++) {
                uint32_t xx = (x + w + (uint32_t)dx[j]) % w, yy = (y + h + (uint32_t)dy[j]) % h, q = yy * w + xx;   /* wrapped, as sampled */
                if (!done[q] || done[q] > pass) continue;
                r += px[q] >> 16 & 255; g += px[q] >> 8 & 255; b += px[q] & 255; n++;
            }
            if (n) { px[k] = (r / n) << 16 | (g / n) << 8 | (b / n); done[k] = (uint8_t)(pass + 1); }
        }
    free(done);
}
static void tex_upload(Tex *t)
{
    uint32_t w = 1u << t->lw, h = 1u << t->lh, n = w * h;
    uint32_t *px = malloc((size_t)n * 4); const uint8_t *src = G_MEM + t->texels; const uint32_t *tab = (const uint32_t *)(G_MEM + t->table);
    for (uint32_t k = 0; k < n; k++) {
        uint8_t i = src[k];
        if (t->mode == TM_ALPHA) px[k] = (uint32_t)i << 24 | 0xffffffu;
        else px[k] = (tab[i] & 0xffffffu) | (t->mode == TM_KEY && !i ? 0u : 0xff000000u);
    }
    if (t->mode == TM_KEY) bleed(px, w, h);
    if (!t->gid) {
        t->levels = mips_on() ? (t->lw > t->lh ? t->lw : t->lh) + 1 : 1; if (t->levels > 12) t->levels = 12;
        t->gid = next_object_id();
        uint32_t *p = cmd_begin(DSR_SURFACE_CREATE, 40); memset(p, 0, 40);
        p[0] = t->gid; p[1] = w; p[2] = h; p[3] = 3; p[4] = 1; p[7] = t->levels; cmd_end();
        t->ids[0] = t->gid;
        for (uint32_t l = 1; l < t->levels; l++) {
            t->ids[l] = next_object_id(); p = cmd_begin(DSR_SURFACE_CREATE, 40); memset(p, 0, 40);
            p[0] = t->ids[l]; p[1] = w >> l ? w >> l : 1; p[2] = h >> l ? h >> l : 1; p[3] = 3; p[4] = 1; p[5] = t->gid; p[6] = l; p[7] = t->levels; cmd_end();
        }
        st_.tex_new++;
    }
    uint32_t lw = w, lh = h;
    for (uint32_t l = 0; l < t->levels; l++) {
        uint32_t *p = cmd_begin(DSR_SURFACE_UPLOAD, 24 + lw * lh * 4);
        p[0] = t->ids[l]; p[1] = 0; p[2] = 0; p[3] = lw; p[4] = lh; p[5] = lw * 4; memcpy(p + 6, px, (size_t)lw * lh * 4); cmd_end();
        if (l + 1 == t->levels) break;
        uint32_t nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;   /* the next level: a box filter, colour weighted by alpha */
        for (uint32_t y = 0; y < nh; y++) for (uint32_t x = 0; x < nw; x++) {
            uint32_t a = 0, r = 0, g = 0, b = 0, ru = 0, gu = 0, bu = 0, cnt = 0;
            for (uint32_t j = 0; j < 2; j++) for (uint32_t i = 0; i < 2; i++) {
                uint32_t sx = x * 2 + i < lw ? x * 2 + i : lw - 1, sy = y * 2 + j < lh ? y * 2 + j : lh - 1, c = px[sy * lw + sx], ca = c >> 24;
                a += ca; r += (c >> 16 & 255) * ca; g += (c >> 8 & 255) * ca; b += (c & 255) * ca;
                ru += c >> 16 & 255; gu += c >> 8 & 255; bu += c & 255; cnt++;
            }
            uint32_t o = a ? (r / a) << 16 | (g / a) << 8 | (b / a) : (ru / cnt) << 16 | (gu / cnt) << 8 | (bu / cnt);
            px[y * nw + x] = o | (a / cnt) << 24;
        }
        lw = nw; lh = nh;
    }
    free(px); st_.tex_up++;
}
static uint32_t tex_slot(const Tex *t) { return ((t->texels * 2654435761u) ^ (t->table * 40503u) ^ (t->lw << 4 | t->lh << 8 | t->mode)) % NTEX; }
/* textures not used since segment `before` go (a mission's set, once the next one runs), and the table is rebuilt */
static void tex_evict(uint32_t before)
{
    static Tex keep[NTEX]; int n = 0;
    for (int k = 0; k < NTEX; k++) {
        Tex *t = &texs[k]; if (!t->texels) continue;
        if (t->frame >= before) { keep[n++] = *t; continue; }
        for (uint32_t l = 0; l < t->levels; l++) destroy(t->ids[l]);
    }
    memset(texs, 0, sizeof texs); ntex = n;
    for (int k = 0; k < n; k++) { uint32_t h = tex_slot(&keep[k]); while (texs[h].texels) h = (h + 1) % NTEX; texs[h] = keep[k]; }
}
static uint32_t tex_get(uint32_t texels, uint32_t table, uint32_t lw, uint32_t lh, uint32_t mode)
{
    if (!texels || lw > 10 || lh > 10) return 0;
    uint32_t key = (texels * 2654435761u) ^ (table * 40503u) ^ (lw << 4 | lh << 8 | mode), h = key % NTEX;
    for (uint32_t k = 0; k < NTEX; k++, h = (h + 1) % NTEX) {
        Tex *t = &texs[h];
        if (t->texels == texels && t->table == table && t->lw == lw && t->lh == lh && t->mode == mode) {
            if (t->frame != S.frame) {               /* once a frame: the texels or the colours may have been replaced */
                t->frame = S.frame;
                uint32_t hh = hash_words(texels, (1u << (lw + lh)) / 4, 1) ^ (table ? hash_words(table, 256, 1) : 0);
                if (hh != t->hash) { t->hash = hh; tex_upload(t); }
            }
            return t->gid;
        }
        if (!t->texels) {
            if (ntex > NTEX * 3 / 4) tex_evict(S.frame);     /* (full: everything not used this frame goes) */
            if (ntex > NTEX * 3 / 4) return 0;
            if (texs[h].texels) return tex_get(texels, table, lw, lh, mode);   /* the table was rebuilt */
            ntex++; t->texels = texels; t->table = table; t->lw = lw; t->lh = lh; t->mode = mode; t->frame = S.frame;
            t->hash = hash_words(texels, (1u << (lw + lh)) / 4, 1) ^ (table ? hash_words(table, 256, 1) : 0);
            tex_upload(t); return t->gid;
        }
    }
    return 0;
}

/* ---- triangles, batched while the state stays the same ---- */
typedef struct { float x, y, z, rhw; uint32_t diffuse; float u, v; } Vtx;          /* FVF XYZRHW | DIFFUSE | TEX1 */
enum { B_OPAQUE, B_KEY, B_BLEND };
typedef struct { uint32_t tex, kind, tfactor, stage1; } BState;
#define MAXV 6144
static Vtx vb[MAXV]; static uint32_t nvb; static BState bcur;
static void apply(const BState *b)
{
    texture(b->tex);
    if (b->tex) { tss(0, 16, point_on() ? 1 : 2); tss(0, 17, point_on() ? 1 : 2); tss(0, 18, mips_on() ? 3 : 1); }
    tss(0, 1, 4); tss(0, 2, 2); tss(0, 3, 0);                                   /* colour: texture x diffuse */
    if (b->kind == B_BLEND && b->stage1 == 2) { tss(0, 4, 4); tss(0, 5, 2); tss(0, 6, 3); }   /* alpha: texture x TFACTOR (the 50% kind) */
    else { tss(0, 4, 2); tss(0, 5, 2); tss(0, 6, 0); }                          /* alpha: the texture's */
    if (b->stage1 == 1) { tss(1, 1, 12); tss(1, 2, 1); tss(1, 3, 3); tss(1, 4, 1); }   /* towards the haze (TFACTOR) by diffuse alpha */
    else tss(1, 1, 1);
    rs(60, b->tfactor);
    rs(14, b->kind != B_BLEND); rs(27, b->kind == B_BLEND); rs(19, 5); rs(20, 6);
    rs(15, b->kind == B_KEY); rs(24, 0x80); rs(25, 7);
}
static void batch_flush(void)
{
    if (!nvb) return;
    apply(&bcur);
    uint32_t *p = cmd_begin(DSR_DRAW, 16 + nvb * (uint32_t)sizeof(Vtx));
    p[0] = 4; p[1] = 0x144; p[2] = nvb; p[3] = 0; memcpy(p + 4, vb, nvb * sizeof(Vtx)); cmd_end();
    nvb = 0; st_.draws++;
}
static void emit(const BState *b, const Vtx *v)
{
    if (nvb && (memcmp(b, &bcur, sizeof *b) || nvb + 3 > MAXV)) batch_flush();
    bcur = *b; memcpy(vb + nvb, v, 3 * sizeof(Vtx)); nvb += 3;
}

/* the polygon of 0x462df0 / 0x46cac0; returns 0 to let the CPU draw it */
static int polygon(Ctx *c)
{
    uint32_t flags = r32(c->esp + 4), verts = r32(c->esp + 8), mat = r32(c->esp + 12), kind = flags & 0x73c;
    int dual = dual_mode() != 0;
    { static uint8_t seen[0x1000]; if (hw_log > 0 && !seen[flags & 0xfff]) { seen[flags & 0xfff] = 1;   /* each kind once */
        fprintf(stderr, "df_hw: first polygon of flags %#x (kind %#x), material %.8s\n", flags, kind, (const char *)(G_MEM + mat)); } }
    if (flags & 0x800) { st_.lines++; if (!dual) cpu_take(1); return 0; }          /* a line (tracers): the CPU's */
    int drawn = kind == 0 || kind == 4 || kind == 8 || kind == 0x14 || kind == 0x18 || kind == 0x34 || kind == 0x38
             || (kind >= 0x210 && kind <= 0x218 && !(kind & 3)) || ((kind & ~0x2cu) == 0x410 && (kind & 0xc) != 0xc);
    int nothing = kind == 0x10 || kind == 0x30 || (kind >= 0x100 && kind <= 0x108 && !(kind & 3)) || (kind >= 0x200 && kind <= 0x208 && !(kind & 3));
    if (!drawn && !nothing) { st_.skipped++; if (!dual) cpu_take(1); return 0; }
    /* as the original: u, v into texels, integer x, y (the vertices are the caller's scratch) */
    int32_t lw = (int32_t)r32(mat + 0x1e) >> 16, lh = (int32_t)r32(mat + 0x20) >> 16;
    int32_t vx[3], vy[3], vz[3], vu[3], vv[3], vs[3];
    for (int k = 0; k < 3; k++) {
        uint32_t v = verts + 0x20 * k;
        vx[k] = (int32_t)r32(v); vy[k] = (int32_t)r32(v + 4); vz[k] = (int32_t)r32(v + 8);
        vu[k] = (int32_t)r32(v + 0xc); vv[k] = (int32_t)r32(v + 0x10); vs[k] = (int32_t)r32(v + 0x14);
        if (dual) continue;
        if (lw >= 0 && lw < 32) w32(v + 0xc, (uint32_t)vu[k] << lw);
        if (lh >= 0 && lh < 32) w32(v + 0x10, (uint32_t)vv[k] << lh);
        w32(v + 0x18, (uint32_t)(vx[k] >> 16)); w32(v + 0x1c, (uint32_t)(vy[k] >> 16));
    }
    c->eax = 0;
    { int32_t x0 = (int32_t)S.x0 << 16, y0 = (int32_t)S.y0 << 16, x1 = (int32_t)(S.x0 + S.w) << 16, y1 = (int32_t)(S.y0 + S.h) << 16;   /* all outside one edge: clipped away */
      if ((vx[0] < x0 && vx[1] < x0 && vx[2] < x0) || (vx[0] >= x1 && vx[1] >= x1 && vx[2] >= x1) ||
          (vy[0] < y0 && vy[1] < y0 && vy[2] < y0) || (vy[0] >= y1 && vy[1] >= y1 && vy[2] >= y1)) { c->eax = 1; return !dual; } }
    if (nothing) return !dual;
    st_.polys++;
    uint32_t pal = (uint32_t)((int32_t)r32(mat + 0x18) >> 16), pbase = PAL_BASE + pal * PAL_STRIDE;
    uint32_t table = r32(pbase + 4), rowoff = 0;                     /* the colour table 0x461fd0 picks */
    if (r32(mat + 0x10) & 0x8000) table = r32(pbase + 0x10);
    if (r32(mat + 0x10) & 4) { table = r32(pbase + 8); rowoff = 32; }
    gpu_take();
    BState b = {0, B_OPAQUE, 0, 0}; Vtx v[3]; uint32_t col[3];
    int gouraud = (kind & 8) != 0;
    if (kind == 0) { col[0] = col[1] = col[2] = r32(mat + 0x14) | 0xff000000u; }
    else if (kind == 4 || kind == 8) {                                /* a palette colour at the shade row */
        uint32_t idx = r32(mat + 0x14) & 255;
        for (int k = 0; k < 3; k++) { uint32_t row = ((uint32_t)vs[gouraud ? k : 0] >> 16) & 63; col[k] = table ? r32(table + row * 1024 + idx * 4) | 0xff000000u : 0xff000000u; }
    } else if (kind >= 0x210 && kind <= 0x218) {                      /* the material's colour, the texel as alpha */
        b.kind = B_BLEND; b.tex = tex_get(r32(mat + 0x44), 0, (uint32_t)lw, (uint32_t)lh, TM_ALPHA);
        col[0] = col[1] = col[2] = r32(mat + 0x14) | 0xff000000u;
    } else {
        Pal *p = pal_get(pal); uint32_t base = p ? p->table + 32 * 1024 : 0;
        if (!base) return !dual;
        int key = (kind & 0x20) != 0, half = (kind & 0x400) != 0;
        b.tex = tex_get(r32(mat + 0x44), base, (uint32_t)lw, (uint32_t)lh, key || half ? TM_KEY : TM_OPAQUE);
        if (half) { b.kind = B_BLEND; b.stage1 = 2; b.tfactor = 0x80000000u; col[0] = col[1] = col[2] = 0xffffffffu; }
        else {
            b.kind = key ? B_KEY : B_OPAQUE; b.stage1 = 1; b.tfactor = p->haze | 0xff000000u;
            for (int k = 0; k < 3; k++) {
                uint32_t row = (((uint32_t)vs[gouraud ? k : 0] >> 16) & 63) + rowoff; if (row > 63) row = 63;
                col[k] = (uint32_t)p->alpha[row] << 24 | (uint32_t)p->gain[row][0] << 16 | (uint32_t)p->gain[row][1] << 8 | p->gain[row][2];
            }
        }
    }
    for (int k = 0; k < 3; k++) {
        double z = (double)(uint32_t)vz[k] / 65536.0;                 /* the 16-bit depth, with its fraction */
        /* the CPU fills the pixels whose centre, x + 0.5 / y + 0.5, is inside (0x461e30 rows, 0x461cd0 spans); the
         * pretransformed path takes D3D's centres at whole numbers, so half a pixel less */
        v[k].x = (float)(vx[k] / 65536.0 - S.x0 - 0.5); v[k].y = (float)(vy[k] / 65536.0 - S.y0 - 0.5);
        v[k].z = (float)(z / 65535.0); if (v[k].z > 1) v[k].z = 1;
        v[k].rhw = point_on() ? 1.0f : (float)(64.0 / (z > 1.0 / 64 ? z : 1.0 / 64));     /* 1 / eye depth: perspective-correct texturing */
        v[k].diffuse = col[k]; v[k].u = (float)(vu[k] / 65536.0); v[k].v = (float)(vv[k] / 65536.0);
    }
    emit(&b, v);
    return !dual;
}

static void seg_open(void)
{
    S.open = 0;
    if (!hw_wanted()) return;
    if (hw_log < 0) hw_log = getenv("DF_HWLOG") != 0;
    uint32_t v = r32(VIEW_PTR); if (!v || r32(v + 0x50) != 32) return;
    int32_t x0 = (int32_t)r32(v + 0x34), y0 = (int32_t)r32(v + 0x38), x1 = (int32_t)r32(v + 0x3c), y1 = (int32_t)r32(v + 0x40);
    if (x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0 || x1 - x0 >= 4096 || y1 - y0 >= 4096) return;
    S.view = v; S.x0 = (uint32_t)x0; S.y0 = (uint32_t)y0; S.w = (uint32_t)(x1 - x0 + 1); S.h = (uint32_t)(y1 - y0 + 1);
    S.open = 1; S.gpu = 0; S.frame++; st_.frames++;
    if (!(S.frame & 511) && S.frame > 3600) tex_evict(S.frame - 3600);   /* textures unused for a minute or so */
    { static int flood = -1; if (flood < 0) flood = getenv("DF_HW_FLOOD") ? atoi(getenv("DF_HW_FLOOD")) : 0;   /* development: raise the water */
      if (flood > 0 && r32(0x00bd5248u)) w32(0x00bd5248u, (uint32_t)flood << 14); }
}
/* DF_HWLOG: the time the object part of the frame takes, with the GPU or without (to compare) */
static uint64_t obj_t0, obj_us, obj_n, obj_last;
static void obj_time(void)
{
    if (hw_log < 0) hw_log = getenv("DF_HWLOG") != 0;
    if (!hw_log || !obj_t0) return;
    uint64_t t = now_us(); obj_us += t - obj_t0; obj_n++;
    if (t - obj_last >= 1000000) {
        fprintf(stderr, "df_hw: objects %.2f ms a frame (%s)\n", obj_n ? obj_us / 1000.0 / obj_n : 0, hw_wanted() ? "GPU" : "CPU");
        obj_us = obj_n = 0; obj_last = t;
    }
}
static void seg_close(void)
{
    if (!S.open) return;
    cpu_take(0); S.open = 0;
    static uint64_t last; uint64_t t = now_us();
    if (hw_log && t - last >= 1000000) {
        fprintf(stderr, "df_hw: %u frames, %u polygons, %u draws, %u textures new / %u uploaded, %u syncs (%u with depth; upload %.2f ms, readback %.2f ms a sync), %u plane passes, %u lines, %u other kinds\n",
                st_.frames, st_.polys, st_.draws, st_.tex_new, st_.tex_up, st_.syncs, st_.zsyncs, st_.syncs ? st_.up_us / 1000.0 / st_.syncs : 0, st_.syncs ? st_.rb_us / 1000.0 / st_.syncs : 0, st_.planes, st_.lines, st_.skipped);
        memset(&st_, 0, sizeof st_); last = t;
    }
}

/* whether a plane pass gets past its early returns: a water level is set, the eye is above it (0x48d040) or below
 * it (0x48d570), and the plane's first row is inside the clip rectangle (the terrain view [0xc94aa0]) */
static int plane_draws(uint32_t addr)
{
    uint32_t tv = r32(0x00c94aa0u); int32_t level = (int32_t)r32(0x00bd5248u);
    if (!tv || !level) return 0;
    int32_t eye = (int32_t)r32(tv + 0xc), top = (int32_t)r32(tv + 0x38), bottom = (int32_t)r32(tv + 0x40);
    if (addr == 0x0048d040u) { int32_t first = (int32_t)r32(0x00c971a4u); if (first < top) first = top; return level < eye && first <= bottom; }
    return level >= eye && top <= bottom;
}
/* ---- the Colors setting: 0 "256", 1 "16 Million", 2 "65 Thousand (3Dfx)", and here 3 "Hardware" ----
 * [0x4ced24] is the settings screen's (and df.cfg's "renderer") value, [0x67f4e4] the one the game runs with, copied
 * by 0x407abe (apply) and 0x407980 (open). Hardware runs as 16 Million (32-bit, every other use of the value compares
 * it with 0 or 2) with df_hw_setting on, so only the copies, the arrows and the label know about 3. */
#define COLORS_MENU 0x004ced24u
static int colors_avail(int v) { return v == 3 ? r32(0x00503144u) != 0 : r32(0x00503140u + 4 * (uint32_t)v) != 0; }   /* 3 needs 32-bit */
static uint32_t hw_label(void)
{
    static uint32_t s;
    if (!s) { static const char t[] = "Hardware"; s = heap_alloc(w32_process_heap, 8, sizeof t); if (s) memcpy(G_MEM + s, t, sizeof t); }
    return s;
}
static int colors_hook(Ctx *c, uint32_t addr)
{
    static int label3;
    switch (addr) {
    case 0x00407ac3u:                                 /* apply: mov [0x67f4e4], eax */
        df_hw_setting = c->eax == 3; if (df_hw_setting) c->eax = 1; return 1;
    case 0x00407985u:                                 /* the settings screen opens: mov [0x4ced24], eax */
        if (df_hw_setting && c->eax == 1) c->eax = 3; return 1;
    case 0x004127dfu: {                               /* an arrow: mov [0x4ced24], ebx (the game's choice of 0..2) */
        int32_t v = (int32_t)r32(COLORS_MENU), step = (int32_t)c->esi == -1 ? -1 : 1;
        for (int k = 0; k < 4; k++) { v = (v + step + 4) % 4; if (colors_avail(v)) break; }
        c->ebx = (uint32_t)v; return 1; }
    case 0x004121b2u:                                 /* the label: 3 takes the 16 Million path ... */
        if (r32(COLORS_MENU) == 3 && hw_label()) { label3 = 1; w32(COLORS_MENU, 1); } return 1;
    case 0x00412382u:                                 /* ... push edx (the text): ours instead, and the value back */
        if (label3) { label3 = 0; c->edx = hw_label(); w32(COLORS_MENU, 3); } return 1;
    }
    return 0;
}

int df_hw_hook(Ctx *c, uint32_t addr)
{
    if (colors_hook(c, addr)) return 1;
    switch (addr) {
    case 0x004856f8u: obj_t0 = now_us(); seg_open(); return 1;   /* the objects begin (call 0x46be50, the list flush) */
    case 0x00485739u: seg_close(); obj_time(); return 1;         /* the 3D frame ends */
    case 0x0048d040u: case 0x0048d570u:               /* the water plane, below / above the eye: it blends with the picture */
        if (S.open && plane_draws(addr)) { st_.planes++; if (!dual_mode()) cpu_take(1); }   /* and tests the depth */
        return 1;
    }
    return 0;
}
int df_hw_override(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case 0x0046cac0u:
        { static int once; if (hw_log > 0 && S.open && !once++) fprintf(stderr, "df_hw: the span buffer is used\n"); }
        /* fall through */
    case 0x00462df0u:                                 /* a polygon (cdecl: flags, verts, material) */
        if (!S.open || !polygon(c)) return 0;
        c->esp += 4; return 1;
    }
    return 0;
}
#endif
