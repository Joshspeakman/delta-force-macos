/* Winsock 1.1 (WSOCK32 by ordinal) on BSD sockets: the TCP and UDP sockets Dungeon Siege II's network layer opens for
 * LAN and direct-IP games. SOCKETs are small handles into a table of host descriptors; fd_sets, sockaddrs, option
 * levels and error codes are translated. (Router port forwarding is separate: portmap.c, off with DS_NO_PORTMAP=1.) */
#include "w32.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static void socket_no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    (void)fd; /* Linux suppresses SIGPIPE on each send with MSG_NOSIGNAL. */
#endif
}

#define INVALID_SOCKET_ 0xffffffffu
#define SOCKET_ERROR_ 0xffffffffu
enum { MAXS = 256, SBASE = 0x1000 };
static struct { int fd; int nb; int bound; int connecting; } S[MAXS];   /* connecting: a non-blocking connect not yet seen to finish */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t wsa_err(int e)
{
    switch (e) {
    case EWOULDBLOCK: case EINPROGRESS: return 10035;    /* WSAEWOULDBLOCK (also for a non-blocking connect) */
    case EALREADY: return 10037; case ENOTSOCK: return 10038; case EDESTADDRREQ: return 10039; case EMSGSIZE: return 10040;
    case ENOPROTOOPT: return 10042; case EPROTONOSUPPORT: return 10043; case EOPNOTSUPP: return 10045; case EAFNOSUPPORT: return 10047;
    case EADDRINUSE: return 10048; case EADDRNOTAVAIL: return 10049; case ENETDOWN: return 10050; case ENETUNREACH: return 10051;
    case ECONNABORTED: return 10053; case ECONNRESET: case EPIPE: return 10054; case ENOBUFS: return 10055; case EISCONN: return 10056;
    case ENOTCONN: return 10057; case ESHUTDOWN: return 10058; case ETIMEDOUT: return 10060; case ECONNREFUSED: return 10061;
    case EHOSTDOWN: return 10064; case EHOSTUNREACH: return 10065; case EACCES: return 10013; case EFAULT: return 10014;
    case EINTR: return 10004; case EMFILE: return 10024; case EBADF: return 10038;
    }
    return 10022;                                       /* WSAEINVAL */
}
/* W32_NETLOG=1: each send/recv with its first bytes (development) */
static void netlog(const char *what, uint32_t sock, const uint8_t *p, long n)
{
    static int on = -1; if (on < 0) on = getenv("W32_NETLOG") != 0;
    if (!on) return;
    fprintf(stderr, "net: %s %x %ld:", what, sock, n);
    for (long k = 0; k < n && k < 48; k++) fprintf(stderr, " %02x", p[k]);
    fprintf(stderr, " |"); for (long k = 0; k < n && k < 48; k++) fputc(p[k] >= 32 && p[k] < 127 ? p[k] : '.', stderr); fprintf(stderr, "|\n");
}
static uint32_t fail(Ctx *c, uint32_t e) { w32_set_last_error(c, e); return SOCKET_ERROR_; }
static uint32_t fail_errno(Ctx *c) { return fail(c, wsa_err(errno)); }
static int sfd(uint32_t s)
{
    if (s < SBASE || s >= SBASE + MAXS) return -1;
    pthread_mutex_lock(&s_lock); int fd = S[s - SBASE].fd - 1; pthread_mutex_unlock(&s_lock);
    return fd;
}
#define SOCK(arg, n) int fd = sfd(arg); if (fd < 0) RET(fail(c, 10038), n)   /* WSAENOTSOCK */

/* sockaddr_in: Windows {u16 family, u16 port, u32 addr, 8 zero} <-> BSD {u8 len, u8 family, ...} */
static int to_host(uint32_t g, uint32_t len, struct sockaddr_in *a)
{
    if (!g || len < 8 || rt_r16(G_MEM, g) != 2) return 0;
    memset(a, 0, sizeof *a);
#ifdef __APPLE__
    a->sin_len = sizeof *a;
#endif
    a->sin_family = AF_INET;
    memcpy(&a->sin_port, GP(g + 2), 2); memcpy(&a->sin_addr, GP(g + 4), 4);
    return 1;
}
static void to_guest(const struct sockaddr_in *a, uint32_t g, uint32_t lenp)
{
    if (!g) return;
    uint32_t cap = lenp ? rt_r32(G_MEM, lenp) : 16; if (cap < 16) return;
    rt_w16(G_MEM, g, 2); memcpy(GP(g + 2), &a->sin_port, 2); memcpy(GP(g + 4), &a->sin_addr, 4); memset(GP(g + 8), 0, 8);
    if (lenp) rt_w32(G_MEM, lenp, 16);
}

/* W32_NETVIRT=k (development, 1-9): this copy is 127.0.0.k on a virtual network of copies running on one computer, so
 * that games whose protocol addresses players by IP alone (Delta Force's Netsock answers every peer on one fixed port)
 * can be hosted and joined between two copies. Every socket is bound on 127.0.0.1 at a port of its own copy's range
 * (30000 + 2000 k + port % 2000; sockets the game never binds get one before they send); datagrams to 127.0.0.j go to
 * copy j's range, to 255.255.255.255 to every copy's; senders are shown as 127.0.0.j at their virtual port. */
static int virt = -1; static uint16_t vports[64]; static int nvports, vnext = 200;
int w32_netvirt(void) { if (virt < 0) { const char *v = getenv("W32_NETVIRT"); virt = v ? atoi(v) : 0; if (virt < 0 || virt > 9) virt = 0; } return virt; }
static uint16_t vreal(int k, uint16_t p) { return (uint16_t)(30000 + 2000 * k + p % 2000); }
static void vlocal(struct sockaddr_in *a, uint16_t vport)          /* a host address for this copy's virtual port */
{
    pthread_mutex_lock(&s_lock);
    int seen = 0; for (int i = 0; i < nvports; i++) if (vports[i] == vport) seen = 1;
    if (!seen && nvports < 64) vports[nvports++] = vport;
    pthread_mutex_unlock(&s_lock);
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK); a->sin_port = htons(vreal(virt, vport));
}
static int vguest(struct sockaddr_in *a)                           /* a host sender -> its virtual address */
{
    uint16_t r = ntohs(a->sin_port);
    if (a->sin_addr.s_addr != htonl(INADDR_LOOPBACK) || r < 32000 || r >= 50000) return 0;
    int k = (r - 30000) / 2000; uint16_t low = (uint16_t)((r - 30000) % 2000), vp = low;
    for (int i = 0; i < nvports; i++) if (vports[i] % 2000 == low) vp = vports[i];
    a->sin_addr.s_addr = htonl(0x7f000000u | (uint32_t)k); a->sin_port = htons(vp); return 1;
}
static int vbind_if_needed(uint32_t s, int fd)
{
    pthread_mutex_lock(&s_lock); int b = S[s - SBASE].bound; pthread_mutex_unlock(&s_lock);
    if (b) return 0;
    struct sockaddr_in a = {0};
#ifdef __APPLE__
    a.sin_len = sizeof a;
#endif
    a.sin_family = AF_INET;
    for (int tries = 0; tries < 800; tries++) {
        pthread_mutex_lock(&s_lock); uint16_t vp = (uint16_t)vnext++; if (vnext >= 1000) vnext = 200; pthread_mutex_unlock(&s_lock);
        vlocal(&a, vp);
        if (!bind(fd, (struct sockaddr *)&a, sizeof a)) { pthread_mutex_lock(&s_lock); S[s - SBASE].bound = 1; pthread_mutex_unlock(&s_lock); return 0; }
    }
    return -1;
}
/* The game's UDP port (Delta Force: Netsock's 3568, set by df_net.c). Netsock receives on a socket bound to it and sends
 * from another, unbound one, and answers every peer at the peer's address on that same fixed port. Across home routers
 * (NAT) that fails: the router of a player who sent from some other port drops an answer to 3568. So while the game
 * port is bound:
 *   - datagrams from the game's unbound UDP sockets leave from the game port's socket (the router then has a mapping
 *     for the game port, and the far side sees the port it will answer to);
 *   - a datagram to a peer's game port goes to the port that peer's datagrams last came from, when its router changed
 *     it (learned from what arrives on the game port, kept two minutes).
 * W32_NET_NAT=0 turns both off. */
static uint16_t game_port; static int game_fd = -1;
static struct { uint32_t ip; uint16_t port; double t; } learned[64];
void (*w32_game_port_closed)(uint16_t port);           /* df_net.c: hosting ended */
void w32_set_game_port(uint16_t port) { pthread_mutex_lock(&s_lock); game_port = port; pthread_mutex_unlock(&s_lock); }
static int nat_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("W32_NET_NAT"); on = !e || atoi(e) != 0; } return on; }
static double mono_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
static void learn(const struct sockaddr_in *a)          /* a datagram arrived on the game port from a */
{
    if (!game_port || !nat_on()) return;
    pthread_mutex_lock(&s_lock);
    int k = 0, old = 0; for (; k < 64 && learned[k].ip != a->sin_addr.s_addr; k++) if (learned[k].t < learned[old].t) old = k;
    if (k == 64) k = old;
    learned[k].ip = a->sin_addr.s_addr; learned[k].port = ntohs(a->sin_port); learned[k].t = mono_s();
    pthread_mutex_unlock(&s_lock);
}
static void nat_dest(struct sockaddr_in *a)             /* to a peer's game port: the port its router uses */
{
    if (!game_port || !nat_on() || ntohs(a->sin_port) != game_port) return;
    pthread_mutex_lock(&s_lock);
    for (int k = 0; k < 64; k++)
        if (learned[k].ip == a->sin_addr.s_addr && learned[k].port != game_port && mono_s() - learned[k].t < 120) { a->sin_port = htons(learned[k].port); break; }
    pthread_mutex_unlock(&s_lock);
}
static int send_fd(uint32_t s, int fd)                  /* an unbound UDP socket sends from the game port */
{
    if (game_fd < 0 || !nat_on() || w32_netvirt()) return fd;
    pthread_mutex_lock(&s_lock); int b = S[s - SBASE].bound, g = game_fd; pthread_mutex_unlock(&s_lock);
    int type = 0; socklen_t tl = sizeof type; getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl);
    return !b && type == SOCK_DGRAM && g >= 0 ? g : fd;
}
/* The game searches (df_net.c): datagrams to 255.255.255.254 go to every host the game list knows and to the local
 * network. Netsock keeps sending to the address it searched (the broadcast address or that one) once it has joined a
 * game, so from then on those go to the host that was joined only. w32_send_drop: datagrams not sent at all. */
int (*w32_search_targets)(uint32_t *ip, uint16_t *port, int cap, uint16_t game_port);
int (*w32_send_drop)(uint32_t ip, uint16_t port);       /* network order ip, host order port */
void (*w32_recv_filter)(uint8_t *buf, uint32_t n, uint32_t ip, uint16_t port);   /* a datagram received (df_net.c) */
#define SEARCH_ADDR 0xfffffffeu                      /* 255.255.255.254 */
static uint32_t session_host;                           /* network order; 0: none joined */
void w32_set_session_host(uint32_t ip) { pthread_mutex_lock(&s_lock); session_host = ip; pthread_mutex_unlock(&s_lock); }

/* Who may talk to a player who joins (df_net.c sets the role). A host must take datagrams from anyone (that is what a
 * public game is), but a joining player only needs answers from the hosts it asked and, once in a game, its host:
 *   - joined: datagrams on the game port from any other address are dropped;
 *   - searching: only from addresses this copy sent to in the last 5 minutes, or from the local network after a
 *     broadcast search.
 * So the game's 1998 network code, on a joining player's computer, is reachable only by the host the player chose.
 * W32_NET_FILTER=0 turns it off. */
static int net_role;                                    /* 0 none, 1 hosting, 2 joining */
static int fd_is_game_side(uint32_t s, int fd)          /* the game port's socket, or an unbound one sending for it */
{
    if (fd == game_fd) return 1;
    pthread_mutex_lock(&s_lock); int b = S[s - SBASE].bound; pthread_mutex_unlock(&s_lock);
    return !b && game_port;
}
static struct { uint32_t ip; double t; } asked[128]; static int nasked; static double broadcast_t;
void w32_set_net_role(int role) { pthread_mutex_lock(&s_lock); net_role = role; pthread_mutex_unlock(&s_lock); }
static void note_sent(uint32_t ip)                      /* network order, as the game addressed it */
{
    pthread_mutex_lock(&s_lock);
    if (ip == 0xffffffffu) broadcast_t = mono_s();
    else {
        int k = 0, old = 0; for (; k < nasked && asked[k].ip != ip; k++) if (asked[k].t < asked[old].t) old = k;
        if (k == nasked) { if (nasked < 128) nasked++; else k = old; }
        asked[k].ip = ip; asked[k].t = mono_s();
    }
    pthread_mutex_unlock(&s_lock);
}
static int on_local_network(uint32_t ip)
{
    static struct { uint32_t a, m; } nets[16]; static int nn = -1; static pthread_mutex_t l = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&l);
    if (nn < 0) {
        nn = 0; struct ifaddrs *ifs = 0;
        if (!getifaddrs(&ifs)) {
            for (struct ifaddrs *i = ifs; i && nn < 16; i = i->ifa_next)
                if (i->ifa_addr && i->ifa_netmask && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_UP)) {
                    nets[nn].a = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr; nets[nn].m = ((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr; nn++;
                }
            freeifaddrs(ifs);
        }
    }
    int ok = 0; for (int k = 0; k < nn; k++) if (((ip ^ nets[k].a) & nets[k].m) == 0) ok = 1;
    pthread_mutex_unlock(&l);
    return ok;
}
static int allowed(uint32_t ip)                         /* ip: the sender as the game sees it (network order) */
{
    static int on = -1; if (on < 0) { const char *e = getenv("W32_NET_FILTER"); on = !e || atoi(e) != 0; }
    if (!on) return 1;
    pthread_mutex_lock(&s_lock);
    int role = net_role, ok = 1; uint32_t host = session_host; double now = mono_s();
    if (role == 2) {
        if (host) ok = ip == host;
        else {
            ok = 0; for (int k = 0; k < nasked; k++) if (asked[k].ip == ip && now - asked[k].t < 300) ok = 1;
            if (!ok && now - broadcast_t < 300) { pthread_mutex_unlock(&s_lock); ok = on_local_network(ip) || (ntohl(ip) >> 24) == 127; return ok; }
        }
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}
/* W32_NETFUZZ=<seed>[:<percent>] (development, test mode only): datagrams arriving on the game port are damaged before
 * the game sees them (bits flipped, bytes changed, cut short, or lengthened with junk up to the game's buffer), a
 * percentage of them (default 30). With a runtime built with DF_SANITIZE=address, this checks what a malicious peer's
 * datagrams can do to the game's network code and the runtime under it. */
static ssize_t netfuzz(uint8_t *b, ssize_t n, uint32_t cap)
{
    static int on = -1; static uint64_t st; static unsigned pct = 30; static long count;
    if (on < 0) {
        const char *e = getenv("W32_NETFUZZ"); on = e && getenv("DS_TEST") && atoi(getenv("DS_TEST"));
        if (on) { st = strtoull(e, 0, 10) * 0x9e3779b97f4a7c15ull + 1; const char *p = strchr(e, ':'); if (p) pct = (unsigned)atoi(p + 1); }
    }
    if (!on) return n;
    #define R() (st ^= st << 13, st ^= st >> 7, st ^= st << 17, (uint32_t)(st >> 11))
    if (R() % 100 >= pct) return n;
    count++;
    switch (R() % 5) {
    case 0: for (int k = 1 + (int)(R() % 4); k > 0 && n > 0; k--) b[R() % (uint32_t)n] ^= (uint8_t)(1u << (R() % 8)); break;
    case 1: for (int k = 1 + (int)(R() % 8); k > 0 && n > 0; k--) b[R() % (uint32_t)n] = (uint8_t)R(); break;
    case 2: if (n > 0) n = R() % (uint32_t)n; break;
    case 3: { ssize_t m = n + 1 + (ssize_t)(R() % 2048); if (m > (ssize_t)cap) m = cap; for (ssize_t k = n; k < m; k++) b[k] = (uint8_t)R(); n = m; break; }
    case 4: if (n >= 4) { uint32_t at = R() % (uint32_t)(n - 3); uint32_t v = R() % 3 ? 0xffffffffu : R(); memcpy(b + at, &v, 4); } break;   /* a length or count */
    }
    #undef R
    if (count % 500 == 0) fprintf(stderr, "net: %ld datagrams damaged (W32_NETFUZZ)\n", count);
    return n;
}
IMPL(wsock32, ord23)                                    /* socket(af, type, protocol) */
{
    uint32_t af = ARG(0), type = ARG(1), proto = ARG(2);
    if (af != 2) { w32_set_last_error(c, 10047); RET(INVALID_SOCKET_, 3); }
    if (type != 1 && type != 2) { w32_set_last_error(c, 10044); RET(INVALID_SOCKET_, 3); }
    int fd = socket(AF_INET, type == 1 ? SOCK_STREAM : SOCK_DGRAM, (int)proto);
    if (fd < 0) { w32_set_last_error(c, wsa_err(errno)); RET(INVALID_SOCKET_, 3); }
    socket_no_sigpipe(fd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (type == 2) { int one = 1; setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one); }   /* LAN searches (df_net.c) */
    pthread_mutex_lock(&s_lock); int k = 0; while (k < MAXS && S[k].fd) k++;
    if (k < MAXS) { S[k].fd = fd + 1; S[k].nb = 0; S[k].connecting = 0; S[k].bound = 0; }
    pthread_mutex_unlock(&s_lock);
    if (k == MAXS) { close(fd); w32_set_last_error(c, 10024); RET(INVALID_SOCKET_, 3); }
    RET(SBASE + (uint32_t)k, 3);
}
IMPL(wsock32, ord3)                                     /* closesocket(s) */
{
    SOCK(ARG(0), 1);
    pthread_mutex_lock(&s_lock); S[ARG(0) - SBASE].fd = 0; int was_game = fd == game_fd; if (was_game) game_fd = -1; uint16_t gp = game_port; pthread_mutex_unlock(&s_lock);
    close(fd);
    if (was_game && w32_game_port_closed) w32_game_port_closed(gp);
    RET(0, 1);
}
IMPL(wsock32, ord2)                                     /* bind(s, addr, len) */
{
    SOCK(ARG(0), 3); struct sockaddr_in a; if (!to_host(ARG(1), ARG(2), &a)) RET(fail(c, 10014), 3);
    if (w32_netvirt()) { uint16_t vp = ntohs(a.sin_port); if (!vp) { pthread_mutex_lock(&s_lock); vp = (uint16_t)vnext++; pthread_mutex_unlock(&s_lock); } vlocal(&a, vp); }
    if (bind(fd, (struct sockaddr *)&a, sizeof a)) RET(fail_errno(c), 3);
    pthread_mutex_lock(&s_lock); S[ARG(0) - SBASE].bound = 1;
    { struct sockaddr_in g = {0}; socklen_t gl = sizeof g; int type = 0; socklen_t tl = sizeof type;
      getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl);
      if (game_port && type == SOCK_DGRAM && !getsockname(fd, (struct sockaddr *)&g, &gl)) {
          if (w32_netvirt()) vguest(&g);
          if (ntohs(g.sin_port) == game_port) game_fd = fd;
      } }
    pthread_mutex_unlock(&s_lock);
    RET(0, 3);
}
IMPL(wsock32, ord4)                                     /* connect(s, addr, len) */
{
    SOCK(ARG(0), 3); struct sockaddr_in a; if (!to_host(ARG(1), ARG(2), &a)) RET(fail(c, 10014), 3);
    if (getenv("W32_NETLOG")) { char ip[32]; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip); fprintf(stderr, "net: connect %x to %s:%u (fd %d)\n", ARG(0), ip, ntohs(a.sin_port), fd); }
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) {
        int e = errno;
        if (e == EINPROGRESS) { pthread_mutex_lock(&s_lock); S[ARG(0) - SBASE].connecting = 1; pthread_mutex_unlock(&s_lock); }
        RET(fail(c, wsa_err(e)), 3);
    }
    RET(0, 3);
}
IMPL(wsock32, ord13) { SOCK(ARG(0), 2); if (listen(fd, (int)ARG(1))) RET(fail_errno(c), 2); RET(0, 2); }   /* listen */
IMPL(wsock32, ord1)                                     /* accept(s, addr, len*) */
{
    SOCK(ARG(0), 3); struct sockaddr_in a; socklen_t l = sizeof a;
    int n = accept(fd, (struct sockaddr *)&a, &l);
    if (n < 0) { w32_set_last_error(c, wsa_err(errno)); RET(INVALID_SOCKET_, 3); }
    socket_no_sigpipe(n); fcntl(n, F_SETFD, FD_CLOEXEC);
    pthread_mutex_lock(&s_lock); int k = 0; while (k < MAXS && S[k].fd) k++;
    if (k < MAXS) { S[k].fd = n + 1; S[k].nb = 0; S[k].connecting = 0; S[k].bound = 1; }   /* the OS bound it (a reused slot kept the old flag) */
    pthread_mutex_unlock(&s_lock);
    if (k == MAXS) { close(n); w32_set_last_error(c, 10024); RET(INVALID_SOCKET_, 3); }
    fcntl(n, F_SETFL, fcntl(n, F_GETFL) & ~O_NONBLOCK);  /* Windows: the new socket starts blocking */
    to_guest(&a, ARG(1), ARG(2));
    RET(SBASE + (uint32_t)k, 3);
}
IMPL(wsock32, ord19)                                    /* send(s, buf, len, flags) */
{
    SOCK(ARG(0), 4); ssize_t n = send(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL);
    if (n < 0) RET(fail_errno(c), 4);
    netlog("send", ARG(0), GP(ARG(1)), n);
    RET((uint32_t)n, 4);
}
IMPL(wsock32, ord16)                                    /* recv(s, buf, len, flags) */
{
    SOCK(ARG(0), 4); ssize_t n = recv(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 3));
    if (n < 0) RET(fail_errno(c), 4);
    netlog("recv", ARG(0), GP(ARG(1)), n);
    RET((uint32_t)n, 4);
}
IMPL(wsock32, ord20)                                    /* sendto(s, buf, len, flags, to, tolen) */
{
    SOCK(ARG(0), 6); struct sockaddr_in a; ssize_t n;
    if (ARG(4) && fd_is_game_side(ARG(0), fd) && to_host(ARG(4), ARG(5), &a)) note_sent(a.sin_addr.s_addr);
    if (ARG(4) && getenv("W32_NETLOG") && to_host(ARG(4), ARG(5), &a)) { char ip[32]; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip); fprintf(stderr, "net: sendto %x %u bytes to %s:%u\n", ARG(0), ARG(2), ip, ntohs(a.sin_port)); }
    if (ARG(4) && to_host(ARG(4), ARG(5), &a)) {
        if (w32_send_drop && w32_send_drop(a.sin_addr.s_addr, ntohs(a.sin_port))) RET(ARG(2), 6);
        uint32_t d = ntohl(a.sin_addr.s_addr);
        if ((d == SEARCH_ADDR || d == 0xffffffffu) && session_host) {           /* a joined game: its host */
            uint32_t to = ARG(4); uint8_t keep[16]; memcpy(keep, GP(to), 16); memcpy(GP(to + 4), &session_host, 4);
            uint32_t saved_esp = c->esp; imp_wsock32_ord20(c); c->esp = saved_esp; uint32_t r = c->eax;
            memcpy(GP(to), keep, 16); RET(r, 6);
        }
    }
    if (ARG(4) && w32_search_targets && to_host(ARG(4), ARG(5), &a) && a.sin_addr.s_addr == htonl(SEARCH_ADDR)) {
        uint32_t ips[160]; uint16_t ports[160]; int nt = w32_search_targets(ips, ports, 160, ntohs(a.sin_port));
        uint32_t to = ARG(4); uint8_t keep[16]; memcpy(keep, GP(to), 16);
        for (int k = 0; k < nt; k++) {                    /* each through the ordinary path below (the virtual network, NAT) */
            memcpy(GP(to + 2), &(uint16_t){htons(ports[k])}, 2); memcpy(GP(to + 4), &ips[k], 4);
            uint32_t saved_esp = c->esp; imp_wsock32_ord20(c); c->esp = saved_esp;
        }
        memcpy(GP(to), keep, 16);
        RET(ARG(2), 6);
    }
    if (ARG(4) && !w32_netvirt() && to_host(ARG(4), ARG(5), &a)) {
        nat_dest(&a); n = sendto(send_fd(ARG(0), fd), GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
        if (n < 0) RET(fail_errno(c), 6);
        RET((uint32_t)n, 6);
    }
    if (ARG(4) && w32_netvirt()) {
        if (!to_host(ARG(4), ARG(5), &a)) RET(fail(c, 10014), 6);
        uint32_t d = ntohl(a.sin_addr.s_addr); uint16_t vp = ntohs(a.sin_port);
        if (d == 0xffffffffu || (d >> 8) == 0x7f0000u) {
            if (vbind_if_needed(ARG(0), fd)) RET(fail_errno(c), 6);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); n = (ssize_t)ARG(2);
            for (int k = d == 0xffffffffu ? 1 : (int)(d & 255); k <= (d == 0xffffffffu ? 9 : (int)(d & 255)); k++) {
                a.sin_port = htons(vreal(k ? k : virt, vp));
                ssize_t r = sendto(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
                if (r < 0 && d != 0xffffffffu) n = r;
            }
        } else n = sendto(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
    } else if (ARG(4)) { if (!to_host(ARG(4), ARG(5), &a)) RET(fail(c, 10014), 6); n = sendto(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a); }
    else n = send(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 1) | MSG_NOSIGNAL);
    if (n < 0) RET(fail_errno(c), 6);
    RET((uint32_t)n, 6);
}
IMPL(wsock32, ord17)                                    /* recvfrom(s, buf, len, flags, from, fromlen*) */
{
    SOCK(ARG(0), 6); struct sockaddr_in a; socklen_t l = sizeof a; memset(&a, 0, sizeof a);
    ssize_t n = recvfrom(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 3), (struct sockaddr *)&a, &l);
    for (int tries = 0; n >= 0 && fd == game_fd; tries++) {       /* datagrams a joining player didn't ask for: dropped */
        struct sockaddr_in g = a; if (w32_netvirt()) vguest(&g);
        if (allowed(g.sin_addr.s_addr)) break;
        if (getenv("W32_NETLOG")) { char ip[32]; inet_ntop(AF_INET, &g.sin_addr, ip, sizeof ip); fprintf(stderr, "net: dropped %zd bytes from %s (not the host asked)\n", n, ip); }
        if (tries == 64) { errno = EWOULDBLOCK; n = -1; break; }
        l = sizeof a; n = recvfrom(fd, GP(ARG(1)), ARG(2), (int)(ARG(3) & 3) | MSG_DONTWAIT, (struct sockaddr *)&a, &l);
    }
    if (n < 0) RET(fail_errno(c), 6);
    if (n >= 0 && fd == game_fd) n = netfuzz(GP(ARG(1)), n, ARG(2));
    if (n > 0 && fd == game_fd && getenv("W32_NETCAPTURE") && getenv("DS_TEST")) {   /* development: a corpus of what arrives */
        static FILE *cf; static pthread_mutex_t cl = PTHREAD_MUTEX_INITIALIZER; pthread_mutex_lock(&cl);
        if (!cf) cf = fopen(getenv("W32_NETCAPTURE"), "ab");
        if (cf) { uint16_t l16 = (uint16_t)n; fwrite(&l16, 2, 1, cf); fwrite(GP(ARG(1)), 1, (size_t)n, cf); fflush(cf); }
        pthread_mutex_unlock(&cl);
    }
    if (getenv("W32_NETLOG")) { char ip[32]; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip); fprintf(stderr, "net: recvfrom %x %zd bytes from %s:%u\n", ARG(0), n, ip, ntohs(a.sin_port));
        static int dumps; if (getenv("W32_NETDUMP") && dumps++ < 3) { for (ssize_t k = 0; k < n; k++) fprintf(stderr, "%s%02x", k % 32 ? " " : "\n  ", G_MEM[ARG(1) + (uint32_t)k]); fprintf(stderr, "\n"); } }
    if (fd == game_fd && !w32_netvirt()) learn(&a);
    if (w32_recv_filter && n > 0) { struct sockaddr_in from = a; if (w32_netvirt()) vguest(&from); w32_recv_filter(GP(ARG(1)), (uint32_t)n, from.sin_addr.s_addr, ntohs(from.sin_port)); }
    if (w32_netvirt()) vguest(&a);
    if (ARG(4)) to_guest(&a, ARG(4), ARG(5));
    RET((uint32_t)n, 6);
}
IMPL(wsock32, ord22) { SOCK(ARG(0), 2); if (shutdown(fd, (int)ARG(1))) RET(fail_errno(c), 2); RET(0, 2); }  /* shutdown */
IMPL(wsock32, ord6)                                     /* getsockname(s, addr, len*) */
{
    SOCK(ARG(0), 3); struct sockaddr_in a; socklen_t l = sizeof a;
    if (getsockname(fd, (struct sockaddr *)&a, &l)) RET(fail_errno(c), 3);
    if (w32_netvirt()) vguest(&a);
    to_guest(&a, ARG(1), ARG(2)); RET(0, 3);
}
IMPL(wsock32, ord5)                                     /* getpeername(s, addr, len*) */
{
    SOCK(ARG(0), 3); struct sockaddr_in a; socklen_t l = sizeof a;
    if (getpeername(fd, (struct sockaddr *)&a, &l)) RET(fail_errno(c), 3);
    to_guest(&a, ARG(1), ARG(2)); RET(0, 3);
}
/* WinSock option numbers follow Windows/BSD, not Linux. Translate explicitly;
 * linger uses two u16s and timeouts use milliseconds on the guest side. */
static int socket_option(uint32_t level, uint32_t name, int *host_level)
{
    if (level == 6 && name == 1) { *host_level = IPPROTO_TCP; return TCP_NODELAY; }
    if (level != 0xffff) return -1;
    *host_level = SOL_SOCKET;
    switch (name) {
    case 1: return SO_DEBUG; case 2: return SO_ACCEPTCONN;
    case 4: return SO_REUSEADDR; case 8: return SO_KEEPALIVE;
    case 0x10: return SO_DONTROUTE; case 0x20: return SO_BROADCAST;
    case 0x80: case 0xff7f: return SO_LINGER; case 0x100: return SO_OOBINLINE;
    case 0x1001: return SO_SNDBUF; case 0x1002: return SO_RCVBUF;
    case 0x1003: return SO_SNDLOWAT; case 0x1004: return SO_RCVLOWAT;
    case 0x1005: return SO_SNDTIMEO; case 0x1006: return SO_RCVTIMEO;
    case 0x1007: return SO_ERROR; case 0x1008: return SO_TYPE;
    default: return -1;
    }
}
IMPL(wsock32, ord21)                                    /* setsockopt(s, level, name, value, len) */
{
    SOCK(ARG(0), 5); uint32_t lvl = ARG(1), nm = ARG(2), v = ARG(3), len = ARG(4);
    if (!v || len < 4) RET(fail(c, 10014), 5);
    int hl = 0, opt = socket_option(lvl, nm, &hl);
    if (opt < 0) RET(fail(c, 10042), 5);
    int iv = (int)rt_r32(G_MEM, v), rc;
    if (hl == SOL_SOCKET && opt == SO_LINGER) {
        struct linger lg = nm == 0xff7f ? (struct linger){!iv, 0} : (struct linger){rt_r16(G_MEM, v), rt_r16(G_MEM, v + 2)};
        rc = setsockopt(fd, hl, opt, &lg, sizeof lg);
    } else if (hl == SOL_SOCKET && (opt == SO_SNDTIMEO || opt == SO_RCVTIMEO)) {
        if (iv < 0) RET(fail(c, 10022), 5);
        struct timeval tv = {iv / 1000, (iv % 1000) * 1000};
        rc = setsockopt(fd, hl, opt, &tv, sizeof tv);
    } else rc = setsockopt(fd, hl, opt, &iv, sizeof iv);
    if (rc) RET(fail_errno(c), 5);
    RET(0, 5);
}
IMPL(wsock32, ord7)                                     /* getsockopt(s, level, name, value, len*) */
{
    SOCK(ARG(0), 5); uint32_t lvl = ARG(1), nm = ARG(2), v = ARG(3), lp = ARG(4);
    if (!v || !lp || rt_r32(G_MEM, lp) < 4) RET(fail(c, 10014), 5);
    int hl = 0, opt = socket_option(lvl, nm, &hl), iv = 0, rc;
    if (opt < 0) RET(fail(c, 10042), 5);
    socklen_t l;
    if (hl == SOL_SOCKET && opt == SO_LINGER) {
        struct linger lg = {0}; l = sizeof lg;
        rc = getsockopt(fd, hl, opt, &lg, &l);
        iv = nm == 0xff7f ? !lg.l_onoff : (lg.l_onoff & 65535) | ((uint32_t)lg.l_linger << 16);
    } else if (hl == SOL_SOCKET && (opt == SO_SNDTIMEO || opt == SO_RCVTIMEO)) {
        struct timeval tv = {0}; l = sizeof tv;
        rc = getsockopt(fd, hl, opt, &tv, &l);
        iv = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    } else { l = sizeof iv; rc = getsockopt(fd, hl, opt, &iv, &l); }
    if (rc) RET(fail_errno(c), 5);
    if (hl == SOL_SOCKET && opt == SO_ERROR) iv = iv ? (int)wsa_err(iv) : 0;
    rt_w32(G_MEM, v, (uint32_t)iv); rt_w32(G_MEM, lp, 4);
    RET(0, 5);
}
IMPL(wsock32, ord12)                                    /* ioctlsocket(s, cmd, u_long*) */
{
    SOCK(ARG(0), 3); uint32_t cmd = ARG(1), p = ARG(2);
    if (cmd == 0x8004667eu) {                           /* FIONBIO */
        int on = rt_r32(G_MEM, p) != 0, fl = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, on ? fl | O_NONBLOCK : fl & ~O_NONBLOCK); RET(0, 3);
    }
    if (cmd == 0x4004667fu) { int n = 0; if (ioctl(fd, FIONREAD, &n)) RET(fail_errno(c), 3); rt_w32(G_MEM, p, (uint32_t)n); RET(0, 3); }   /* FIONREAD */
    RET(fail(c, 10022), 3);
}
/* select(nfds, read, write, except, timeout): a Windows fd_set is {u32 count; SOCKET array[64]}. One poll entry per
 * socket with the events of every set it is in (macOS's poll misreports a descriptor listed twice).
 * Windows reports a closed or reset connection only as readable (recv then returns 0 or the error), and puts a socket
 * in the except set only for out-of-band data or a failed non-blocking connect. macOS's poll adds POLLPRI to POLLHUP
 * when the other end closes: that is not out-of-band data (from diablo-2-macos#2: Diablo II's TCP/IP thread skips a
 * socket in the except set without reading it, so a closed connection reported there was never closed and the thread
 * spun until the game freed the connections under it at exit). */
IMPL(wsock32, ord18)
{
    uint32_t sets[3] = {ARG(1), ARG(2), ARG(3)}, tv = ARG(4);
    struct pollfd pf[192]; uint32_t who[192]; int n = 0;
    static const short ev[3] = {POLLIN, POLLOUT, POLLPRI};
    for (int k = 0; k < 3; k++) {
        if (!sets[k]) continue;
        uint32_t cnt = rt_r32(G_MEM, sets[k]); if (cnt > 64) cnt = 64;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t s = rt_r32(G_MEM, sets[k] + 4 + 4 * i); int fd = sfd(s);
            if (fd < 0) RET(fail(c, 10038), 5);
            int j = 0; while (j < n && who[j] != s) j++;
            if (j == n) { pf[n].fd = fd; pf[n].events = 0; pf[n].revents = 0; who[n] = s; n++; }
            pf[j].events |= ev[k];
        }
    }
    int ms = tv ? (int)(rt_r32(G_MEM, tv) * 1000 + rt_r32(G_MEM, tv + 4) / 1000) : -1;
    int r = poll(pf, (nfds_t)n, ms);
    if (r < 0) RET(fail_errno(c), 5);
    if (getenv("W32_NETLOG") && atoi(getenv("W32_NETLOG")) > 1) for (int i = 0; i < n; i++) fprintf(stderr, "net: select %x events %x revents %x\n", who[i], pf[i].events, pf[i].revents);
    uint32_t total = 0;
    for (int k = 0; k < 3; k++) {
        if (!sets[k]) continue;
        uint32_t cnt = rt_r32(G_MEM, sets[k]), out = 0; if (cnt > 64) cnt = 64;
        for (uint32_t i = 0; i < cnt; i++) {
            uint32_t s = rt_r32(G_MEM, sets[k] + 4 + 4 * i); int j = 0; while (j < n && who[j] != s) j++;
            short re = j < n ? pf[j].revents : 0;
            int connecting = 0;
            if (k == 2 && (re & POLLERR)) { pthread_mutex_lock(&s_lock); if (s >= SBASE && s < SBASE + MAXS) connecting = S[s - SBASE].connecting; pthread_mutex_unlock(&s_lock); }
            int hit = k == 0 ? (re & (POLLIN | POLLHUP | POLLERR)) != 0 : k == 1 ? (re & POLLOUT) != 0
                    : ((re & POLLPRI) && !(re & POLLHUP)) || ((re & POLLERR) && connecting);
            if (hit) { rt_w32(G_MEM, sets[k] + 4 + 4 * out, s); out++; total++; }
        }
        rt_w32(G_MEM, sets[k], out);
    }
    for (int i = 0; i < n; i++)                                       /* a pending connect that is now writable has finished */
        if ((pf[i].revents & POLLOUT) && !(pf[i].revents & (POLLERR | POLLHUP)) && who[i] >= SBASE && who[i] < SBASE + MAXS) {
            pthread_mutex_lock(&s_lock); S[who[i] - SBASE].connecting = 0; pthread_mutex_unlock(&s_lock);
        }
    RET(total, 5);
}
IMPL(wsock32, ord151)                                   /* __WSAFDIsSet(s, set) */
{
    uint32_t s = ARG(0), set = ARG(1), cnt = rt_r32(G_MEM, set); if (cnt > 64) cnt = 64;
    for (uint32_t i = 0; i < cnt; i++) if (rt_r32(G_MEM, set + 4 + 4 * i) == s) RET(1, 2);
    RET(0, 2);
}
IMPL(wsock32, ord111) { RET(rt_r32(G_MEM, c->fs_base + 0x34), 0); }   /* WSAGetLastError */
IMPL(wsock32, ord8) { RET(__builtin_bswap32(ARG(0)), 1); }             /* htonl */
IMPL(wsock32, ord9) { RET(__builtin_bswap16((uint16_t)ARG(0)), 1); }   /* htons */
IMPL(wsock32, ord15) { RET(__builtin_bswap16((uint16_t)ARG(0)), 1); }  /* ntohs */
IMPL(wsock32, ord11)                                    /* inet_ntoa(in_addr): a per-thread buffer, as on Windows */
{
    static __thread uint32_t buf; if (!buf) buf = heap_alloc(w32_process_heap, 8, 16);
    uint32_t a = ARG(0); snprintf((char *)GP(buf), 16, "%u.%u.%u.%u", a & 255, a >> 8 & 255, a >> 16 & 255, a >> 24);
    RET(buf, 1);
}
