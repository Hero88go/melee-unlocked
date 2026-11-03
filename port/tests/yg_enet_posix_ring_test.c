/* Real POSIX helper leases and vendored ENet, entirely offline.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "enet/yg_enet_socket.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
/* Detect accidental native networking. These test-only libc
 * interpositions keep even a dispatch regression entirely offline. */
static unsigned native_calls, expected_native_calls;
int socket(int domain,int type,int protocol) {
    (void)domain; (void)type; (void)protocol; native_calls++; errno=EACCES; return -1;
}
struct hostent *gethostbyname(const char *name) { (void)name; native_calls++; return NULL; }
struct hostent *gethostbyaddr(const void *addr,socklen_t len,int type) {
    (void)addr; (void)len; (void)type; native_calls++; return NULL;
}
typedef struct Lease { int section, g2h[2], h2g[2]; unsigned char *m; uint32_t rx, tx; } Lease;
static uint32_t get32(const void *p) { return __atomic_load_n((const uint32_t *)p,__ATOMIC_ACQUIRE); }
static void put32(void *p,uint32_t n) { __atomic_store_n((uint32_t *)p,n,__ATOMIC_RELEASE); }
static unsigned get16(const unsigned char *p) { return p[0]|((unsigned)p[1]<<8); }
static void put16(unsigned char *p,unsigned n) { p[0]=(unsigned char)n; p[1]=(unsigned char)(n>>8); }
static uint64_t ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static void delay(unsigned n) { struct timespec t={n/1000,(long)(n%1000)*1000000}; while(nanosleep(&t,&t) && errno==EINTR) {} }
static void nonblock(int fd) { CHECK(fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)==0); }
static void lease(Lease *l) {
    char path[]="/tmp/mu-ring-XXXXXX"; unsigned i;
    memset(l,0,sizeof *l); l->section=mkstemp(path); CHECK(l->section>=3); CHECK(unlink(path)==0);
    CHECK(ftruncate(l->section,YG_SECTION_SIZE)==0);
    l->m=mmap(NULL,YG_SECTION_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,l->section,0); CHECK(l->m!=MAP_FAILED);
    memcpy(l->m,"YGDL",4); put32(l->m+4,1); put32(l->m+8,YG_SLOTS); put32(l->m+12,YG_STRIDE);
    put32(l->m+16,YG_DATAGRAM); put32(l->m+20,YG_OUT); put32(l->m+24,YG_IN);
    put16(l->m+32,1); l->m[38]=2;
    l->m[44]=10; l->m[45]=255; l->m[46]=0; l->m[47]=1; put16(l->m+56,7777);
    CHECK(pipe(l->g2h)==0 && pipe(l->h2g)==0);
    for(i=0;i<2;i++) { nonblock(l->g2h[i]); nonblock(l->h2g[i]); }
}
static void dispose(Lease *l) {
    close(l->section); close(l->g2h[0]); close(l->g2h[1]); close(l->h2g[0]); close(l->h2g[1]);
    CHECK(munmap(l->m,YG_SECTION_SIZE)==0);
}
static int attach(Lease *l) { return yg_enet_attach_posix(l->section,l->g2h[1],l->h2g[0],YG_SECTION_SIZE); }
static ENetSocket start(Lease *l) {
    ENetSocket s; CHECK(attach(l)==0); yg_enet_select(1); yg_enet_enable(1);
    s=enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM); CHECK(yg_enet_is_socket(s)); return s;
}
static void refused(Lease *l,int section,int wake,int wait,size_t size) {
    CHECK(yg_enet_attach_posix(section,wake,wait,size)==-1);
    CHECK(!yg_enet_channel_ready());
    CHECK(fcntl(l->section,F_GETFD)>=0 && fcntl(l->g2h[1],F_GETFD)>=0 && fcntl(l->h2g[0],F_GETFD)>=0);
    yg_enet_select(1); yg_enet_enable(1);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM)==ENET_SOCKET_NULL); // no native fallback
}
static void invalid(void) {
    Lease l; unsigned i; const unsigned offsets[]={4,8,12,16,20,24}; struct stat st;
    lease(&l); CHECK(fstat(l.section,&st)==0 && st.st_nlink==0);
    l.m[0]='X'; refused(&l,l.section,l.g2h[1],l.h2g[0],YG_SECTION_SIZE); l.m[0]='Y';
    for(i=0;i<sizeof offsets/sizeof *offsets;i++) {
        unsigned off=offsets[i]; uint32_t old=get32(l.m+off); put32(l.m+off,old+1);
        refused(&l,l.section,l.g2h[1],l.h2g[0],YG_SECTION_SIZE); put32(l.m+off,old);
    }
    refused(&l,l.section,l.g2h[1],l.h2g[0],YG_SECTION_SIZE-1);
    refused(&l,l.section,l.g2h[1],l.h2g[0],YG_SECTION_SIZE+1);
    CHECK(ftruncate(l.section,YG_SECTION_SIZE-1)==0);
    refused(&l,l.section,l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
    CHECK(ftruncate(l.section,YG_SECTION_SIZE)==0);
    refused(&l,l.g2h[0],l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
    refused(&l,l.section,l.section,l.h2g[0],YG_SECTION_SIZE);
    refused(&l,l.section,l.g2h[1],l.section,YG_SECTION_SIZE);
    refused(&l,l.section,l.g2h[0],l.h2g[0],YG_SECTION_SIZE);
    refused(&l,l.section,l.g2h[1],l.h2g[1],YG_SECTION_SIZE);
    refused(&l,l.section,l.g2h[1],l.g2h[1],YG_SECTION_SIZE);
    {
        int regular=dup(l.section), directory=open("/tmp",O_RDONLY);
        CHECK(regular>=3 && directory>=3);
        refused(&l,l.section,regular,l.h2g[0],YG_SECTION_SIZE);
        refused(&l,l.section,l.g2h[1],regular,YG_SECTION_SIZE);
        refused(&l,directory,l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
        close(regular); close(directory);
    }
    for(i=0;i<3;i++) {
        refused(&l,(int)i,l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
        refused(&l,l.section,(int)i,l.h2g[0],YG_SECTION_SIZE);
        refused(&l,l.section,l.g2h[1],(int)i,YG_SECTION_SIZE);
    }
    refused(&l,-1,l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
    refused(&l,INT_MAX,l.g2h[1],l.h2g[0],YG_SECTION_SIZE);
    CHECK(fcntl(l.g2h[1],F_SETFL,fcntl(l.g2h[1],F_GETFL)&~O_NONBLOCK)==0);
    CHECK(fcntl(l.h2g[0],F_SETFL,fcntl(l.h2g[0],F_GETFL)&~O_NONBLOCK)==0);
    CHECK(attach(&l)==0 && attach(&l)==0);
    CHECK((fcntl(l.section,F_GETFD)&FD_CLOEXEC) && (fcntl(l.g2h[1],F_GETFD)&FD_CLOEXEC) && (fcntl(l.h2g[0],F_GETFD)&FD_CLOEXEC));
    CHECK((fcntl(l.section,F_GETFL)&O_NONBLOCK) && (fcntl(l.g2h[1],F_GETFL)&O_NONBLOCK) && (fcntl(l.h2g[0],F_GETFL)&O_NONBLOCK));
    dispose(&l);
}
static void full_and_sigpipe(void) {
    Lease l; ENetSocket s; ENetAddress a; char data[256]={1}; ENetBuffer b={data,1}; ssize_t n; enet_uint32 c;
    CHECK(signal(SIGPIPE,SIG_DFL)!=SIG_ERR);
    lease(&l); s=start(&l); CHECK(yg_enet_link_address(1,&a)==0);
    while((n=write(l.g2h[1],data,1))>0) {} CHECK(n==-1 && errno==EAGAIN);
    CHECK(enet_socket_send(s,&a,&b,1)==1 && yg_enet_channel_ready()); // full wake pipe is fine
    CHECK(get32(l.m+YG_OUT)==1);
    close(l.g2h[0]); l.g2h[0]=-1; // EPIPE, with default disposition this would kill the process
    CHECK(enet_socket_send(s,&a,&b,1)==-1 && !yg_enet_enabled() && !yg_enet_channel_ready());
    c=ENET_SOCKET_WAIT_RECEIVE; CHECK(enet_socket_wait(s,&c,20)==-1);
    CHECK(enet_socket_receive(s,&a,&b,1)==-1);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM)==ENET_SOCKET_NULL);
    enet_socket_destroy(s); dispose(&l);
}
static void eof(void) {
    Lease l; ENetSocket s; enet_uint32 c=ENET_SOCKET_WAIT_RECEIVE; char byte=1;
    lease(&l); s=start(&l);
    CHECK(write(l.h2g[1],&byte,1)==1); // HUP with a final pending byte must still fail
    close(l.h2g[1]); l.h2g[1]=-1;
    CHECK(enet_socket_wait(s,&c,500)==-1 && !yg_enet_channel_ready() && !yg_enet_enabled());
    CHECK(attach(&l)==-1); enet_socket_destroy(s); dispose(&l);
}
static volatile sig_atomic_t signals;
static void signal_seen(int sig) { (void)sig; signals++; }
typedef struct Interrupt { pthread_t target; ENetSocket s; } Interrupt;
static void *interrupt_wait(void *v) {
    Interrupt *i=v; unsigned n; for(n=0;n<4;n++) { delay(10); CHECK(pthread_kill(i->target,SIGUSR1)==0); } return NULL;
}
static void *wake_wait(void *v) {
    Interrupt *i=v; char zero=0; ENetBuffer b={&zero,1}; ENetAddress a={ENET_HOST_TO_NET_32(0x7f000001),7777};
    delay(20); CHECK(enet_socket_send(i->s,&a,&b,1)==1); return NULL;
}
static void waits(void) {
    Lease l; ENetSocket s; enet_uint32 c; pthread_t thread; Interrupt i; uint64_t before; char bytes[100]={1}; struct sigaction sa;
    lease(&l); s=start(&l); CHECK(attach(&l)==0);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_STREAM)==ENET_SOCKET_NULL);
    {
        ENetAddress a; int error=42;
        CHECK(enet_socket_get_address(s,&a)==0 && a.port==7777);
        CHECK(enet_socket_set_option(s,ENET_SOCKOPT_NONBLOCK,1)==0);
        CHECK(enet_socket_get_option(s,ENET_SOCKOPT_ERROR,&error)==0 && error==0);
        CHECK(enet_socket_listen(s,1)==-1 && enet_socket_connect(s,&a)==-1);
        CHECK(enet_socket_accept(s,&a)==ENET_SOCKET_NULL && enet_socket_shutdown(s,ENET_SOCKET_SHUTDOWN_READ_WRITE)==0);
    }
    memset(&sa,0,sizeof sa); sa.sa_handler=signal_seen; sigemptyset(&sa.sa_mask); CHECK(sigaction(SIGUSR1,&sa,NULL)==0);
    i.target=pthread_self(); i.s=s; CHECK(pthread_create(&thread,NULL,interrupt_wait,&i)==0);
    before=ms(); c=ENET_SOCKET_WAIT_RECEIVE|ENET_SOCKET_WAIT_INTERRUPT;
    CHECK(enet_socket_wait(s,&c,80)==0 && c==ENET_SOCKET_WAIT_NONE);
    CHECK(ms()-before>=75 && ms()-before<500 && signals==4); CHECK(pthread_join(thread,NULL)==0);
    // Empty inbound doorbells are coalesced/drained, retaining the original wait deadline.
    CHECK(write(l.h2g[1],bytes,sizeof bytes)==sizeof bytes);
    c=ENET_SOCKET_WAIT_RECEIVE; before=ms(); CHECK(enet_socket_wait(s,&c,30)==0 && !c && ms()-before>=25);
    CHECK(read(l.h2g[0],bytes,sizeof bytes)==-1 && errno==EAGAIN);
    CHECK(pthread_create(&thread,NULL,wake_wait,&i)==0); before=ms(); c=ENET_SOCKET_WAIT_RECEIVE;
    CHECK(enet_socket_wait(s,&c,2000)==0 && !c && ms()-before<500); CHECK(pthread_join(thread,NULL)==0);
    CHECK(get32(l.m+YG_OUT)==0); // self wake never appears in shared outbound ring
    {
        Lease next; lease(&next); CHECK(attach(&next)==-1 && yg_enet_channel_ready());
        enet_socket_destroy(s); CHECK(attach(&next)==0);
        CHECK(fcntl(l.section,F_GETFD)>=0 && fcntl(l.g2h[1],F_GETFD)>=0 && fcntl(l.h2g[0],F_GETFD)>=0);
        dispose(&next);
    }
    CHECK(enet_socket_bind(s,NULL)==-1 && enet_socket_wait(s,&c,0)==-1); // retired ids remain virtual
    {
        ENetSocketSet set; ENET_SOCKETSET_EMPTY(set); ENET_SOCKETSET_ADD(set,s);
        CHECK(!ENET_SOCKETSET_CHECK(set,s) && enet_socketset_select(s,&set,NULL,0)==-1);
    }
    dispose(&l);
}
static void endpoint(Lease *l,int client) {
    ENetAddress bindaddr={ENET_HOST_ANY,7777}, remote; ENetHost *h; ENetPeer *peer=NULL;
    unsigned connected=0, received=0; uint64_t until=ms()+8000;
    close(l->g2h[0]); close(l->h2g[1]); CHECK(attach(l)==0); yg_enet_select(1); yg_enet_enable(1);
    CHECK(enet_initialize()==0); h=enet_host_create(&bindaddr,1,2,0,0); CHECK(h && yg_enet_is_socket(h->socket) && h->mtu==1200);
    CHECK(enet_address_set_host(&remote,"example.invalid")==-1); // selected mode never resolves names
    CHECK(enet_address_set_host(&remote,"10.255.0.1")==0); remote.port=7777;
    if(client) { peer=enet_host_connect(h,&remote,2,0); CHECK(peer); }
    while(ms()<until) {
        ENetEvent e; int n=enet_host_service(h,&e,20); CHECK(n>=0); if(!n) continue;
        if(e.type==ENET_EVENT_TYPE_CONNECT) {
            ENetPacket *p; CHECK(!connected); connected=1; peer=e.peer;
            p=enet_packet_create(client?"client":"helper-peer",client?7:12,ENET_PACKET_FLAG_RELIABLE); CHECK(p);
            CHECK(enet_peer_send(peer,0,p)==0);
        } else if(e.type==ENET_EVENT_TYPE_RECEIVE) {
            CHECK(connected && !received && e.packet->dataLength==(client?12u:7u));
            CHECK(!memcmp(e.packet->data,client?"helper-peer":"client",e.packet->dataLength));
            received=1; enet_packet_destroy(e.packet);
            if(client) enet_peer_disconnect(peer,9);
        } else if(e.type==ENET_EVENT_TYPE_DISCONNECT) {
            CHECK(connected && received && native_calls==0); enet_host_destroy(h); _exit(0);
        }
    }
    CHECK(0 && "ENet handshake/delivery/disconnect timed out");
}
static unsigned relay(Lease *from,Lease *to) {
    unsigned count=0;
    while(from->rx!=get32(from->m+YG_OUT)) {
        unsigned char *src=from->m+YG_OUT+128+(from->rx%YG_SLOTS)*YG_STRIDE;
        unsigned char *dst=to->m+YG_IN+128+(to->tx%YG_SLOTS)*YG_STRIDE; unsigned len=get16(src); char byte=1; ssize_t n;
        CHECK(len>0 && len<=1200 && get16(src+2)==1 && to->tx-get32(to->m+YG_IN+64)<YG_SLOTS);
        memcpy(dst,src,8+len); put32(to->m+YG_IN,++to->tx); put32(from->m+YG_OUT+64,++from->rx);
        n=write(to->h2g[1],&byte,1); CHECK(n==1 || (n==-1 && errno==EAGAIN)); count++;
    }
    return count;
}
static void enet_pair(void) {
    Lease l[2]; pid_t children[2]; unsigned i,done=0,count=0; uint64_t until=ms()+10000;
    lease(&l[0]); lease(&l[1]);
    for(i=0;i<2;i++) {
        children[i]=fork(); CHECK(children[i]>=0);
        if(!children[i]) { dispose(&l[1-i]); endpoint(&l[i],i==0); _exit(1); }
    }
    while(done!=3 && ms()<until) {
        struct pollfd fds[2]={{l[0].g2h[0],POLLIN,0},{l[1].g2h[0],POLLIN,0}}; char bytes[256];
        CHECK(poll(fds,2,10)>=0);
        for(i=0;i<2;i++) {
            while(read(l[i].g2h[0],bytes,sizeof bytes)>0) {}
            count+=relay(&l[i],&l[1-i]);
            if(!(done&(1u<<i))) { int status; pid_t p=waitpid(children[i],&status,WNOHANG); CHECK(p>=0);
                if(p) { CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0); done|=1u<<i; }
            }
        }
    }
    CHECK(done==3 && count>=6); dispose(&l[0]); dispose(&l[1]);
}
static void standalone(void) {
    yg_enet_select(0);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM)==ENET_SOCKET_NULL);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_STREAM)==ENET_SOCKET_NULL);
    CHECK(native_calls==2); expected_native_calls=2; // delegated to our offline native stub
}
static void run(void (*test)(void),const char *name) {
    pid_t p=fork(); int status; CHECK(p>=0);
    if(!p) { alarm(15); test(); CHECK(native_calls==expected_native_calls); _exit(0); }
    CHECK(waitpid(p,&status,0)==p && WIFEXITED(status) && WEXITSTATUS(status)==0);
    printf("yg_enet_posix: %s passed\n",name); fflush(stdout);
}
int main(void) {
    run(invalid,"fd/header/size validation, ownership and idempotence");
    run(full_and_sigpipe,"full doorbell EAGAIN and helper EPIPE/SIGPIPE");
    run(eof,"helper EOF with pending doorbell");
    run(waits,"EINTR deadline, coalescing, local wake and retired ids");
    run(enet_pair,"helper relays real ENet connect/send/receive/disconnect");
    run(standalone,"no-broker native dispatch (offline stubs)");
    return 0;
}
