#include "security.h"
#include "steam.h"
#include "utilities.h"
#include <string.h>

extern bool friends_only;
static void *(*client_session)(unsigned);
static void *(*find_member)(void *,uint64_t);
static bool (*original_p2p)(void *,void *,unsigned,unsigned *,uint64_t *,int);
static uint64_t (*original_create)(void *,int,int);
static int (*original_chat)(void *,uint64_t,int,uint64_t *,void *,int,int *);

static bool session_member(uint64_t sender) {
    if (!sender) return false;
    /* Both the private party and game are current BO3 sessions. A Steam lobby
     * alone is insufficient. Native entries hold XUID at +0, activeClient +8. */
    for (unsigned type=0;type<2;++type) {
        void *session=client_session(type);
        uint8_t *member=session ? find_member(session,sender) : NULL;
        void *active=NULL;
        if (member) memcpy(&active,member+8,sizeof(active));
        if (active) return true;
    }
    return false;
}
static bool read_p2p(void *api,void *data,unsigned capacity,unsigned *size,uint64_t *sender,int channel) {
    bool ok=original_p2p(api,data,capacity,size,sender,channel);
    if (!ok) return false;
    if (!data || !size || !sender || *size>capacity) return false;
    if (*size>5) {
        unsigned char type=((unsigned char *)data)[5];
        if (type=='e' || type=='m' || type=='f') return false;
        if (type=='h' && friends_only && !steam_friends_with_user(*sender)) return false;
    }
    return true;
}
static int chat_entry(void *api,uint64_t lobby,int id,uint64_t *sender,void *data,int capacity,int *type) {
    int size=original_chat(api,lobby,id,sender,data,capacity,type);
    if (size<=0) return size;
    if (!sender || !data || !type || size>capacity || !session_member(*sender)) return 0;
    if (*type==1) {
        char *text=data;
        char *end=memchr(text,0,(size_t)size);
        if (!end) return 0;
        for (char *p=text;p<end;++p) {
            unsigned char c=(unsigned char)*p;
            if (c=='^' || c=='%' || c<32 || c>126 ||
                (c=='$' && p[1]=='(') || (c=='[' && p[1]=='{')) *p='.';
        }
    }
    return size;
}

static uint64_t create_lobby(void *api,int type,int maximum) {
    (void)type;
    return original_create(api,1,maximum); /* k_ELobbyTypeFriendsOnly, matching T7Patch */
}

int bo3_install_security_steam(uint64_t base,void *matchmaking,void *networking) {
    /* Client session access and member lookup are audited native helpers. */
    static const unsigned char session_bytes[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xfe,0xe8,0xa6,0xff,0xff,0xff};
    static const unsigned char member_bytes[]={0x55,0x48,0x89,0xe5,0x48,0x81,0xc7,0xf8,0x00,0x00,0x00};
    if (!matchmaking || !networking ||
        memcmp((void *)(uintptr_t)(base+0x1362cdf),session_bytes,sizeof(session_bytes)) ||
        memcmp((void *)(uintptr_t)(base+0x12b33aa),member_bytes,sizeof(member_bytes))) return -1;
    client_session=(void *)(uintptr_t)(base+0x1362cdf);
    find_member=(void *)(uintptr_t)(base+0x12b33aa);
    void **mm=*(void ***)matchmaking, **net=*(void ***)networking;
    if (net[2]!=(void *)read_p2p) {
        original_p2p=net[2];replace_vtable_entry(&net[2],read_p2p);
    }
    if (mm[27]!=(void *)chat_entry) {
        original_chat=mm[27];replace_vtable_entry(&mm[27],chat_entry);
    }
    if (mm[13]!=(void *)create_lobby) {
        original_create=mm[13];replace_vtable_entry(&mm[13],create_lobby);
    }
    return (mm[13]==(void *)create_lobby)+(net[2]==(void *)read_p2p)+(mm[27]==(void *)chat_entry);
}
