#include "security.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../source/security_ui.c"

static unsigned calls,hooks_installed;
static uint8_t wire_tag;
static uint8_t byte_original(void *msg) { assert(msg);return wire_tag; }
bool friends_only;
static bool is_friend;
bool steam_friends_with_user(uint64_t id) { return id==123 && is_friend; }
static void invite_original(unsigned controller,void *message,uint64_t sender) {
    assert(controller==3 && message==(void *)7 && sender==123);++calls;
}
static void info_original(unsigned controller,uint64_t sender,void *message) { invite_original(controller,message,sender); }
static const char *last_input;
static bool text_original(int client,const char *input,char *out) {
    assert(client==3);++calls;strcpy(out,input);return true;
}
static bool model_original(int client,void *element,const char *input,char *out,unsigned capacity) {
    assert(client==3 && element==(void *)7);++calls;snprintf(out,capacity,"%s",input);return true;
}
static uint16_t path_original(uint16_t parent,const char *path,bool create,bool persistent) {
    assert(parent==9 && path && create && !persistent);++calls;return 42;
}
static const char *config_original(int index) { (void)index;++calls;return last_input; }
static int idle_original(void *state) { assert(state==(void *)7);++calls;return 11; }
static const char *type_original(int type) { (void)type;++calls;return "type"; }
static const char *name_original(unsigned instance,unsigned index) {
    assert(instance==0 && index<64);++calls;return "menu";
}
static const char *event_original(unsigned instance,unsigned index) {
    assert(instance==0 && index<256);++calls;return last_input;
}
static void menu_original(void *entity) { assert(entity);++calls; }
static const char *menu_index="1", *response="normal", *sl_index="1";
static bool hosting;
static void argv_original(int index,char *out,int capacity) {
    snprintf(out,(size_t)capacity,"%s",index==1?sl_index:(index==2?menu_index:response));
}
static bool host_original(unsigned lobby) { assert(lobby==1);return hosting; }
static bool binding_original(int client,int *offset,int *length,char *buffer) {
    assert(client==3 && offset && length && buffer);++calls;return true;
}
static int presence_original(bool unpack,void *state,void *data,size_t capacity) {
    (void)unpack;assert(state && data && capacity==128);++calls;return 24;
}
static void read_original(void *msg,void *out,int length) {
    assert(msg && out && length>=0);++calls;
}
static float note_original(const void *animations,int index,int note) {
    assert(animations && !index && note==7);++calls;return 0.5f;
}
static bool binding_text_original(int client,int *offset,int *length,char *buffer,unsigned capacity) {
    assert(capacity==4096);return binding_original(client,offset,length,buffer);
}
static bool subscribe_original(uint64_t id) { assert(id==123);++calls;return true; }
static float width_original(int client,const char *text,int limit,const void *font,int height,float spacing) {
    assert(client==3 && text && limit==12 && font==(void *)7 && height==24 && spacing==0.5f);++calls;return 3.5f;
}
static const char *localize_original(const char *text,const char *context,int error) {
    assert(text && context && error==1);++calls;return text;
}
static bool package_original(void *object,void *message) { assert(object==(void *)7 && message);++calls;return true; }
static void sl_original(void) { ++calls; }
int DobbyCodePatch(void *address,uint8_t *data,uint32_t size) { assert(size==1);memcpy(address,data,size);return 0; }
int DobbyHook(void *address,void *replacement,void **original) {
    (void)address;++hooks_installed;
    if (!original) return 0;
#define MOCK(hook,fn) if (replacement==(void *)hook) { *original=(void *)fn;return 0; }
    MOCK(info_package,package_original) MOCK(state_package,package_original) MOCK(game_package,package_original)
    MOCK(heartbeat_package,package_original) MOCK(join_package,package_original)
    MOCK(sl_command,sl_original)
    MOCK(accept_invite,invite_original) MOCK(join_action,invite_original) MOCK(join_info,info_original)
    MOCK(text_width,width_original) MOCK(localize,localize_original)
    MOCK(binding_text,binding_text_original) MOCK(subscribe_ugc,subscribe_original)
    MOCK(directive,text_original) MOCK(model_string,model_original) MOCK(model_path,path_original)
    MOCK(config,config_original) MOCK(idle,idle_original) MOCK(type_name,type_original)
    MOCK(menu_name,name_original) MOCK(event_name,event_original)
    MOCK(menu,menu_original) MOCK(cached_menu,menu_original) MOCK(binding,binding_original)
    MOCK(presence,presence_original) MOCK(read_data,read_original) MOCK(notetrack,note_original)
#undef MOCK
    abort();
}
static uint8_t dvar_fixture[2][0x80];
static bool registered;
static void *dvar_find(const char *name) {
    if (!registered) return NULL;
    return dvar_fixture[!strcmp(name,"maxvoicepacketsperframe")];
}
static void *dvar_set(const char *name,const char *value,bool create) {
    assert(!create && !strcmp(value,"0"));
    uint8_t *d=dvar_find(name);assert(d);int zero=0;memcpy(d+0x28,&zero,4);return d;
}
int main(int argc,char **argv) {
    assert(argc==1 || argc==2);
    assert(!apply_security_dvars(dvar_find,dvar_set));
    registered=true;memset(dvar_fixture,0xff,sizeof(dvar_fixture));
    int boolean_type=1,integer_type=6;
    memcpy(dvar_fixture[0]+0x1c,&boolean_type,4);memcpy(dvar_fixture[1]+0x1c,&integer_type,4);
    assert(apply_security_dvars(dvar_find,dvar_set)==2);
    for (int i=0;i<2;++i) {
        int flags,value;memcpy(&flags,dvar_fixture[i]+0x18,4);memcpy(&value,dvar_fixture[i]+0x28,4);
        assert(!flags && !value && dvar_fixture[i][0x20]==0xff);
    }
    size_t size=0x125dc600;
    uint8_t *base=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(base!=MAP_FAILED);
    if (argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        assert(fread(base,1,size,f)>0x15318d0);fclose(f);
    } else {
        for (size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i)
            memcpy(base+lengths[i].offset,lengths[i].bytes,5);
        for (size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)
            memcpy(base+hooks[i].offset,hooks[i].bytes,20);
        { const unsigned char bytes[]={0x55,0x48,0x89,0xe5,0x48,0x63,0xc2,0x48,0x8d,0x0d,0xb7,0x66,0x66,0x11,0x48,0x63,0x15,0xb0,0x66,0x66};memcpy(base+0xf75cfb,bytes,sizeof(bytes)); }
        { const unsigned char bytes[]={0x55,0x48,0x89,0xe5,0xe8,0x53,0x6d,0x60,0x00,0x48,0x89,0xc7,0x5d,0xe9,0xf1,0x2c,0x48,0x00,0x55,0x48};memcpy(base+0xe3152b,bytes,sizeof(bytes)); }
        { const unsigned char bytes[]={0x55,0x48,0x89,0xe5,0x48,0x63,0x47,0x24,0x8b,0x4f,0x1c,0x8b,0x57,0x20,0x01,0xca,0x39,0xd0,0x7d,0x0d};memcpy(base+0x1247989,bytes,sizeof(bytes)); }
    }
    assert(bo3_install_security_ui((uintptr_t)base)==41 && hooks_installed==34);
    for (size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i) assert(base[lengths[i].offset+2]==0xb6);
    assert(!text_width(3,NULL,12,(void *)7,24,0.5f));
    assert(text_width(3,"valid",12,(void *)7,24,0.5f)==3.5f);
    assert(!localize(NULL,NULL,1)[0]);
    read_byte=byte_original;
    uint8_t tag_message[0x44]={0};wire_tag=13;assert(read_type(tag_message)==13 && !tag_message[0]);
    wire_tag=14;assert(read_type(tag_message)==14 && !tag_message[0]);
    wire_tag=15;assert(!read_type(tag_message) && tag_message[0]==1);
    tag_message[0x40]=2;
    assert(!info_package((void *)7,tag_message) && !state_package((void *)7,tag_message));
    assert(!game_package((void *)7,tag_message) && !heartbeat_package((void *)7,tag_message) && !join_package((void *)7,tag_message));
    tag_message[0]=0;assert(info_package((void *)7,tag_message));
    tag_message[0]=1;tag_message[0x40]=1;assert(info_package((void *)7,tag_message));
    server_arg=argv_original;is_host=host_original;
    char out[4096],long_text[4097];
    assert(directive(3,"^Bunclosed",out) && out[0]=='.');
    assert(directive(3,NULL,out) && !out[0]);
    memset(long_text,'x',4096);long_text[4096]=0;
    unsigned before=calls;
    assert(!directive(3,long_text,out) && !directive(3,"ok",NULL));assert(calls==before);
    assert(model_string(3,(void *)7,"$(unclosed",out,sizeof(out)) && !strcmp(out,"..unclosed"));
    assert(model_string(3,(void *)7,"$(player.score)",out,sizeof(out)) && !strcmp(out,"$(player.score)"));
    assert(!model_string(3,(void *)7,"ok",out,0));
    assert(model_path(9,"player.score",true,false)==42);
    before=calls;assert(!model_path(9,long_text,true,false) && calls==before);
    last_input="mspreload test";base[0x6939a14]=0;
    assert(!config(3627)[0] && !config(3514)[0]);assert(config(1)==last_input);
    assert(!idle((void *)7));base[0x6939a14]=2;
    assert(config(3627)==last_input && idle((void *)7)==11);
    assert(!strcmp(type_name(32),"type"));before=calls;
    assert(!strcmp(type_name(33),"Invalid") && calls==before);
    assert(!menu_name(0,64)[0] && !menu_name(2,0)[0] && !event_name(0,256)[0]);
    int entity=1,argc4=4;memset(base+0x125dc3c0,0,0x100);
    memcpy(base+0x125dc3c0+0x64,&argc4,4);
    base[0x6939a14]=0;before=calls;menu(&entity);assert(calls==before+1);
    response="restart_level_zm";before=calls;menu(&entity);assert(calls==before);
    response="killserverpc";menu(&entity);assert(calls==before);
    response="badspawn";menu(&entity);assert(calls==before);
    response="normal";menu_index="64";menu(&entity);assert(calls==before);menu_index="1";
    response="256";cached_menu(&entity);assert(calls==before);
    response="1";last_input="endgame";assert(!menu_allowed(&entity,true));
    last_input="normal";assert(menu_allowed(&entity,true));
    entity=0;hosting=true;response="endgame";assert(menu_allowed(&entity,false));
    int offset=0,length=13;char binding_buffer[1024]="[{unclosedxxx";
    length=(int)strlen(binding_buffer);assert(binding(3,&offset,&length,binding_buffer) && binding_buffer[0]=='.');
    length=-1;before=calls;assert(!binding(3,&offset,&length,binding_buffer) && calls==before);
    length=1024;assert(!binding(3,&offset,&length,binding_buffer));
    length=3;offset=4;assert(!binding(3,&offset,&length,"abc"));
    uint8_t state[512]={0},data[128]={0};
    data[1]=18<<2;assert(presence(true,state,data,128)==24);
    data[1]=19<<2;before=calls;assert(!presence(true,state,data,128) && calls==before);
    int count=19;memcpy(state+0x60,&count,4);assert(!presence(false,state,data,128));
    count=18;memcpy(state+0x60,&count,4);assert(presence(false,state,data,128)==24);
    uint8_t message[0x40]={0};before=calls;
    read_data(message,out,-1);assert(message[0]==1 && calls==before);
    read_data(message,out,1);assert(calls==before+1);
    int huge=INT32_MAX;memcpy(message+0x24,&huge,4);before=calls;
    read_data(message,out,1);assert(calls==before);
    uint8_t animations[80]={0};assert(notetrack(animations,0,7)==-1.0f);
    void *parts=(void *)7;memcpy(animations+56,&parts,sizeof(parts));
    assert(notetrack(animations,0,7)==0.5f);
    assert(!global_im(NULL,0,NULL,NULL,0) && !browser("http://example.invalid",0));
    length=3;offset=0;strcpy(out,"abc");assert(binding_text(3,&offset,&length,out,4096));
    assert(!binding_text(3,&offset,&length,out,3));
    char unterminated[1023];memset(unterminated,'x',sizeof(unterminated));
    length=1023;assert(binding(3,&offset,&length,out)==false); /* embedded null with mismatched length */
    char bounded[1024];memset(bounded,'x',sizeof(bounded));
    assert(binding(3,&offset,&length,bounded)); /* scans only the declared length */
    unsetenv("BO3MACFIX_AUTOINSTALLMODS");assert(!subscribe_ugc(123));
    setenv("BO3MACFIX_AUTOINSTALLMODS","true",1);assert(subscribe_ugc(123));
    unsetenv("BO3MACFIX_AUTOINSTALLMODS");
    before=calls;friends_only=true;accept_invite(3,(void *)7,123);join_action(3,(void *)7,123);join_info(3,123,(void *)7);assert(calls==before);
    is_friend=true;accept_invite(3,(void *)7,123);join_action(3,(void *)7,123);join_info(3,123,(void *)7);assert(calls==before+3);
    int argc3=3;memcpy(base+0x125dc3c0+0x64,&argc3,4);before=calls;
    sl_command();assert(calls==before+1);sl_index="1024";sl_command();assert(calls==before+1);
    for (size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i) memcpy(base+lengths[i].offset,lengths[i].bytes,5);
    base[hooks[0].offset]^=1;hooks_installed=0;
    assert(bo3_install_security_ui((uintptr_t)base)==-1 && !hooks_installed);
    munmap(base,size);
    puts("PASS: guarded engine hooks, UI originals/ABIs, menu authorization, presence, signed reads, preload modes");
}
