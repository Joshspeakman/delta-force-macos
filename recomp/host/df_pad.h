/* Delta Force 1 and 2: a game controller (Xbox, PlayStation, ... as the system reports it: GameController on the Mac,
 * SDL's game controller on Linux), played as the keyboard and mouse the games know. Shared by host/main.m and
 * host/linux.c, which read the controller and include this after defining
 *   pad_key(vk, down)        a key of the game's key map pressed or let go (as the keyboard would)
 *   pad_motion(dx, dy)       mouse motion in game pixels (the mouse's own path: look in a mission, the pointer in menus)
 *   pad_click(down)          the left mouse button
 * then call pad_tick() a few hundred times a second with the controller's state (all zero when there is none).
 *
 * In a mission (df_in_mission, df_frame.c) the sticks move and look and the buttons are the soldier's actions, sent as
 * the keys of his key map: DF_PAD_KEYS (the launch window passes the map the soldiers have: Classic, Modern or the
 * player's own, as hex, one byte per action in the order of the game's Remap screen), else the Classic keys. Elsewhere
 * (the menus, the score screen) both sticks move the pointer, A or the right trigger clicks, B is Escape, the d-pad
 * the arrow keys. DF_PAD=0 turns the controller off; DF_PAD_LOOK and DF_PAD_POINTER scale the look and pointer speeds;
 * DF_PAD_LOG=1 prints what it does. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PB_A = 1 << 0, PB_B = 1 << 1, PB_X = 1 << 2, PB_Y = 1 << 3, PB_LB = 1 << 4, PB_RB = 1 << 5, PB_VIEW = 1 << 6, PB_MENU = 1 << 7,
       PB_L3 = 1 << 8, PB_R3 = 1 << 9, PB_UP = 1 << 10, PB_DOWN = 1 << 11, PB_LEFT = 1 << 12, PB_RIGHT = 1 << 13 };
typedef struct { double lx, ly, rx, ry, lt, rt; uint32_t b; } PadState;   /* sticks -1..1, up positive; triggers 0..1 */

int df_in_mission(void), df_scoped(void);
static void pad_key(int vk, int down);
static void pad_motion(double dx, double dy);
static void pad_click(int down);

/* the actions, as entries of the key map (launcher.m's tables) */
enum { A_LEFT = 0, A_RIGHT = 1, A_FWD = 2, A_BACK = 3, A_PRONE = 4, A_CROUCH = 5, A_STAND = 6, A_JUMP = 7, A_FIRE = 8, A_SCOPE = 9,
       A_WALK = 10, A_CYCLE = 11, A_BINOC = 12, A_GRENADE = 21, A_ZOOMIN = 23, A_ZOOMOUT = 24, A_WAYPOINT = 27, A_AUDIO = 28,
       A_RELOAD = 31, A_ORDERS = 37, A_GOALS = 40, A_MAP = 41,
#ifdef DF2_EDITION
       A_NIGHT = 47, PAD_KEYS_N = 59
#else
       A_NIGHT = -1, PAD_KEYS_N = 46
#endif
};
static const uint8_t pad_classic[PAD_KEYS_N] = {   /* the Classic map (launcher.m classic_keys) */
#ifdef DF2_EDITION
    0x25,0x27,0x26,0x28,0x22,0x23,0x2E,0x20,0x11,0x53,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xDD,0xDB,0x54,0x59,0x57,0x41,0x56,
    0x44,0x4D,0x50,0x4C,0xBA,0xBE,0x09,0x4F,0x52,0x46,0x47,0x43,0x48,0x51,0x24,0x58,0x5A,0x4E,0x49,0x6F,0x6A,0x6D,0x6B,0x61,0x63,0x60,0x6E,0xDC,0x45};
#else
    0x25,0x27,0x26,0x28,0x22,0x23,0x2E,0x20,0x11,0x53,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBB,0xBD,0x54,0x59,0x57,0x41,0x56,
    0x44,0x4D,0x68,0x62,0x66,0x64,0x09,0x4F,0x52,0x46,0x47,0x43,0x48,0x51,0x45,0x24};
#endif

/* in a mission: each button held is one action held (its key down while the button is) */
static const struct { uint32_t b; int act; } pad_held[] = {
    {PB_A, A_JUMP}, {PB_X, A_RELOAD}, {PB_Y, A_CYCLE}, {PB_LB, A_BINOC}, {PB_RB, A_GRENADE}, {PB_VIEW, A_MAP}, {PB_L3, A_GOALS},
    {PB_R3, A_NIGHT}, {PB_UP, A_ZOOMIN}, {PB_DOWN, A_ZOOMOUT}, {PB_LEFT, A_WAYPOINT}, {PB_RIGHT, A_AUDIO}};
/* in the menus: buttons as keys (the A button and the right trigger click) */
static const struct { uint32_t b; int vk; } pad_menu[] = {
    {PB_B, 0x1b}, {PB_MENU, 0x1b}, {PB_X, 0x08}, {PB_Y, 0x0d}, {PB_UP, 0x26}, {PB_DOWN, 0x28}, {PB_LEFT, 0x25}, {PB_RIGHT, 0x27}};

static struct {
    int init, off, lg, scoped; double look, pointer, scope;
    uint8_t keys[PAD_KEYS_N];
    int mode;                       /* -1 nothing held yet, 0 menus, 1 mission */
    uint8_t down[256];              /* the keys this controller holds down */
    int clicking;
    uint32_t prev;                  /* the buttons last time (bit 31: the right trigger) */
    uint32_t latched;               /* buttons held when a mission began or ended: ignored until let go */
    int stance;                     /* 0 standing, 1 crouched, 2 prone: as the B button left him */
    int ask;                        /* the Menu button sent Escape: the game asks "End Mission? (Y/N)" */
    double b_held;                  /* how long B has been held (-1: its hold made him prone already) */
    struct { int vk; double left; } pulse[8];   /* stance keys: pressed briefly */
    double fx, fy;
} pad = { .mode = -1 };

static void pad_set(int vk, int down)
{
    if (vk <= 0 || vk > 255 || pad.down[vk] == !!down) return;
    pad.down[vk] = !!down; pad_key(vk, down);
    if (pad.lg) fprintf(stderr, "pad: key %02x %s\n", vk, down ? "down" : "up");
}
static int pad_vk(int act) { return act >= 0 && act < PAD_KEYS_N ? pad.keys[act] : 0; }
static void pad_release_all(void)
{
    for (int k = 0; k < 256; k++) if (pad.down[k]) pad_set(k, 0);
    for (int k = 0; k < 8; k++) pad.pulse[k].vk = 0;
    if (pad.clicking) { pad.clicking = 0; pad_click(0); }
}
static void pad_pulse(int vk)       /* a key pressed for 60 ms: the game sees it on a few of its 16 ms ticks */
{
    if (!vk) return;
    for (int k = 0; k < 8; k++) if (!pad.pulse[k].vk || pad.pulse[k].vk == vk) { pad.pulse[k].vk = vk; pad.pulse[k].left = 0.06; pad_set(vk, 1); return; }
}
static void pad_stance(int s) { pad.stance = s; pad_pulse(pad_vk(s == 2 ? A_PRONE : s == 1 ? A_CROUCH : A_STAND)); }

/* a stick past its dead zone, as 0..1 along its own direction, the curve making small moves fine */
static double pad_shape(double x, double y, double dz, double power, double *ox, double *oy)
{
    double m = sqrt(x * x + y * y);
    if (m <= dz) { *ox = *oy = 0; return 0; }
    double v = fmin(1, (m - dz) / (1 - dz)), s = pow(v, power) / m;
    *ox = x * s; *oy = y * s; return v;
}

static void pad_tick(const PadState *s, double dt)
{
    if (!pad.init) {
        pad.init = 1;
        const char *e = getenv("DF_PAD"); pad.off = e && !atoi(e);
        e = getenv("DF_PAD_LOG"); pad.lg = e && atoi(e);
        e = getenv("DF_PAD_LOOK"); pad.look = 1100 * (e && atof(e) > 0 ? atof(e) : 1);
        /* looking through the scope: slower, and finer still for a small push. Delta Force already turns a quarter as
         * fast scoped; Delta Force 2 turns faster the larger each tick's motion, so the stick's steady motion stays slow */
#ifdef DF2_EDITION
        pad.scope = 0.3;
#else
        pad.scope = 0.5;
#endif
        e = getenv("DF_PAD_SCOPE"); if (e && atof(e) > 0) pad.scope *= atof(e);
        e = getenv("DF_PAD_POINTER"); pad.pointer = 700 * (e && atof(e) > 0 ? atof(e) : 1);
        memcpy(pad.keys, pad_classic, sizeof pad.keys);
        e = getenv("DF_PAD_KEYS");
        if (e && strlen(e) == 2 * PAD_KEYS_N) for (int k = 0; k < PAD_KEYS_N; k++) { char h[3] = {e[2 * k], e[2 * k + 1], 0}; pad.keys[k] = (uint8_t)strtoul(h, 0, 16); }
    }
    if (pad.off) return;
    if (dt > 0.1) dt = 0.1;
    static PadState fake; static const char *fake_path; static int fake_init;
    if (!fake_init) { fake_init = 1; fake_path = getenv("DF_PAD_TEST"); }
    if (fake_path) {                /* tests: the controller's state from a file, "buttons(hex) lx ly rx ry lt rt" */
        FILE *f = fopen(fake_path, "r");
        if (f) { PadState t = {0}; unsigned bb = 0; if (fscanf(f, "%x %lf %lf %lf %lf %lf %lf", &bb, &t.lx, &t.ly, &t.rx, &t.ry, &t.lt, &t.rt) >= 1) { t.b = bb; fake = t; } fclose(f); }
        s = &fake;
    }
    for (int k = 0; k < 8; k++) if (pad.pulse[k].vk && (pad.pulse[k].left -= dt) <= 0) { pad_set(pad.pulse[k].vk, 0); pad.pulse[k].vk = 0; }

    int mode = df_in_mission() ? 1 : 0;
    uint32_t raw = s->b | (s->rt > 0.5 ? 1u << 31 : 0);
    if (mode != pad.mode) {         /* in or out of a mission: let go of everything; a button held across waits for its release */
        pad_release_all(); pad.fx = pad.fy = 0;
        if (pad.lg) fprintf(stderr, "pad: %s\n", mode ? "mission" : "menus");
        if (mode) { pad.stance = 0; pad.b_held = -1; }
        pad.ask = 0;
        pad.mode = mode; pad.latched = raw; pad.prev = raw;
    }
    pad.latched &= raw;
    uint32_t b = raw & ~pad.latched, pressed = b & ~pad.prev;
    double rt = (pad.latched >> 31) ? 0 : s->rt;
    double x, y;
    if (mode) {
        /* Menu: Escape, and the game asks whether to end the mission: A answers Y, B or Menu N (the buttons then do
         * nothing else until let go) */
        if (pad.ask && (pressed & (PB_A | PB_B | PB_MENU))) {
            pad_pulse((pressed & PB_A) ? 'Y' : 'N'); pad.ask = 0;
            pad.latched |= pressed & (PB_A | PB_B | PB_MENU); b &= ~pad.latched; pressed &= ~pad.latched;
        } else if (pressed & PB_MENU) { pad_pulse(0x1b); pad.ask = 1; }
        /* left stick: the movement keys, the walk key held for a small push */
        double v = pad_shape(s->lx, s->ly, 0.2, 1, &x, &y);
        pad_set(pad_vk(A_FWD), y > 0.38 * v && v > 0);  pad_set(pad_vk(A_BACK), y < -0.38 * v && v > 0);
        pad_set(pad_vk(A_RIGHT), x > 0.38 * v && v > 0); pad_set(pad_vk(A_LEFT), x < -0.38 * v && v > 0);
        pad_set(pad_vk(A_WALK), v > 0 && v < 0.55);
        /* right stick: look, as mouse motion */
        int sc = df_scoped();
        if (sc != pad.scoped) { pad.scoped = sc; if (pad.lg) fprintf(stderr, "pad: scope %s\n", sc ? "up" : "down"); }
        double look = pad.look * (sc ? pad.scope : 1);
        pad_shape(s->rx, s->ry, 0.12, sc ? 3 : 2.2, &x, &y);
        pad.fx += x * look * dt; pad.fy -= y * look * 0.7 * dt;
        /* triggers: fire and scope */
        pad_set(pad_vk(A_FIRE), rt > 0.35);
        pad_set(pad_vk(A_SCOPE), s->lt > 0.35);
        for (size_t k = 0; k < sizeof pad_held / sizeof *pad_held; k++) if (pad_held[k].act >= 0) pad_set(pad_vk(pad_held[k].act), (b & pad_held[k].b) != 0);
        if (pressed & PB_A) pad.stance = 0;          /* a jump stands him up */
        /* B: a tap crouches (or stands up again), held it goes prone */
        if (pressed & PB_B) pad.b_held = 0;
        if ((b & PB_B) && pad.b_held >= 0 && (pad.b_held += dt) >= 0.35) { pad_stance(2); pad.b_held = -1; }
        if (!(b & PB_B) && (pad.prev & PB_B) && pad.b_held >= 0) { pad_stance(pad.stance ? 0 : 1); pad.b_held = -1; }
    } else {
        /* menus: either stick moves the pointer */
        double ax, ay, bx, by;
        pad_shape(s->lx, s->ly, 0.18, 1.6, &ax, &ay); pad_shape(s->rx, s->ry, 0.18, 1.6, &bx, &by);
        x = fmax(-1, fmin(1, ax + bx)); y = fmax(-1, fmin(1, ay + by));
        pad.fx += x * pad.pointer * dt; pad.fy -= y * pad.pointer * dt;
        int click = (b & PB_A) || rt > 0.5;
        if (click != pad.clicking) { pad.clicking = click; pad_click(click); }
        uint8_t want[256] = {0};
        for (size_t k = 0; k < sizeof pad_menu / sizeof *pad_menu; k++) if (b & pad_menu[k].b) want[pad_menu[k].vk] = 1;
        for (size_t k = 0; k < sizeof pad_menu / sizeof *pad_menu; k++) pad_set(pad_menu[k].vk, want[pad_menu[k].vk]);
    }
    int dx = (int)pad.fx, dy = (int)pad.fy;
    if (dx || dy) { pad.fx -= dx; pad.fy -= dy; pad_motion(dx, dy); }
    pad.prev = b;
}
