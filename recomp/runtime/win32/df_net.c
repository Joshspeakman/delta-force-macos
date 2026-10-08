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
 * The address arguments are pointers to a string pointer (Netsock's InitTCPIP reads **). */
#include "w32.h"
#include "edition.h"

#ifdef DF_EDITION
static uint32_t guest_address(const char *ip)    /* guest: a pointer to a string pointer, one per address */
{
    static uint32_t bc, any; uint32_t *pp = ip[0] == '2' ? &bc : &any;
    if (!*pp) { *pp = heap_alloc(w32_process_heap, 8, 32); rt_w32(G_MEM, *pp, *pp + 8); strcpy((char *)GP(*pp + 8), ip); }
    return *pp;
}
#define broadcast_address() guest_address("255.255.255.255")
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
    switch (addr) {
#ifdef DF2_EDITION
    case 0x0045c38du: if (c->eax == 0) { c->eax = 1; host_address(c->esp + 0x14); lan_note("host"); } return 1;
    case 0x0040861du: if (c->eax == 0) { c->eax = 1; rt_w32(G_MEM, c->esp + 0x14, broadcast_address()); lan_note("join (broadcast search)"); } return 1;
#else
    case 0x0044691fu: if (c->eax == 0) { c->eax = 1; host_address(c->esp + 0x18); lan_note("host"); } return 1;
    case 0x00470c13u: if (c->eax == 0) { c->eax = 1; c->edi = broadcast_address(); lan_note("join (broadcast search)"); } return 1;
#endif
    }
    return 0;
}
#endif
