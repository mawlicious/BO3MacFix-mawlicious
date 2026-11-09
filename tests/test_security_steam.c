#include "security.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../source/security_steam.c"

bool friends_only;
static bool friend,receive_ok=true,member_active;
static uint8_t fake_member[48];
static unsigned receive_size=6;
static uint64_t receive_sender=123;
static const char *chat_text="Hello ^1 $(bad) [{bad}] %s";
static int chat_size;
static unsigned p2p_calls,chat_calls;
bool steam_friends_with_user(uint64_t id) { return id==receive_sender && friend; }
void replace_vtable_entry(void *entry,void *replacement) { memcpy(entry,&replacement,sizeof(replacement)); }
static void *get_session(unsigned type) { assert(type<2);return (void *)(uintptr_t)(type+1); }
static void *get_member(void *session,uint64_t sender) {
    assert(session);void *active=member_active?(void *)7:NULL;
    memcpy(fake_member+8,&active,sizeof(active));
    return sender==receive_sender?fake_member:NULL;
}
static bool net_original(void *api,void *data,unsigned capacity,unsigned *size,uint64_t *sender,int channel) {
    assert(api && data && capacity==64 && size && sender && channel==2);++p2p_calls;
    *size=receive_size;*sender=receive_sender;return receive_ok;
}
static int chat_original(void *api,uint64_t lobby,int id,uint64_t *sender,void *data,int capacity,int *type) {
    assert(api && lobby==99 && id==4 && sender && data && capacity==64 && type);++chat_calls;
    *sender=receive_sender;*type=1;strcpy(data,chat_text);
    return chat_size?chat_size:(int)strlen(chat_text)+1;
}
static uint64_t create_original(void *api,int type,int maximum) { assert(api && type==1 && maximum==4);return 1234; }
int main(int argc,char **argv) {
    assert(argc==1 || argc==2);
    size_t bytes=0x1362d21;
    uint8_t *base=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(base!=MAP_FAILED);
    if (argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(base,1,bytes,f)==bytes);fclose(f);
    } else {
        const unsigned char session[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xfe,0xe8,0xa6,0xff,0xff,0xff};
        const unsigned char member[]={0x55,0x48,0x89,0xe5,0x48,0x81,0xc7,0xf8,0,0,0};
        memcpy(base+0x1362cdf,session,sizeof(session));memcpy(base+0x12b33aa,member,sizeof(member));
    }
    void *net[3]={0},*mm[28]={0};net[2]=(void *)net_original;mm[27]=(void *)chat_original;mm[13]=(void *)create_original;
    void **net_object=net,**mm_object=mm;
    assert(bo3_install_security_steam((uintptr_t)base,&mm_object,&net_object)==3);
    assert(bo3_install_security_steam((uintptr_t)base,&mm_object,&net_object)==3); /* don't hook ourselves */
    assert(create_lobby(&mm_object,2,4)==1234);
    client_session=get_session;find_member=get_member;
    char data[64]={0};unsigned size;uint64_t sender;data[5]='h';
    assert(read_p2p(&net_object,data,64,&size,&sender,2) && p2p_calls==1);
    friends_only=true;assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    friend=true;assert(read_p2p(&net_object,data,64,&size,&sender,2));
    data[5]='e';assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    data[5]='m';assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    data[5]='f';assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    data[5]='x';assert(read_p2p(&net_object,data,64,&size,&sender,2));
    receive_size=65;assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    receive_size=6;receive_ok=false;assert(!read_p2p(&net_object,data,64,&size,&sender,2));
    int type;
    assert(!chat_entry(&mm_object,99,4,&sender,data,64,&type) && chat_calls==1);
    member_active=true;assert(chat_entry(&mm_object,99,4,&sender,data,64,&type)>0);
    assert(!strcmp(data,"Hello .1 .(bad) .{bad}] .s"));
    chat_size=65;assert(!chat_entry(&mm_object,99,4,&sender,data,64,&type));
    chat_size=2;assert(!chat_entry(&mm_object,99,4,&sender,data,64,&type)); /* no terminator within result */
    base[0x1362cdf]^=1;assert(bo3_install_security_steam((uintptr_t)base,&mm_object,&net_object)==-1);
    munmap(base,bytes);
    puts("PASS: Steam P2P filter, friends-only, active BO3 membership, bounded chat sanitization, idempotent hooks");
}
