/* Offline fake ring pair + real vendored ENet, no OS sockets.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "enet/yg_enet_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned char memory[YG_SECTION_SIZE];
static uint32_t helper_rx, helper_tx, test_time;
static unsigned notified, wake_pending, waits, interrupts, native_creates, datagrams, largest, connects, verifies, lose;
static unsigned get16(const void *v) { const unsigned char *p=v; return p[0]|((unsigned)p[1]<<8); }
static uint32_t get32(const void *v) { return __atomic_load_n((const uint32_t *)v,__ATOMIC_ACQUIRE); }
static void put16(void *v,unsigned n) { unsigned char *p=v; p[0]=(unsigned char)n; p[1]=(unsigned char)(n>>8); }
static void put32(void *v,uint32_t n) { __atomic_store_n((uint32_t *)v,n,__ATOMIC_RELEASE); }
static void notify(void *p) { (void)p; notified++; }
static void wake(void *p) { (void)p; wake_pending=1; }
static int wait_fake(void *p,enet_uint32 timeout) {
    (void)p; waits++;
    if(interrupts) { interrupts--; return 3; }
    if(wake_pending) { wake_pending=0; return 2; }
    {
        struct timespec delay={(time_t)(timeout/1000),(long)(timeout%1000)*1000000};
        while(nanosleep(&delay,&delay) && errno==EINTR) {}
        test_time+=timeout;
    }
    return 0;
}
static uint64_t elapsed_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void init(void) {
    YGDoorbells b={NULL,notify,wake,wait_fake};
    memset(memory,0,sizeof memory); memcpy(memory,"YGDL",4); put32(memory+4,1);
    put32(memory+8,YG_SLOTS); put32(memory+12,YG_STRIDE); put32(memory+16,YG_DATAGRAM);
    put32(memory+20,YG_OUT); put32(memory+24,YG_IN);
    put16(memory+32,1); memory[38]=2;
    put16(memory+34,257); memory[39]=2;
    memory[44]=10; memory[45]=255; memory[46]=0; memory[47]=1; put16(memory+56,7777);
    memory[48]=10; memory[49]=255; memory[50]=1; memory[51]=1; put16(memory+58,7777);
    CHECK(yg_enet_attach(memory,sizeof memory,b)==0);
    yg_enet_select(1); yg_enet_enable(1); helper_rx=helper_tx=0; notified=wake_pending=waits=interrupts=0;
}
static void inbound(unsigned link,const void *data,unsigned len) {
    unsigned char *slot=memory+YG_IN+128+(helper_tx%YG_SLOTS)*YG_STRIDE;
    CHECK(helper_tx-get32(memory+YG_IN+64)<YG_SLOTS);
    put16(slot,len); put16(slot+2,link);
    if(len<=YG_DATAGRAM) memcpy(slot+8,data,len);
    put32(memory+YG_IN,++helper_tx);
}
static void basic_io(void) {
    ENetSocket s; ENetAddress a,b,local; unsigned char data[1201],out[4096];
    ENetBuffer buffers[65],receive[2]; enet_uint32 cond; unsigned i;
    init(); s=yg_enet_create(); CHECK(s!=ENET_SOCKET_NULL && yg_enet_is_socket(s));
    CHECK(yg_enet_create()==ENET_SOCKET_NULL); /* single consumer */
    {
        static unsigned char replacement[YG_SECTION_SIZE];
        YGDoorbells replacement_bells={NULL,notify,wake,wait_fake};
        memcpy(replacement,memory,sizeof replacement);
        CHECK(yg_enet_attach(replacement,sizeof replacement,replacement_bells)==-1);
        CHECK(yg_enet_channel_ready()); /* cannot replace an attachment while its socket lives */
    }
    CHECK(yg_enet_link_address(1,&a)==0 && yg_enet_link_address(257,&b)==0);
    CHECK(a.host!=b.host && a.port==7777 && b.port==7777);
    CHECK(yg_enet_link_address(1,&local)==0 && local.host==a.host);
    local.host=0; local.port=4567; CHECK(!yg_enet_bind(s,&local));
    CHECK(!yg_enet_get_address(s,&local) && local.port==4567);
    memset(data,0x5a,sizeof data);
    for(i=0;i<65;i++) { buffers[i].data=data; buffers[i].dataLength=i==64?48:18; }
    CHECK(yg_enet_send(s,&a,buffers,65)==1200 && notified==1);
    CHECK(get16(memory+YG_OUT+128)==1200 && get16(memory+YG_OUT+130)==1);
    for(i=1;i<YG_SLOTS;i++) CHECK(yg_enet_send(s,&a,buffers,65)==1200);
    CHECK(get32(memory+YG_OUT)==YG_SLOTS);
    CHECK(yg_enet_send(s,&a,buffers,65)==1200 && get32(memory+YG_OUT)==YG_SLOTS); /* drop newest */
    buffers[0].dataLength=1201;
    CHECK(yg_enet_send(s,&a,buffers,1)==1201 && get32(memory+YG_OUT)==YG_SLOTS);
    buffers[0].dataLength=10; b.host=ENET_HOST_TO_NET_32(0x0aff00ff);
    CHECK(yg_enet_send(s,&b,buffers,1)==10 && get32(memory+YG_OUT)==YG_SLOTS);
    receive[0].data=out; receive[0].dataLength=3; receive[1].data=out+3; receive[1].dataLength=4093;
    CHECK(yg_enet_receive(s,&b,receive,2)==0);
    inbound(257,data,1200);
    CHECK(yg_enet_receive(s,&b,receive,2)==1200 && b.host!=a.host && b.port==7777 && !memcmp(data,out,1200));
    inbound(999,data,10); inbound(1,data,65535); inbound(1,data,10);
    CHECK(yg_enet_receive(s,&b,receive,2)==10 && b.host==a.host);
    CHECK(get32(memory+YG_IN+64)==4);
    cond=ENET_SOCKET_WAIT_RECEIVE|ENET_SOCKET_WAIT_INTERRUPT; interrupts=2; wake_pending=1;
    CHECK(!yg_enet_wait(s,&cond,250) && cond==ENET_SOCKET_WAIT_NONE && waits==3);
    data[0]=0; buffers[0].dataLength=1; local.host=ENET_HOST_TO_NET_32(0x7f000001);
    CHECK(yg_enet_send(s,&local,buffers,1)==1 && wake_pending);
    cond=ENET_SOCKET_WAIT_RECEIVE; CHECK(!yg_enet_wait(s,&cond,250) && !cond);
    CHECK(get32(memory+YG_OUT)==YG_SLOTS); // wake never joins the out ring
    inbound(1,data,10); cond=ENET_SOCKET_WAIT_RECEIVE;
    CHECK(!yg_enet_wait(s,&cond,250) && cond==ENET_SOCKET_WAIT_RECEIVE);
    CHECK(yg_enet_receive(s,&b,receive,2)==10);
    {
        uint64_t before=elapsed_ms();
        cond=ENET_SOCKET_WAIT_RECEIVE;
        CHECK(!yg_enet_wait(s,&cond,25) && !cond);
        CHECK(elapsed_ms()-before>=24); /* the fake obeys the requested idle timeout */
    }
    put32(memory+YG_IN,helper_tx+YG_SLOTS+1);
    CHECK(yg_enet_receive(s,&b,receive,2)==0 && yg_enet_link_state(1)==3);
    yg_enet_destroy(s);
    CHECK(yg_enet_is_socket(s)); // never route retired virtual ids to OS functions
}

/* Platform stubs let the actual host/peer/protocol code run against the ring. */
int enet_initialize(void) { return 0; }
void enet_deinitialize(void) {}
enet_uint32 enet_time_get(void) { return test_time; }
void enet_time_set(enet_uint32 t) { test_time=t; }
enet_uint32 enet_host_random_seed(void) { return 12345; }
ENetSocket enet_socket_create(ENetSocketType t) { (void)t; if(yg_enet_selected()) return yg_enet_create(); native_creates++; return 42; }
void enet_socket_destroy(ENetSocket s) { if(yg_enet_is_socket(s)) yg_enet_destroy(s); }
int enet_socket_bind(ENetSocket s,const ENetAddress *a) { return yg_enet_is_socket(s)?yg_enet_bind(s,a):0; }
int enet_socket_get_address(ENetSocket s,ENetAddress *a) { return yg_enet_get_address(s,a); }
int enet_socket_set_option(ENetSocket s,ENetSocketOption o,int v) { (void)s;(void)o;(void)v; return 0; }
int enet_socket_send(ENetSocket s,const ENetAddress *a,const ENetBuffer *b,size_t n) { return yg_enet_send(s,a,b,n); }
int enet_socket_receive(ENetSocket s,ENetAddress *a,ENetBuffer *b,size_t n) { return yg_enet_receive(s,a,b,n); }
int enet_socket_wait(ENetSocket s,enet_uint32 *c,enet_uint32 t) { return yg_enet_wait(s,c,t); }
static void pair_step(void) {
    while(helper_rx!=get32(memory+YG_OUT)) {
        unsigned char *slot=memory+YG_OUT+128+(helper_rx%YG_SLOTS)*YG_STRIDE;
        unsigned len=get16(slot),link=get16(slot+2); unsigned char data[YG_DATAGRAM];
        ENetProtocolHeader *h; ENetProtocol *command; unsigned hs,kind;
        CHECK(len<=YG_DATAGRAM); if(len>largest)largest=len; datagrams++;
        memcpy(data,slot+8,len);
        h=(ENetProtocolHeader *)data;
        hs=(ENET_NET_TO_HOST_16(h->peerID)&ENET_PROTOCOL_HEADER_FLAG_SENT_TIME)?sizeof *h:2;
        CHECK(len>=hs+2); command=(ENetProtocol *)(data+hs);
        kind=command->header.command&ENET_PROTOCOL_COMMAND_MASK;
        if(kind==ENET_PROTOCOL_COMMAND_CONNECT) {
            CHECK(ENET_NET_TO_HOST_32(command->connect.mtu)==1200); connects++;
            command->connect.mtu=ENET_HOST_TO_NET_32(4096); // hostile/older remote advertises too much
        }
        if(kind==ENET_PROTOCOL_COMMAND_VERIFY_CONNECT) {
            CHECK(ENET_NET_TO_HOST_32(command->verifyConnect.mtu)==1200); verifies++;
            command->verifyConnect.mtu=ENET_HOST_TO_NET_32(4096);
        }
        if(!lose || datagrams%20) inbound(link,data,len); // 5% deterministic datagram loss
        put32(memory+YG_OUT+64,++helper_rx);
    }
}
static void real_enet(void) {
    ENetHost *host; ENetPeer *p; ENetAddress a; ENetEvent ev; unsigned i,connected=0,received=0;
    unsigned char message[8192],pad[1046];
    init(); CHECK(yg_enet_link_address(1,&a)==0);
    host=enet_host_create(NULL,10,3,0,0); CHECK(host && host->mtu==1200 && native_creates==0);
    for(i=0;i<10;i++) CHECK(host->peers[i].mtu==1200);
    host->peers[0].mtu=1400; // connect must enforce the cap even on a stale peer
    p=enet_host_connect(host,&a,3,0); CHECK(p && p->mtu==1200);
    for(i=0;i<500 && connected<2;i++) {
        test_time+=10; pair_step();
        while(enet_host_service(host,&ev,0)>0) if(ev.type==ENET_EVENT_TYPE_CONNECT) connected++;
    }
    CHECK(connected==2 && connects && verifies);
    for(i=0;i<10;i++) CHECK(host->peers[i].mtu==1200);
    memset(message,0x61,sizeof message); memset(pad,0x42,sizeof pad);
    CHECK(enet_peer_send(p,0,enet_packet_create(message,sizeof message,ENET_PACKET_FLAG_RELIABLE))==0);
    for(i=0;i<30;i++) CHECK(enet_peer_send(p,1,enet_packet_create(pad,sizeof pad,ENET_PACKET_FLAG_UNSEQUENCED))==0);
    for(i=0;i<40;i++) CHECK(enet_peer_send(p,0,enet_packet_create(message,40,ENET_PACKET_FLAG_RELIABLE))==0);
    lose=1;
    for(i=0;i<3000 && received<41;i++) {
        test_time+=10; pair_step();
        while(enet_host_service(host,&ev,0)>0) if(ev.type==ENET_EVENT_TYPE_RECEIVE) {
            if(ev.channelID==0) { CHECK(ev.packet->dataLength==40 || ev.packet->dataLength==8192); CHECK(!memcmp(ev.packet->data,message,ev.packet->dataLength)); received++; }
            enet_packet_destroy(ev.packet);
        }
    }
    CHECK(received==41 && largest<=1200 && largest>1100);
    for(i=0;i<10;i++) { enet_peer_reset(&host->peers[i]); CHECK(host->peers[i].mtu==1200); }
    p=enet_host_connect(host,&a,3,0); CHECK(p && p->mtu==1200);
    // An idle service with only the local wake exits now, not after the 250 ms service timeout.
    wake_pending=1; CHECK(enet_host_service(host,&ev,250)==0 && !wake_pending);
    enet_host_destroy(host);
    yg_enet_enable(0); yg_enet_select(0); host=enet_host_create(NULL,10,3,0,0);
    CHECK(host && host->mtu==1400 && native_creates==1);
    for(i=0;i<10;i++) CHECK(host->peers[i].mtu==1400);
    enet_host_destroy(host);
    init(); put32(memory+28,1); yg_enet_enable(0);
    CHECK(enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM)==ENET_SOCKET_NULL && native_creates==1); // no UDP fallback after helper loss
    yg_enet_enable(0);
}
int main(void) {
    basic_io(); real_enet();
    printf("yg_enet_socket: ring/drop/map/wake/interrupt, real ENet negotiation/reset/reuse and fragmented reliable delivery at 5%% loss passed (%u datagrams, max %u B)\n",datagrams,largest);
    return 0;
}
