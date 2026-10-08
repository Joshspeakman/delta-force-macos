/* Delta Force 1 and 2: high frame rates (tools/lift.py DF_HOOKS 'frame'). DF_HIGH_FPS=0 turns all of it off.
 *
 * Both games advance the world in a fixed 16 ms tick. The main loop (DF1 0x461270, DF2 0x468ab0) adds the
 * milliseconds of timeGetTime to an accumulator (DF1 ebx, DF2 edi; the last reading in ebp), runs the state's update
 * (+0x24) once for every 16 ms in it, then the state's render (+0x28), and spins on Sleep(0) until the next tick is
 * due: at most 62.5 frames a second. In a mission (the "Game Loop" state object, DF1 0x4cece0, DF2 0x4cd180):
 *  - The loop's Sleep(0) path renders too, one frame per refresh of the display: the producer's frame cap (ddraw.c,
 *    "display") holds each frame until its refresh comes. A render only reads the world: a second render in the same
 *    tick changes nothing but frame and profiler counters (memory diffs, both games). When the next tick falls due
 *    before the frame would be shown, the loop ticks first and the game's own render makes that frame.
 *  - Every frame shows the world in between the last two ticks. The render draws the objects of five arrays of
 *    0x114-byte entries (position +0x24/+0x28/+0x2c in 16.16, angles +0x30/+0x34/+0x38 as 32-bit binary angles);
 *    their values are copied when a tick starts, and a render writes prev + (cur - prev) * alpha, alpha being how far
 *    the frame's display time is into the 16 ms after the last tick, then puts the tick's own values back when it
 *    returns. So the world is shown one tick late, smoothly. An object snaps instead (drawn where the tick put it) when
 *    its slot holds a different object, it died or came back to life, or it jumped (teleport, respawn).
 *  - Mouse-look is immediate. The tick takes the mouse motion as the host reported it (w32_raw_dx/dy, user32.c): the
 *    game's own measurement, the last WM_MOUSEMOVE against the cursor it re-centres, loses motion that arrives between
 *    its reading and its SetCursorPos, which happens often once frames come faster than ticks. A render turns the
 *    player by the motion the next tick has not taken yet, through the game's own scale (sensitivity, zoom); that
 *    tick then turns the player to exactly there. Turning that does not come from the mouse (keys) is in between, as
 *    for every other object.
 *  - The muzzle flash: a tick sets "fired" bits on the shooter (DF1 byte +0x6d, DF2 +0x65, 0x18) and the render clears
 *    them when it ends; the renders after the first one in a tick set them again, so the flash shows the whole tick.
 * Menus and the multiplayer score screen are as before (one render per tick). The simulation, the commands it records
 * (demo files, the network) and its 16 ms are the game's own: the hooks read, and undo what a render needed changed.
 * DF_FRAME_LOG=1: once a second, ticks, renders and what the in-between did, and any frame that began late; 2: also
 * every tick and every frame; 3: also the timing of each wait and render (the printing itself then disturbs it). */
#include "w32.h"
#include "platform.h"
#include <math.h>

#ifdef DF_EDITION
typedef struct { uint32_t base, count, cap; } Arr;   /* an object array: count's address (0: always cap entries) */
#ifdef DF2_EDITION
#define H_TICK 0x00464cefu        /* update, past the state-change check: the tick runs */
#define H_MOUSE 0x0041eb85u       /* the mouse-look scaler (0x41eb50), after its sampler (0x479b80) */
#define H_SLEEP 0x00468b06u       /* the loop's Sleep(0) */
#define H_PRERENDER 0x00468b2cu   /* the loop, before the state's render */
#define H_RENDER 0x00465e40u      /* the Game Loop render (an override: it may be left out) */
#define H_RENDER_RET 0x00465ecbu  /* its ret */
#define A_STATE 0x6772f0u         /* the loop's current state object */
#define GAME_LOOP 0x4cd180u
#define A_PLAYER 0x79a2c0u        /* the player's entry in the first array */
#define A_MDX 0xe0ff0cu           /* the sampler's delta, which the scaler reads next */
#define A_MDY 0xe0ff10u
#define A_SENS 0x67ccf8u
#define A_INVERT 0x67ccf4u
#define F_ZOOMED 0x455790u        /* the scaler's "zoomed" test */
#define A_CAM_MODE 0xbf2b40u      /* 0: first person */
#define A_CAM_TARGET 0xbf2b44u
#define A_EYE 0x79a688u           /* eye height, added to the camera's z */
#define O_FLAGS 0x64u             /* entry flags: 2 not drawn, 4 dead */
#define O_FLASH 0x65u             /* the "fired" bits 0x18 */
#define ACC(c) ((c)->edi)
static const Arr arrs[] = {{0xc720e0, 0xc310a4, 256}, {0xc834e0, 0xc720c4, 1024}, {0xcc84e0, 0, 1024}, {0xd0d4e0, 0xc310a8, 1024}, {0x745ea0, 0, 512}};
static int score_screen(void)     /* the render's other branch (0x45e4c0) */
{
    return rt_r32(G_MEM, 0x67cd34) && rt_r32(G_MEM, 0x67cd2c) && (!rt_r32(G_MEM, 0x67cd38) || rt_r32(G_MEM, 0x67d3d4));
}
static int mouse_look_on(void)    /* the scaler's own gate */
{
    return rt_r32(G_MEM, 0xa6de88) && !rt_r32(G_MEM, 0x67d39c) && rt_r32(G_MEM, 0xe10384);
}
#else
#define H_TICK 0x00404357u
#define H_MOUSE 0x00479654u       /* the scaler 0x479630, after its sampler 0x497d20 */
#define H_SLEEP 0x0046130fu
#define H_PRERENDER 0x0046131eu
#define H_RENDER 0x00404b00u
#define H_RENDER_RET 0x00404b71u
#define A_STATE 0x8ee1d0u
#define GAME_LOOP 0x4cece0u
#define A_PLAYER 0xc23820u
#define A_MDX 0xd0934cu
#define A_MDY 0xd09350u
#define A_SENS 0x67f500u
#define A_INVERT 0x67f4fcu
#define F_ZOOMED 0x484740u
#define A_CAM_MODE 0x6ba3ecu
#define A_CAM_TARGET 0x6ba3f0u
#define A_EYE 0xc23a18u
#define O_FLAGS 0x6cu
#define O_FLASH 0x6du
#define ACC(c) ((c)->ebx)
static const Arr arrs[] = {{0x7cca80, 0x8cfa18, 256}, {0x7dde80, 0x8cfa1c, 1024}, {0x822e80, 0, 1024}, {0x867e80, 0x8cf904, 1024}, {0x9f3098, 0, 512}};
static int score_screen(void)     /* the render's other branch (0x440870) */
{
    return rt_r32(G_MEM, 0x67f528) && rt_r32(G_MEM, 0x67f520) && (!rt_r32(G_MEM, 0x67f52c) || rt_r32(G_MEM, 0x67dbb8));
}
static int mouse_look_on(void)    /* the scaler's own gate: full screen, captured */
{
    return !rt_r32(G_MEM, 0x503130) && rt_r32(G_MEM, 0xcc0884);
}
#endif
#define NARR (int)(sizeof arrs / sizeof *arrs)
#define NSLOT (256 + 3 * 1024 + 512)
#define STRIDE 0x114u
#define PITCH_LO ((int32_t)0xd0000000)   /* the tick clamps the player's pitch to this range */
#define PITCH_HI 0x40000000

extern int32_t w32_raw_dx, w32_raw_dy;    /* user32.c */
uint64_t dsr_next_present_us(void); extern uint64_t dsr_cap_waited_us;   /* ddraw.c */

static uint32_t r32(uint32_t a) { return rt_r32(G_MEM, a); }
static void wr32(uint32_t a, uint32_t v) { rt_w32(G_MEM, a, v); }
static uint64_t now_us(void) { return ds_clock_ns() / 1000; }
static uint32_t call_guest(Ctx *c, uint32_t fn)   /* a game function, the interrupted instruction's registers kept */
{
    Ctx save = *c; uint32_t r = w32_callback(c, fn, 0, 0), fs = c->fs_base;
    *c = save; c->fs_base = fs;
    return r;
}
static int32_t mul16(int32_t a, int32_t b) { return (int32_t)(uint32_t)(uint64_t)(((int64_t)a * b) >> 16); }   /* imul; shrd 16 */
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* What the scaler turns the player by for a mouse delta (dx, dy), as its commands 0x9b..0x9e do: yaw -= dx * k,
 * pitch += (inverted ? dy : -dy) * k, k from the sensitivity (16.16 products). 0 when the delta goes elsewhere
 * (DF2: a mounted gun) or the game's arithmetic would not produce a turn. */
static int turn(Ctx *c, int32_t dx, int32_t dy, int32_t *dyaw, int32_t *dpitch)
{
    int32_t py = r32(A_INVERT) ? dy : -dy, k;
#ifdef DF2_EDITION
    uint32_t gun = r32(0x79a6b8);
    if (gun && r32(0x79a5e0 + 4 * r32(0x79a5d4)) == 0x30 && !(r32(gun + 0x64) & 4) && r32(0x79a6b4)) return 0;   /* commands 0x35..0x38 */
    int32_t s = (int32_t)r32(A_SENS); if (s <= 0) s = 0x80; else if (s >= 0x200) s = 0x1ff;
    k = s << 14;
    if (call_guest(c, F_ZOOMED)) {   /* zoomed: k from the larger axis, a curve (0x41ec02..0x41ec9d) */
        int32_t s2 = (int32_t)r32(0x67ccfc); if (s2 <= 0x80) s2 = 0x80; else if (s2 >= 0x200) s2 = 0x1ff;
        int32_t num = (int32_t)((uint32_t)(iabs(dx) > iabs(py) ? dx : py) << 16), den = (int32_t)(r32(iabs(dx) > iabs(py) ? 0x67f428 : 0x67f42c) << 15);
        if (!den) return 0;
        int64_t q = ((int64_t)num * 65536) / den;
        if (q > INT32_MAX || q < INT32_MIN) return 0;   /* the game's idiv would fault */
        int32_t t = mul16((int32_t)q, (int32_t)q);
        t = mul16(s2 << 7, t); t = mul16(t, 0x200000);
        k = t + 0x40000;
    }
#else
    k = (int32_t)(r32(A_SENS) << 14);
    if (call_guest(c, F_ZOOMED)) k >>= 2;
#endif
    *dyaw = -mul16((int32_t)((uint32_t)dx << 16), k); *dpitch = mul16((int32_t)((uint32_t)py << 16), k);
    return 1;
}

/* ---- state ---- */
typedef struct { int32_t v[6]; uint32_t id0, id10, flags; int32_t last; uint8_t ok, stepped; } Slot;
static Slot snap[NSLOT];         /* every drawn object as the tick before the last one left it */
static int snap_valid;           /* snap is of the tick before the last one */
static uint32_t ticks;           /* mission ticks seen (since the last time the loop was outside a mission) */
static int32_t eye_prev;
static uint32_t mouse_tick;      /* the tick that last took the raw motion (0: none since a break) */
static int32_t m_dyaw, m_dpitch; static int m_known;   /* what the last tick's mouse commands turned the player by */
static int look_ok = 1;          /* the ticks apply the mouse as turn() predicts */
static uint32_t sim_ms;          /* the loop's time of the last tick (timeGetTime ms: last reading - accumulator) */
static int sim_known;
static uint8_t flash[256]; static uint32_t flash_tick;
typedef struct { uint32_t a, was, wrote; } Undo;
static Undo undo[NSLOT * 6 + 4]; static int nundo, in_render;
static uint64_t tick_t0, last_start, last_end, last_work; static uint32_t last_ticks; extern double dsr_display_hz;
static uint64_t est_us = 4000;   /* a render's own work (the cap's waits taken out): the second longest of the last 48 */
static uint64_t works[48]; static int nworks;
static uint64_t render_t0, render_w0;
static int on = -1, lg;
static struct { long ticks, renders, extras, skips, dropped, late, interp, snaps, jumps, looks, flashes; uint64_t tickmax; double amin, amax, asum; int32_t move[NARR]; } st;

static void reset(void) { snap_valid = 0; ticks = 0; mouse_tick = 0; sim_known = 0; look_ok = 1; m_known = 0; flash_tick = 0; }
static int enabled(void)
{
    if (on < 0) { const char *e = getenv("DF_HIGH_FPS"); on = !(e && !atoi(e)); e = getenv("DF_FRAME_LOG"); lg = e ? atoi(e) : 0;
                  st.amin = 2; if (on) fprintf(stderr, "df: high frame rates on (DF_HIGH_FPS=0 for one frame per tick)\n"); }
    return on && !w32_deterministic;
}
static uint32_t count_of(const Arr *a) { if (!a->count) return a->cap; int32_t n = (int32_t)r32(a->count); return n < 0 ? 0 : (uint32_t)n < a->cap ? (uint32_t)n : a->cap; }
static int drawn(const Arr *a, uint32_t e)       /* the render's own test for drawing an entry */
{
    if (!r32(e) || !r32(e + 0x10)) return 0;
    return a == &arrs[NARR - 1] ? (int32_t)r32(e + 0xfc) > 0 : !(r32(e + O_FLAGS) & 2);
}
static int player_slot(void)
{
    uint32_t p = r32(A_PLAYER);
    if (p < arrs[0].base || p >= arrs[0].base + arrs[0].cap * STRIDE || (p - arrs[0].base) % STRIDE) return -1;
    return (int)((p - arrs[0].base) / STRIDE);
}
static void undo_all(void)       /* the tick's values back, where nothing else wrote meanwhile */
{
    while (nundo > 0) { Undo *u = &undo[--nundo]; if (r32(u->a) == u->wrote) wr32(u->a, u->was); }
    in_render = 0;
}
static void put(uint32_t a, uint32_t was, uint32_t v) { if (v == was) return; undo[nundo++] = (Undo){a, was, v}; wr32(a, v); }

/* ---- a tick starts: keep what the last one left */
static void tick_starts(void)
{
    if (nundo) undo_all();
    if (!tick_t0) tick_t0 = now_us();
    if (!r32(A_PLAYER)) { reset(); return; }
    for (int k = 0, i = 0; i < NARR; i++) {
        const Arr *a = &arrs[i]; uint32_t n = count_of(a);
        for (uint32_t j = 0; j < a->cap; j++, k++) {
            Slot *s = &snap[k]; uint32_t e = a->base + j * STRIDE;
            if (j >= n || !drawn(a, e)) { s->ok = 0; continue; }
            int32_t v[6]; for (int q = 0; q < 6; q++) v[q] = (int32_t)r32(e + 0x24 + 4 * q);
            uint32_t id0 = r32(e), id10 = r32(e + 0x10);
            if (snap_valid && s->ok && s->id0 == id0 && s->id10 == id10) {
                int32_t m = 0; for (int q = 0; q < 3; q++) { int32_t d = iabs(v[q] - s->v[q]); if (d > m) m = d; }
                s->last = m; s->stepped = 1;
                if (m > st.move[i]) st.move[i] = m;
            } else s->stepped = 0;
            memcpy(s->v, v, sizeof v); s->id0 = id0; s->id10 = id10; s->flags = r32(e + O_FLAGS); s->ok = 1;
        }
    }
    eye_prev = (int32_t)r32(A_EYE);
    snap_valid = 1; ticks++; st.ticks++;
    m_known = 0; m_dyaw = m_dpitch = 0;
}

/* ---- the scaler has sampled the mouse: the tick's delta from the raw motion */
static void mouse_sampled(Ctx *c)
{
    int32_t rx = __atomic_exchange_n(&w32_raw_dx, 0, __ATOMIC_RELAXED), ry = __atomic_exchange_n(&w32_raw_dy, 0, __ATOMIC_RELAXED);
    if (!ticks) return;
    /* the motion since the previous tick, if that tick took it too; otherwise (the first tick, or after menus,
     * pauses, the mouse not captured) the game's own reading this once, and what piled up meanwhile is dropped */
    if (mouse_tick && mouse_tick + 1 == ticks) { wr32(A_MDX, (uint32_t)rx); wr32(A_MDY, (uint32_t)ry); }
    mouse_tick = ticks;
    m_known = turn(c, (int32_t)r32(A_MDX), (int32_t)r32(A_MDY), &m_dyaw, &m_dpitch);
    if (lg >= 2) fprintf(stderr, "dff: tick %u mouse %d,%d (raw %d,%d) turn %d,%d%s\n", ticks, (int32_t)r32(A_MDX), (int32_t)r32(A_MDY), rx, ry,
                         m_dyaw, m_dpitch, m_known ? "" : " (not the player)");
}

/* ---- when to render. With the frame cap on, the next frame is released at a known time (dsr_next_present_us); a
 * render starts its own work's length before that (est_us: nearly the longest of late, plus a margin), so the mouse
 * is read as late as it can be and every frame is read the same time before it is shown. Its world is the one at that
 * start time, one tick back: alpha = (start - last tick) / 16 ms, between the last two ticks. Without a cap
 * (DSR_FPSCAP=0), a render starts as soon as it can. */
static uint64_t start_time(uint64_t now)                 /* when the render for the next frame should begin */
{
    uint64_t d = dsr_next_present_us(), lead = est_us + 1000;
    return d > now + lead ? d - lead : now;
}
static double alpha_at(uint64_t t)                       /* how far t is into the 16 ms after the last tick */
{
    uint64_t now = now_us(), ms = now / 1000;
    return ((double)(int32_t)((uint32_t)ms - sim_ms) + (double)(now % 1000) / 1000 + (double)(int64_t)(t - now) / 1000) / 16;
}
static void sleep_until(uint64_t t)
{
    for (uint64_t n; (n = now_us()) + 50 < t; ) { if (t - n > 1500) usleep(1000); else if (t - n > 300) usleep((useconds_t)(t - n - 250)); }
}
static int32_t lerp_back(int32_t cur, int32_t d, double back) { return cur - (int32_t)lround(back * d); }   /* cur - (1 - alpha) * d */
static int render_starts(Ctx *c)  /* 1: not drawn now */
{
    static int dropped;
    in_render = 0; nundo = 0;
    if (!ticks || !snap_valid || !r32(A_PLAYER) || r32(A_STATE) != GAME_LOOP || score_screen()) return 0;
    uint64_t now = now_us(), at = start_time(now);
    /* The game's own render, after a tick, when the next frame is due after the next tick (frames slower than ticks:
     * displays below 62.5 Hz): that tick first, and its render makes the frame. The game does the same whenever two
     * ticks fall due together. */
    if (sim_known && !dropped && alpha_at(at) > 1) { dropped = 1; st.dropped++; c->esp += 4; return 1; }
    dropped = 0;
    if (lg >= 3 && at > now + 2000) fprintf(stderr, "dff: %.3f render waits %.1f ms (release in %.1f)\n", now / 1000.0, (at - now) / 1000.0, ((double)(int64_t)(dsr_next_present_us() - now)) / 1000.0);
    if (at > now) { sleep_until(at); now = now_us(); }  /* the game's own render, right after a tick: no earlier than needed */
    if (lg && last_start && dsr_display_hz > 20 && now - last_start > 1.5e6 / dsr_display_hz)
        fprintf(stderr, "dff: %.1f ms since the last frame began (it took %.2f ms and ended %.1f ms ago; %u ticks since; next release in %.1f ms)\n",
                (now - last_start) / 1000.0, last_work / 1000.0, (now - last_end) / 1000.0, ticks - last_ticks,
                ((double)(int64_t)(dsr_next_present_us() - now)) / 1000.0);
    last_start = now; last_ticks = ticks;
    if (lg >= 3) fprintf(stderr, "dff: %.3f start tick %u (release in %.1f)\n", now / 1000.0, ticks, ((double)(int64_t)(dsr_next_present_us() - now)) / 1000.0);
    double alpha = sim_known ? alpha_at(now) : 1;
    if (alpha > 1) alpha = 1; if (alpha < 0) alpha = 0;
    double back = 1 - alpha;
    in_render = 1; render_t0 = now; render_w0 = dsr_cap_waited_us; st.renders++;
    if (alpha < st.amin) st.amin = alpha; if (alpha > st.amax) st.amax = alpha; st.asum += alpha;

    /* the muzzle flash: saved at the tick's first render, set again for the others */
    { uint32_t n = count_of(&arrs[0]);
      if (flash_tick != ticks) { flash_tick = ticks; for (uint32_t j = 0; j < n; j++) flash[j] = (uint8_t)(rt_r8(G_MEM, arrs[0].base + j * STRIDE + O_FLASH) & 0x18); }
      else for (uint32_t j = 0; j < n; j++) if (flash[j]) { uint32_t a = arrs[0].base + j * STRIDE + O_FLASH; rt_w8(G_MEM, a, rt_r8(G_MEM, a) | flash[j]); st.flashes++; } }

    /* the player's turn: did the last tick apply the mouse as predicted (it does not in some states) */
    int ps = player_slot(); Slot *pl = ps >= 0 && snap[ps].ok ? &snap[ps] : 0;
    int32_t dyaw = 0, dpitch = 0; uint32_t p = r32(A_PLAYER);
    if (pl && pl->id0 == r32(p) && pl->id10 == r32(p + 0x10)) { dyaw = (int32_t)(r32(p + 0x30) - (uint32_t)pl->v[3]); dpitch = (int32_t)(r32(p + 0x34) - (uint32_t)pl->v[4]); }
    if (m_known && mouse_tick == ticks && (m_dyaw || m_dpitch)) look_ok = (m_dyaw && dyaw) || (m_dpitch && dpitch);
    int look = m_known && mouse_tick == ticks && look_ok && mouse_look_on() && !r32(A_CAM_MODE) && r32(A_CAM_TARGET) == p;
    int32_t lyaw = 0, lpitch = 0;
    if (look) {
        int32_t px = __atomic_load_n(&w32_raw_dx, __ATOMIC_RELAXED), py = __atomic_load_n(&w32_raw_dy, __ATOMIC_RELAXED);
        if ((px || py) && !turn(c, px, py, &lyaw, &lpitch)) look = 0;
        if (look) st.looks++;
    }

    /* every drawn object between the two ticks */
    for (int k = 0, i = 0; i < NARR; i++) {
        const Arr *a = &arrs[i]; uint32_t n = count_of(a);
        for (uint32_t j = 0; j < a->cap; j++, k++) {
            Slot *s = &snap[k]; if (!s->ok) continue;
            uint32_t e = a->base + j * STRIDE;
            if (j >= n || !drawn(a, e) || r32(e) != s->id0 || r32(e + 0x10) != s->id10 || ((r32(e + O_FLAGS) ^ s->flags) & 4)) { st.snaps++; continue; }
            int32_t v[6]; for (int q = 0; q < 6; q++) v[q] = (int32_t)r32(e + 0x24 + 4 * q);
            int32_t d[3], m = 0; for (int q = 0; q < 3; q++) { d[q] = v[q] - s->v[q]; if (iabs(d[q]) > m) m = iabs(d[q]); }
            /* a jump: more than 64 units in one tick, or above 4 units and over four times the step before */
            if (m > 0x400000 || (m > 0x40000 && s->stepped && m > 4 * s->last + 0x40000)) { st.jumps++; continue; }
            if (back > 0) for (int q = 0; q < 3; q++) put(e + 0x24 + 4 * q, (uint32_t)v[q], (uint32_t)lerp_back(v[q], d[q], back));
            for (int q = 3; q < 6; q++) {
                int32_t da = (int32_t)((uint32_t)v[q] - (uint32_t)s->v[q]);
                if (k == ps && q < 5) continue;                       /* the player's yaw and pitch: below */
                if (back > 0 && iabs(da) < 0x40000000) put(e + 0x24 + 4 * q, (uint32_t)v[q], (uint32_t)lerp_back(v[q], da, back));
            }
            st.interp++;
        }
    }
    if (p) {   /* the player: the mouse's part now, the rest in between */
        int32_t oy = look ? dyaw - m_dyaw : dyaw, op = look ? dpitch - m_dpitch : dpitch;
        if (iabs(oy) >= 0x40000000) oy = 0; if (iabs(op) >= 0x40000000) op = 0;
        uint32_t cy = r32(p + 0x30), cp = r32(p + 0x34);
        int32_t ny = (int32_t)(cy + (uint32_t)lyaw) - (int32_t)lround(back * oy);
        int32_t np = (int32_t)cp + lpitch - (int32_t)lround(back * op);
        if (lpitch > 0 && np > PITCH_HI) np = (int32_t)cp > PITCH_HI ? (int32_t)cp : PITCH_HI;
        if (lpitch < 0 && np < PITCH_LO) np = (int32_t)cp < PITCH_LO ? (int32_t)cp : PITCH_LO;
        put(p + 0x30, cy, (uint32_t)ny); put(p + 0x34, cp, (uint32_t)np);
        int32_t eye = (int32_t)r32(A_EYE), de = eye - eye_prev;
        if (back > 0 && iabs(de) < 0x100000) put(A_EYE, (uint32_t)eye, (uint32_t)lerp_back(eye, de, back));
        if (lg >= 2) fprintf(stderr, "dff: frame %.3f tick %u alpha %.3f yaw %08x pitch %08x pos %08x %08x %08x look %d,%d\n", now / 1000.0, ticks, alpha,
                             r32(p + 0x30), r32(p + 0x34), r32(p + 0x24), r32(p + 0x28), r32(p + 0x2c), lyaw, lpitch);
    }
    return 0;
}
static void render_ends(void)
{
    if (!in_render) return;
    undo_all();
    uint64_t t = now_us(), waited = dsr_cap_waited_us - render_w0, w = (t - render_t0) - waited;
    if (!waited && dsr_next_present_us()) st.late++;     /* the frame was not held: it was late, or only just in time */
    last_end = t; last_work = w;
    if (lg >= 3) fprintf(stderr, "dff: %.3f end, work %.2f, held %.2f\n", t / 1000.0, w / 1000.0, waited / 1000.0);
    if (w > 20000) w = 20000;
    works[nworks++ % 48] = w;
    { uint64_t a = 0, b = 0; for (int k = 0; k < 48 && k < nworks; k++) { if (works[k] > a) { b = a; a = works[k]; } else if (works[k] > b) b = works[k]; } est_us = b ? b : a; }
}

/* ---- the loop's Sleep(0): a frame in between, when the next frame is due before the next tick */
static void loop_waits(Ctx *c)
{
    if (r32(A_STATE) != GAME_LOOP || !r32(A_PLAYER)) { if (ticks) reset(); return; }
    sim_ms = c->ebp - ACC(c); sim_known = 1;
    if (ticks < 2 || !snap_valid || score_screen()) return;
    uint64_t now = now_us(), at = start_time(now);
    if (alpha_at(at) > 1) {   /* the tick falls due first: the loop ticks, and the game's own render makes the frame */
        st.skips++;
        double a = alpha_at(now);
        if (a < 1) { uint64_t due = now + (uint64_t)((1 - a) * 16000); if (due > now + 400) sleep_until(due - 250);
                     if (lg >= 3 && due > now + 2000) fprintf(stderr, "dff: %.3f wait for the tick %.1f ms (frame start in %.1f, release in %.1f)\n", now / 1000.0, (due - now) / 1000.0, ((double)(int64_t)(at - now)) / 1000.0, ((double)(int64_t)(dsr_next_present_us() - now)) / 1000.0); }   /* not a busy wait */
        return;
    }
    if (lg >= 3 && at > now + 2000) fprintf(stderr, "dff: %.3f wait to start %.1f ms (release in %.1f)\n", now / 1000.0, (at - now) / 1000.0, ((double)(int64_t)(dsr_next_present_us() - now)) / 1000.0);
    if (at > now) sleep_until(at);
    uint32_t fn = r32(r32(A_STATE) + 0x28); if (!fn) return;
    st.extras++;
    call_guest(c, fn);
}

int df_frame_hook(Ctx *c, uint32_t addr)
{
    switch (addr) {
    case H_TICK: case H_MOUSE: case H_SLEEP: case H_PRERENDER: case H_RENDER_RET: break;
    default: return 0;
    }
    if (!enabled()) return 1;
    switch (addr) {
    case H_TICK: tick_starts(); break;
    case H_MOUSE: mouse_sampled(c); break;
    case H_SLEEP: loop_waits(c); break;
    case H_PRERENDER: sim_ms = c->ebp - ACC(c); sim_known = 1;
        if (tick_t0) { uint64_t d = now_us() - tick_t0; if (d > st.tickmax) st.tickmax = d; tick_t0 = 0; } break;
    case H_RENDER_RET: render_ends(); break;
    }
    if (lg) {
        static uint64_t t0; uint64_t t = now_us(); if (!t0) t0 = t;
        if (t - t0 >= 1000000) {
            fprintf(stderr, "dff: %.2fs: ticks %ld renders %ld (in between %ld, ticked first %ld, not drawn %ld, late %ld) alpha %.2f..%.2f avg %.2f; per frame %.0f objects in between, "
                    "%.1f snapped, %.2f jumps; look-ahead %ld; flash kept %ld; render %.2f ms; ticks up to %.2f ms; largest steps", (t - t0) / 1e6, st.ticks, st.renders, st.extras, st.skips, st.dropped, st.late,
                    st.renders ? st.amin : 0, st.renders ? st.amax : 0, st.renders ? st.asum / st.renders : 0, st.renders ? (double)st.interp / st.renders : 0,
                    st.renders ? (double)st.snaps / st.renders : 0, st.renders ? (double)st.jumps / st.renders : 0, st.looks, st.flashes, est_us / 1000.0, st.tickmax / 1000.0);
            for (int i = 0; i < NARR; i++) fprintf(stderr, " %.2f", st.move[i] / 65536.0);
            fprintf(stderr, "\n");
            memset(&st, 0, sizeof st); st.amin = 2; t0 = t;
        }
    }
    return 1;
}
int df_frame_override(Ctx *c, uint32_t addr)   /* the Game Loop render: 1 when it is not drawn this time */
{
    return addr == H_RENDER && enabled() ? render_starts(c) : 0;
}
#endif
