/* bind through the real WinSock import boundary, no game files: an address that isn't this machine's falls back to
 * every local address (gethostbyname lists the router's public address first when port mapping found one, net_ole.c),
 * and real errors such as EADDRINUSE still fail. Runs on macOS and Linux. */
#include <assert.h>
#include <stdarg.h>
#include "../runtime/win32/winsock.c"
uint8_t memory[8192];
uint8_t *G_MEM = memory;
static uint32_t last_error;
void w32_set_last_error(Ctx *c, uint32_t error) { (void)c; last_error = error; }
static uint32_t call(void (*fn)(Ctx *), int n, ...) {
    Ctx c = {.esp = 256}; va_list ap; va_start(ap,n);
    for(int i=0;i<n;i++) rt_w32(G_MEM,c.esp+4+4*i,va_arg(ap,unsigned));
    va_end(ap); fn(&c); assert(c.esp==256+4+4*n); return c.eax;
}
static void addr(uint32_t at, uint32_t ip_be, uint16_t port_be) { memset(G_MEM+at,0,16); rt_w16(G_MEM,at,2); rt_w16(G_MEM,at+2,port_be); rt_w32(G_MEM,at+4,ip_be); }
static void name(uint32_t s) { rt_w32(G_MEM,620,16); assert(call(imp_wsock32_ord6,3,s,640u,620u)==0); }   /* getsockname -> 640 */
int main(void) {
    /* UDP to the public address: not local, so bound to any (and the port asked for kept) */
    uint32_t s=call(imp_wsock32_ord23,3,2u,2u,0u); assert(s!=INVALID_SOCKET_);
    addr(600,inet_addr("203.0.113.7"),0);
    assert(call(imp_wsock32_ord2,3,s,600u,16u)==0);
    name(s); assert(rt_r32(G_MEM,644)==htonl(INADDR_ANY) && rt_r16(G_MEM,642)!=0);
    uint16_t port=rt_r16(G_MEM,642);
    assert(call(imp_wsock32_ord3,1,s)==0);
    s=call(imp_wsock32_ord23,3,2u,2u,0u); assert(s!=INVALID_SOCKET_);
    addr(600,inet_addr("203.0.113.7"),port);
    assert(call(imp_wsock32_ord2,3,s,600u,16u)==0);
    name(s); assert(rt_r32(G_MEM,644)==htonl(INADDR_ANY) && rt_r16(G_MEM,642)==port);
    assert(call(imp_wsock32_ord3,1,s)==0);
    /* a local address still binds to that address */
    s=call(imp_wsock32_ord23,3,2u,2u,0u); assert(s!=INVALID_SOCKET_);
    addr(600,htonl(INADDR_LOOPBACK),0);
    assert(call(imp_wsock32_ord2,3,s,600u,16u)==0);
    name(s); assert(rt_r32(G_MEM,644)==htonl(INADDR_LOOPBACK));
    assert(call(imp_wsock32_ord3,1,s)==0);
    /* a taken port is EADDRINUSE (10048), not a fallback */
    uint32_t t1=call(imp_wsock32_ord23,3,2u,1u,0u), t2=call(imp_wsock32_ord23,3,2u,1u,0u);
    assert(t1!=INVALID_SOCKET_ && t2!=INVALID_SOCKET_);
    addr(600,htonl(INADDR_LOOPBACK),0);
    assert(call(imp_wsock32_ord2,3,t1,600u,16u)==0 && call(imp_wsock32_ord13,2,t1,1u)==0);
    name(t1); addr(600,htonl(INADDR_LOOPBACK),rt_r16(G_MEM,642));
    last_error=0; assert(call(imp_wsock32_ord2,3,t2,600u,16u)==SOCKET_ERROR_ && last_error==10048);
    assert(call(imp_wsock32_ord3,1,t1)==0 && call(imp_wsock32_ord3,1,t2)==0);
    puts("WinSock bind: non-local address falls back to any, local and in-use binds unchanged");
}
