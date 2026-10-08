/* Delta Force 1 and 2: multiplayer additions.
 *
 * LAN. Both games play LAN games over IPX through DirectPlay, and internet and NovaWorld games over TCP/IP through
 * their own Netsock.dll (UDP, port 3568). There is no IPX here, so a LAN game is a TCP/IP game: hosting with
 * "LAN (IPX)" hosts as the Internet choice does, and searching sends the Internet choice's session search to the
 * broadcast address, which every host on the local network answers. Each game picks its network layer in one
 * connection-type switch for hosting and one for joining (0 LAN, 1 Internet, 2 modem, 3 serial, 4 NovaWorld); the
 * hooks sit on the switch's jump, where the type is in eax.
 *   DF1 (Watcom): host 0x446910(type, ?, ?, address**), the address at esp+0x18 at the jump; join
 *                 0x470c00(type, phone**, ?, address**), the address already in edi.
 *   DF2 (MSVC):   host 0x45c370(type, ?, ?, address**, port) and join 0x408600(type, phone**, ?, address**, ...),
 *                 the address at esp+0x14 at the jump.
 * The address arguments are pointers to a string pointer (Netsock's InitTCPIP reads **).
 *
 * NovaWorld, a game list without a server (p2p_dht.c). A game hosted with "NovaWorld" is announced on BitTorrent's
 * public DHT under a key of the game's own, and its UDP port is forwarded on the router (portmap.c) while it is
 * hosted. A NovaWorld search goes to 255.255.255.254, which the socket layer (winsock.c) sends to every host the DHT
 * has named and to the local network; the game's own session search then lists them. NovaLogic's login and its web
 * pages are not involved. DF_NO_P2P=1 leaves the DHT alone (local network only). */
#include "w32.h"
#include "edition.h"
#include "p2p_dht.h"
#include <arpa/inet.h>

#ifdef DF_EDITION
#ifdef DF2_EDITION
#define P2P_NAME "Delta Force 2 / NovaWorld stand-in / 1"
#else
#define P2P_NAME "Delta Force / NovaWorld stand-in / 1"
#endif
void w32_set_game_port(uint16_t port); void w32_set_session_host(uint32_t ip);
extern int (*w32_send_drop)(uint32_t ip, uint16_t port);
extern void (*w32_recv_filter)(uint8_t *buf, uint32_t n, uint32_t ip, uint16_t port);
extern void (*w32_game_port_closed)(uint16_t port);
extern int (*w32_search_targets)(uint32_t *ip, uint16_t *port, int cap, uint16_t game_port);
void portmap_open(uint16_t game_port, uint16_t enum_port); void portmap_close(void);
static pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;
static P2pDht *dht; static uint16_t hosting_port; static int searching;
static P2pDht *p2p(void)
{
    pthread_mutex_lock(&p2p_lock);
    if (!dht && !getenv("DF_NO_P2P")) { uint8_t key[20]; p2p_key(P2P_NAME, key); dht = p2p_dht_start(key, 0); }
    P2pDht *d = dht; pthread_mutex_unlock(&p2p_lock);
    return d;
}
static void hosting_ended(uint16_t port)
{
    pthread_mutex_lock(&p2p_lock); int was = hosting_port == port && port; if (was) hosting_port = 0; pthread_mutex_unlock(&p2p_lock);
    if (!was) return;
    if (dht) p2p_dht_announce(dht, 0);
    portmap_close();
    fprintf(stderr, "df: NovaWorld: no longer listed\n");
}
static void host_public(uint16_t port)               /* hosting with NovaWorld: listed and reachable */
{
    pthread_mutex_lock(&p2p_lock); hosting_port = port; pthread_mutex_unlock(&p2p_lock);
    portmap_open(port, 0);
    P2pDht *d = p2p(); if (d) p2p_dht_announce(d, port);
    fprintf(stderr, "df: NovaWorld: hosting on UDP %u, listed on the DHT%s\n", port, d ? "" : " (off: DF_NO_P2P)");
}
static int search_targets(uint32_t *ip, uint16_t *port, int cap, uint16_t game_port)
{
    int n = 0;
    ip[n] = 0xffffffffu; port[n++] = game_port;          /* the local network */
    P2pDht *d = p2p(); if (!d) return n;
    if (!searching) { searching = 1; p2p_dht_search(d, 20); fprintf(stderr, "df: NovaWorld: looking for games on the DHT\n"); }
    n += p2p_dht_peers(d, ip + n, port + n, cap - n);
    static int last = -1; if (n - 1 != last) { last = n - 1; fprintf(stderr, "df: NovaWorld: %d host%s known\n", last, last == 1 ? "" : "s"); }
    return n;
}
/* NovaLogic's servers: the games report a hosted game to them (DF1 207.178.209.201 UDP 3875, DF2 208.231.90.230 UDP
 * 2827); the addresses are no longer NovaLogic's, so nothing is sent there */
static int send_drop(uint32_t ip, uint16_t port)
{
    uint32_t h = ntohl(ip);
    if ((h == 0xcfb2d1c9u && port == 3875) || (h == 0xd0e75ae6u && port == 2827)) {
        static int said; if (!said++) fprintf(stderr, "df: NovaWorld: the game's report to NovaLogic's old server is not sent\n");
        return 1;
    }
    return 0;
}
static void received(uint8_t *b, uint32_t n, uint32_t ip, uint16_t port);
static void net_init(void)
{
    static int done; if (done++) return;
    w32_game_port_closed = hosting_ended; w32_search_targets = search_targets; w32_send_drop = send_drop; w32_recv_filter = received;
}
/* Which host was chosen. Netsock's session descriptor (0x100 bytes: the game's id at +0xe0) has a 16-byte instance id
 * at +0xd0, which the games leave zero; the game lists one session per instance id, so with several hosts answering a
 * search only one would be listed, and Netsock joins the address it searched, so the join (and the game after it)
 * would go to every host. A host's session gets an instance id of its own when it is created (the call of
 * netsock_HostSession, DF1 0x4b92c4, DF2 0x480723: "DFNW" and 12 random bytes); Netsock carries it in its search
 * answers (an 8-byte header, then the descriptor, the id at +0xd8), so each host is listed. Each answer's sender is
 * remembered under its id, and at the call of netsock_JoinSession (desc+0xd0: the chosen session's id) that sender
 * becomes the host the game's traffic goes to (winsock.c). */
static struct { uint8_t id[16]; uint32_t ip; } senders[64]; static int nsenders;
static pthread_mutex_t snd_lock = PTHREAD_MUTEX_INITIALIZER;
static void received(uint8_t *b, uint32_t n, uint32_t ip, uint16_t port)
{
    (void)port;
    if (n != 8 + 0x100 || memcmp(b + 8 + 0xd0, "DFNW", 4)) return;   /* a search answer from one of these hosts */
    pthread_mutex_lock(&snd_lock);
    int k = 0; while (k < nsenders && memcmp(senders[k].id, b + 8 + 0xd0, 16)) k++;
    if (k == nsenders) { if (nsenders == 64) { memmove(senders, senders + 1, sizeof senders[0] * 63); k = 63; } else nsenders++; }
    memcpy(senders[k].id, b + 8 + 0xd0, 16); senders[k].ip = ip;
    pthread_mutex_unlock(&snd_lock);
}
static void hosting_session(uint32_t desc)
{
    uint8_t *f = GP(desc + 0xd0);
    for (int k = 0; k < 16; k++) if (f[k]) return;
    static uint8_t id[16]; if (!id[0]) { memcpy(id, "DFNW", 4); arc4random_buf(id + 4, 12); }   /* one per run: the */
    memcpy(f, id, 16);                                   /* session is created again when a mission starts */
}
static void joining(uint32_t desc)                    /* every frame while joining */
{
    const uint8_t *f = GP(desc + 0xd0); uint32_t ip = 0;
    pthread_mutex_lock(&snd_lock);
    for (int k = 0; k < nsenders; k++) if (!memcmp(senders[k].id, f, 16)) ip = senders[k].ip;
    pthread_mutex_unlock(&snd_lock);
    if (!ip) return;                                     /* a session from elsewhere: Netsock's own way */
    w32_set_session_host(ip);
    static uint32_t last; if (ip != last) { last = ip; char t[32]; inet_ntop(AF_INET, &ip, t, sizeof t); fprintf(stderr, "df: joining the game hosted at %s\n", t); }
}
static uint32_t guest_address(const char *ip)    /* guest: a pointer to a string pointer, one per address */
{
    static uint32_t bc, any, search; uint32_t *pp = !strcmp(ip, "255.255.255.255") ? &bc : !strcmp(ip, "0.0.0.0") ? &any : &search;
    if (!*pp) { *pp = heap_alloc(w32_process_heap, 8, 32); rt_w32(G_MEM, *pp, *pp + 8); strcpy((char *)GP(*pp + 8), ip); }
    return *pp;
}
#define broadcast_address() guest_address("255.255.255.255")
#define search_address() guest_address("255.255.255.254")     /* winsock.c: the DHT's hosts and the local network */
static void host_address(uint32_t slot)           /* the LAN caller passes none; NovaWorld hosting passes 0.0.0.0 */
{
    if (!rt_r32(G_MEM, slot)) rt_w32(G_MEM, slot, guest_address("0.0.0.0"));
}
static void lan_note(const char *what)
{
    static int said[2]; int k = what[0] == 'j';
    if (!said[k]++) fprintf(stderr, "df: LAN game: %s over TCP/IP\n", what);
}
int df_hook(Ctx *c, uint32_t addr)
{
    net_init();
    switch (addr) {
#ifdef DF2_EDITION
    case 0x0045c38du:                                    /* host; esi: the game port */
        w32_set_game_port((uint16_t)c->esi);
        if (c->eax == 0) { c->eax = 1; host_address(c->esp + 0x14); lan_note("host"); }
        else if (c->eax == 4) host_public((uint16_t)c->esi);
        return 1;
    case 0x0040861du:                                    /* join; esi: the game port */
        w32_set_game_port((uint16_t)c->esi); w32_set_session_host(0);
        if (c->eax == 0) { c->eax = 1; rt_w32(G_MEM, c->esp + 0x14, broadcast_address()); lan_note("join (broadcast search)"); }
        else if (c->eax == 4) { c->eax = 1; rt_w32(G_MEM, c->esp + 0x14, search_address()); }
        return 1;
    case 0x004805b1u: joining(rt_r32(G_MEM, c->esp)); return 1;
    case 0x00480723u: hosting_session(rt_r32(G_MEM, c->esp)); return 1;
#else
    case 0x004b9156u: joining(rt_r32(G_MEM, c->esp)); return 1;
    case 0x004b92c4u: hosting_session(rt_r32(G_MEM, c->esp)); return 1;
    case 0x0044691fu:                                    /* host (Netsock on 3568) */
        w32_set_game_port(3568);
        if (c->eax == 0) { c->eax = 1; host_address(c->esp + 0x18); lan_note("host"); }
        else if (c->eax == 4) host_public(3568);
        return 1;
    case 0x00470c13u:                                    /* join */
        w32_set_game_port(3568); w32_set_session_host(0);
        if (c->eax == 0) { c->eax = 1; c->edi = broadcast_address(); lan_note("join (broadcast search)"); }
        else if (c->eax == 4) { c->eax = 1; c->edi = search_address(); }
        return 1;
#endif
    }
    return 0;
}
#endif
