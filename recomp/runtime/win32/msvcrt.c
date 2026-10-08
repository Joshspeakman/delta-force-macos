/* MSVCRT.dll: the C runtime entry points the games' DLLs import (The Sims: ijl10.dll, Intel's JPEG library of 1999;
 * Delta Force 1 and 2: Netsock.dll, the network layer). The executables link their own C runtime; these are only for
 * DLLs built against the system one. */
#include "w32.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* data imports: an IAT slot that holds the variable's address, not a function's (loader.c w32_data_import) */
uint32_t w32_data_import(const char *canon_name)
{
    static uint32_t page;
    if (!page) page = vm_alloc(0, 0x1000, 0x3000, 4);                     /* before the process heap exists */
    if (!strcmp(canon_name, "msvcrt!_adjust_fdiv")) return page;          /* int: 0 = no Pentium FDIV workaround */
    return 0;
}

IMPL(msvcrt, malloc) { RETC(heap_alloc(w32_process_heap, 0, ARG(0))); }
IMPL(msvcrt, free) { if (ARG(0)) heap_free(w32_process_heap, ARG(0)); RETC(0); }
IMPL(msvcrt, op_new) { RETC(heap_alloc(w32_process_heap, 0, ARG(0))); }                     /* ??2@YAPAXI@Z */
IMPL(msvcrt, op_delete) { if (ARG(0)) heap_free(w32_process_heap, ARG(0)); RETC(0); }      /* ??3@YAXPAX@Z */
IMPL(msvcrt, strchr) { const char *s = GS(ARG(0)), *p = strchr(s, (int)(ARG(1) & 0xff)); RETC(p ? ARG(0) + (uint32_t)(p - s) : 0); }
IMPL(msvcrt, strrchr) { const char *s = GS(ARG(0)), *p = strrchr(s, (int)(ARG(1) & 0xff)); RETC(p ? ARG(0) + (uint32_t)(p - s) : 0); }
IMPL(msvcrt, exit) { w32_exit(c, ARG(0)); }

/* _initterm(begin, end): each non-null function pointer in [begin, end), in order */
IMPL(msvcrt, _initterm)
{
    uint32_t b = ARG(0), e = ARG(1), ret = rt_r32(G_MEM, c->esp);
    for (uint32_t p = b; p < e; p += 4) { uint32_t fn = rt_r32(G_MEM, p); if (fn) w32_callback(c, fn, 0, 0); }
    (void)ret; RETC(0);
}

/* _ftol: ST(0) popped and converted toward zero to a 64-bit integer in edx:eax */
IMPL(msvcrt, _ftol)
{
    double v = ST(0); FPOP();
    int64_t r = (v != v || fabs(v) >= 9223372036854775808.0) ? INT64_MIN : (int64_t)v;
    c->edx = (uint32_t)((uint64_t)r >> 32); RETC((uint32_t)r);
}

uint32_t w32_format(char *out, size_t cap, const char *f, uint32_t va);
IMPL(msvcrt, sprintf)
{
    char buf[2048]; uint32_t n = w32_format(buf, sizeof buf, GS(ARG(1)), c->esp + 12);
    memcpy(GP(ARG(0)), buf, n + 1); RETC(n);
}
IMPL(msvcrt, _vsnprintf)
{
    char buf[2048]; uint32_t n = w32_format(buf, sizeof buf, GS(ARG(2)), ARG(3)), cap = ARG(1);
    if (n < cap) { memcpy(GP(ARG(0)), buf, n + 1); RETC(n); }
    memcpy(GP(ARG(0)), buf, cap); RETC((uint32_t)-1);
}

/* _except_handler3: ijl10 guards its CPU checks with __try; the recompiled DLLs raise no exceptions there (rt_cpuid
 * reports the CPU), so a frame that is reached is reported and passed on (ExceptionContinueSearch) */
IMPL(msvcrt, _except_handler3)
{
    static int once; if (!once++) fprintf(stderr, "w32: msvcrt _except_handler3 reached (exception %08x): passed on\n", rt_r32(G_MEM, ARG(0)));
    RETC(1);
}

/* a stdcall import called from here with its arguments in a frame of its own (it pops them) */
static void call_stdcall(Ctx *c, void (*fn)(Ctx *), int n, const uint32_t *args)
{
    c->esp -= 4 * (uint32_t)(n + 1);
    rt_w32(G_MEM, c->esp, 0);
    for (int k = 0; k < n; k++) rt_w32(G_MEM, c->esp + 4 + 4 * (uint32_t)k, args[k]);
    fn(c);
}

IMPL(msvcrt, calloc)
{
    uint64_t n = (uint64_t)ARG(0) * ARG(1); if (n >> 31) RETC(0);
    RETC(heap_alloc(w32_process_heap, 8, (uint32_t)n));                  /* HEAP_ZERO_MEMORY */
}
IMPL(msvcrt, realloc)
{
    uint32_t p = ARG(0), n = ARG(1);
    if (!p) RETC(heap_alloc(w32_process_heap, 0, n));
    if (!n) { heap_free(w32_process_heap, p); RETC(0); }
    RETC(heap_realloc(w32_process_heap, 0, p, n));
}
IMPL(msvcrt, atoi) { RETC((uint32_t)atoi(GS(ARG(0)))); }
IMPL(msvcrt, strncmp) { RETC((uint32_t)strncmp(GS(ARG(0)), GS(ARG(1)), ARG(2))); }
IMPL(msvcrt, strncpy) { strncpy((char *)GP(ARG(0)), GS(ARG(1)), ARG(2)); RETC(ARG(0)); }
IMPL(msvcrt, strstr) { const char *s = GS(ARG(0)), *p = strstr(s, GS(ARG(1))); RETC(p ? ARG(0) + (uint32_t)(p - s) : 0); }
IMPL(msvcrt, _strupr) { for (char *s = (char *)GP(ARG(0)); *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; RETC(ARG(0)); }

/* qsort: an insertion sort in guest memory (the lists sorted are short), the game's comparison called for each pair */
IMPL(msvcrt, qsort)
{
    uint32_t base = ARG(0), n = ARG(1), size = ARG(2), cmp = ARG(3);
    uint32_t hold = heap_alloc(w32_process_heap, 0, size ? size : 1);
    for (uint32_t i = 1; i < n; i++) {
        memcpy(GP(hold), GP(base + i * size), size);
        uint32_t j = i;
        while (j > 0) {
            uint32_t a[2] = {base + (j - 1) * size, hold};
            if ((int32_t)w32_callback(c, cmp, 2, a) <= 0) break;
            memmove(GP(base + j * size), GP(base + (j - 1) * size), size); j--;
        }
        memcpy(GP(base + j * size), GP(hold), size);
    }
    heap_free(w32_process_heap, hold); RETC(0);
}

/* time, localtime, asctime: time_t is 32 bits; localtime and asctime return one static buffer each, as MSVCRT's do */
IMPL(msvcrt, time) { uint32_t t = (uint32_t)time(0); if (ARG(0)) rt_w32(G_MEM, ARG(0), t); RETC(t); }
IMPL(msvcrt, localtime)
{
    static uint32_t out; if (!out) out = heap_alloc(w32_process_heap, 8, 36);
    time_t t = (time_t)(int32_t)rt_r32(G_MEM, ARG(0)); struct tm tm; if (!localtime_r(&t, &tm)) RETC(0);
    int v[9] = {tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_wday, tm.tm_yday, tm.tm_isdst > 0};
    for (int k = 0; k < 9; k++) rt_w32(G_MEM, out + 4 * (uint32_t)k, (uint32_t)v[k]);
    RETC(out);
}
IMPL(msvcrt, asctime)
{
    static uint32_t out; if (!out) out = heap_alloc(w32_process_heap, 8, 32);
    struct tm tm = {0}; int *f[9] = {&tm.tm_sec, &tm.tm_min, &tm.tm_hour, &tm.tm_mday, &tm.tm_mon, &tm.tm_year, &tm.tm_wday, &tm.tm_yday, &tm.tm_isdst};
    for (int k = 0; k < 9; k++) *f[k] = (int)rt_r32(G_MEM, ARG(0) + 4 * (uint32_t)k);
    char b[64]; asctime_r(&tm, b); snprintf((char *)GP(out), 26, "%s", b); RETC(out);
}

/* _beginthread(start, stack, arg) -> CreateThread; the cdecl start routine leaves its argument, which ends the thread
 * all the same */
void imp_kernel32_CreateThread(Ctx *); void imp_kernel32_ExitThread(Ctx *); void imp_kernel32_CloseHandle(Ctx *);
IMPL(msvcrt, _beginthread)
{
    uint32_t a[6] = {0, ARG(1), ARG(0), ARG(2), 0, 0};
    call_stdcall(c, imp_kernel32_CreateThread, 6, a);
    uint32_t h = c->eax; RETC(h ? h : 0xffffffffu);
}
IMPL(msvcrt, _endthread) { uint32_t a[1] = {0}; call_stdcall(c, imp_kernel32_ExitThread, 1, a); RETC(0); }

/* FILE: a guest cell per open file (MSVCRT's FILE is opaque to these DLLs), the host stream kept beside it. Text mode
 * reads drop the \r of each \r\n, as MSVCRT's do. */
enum { MAXF = 32 };
static struct { uint32_t g; FILE *f; int text; } F[MAXF];
static pthread_mutex_t f_lock = PTHREAD_MUTEX_INITIALIZER;
static int f_find(uint32_t g) { for (int k = 0; k < MAXF; k++) if (g && F[k].g == g) return k; return -1; }
IMPL(msvcrt, fopen)
{
    const char *mode = GS(ARG(1)); char host[2048], hm[8]; size_t n = 0;
    int write = strpbrk(mode, "wa+") != 0;
    if (w32_host_path(GS(ARG(0)), host, sizeof host, write)) RETC(0);
    for (const char *m = mode; *m && n < sizeof hm - 1; m++) if (strchr("rwa+", *m)) hm[n++] = *m;
    hm[n] = 0;
    FILE *f = fopen(host, hm); if (!f) RETC(0);
    pthread_mutex_lock(&f_lock);
    int k = 0; while (k < MAXF && F[k].g) k++;
    if (k == MAXF) { pthread_mutex_unlock(&f_lock); fclose(f); RETC(0); }
    F[k].g = heap_alloc(w32_process_heap, 8, 32); F[k].f = f; F[k].text = !strchr(mode, 'b');
    uint32_t g = F[k].g; pthread_mutex_unlock(&f_lock);
    RETC(g);
}
IMPL(msvcrt, fclose)
{
    pthread_mutex_lock(&f_lock); int k = f_find(ARG(0));
    if (k < 0) { pthread_mutex_unlock(&f_lock); RETC((uint32_t)-1); }
    fclose(F[k].f); heap_free(w32_process_heap, F[k].g); F[k].g = 0; F[k].f = 0;
    pthread_mutex_unlock(&f_lock); RETC(0);
}
IMPL(msvcrt, fgets)
{
    uint32_t buf = ARG(0); int n = (int)ARG(1); int k = f_find(ARG(2));
    if (k < 0 || n <= 0 || !fgets((char *)GP(buf), n, F[k].f)) RETC(0);
    if (F[k].text) { char *s = (char *)GP(buf); size_t l = strlen(s); if (l >= 2 && s[l - 2] == '\r' && s[l - 1] == '\n') { s[l - 2] = '\n'; s[l - 1] = 0; } }
    RETC(buf);
}
IMPL(msvcrt, fprintf)
{
    int k = f_find(ARG(0)); if (k < 0) RETC((uint32_t)-1);
    char buf[4096]; uint32_t n = w32_format(buf, sizeof buf, GS(ARG(1)), c->esp + 12);
    if (n >= sizeof buf) n = sizeof buf - 1;
    fwrite(buf, 1, n, F[k].f); fflush(F[k].f); RETC(n);
}

/* sscanf: the format taken one conversion at a time (with the literal text before it) through the host's sscanf, each
 * result stored through the next guest pointer. Conversions: d i u o x X c s [ f e g (l: double), n, * to skip. */
IMPL(msvcrt, sscanf)
{
    const char *in = GS(ARG(0)), *f = GS(ARG(1)); uint32_t va = c->esp + 12; int pos = 0, got = 0;
    while (*f) {
        const char *pc = strchr(f, '%');
        while (pc && pc[1] == '%') pc = strchr(pc + 2, '%');
        if (!pc) break;
        const char *q = pc + 1; int skip = *q == '*'; if (skip) q++;
        while (*q >= '0' && *q <= '9') q++;
        int lng = 0; while (*q == 'l' || *q == 'h') { if (*q == 'l') lng = 1; q++; }
        char conv = *q; if (!conv) break;
        if (conv == '[') { q++; if (*q == '^') q++; if (*q == ']') q++; while (*q && *q != ']') q++; if (!*q) break; }
        q++;
        char seg[512]; size_t sl = (size_t)(q - f); if (sl > sizeof seg - 4) break;
        memcpy(seg, f, sl); strcpy(seg + sl, "%n"); f = q;
        if (!in[pos] && got == 0 && conv != 'n') RETC((uint32_t)-1);
        int used = -1, r;
        if (conv == 'n') { r = sscanf(in + pos, seg, &used); used = 0; if (!skip) { rt_w32(G_MEM, rt_r32(G_MEM, va), (uint32_t)pos); va += 4; } continue; }
        if (skip) { r = sscanf(in + pos, seg, &used); if (used < 0) break; pos += used; continue; }
        uint32_t dst = rt_r32(G_MEM, va); va += 4;
        if (strchr("diuoxX", conv)) { unsigned v = 0; r = sscanf(in + pos, seg, &v, &used); if (r < 1) break; rt_w32(G_MEM, dst, v); }
        else if (strchr("feEgG", conv)) {
            double d = 0; float fl = 0;
            if (lng) { r = sscanf(in + pos, seg, &d, &used); if (r < 1) break; rt_w64(G_MEM, dst, *(uint64_t *)&d); }
            else { r = sscanf(in + pos, seg, &fl, &used); if (r < 1) break; rt_w32(G_MEM, dst, *(uint32_t *)&fl); }
        } else {                                                          /* c s [ : into a host buffer, then copied */
            char tmp[4096]; memset(tmp, 0, sizeof tmp); r = sscanf(in + pos, seg, tmp, &used); if (r < 1) break;
            size_t len = conv == 'c' ? 1 : strlen(tmp) + 1;
            if (conv == 'c') { const char *w = pc + 1; int wid = atoi(w); if (wid > 1) len = (size_t)wid; }
            memcpy(GP(dst), tmp, len);
        }
        if (used < 0) break;
        pos += used; got++;
    }
    RETC((uint32_t)got);
}

/* _findfirst / _findnext / _findclose on FindFirstFileA: struct _finddata_t is attrib, three time_t (create, access,
 * write), size, name[260] */
void imp_kernel32_FindFirstFileA(Ctx *); void imp_kernel32_FindNextFileA(Ctx *); void imp_kernel32_FindClose(Ctx *);
static void finddata(uint32_t wfd, uint32_t out)
{
    rt_w32(G_MEM, out, rt_r32(G_MEM, wfd));
    for (int k = 0; k < 3; k++) {
        uint64_t ft = rt_r32(G_MEM, wfd + 4 + 8 * (uint32_t)k) | (uint64_t)rt_r32(G_MEM, wfd + 8 + 8 * (uint32_t)k) << 32;
        rt_w32(G_MEM, out + 4 + 4 * (uint32_t)k, ft ? (uint32_t)(ft / 10000000 - 11644473600ull) : 0);
    }
    rt_w32(G_MEM, out + 16, rt_r32(G_MEM, wfd + 32));
    snprintf((char *)GP(out + 20), 260, "%s", GS(wfd + 44));
}
IMPL(msvcrt, _findfirst)
{
    uint32_t wfd = heap_alloc(w32_process_heap, 8, 320), a[2] = {ARG(0), wfd}, out = ARG(1);
    call_stdcall(c, imp_kernel32_FindFirstFileA, 2, a);
    uint32_t h = c->eax; if (h != 0xffffffffu) finddata(wfd, out);
    heap_free(w32_process_heap, wfd); RETC(h);
}
IMPL(msvcrt, _findnext)
{
    uint32_t wfd = heap_alloc(w32_process_heap, 8, 320), a[2] = {ARG(0), wfd}, out = ARG(1);
    call_stdcall(c, imp_kernel32_FindNextFileA, 2, a);
    uint32_t ok = c->eax; if (ok) finddata(wfd, out);
    heap_free(w32_process_heap, wfd); RETC(ok ? 0 : (uint32_t)-1);
}
IMPL(msvcrt, _findclose) { uint32_t a[1] = {ARG(0)}; call_stdcall(c, imp_kernel32_FindClose, 1, a); RETC(c->eax ? 0 : (uint32_t)-1); }
