#include "security.h"
#include "dobby.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static uint64_t image;
static bool (*original_directive)(int,const char *,char *);
static bool (*original_model_string)(int,void *,const char *,char *,unsigned);
static uint16_t (*original_path)(uint16_t,const char *,bool,bool);
static const char *(*original_config)(int);

static bool directive(int client,const char *text,char *out) {
    char safe[4096];
    if (!out || !bo3_security_ui_text(text,safe)) return false;
    return original_directive(client,safe,out);
}
static uint16_t model_path(uint16_t parent,const char *path,bool create,bool persistent) {
    if (!bo3_security_model_path(path)) return 0;
    return original_path(parent,path,create,persistent);
}
static bool model_string(int client,void *element,const char *text,char *out,unsigned capacity) {
    char safe[4096];
    if (!out || !capacity || !bo3_security_ui_text(text,safe)) return false;
    /* The native routine copies a model reference into a fixed 4096-byte array.
     * Keep valid substitutions, render unterminated references as literal text. */
    for (char *p=safe; (p=strstr(p,"$(")); p+=2) {
        if (!strchr(p+2,')')) p[0]=p[1]='.';
    }
    return original_model_string(client,element,safe,out,capacity);
}
static const char *config(int index) {
    const char *text=original_config(index);
    uint32_t mode;memcpy(&mode,(void *)(uintptr_t)(image+0x6939a14),4);
    if ((mode&15)!=2 && (index==3514 || index==3627) && text &&
        !strncmp(text,"mspreload",9)) return "";
    return text;
}

static int (*original_idle)(void *);
static const char *(*original_type_name)(int);
static int idle(void *state) {
    uint32_t mode;memcpy(&mode,(void *)(uintptr_t)(image+0x6939a14),4);
    return (mode&15)==2 ? original_idle(state) : 0;
}
static const char *type_name(int type) {
    if (type < -1 || type > 32) return "Invalid";
    return original_type_name(type);
}
static int debug_message(void *msg,bool force) { (void)msg;(void)force;return 0; }
static int browser(const char *url,int flags) { (void)url;(void)flags;return 0; }

static const char *(*original_menu_name)(unsigned,unsigned);
static const char *(*original_event_name)(unsigned,unsigned);
static void (*original_menu)(void *), (*original_cached_menu)(void *);
static void (*server_arg)(int,char *,int);
static bool (*is_host)(unsigned);
static bool (*original_binding)(int,int *,int *,char *);
static int (*original_presence)(bool,void *,void *,size_t);

static const char *menu_name(unsigned instance,unsigned index) {
    return instance<2 && index<64 ? original_menu_name(instance,index) : "";
}
static const char *event_name(unsigned instance,unsigned index) {
    return instance<2 && index<256 ? original_event_name(instance,index) : "";
}
static bool menu_allowed(void *entity,bool cached) {
    if (!entity) return false;
    const unsigned char *args=(void *)(uintptr_t)(image+0x125dc3c0);
    int nesting,argc;memcpy(&nesting,args,4);
    if (nesting<0 || nesting>=8) return false;
    memcpy(&argc,args+0x64+4*nesting,4);
    if (argc!=4) return false;
    char text[1024]={0};
    server_arg(2,text,sizeof(text));
    int index=atoi(text);
    if ((unsigned)index>=64) return false;
    server_arg(3,text,sizeof(text));
    const char *response=text;
    if (cached) {
        index=atoi(text);
        if ((unsigned)index>=256) return false;
        if (index) response=event_name(0,(unsigned)index);
    }
    if (!strcasecmp(response,"badspawn")) return false;
    bool kill=!strcasecmp(response,"killserverpc");
    bool restart=!strcasecmp(response,"restart_level_zm");
    bool end=!strcasecmp(response,"endgame") || !strcasecmp(response,"endround");
    uint32_t mode;memcpy(&mode,(void *)(uintptr_t)(image+0x6939a14),4);
    int entity_num;memcpy(&entity_num,entity,4);
    if (entity_num && (kill || restart || (end && (mode&15)!=2))) return false;
    return !(kill || end) || ((mode&15)==2 && end) || is_host(1);
}
static void menu(void *entity) { if (menu_allowed(entity,false)) original_menu(entity); }
static void cached_menu(void *entity) { if (menu_allowed(entity,true)) original_cached_menu(entity); }

/* This second binding parser is reached independently of SEH_ReplaceDirective.
 * Its initial memcpy and token copy use fixed 1024-byte local arrays. */
static bool binding(int client,int *offset,int *length,char *buffer) {
    if (!offset || !length || !buffer || *length<0 || *length>=1024 ||
        *offset<0 || *offset>*length) return false;
    char safe[4096];
    memcpy(safe,buffer,(size_t)*length);safe[*length]=0;
    if (memchr(safe,0,(size_t)*length) || !bo3_security_ui_text(safe,safe)) return false;
    memcpy(buffer,safe,(size_t)*length+1);
    return original_binding(client,offset,length,buffer);
}

/* Mac presence packing uses an explicit unpack flag and four arguments. Its
 * wire control byte contains the same five-bit player count as T7Patch. */
static int presence(bool unpack,void *state,void *data,size_t capacity) {
    if (!state || !data || capacity<2) return 0;
    if (unpack) {
        if ((((const uint8_t *)data)[1]>>2 & 31)>18) return 0;
    } else {
        int count,stored;
        memcpy(&count,(uint8_t *)state+0x60,4);
        memcpy(&stored,(uint8_t *)state+0x64,4);
        if (count<0 || count>18 || stored<0 || stored>18) return 0;
    }
    return original_presence(unpack,state,data,capacity);
}

/* Demonware's global IM callback is a separate source from Steam P2P. T7
 * disables this source entirely; Steam joins keep using the checked dispatcher. */
static bool global_im(void *context,uint64_t sender,const char *name,const void *data,unsigned length) {
    (void)context;(void)sender;(void)name;(void)data;(void)length;
    return false;
}
static void (*original_read_data)(void *,void *,int);
static void read_data(void *message,void *output,int length) {
    if (!message) return;
    int read,first,second;
    memcpy(&read,(uint8_t *)message+0x24,4);
    memcpy(&first,(uint8_t *)message+0x1c,4);
    memcpy(&second,(uint8_t *)message+0x20,4);
    if (length<0 || (length && !output) || read<0 || first<0 || second<0 ||
        (int64_t)read+length>INT32_MAX || (int64_t)first+second>INT32_MAX) {
        int overflow=1;memcpy(message,&overflow,4);
        return;
    }
    if (length) original_read_data(message,output,length);
}

/* T7's unnamed character-index hook guards this animation notetrack lookup:
 * entries have a 24-byte stride, with an optional parts pointer at +56. */
static float (*original_notetrack)(const void *,int,int);
static float notetrack(const void *animations,int index,int note) {
    if (!animations || index<0) return -1.0f;
    const void *parts;
    memcpy(&parts,(const uint8_t *)animations+56+(size_t)index*24,sizeof(parts));
    return parts ? original_notetrack(animations,index,note) : -1.0f;
}

static bool (*original_binding_text)(int,int *,int *,char *,unsigned);
static bool binding_text(int client,int *offset,int *length,char *buffer,unsigned capacity) {
    if (!offset || !length || !buffer || *length<0 || *length>=4096 ||
        (unsigned)*length>=capacity || *offset<0 || *offset>*length) return false;
    char safe[4096];memcpy(safe,buffer,(size_t)*length);safe[*length]=0;
    if (memchr(safe,0,(size_t)*length) || !bo3_security_ui_text(safe,safe)) return false;
    memcpy(buffer,safe,(size_t)*length+1);
    return original_binding_text(client,offset,length,buffer,capacity);
}
static void exec_lua(void) { }
/* Match T7Patch's default: subscribe in Steam explicitly rather than letting
 * a remote lobby trigger an automatic Workshop subscription. */
static bool (*original_subscribe)(uint64_t);
static bool subscribe_ugc(uint64_t id) {
    const char *allow=getenv("BO3MACFIX_AUTOINSTALLMODS");
    return allow && (!strcmp(allow,"1") || !strcasecmp(allow,"true")) ? original_subscribe(id) : false;
}

static float (*original_text_width)(int,const char *,int,const void *,int,float);
static float text_width(int client,const char *text,int limit,const void *font,int height,float spacing) {
    return text ? original_text_width(client,text,limit,font,height,spacing) : 0.0f;
}
static const char *(*original_localize)(const char *,const char *,int);
static const char *localize(const char *text,const char *context,int error) {
    return original_localize(text?text:"",context?context:"",error);
}

extern bool friends_only;
extern bool steam_friends_with_user(uint64_t id);
static void (*original_accept)(unsigned,void *,uint64_t);
static void (*original_join_action)(unsigned,void *,uint64_t);
static void (*original_join_info)(unsigned,uint64_t,void *);
static bool invite_allowed(uint64_t sender) {
    return !friends_only || steam_friends_with_user(sender);
}
static void accept_invite(unsigned controller,void *message,uint64_t sender) {
    if (invite_allowed(sender)) original_accept(controller,message,sender);
}
static void join_action(unsigned controller,void *message,uint64_t sender) {
    if (invite_allowed(sender)) original_join_action(controller,message,sender);
}
static void join_info(unsigned controller,uint64_t sender,void *message) {
    if (invite_allowed(sender)) original_join_info(controller,sender,message);
}

static void (*original_sl)(void);
static void sl_command(void) {
    const unsigned char *args=(void *)(uintptr_t)(image+0x125dc3c0);
    int nesting,argc;memcpy(&nesting,args,4);
    if (nesting<0 || nesting>=8) return;
    memcpy(&argc,args+0x64+4*nesting,4);
    if (argc!=3) return;
    char text[256]={0};server_arg(1,text,sizeof(text));
    if ((unsigned)atoi(text)>=1024) return;
    original_sl();
}

static void steam_server_init(void) { }
/* The Mac frame loop otherwise retries the disabled initialization each frame.
 * Disable the embedded server pump as well; client Steam callbacks are separate. */
static void steam_server_frame(void *server) { (void)server; }
static uint8_t (*read_byte)(void *);
/* Unlike Windows, the Mac helper also reads normal field tags. Preserve those
 * tags; reject the optional debug-section tag before its recursive decoder. */
static int read_type(void *message) {
    unsigned type=read_byte(message);
    if (type==15) {
        int overflow=1;memcpy(message,&overflow,4);return 0;
    }
    return (int)type;
}

/* Array termination is not sufficient on its own: some native packagers
 * return their last scalar result without re-checking msg.overflowed. Ensure
 * no partially parsed object is accepted after a rejected array element. */
static bool clean_package(void *message,bool result) {
    int mode,overflow;
    memcpy(&mode,(uint8_t *)message+0x40,4);memcpy(&overflow,message,4);
    return result && (mode!=2 || !overflow);
}
#define PACKAGE_GUARD(name) \
    static bool (*original_##name)(void *,void *); \
    static bool name(void *object,void *message) { \
        return clean_package(message,original_##name(object,message)); \
    }
PACKAGE_GUARD(info_package)
PACKAGE_GUARD(state_package)
PACKAGE_GUARD(game_package)
PACKAGE_GUARD(heartbeat_package)
PACKAGE_GUARD(join_package)
#undef PACKAGE_GUARD

/* Pinned Mac v98 entry guards. Validate every entry before installing any. */
struct ui_hook { size_t offset; void *replacement; void **original; unsigned char bytes[20]; };
static const struct ui_hook hooks[]={
    {0xf47799,(void *)directive,(void **)&original_directive,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x10,0x48,0x89,0xd3,0x49,0x89}},
    {0xd50fdc,(void *)model_string,(void **)&original_model_string,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x10,0x00,0x00}},
    {0x1036426,(void *)model_path,(void **)&original_path,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x68,0x89,0xd3,0x41}},
    {0xe2e90a,(void *)config,(void **)&original_config,{0x55,0x48,0x89,0xe5,0x48,0x63,0xc7,0x48,0x8d,0x0d,0x98,0x5c,0x70,0x10,0x48,0x63,0x84,0x81,0xb8,0x66}},
    {0x131489c,(void *)idle,(void **)&original_idle,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x88,0x00,0x00,0x00}},
    {0x1311e7c,(void *)type_name,(void **)&original_type_name,{0x55,0x48,0x89,0xe5,0x83,0xff,0xff,0x74,0x10,0x48,0x63,0xc7,0x48,0x8d,0x0d,0x61,0x6e,0x86,0x00,0x48}},
    {0xf417ae,(void *)debug_message,NULL,{0x55,0x48,0x89,0xe5,0x53,0x48,0x83,0xec,0x18,0x48,0x89,0xfb,0x40,0x84,0xf6,0x75,0x13,0x48,0x8d,0x05}},
    {0xd62926,(void *)browser,NULL,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x41,0x89,0xf6,0x48,0x89,0xfb,0x48,0x8d,0x35,0x06,0x2b,0x8d,0x00}},
    {0x12a693e,(void *)menu_name,(void **)&original_menu_name,{0x55,0x48,0x89,0xe5,0x89,0xf8,0x48,0x63,0xce,0x48,0x69,0xc0,0x00,0xfe,0x45,0x00,0x48,0x8d,0x15,0xdb}},
    {0x12a6d30,(void *)event_name,(void **)&original_event_name,{0x55,0x48,0x89,0xe5,0x89,0xf8,0x48,0x63,0xce,0x48,0x69,0xc0,0x00,0xfe,0x45,0x00,0x48,0x8d,0x15,0xe9}},
    {0xf451d1,(void *)menu,(void **)&original_menu,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x81,0xec,0x20,0x0d,0x00,0x00,0x49,0x89,0xfe,0x48,0x8b,0x05}},
    {0xf453b0,(void *)cached_menu,(void **)&original_cached_menu,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x81,0xec,0x18,0x0d,0x00,0x00,0x49,0x89,0xfe,0x48}},
    {0x12c8e15,(void *)binding,(void **)&original_binding,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x0e,0x00,0x00}},
    {0x15312a1,(void *)presence,(void **)&original_presence,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x48,0x49,0x89,0xcd}},
    {0xe374a2,(void *)global_im,NULL,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x45,0x89,0xc6,0x49,0x89,0xcf,0x49,0x89,0xf4}},
    {0x1247f67,(void *)read_data,(void **)&original_read_data,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x41,0x89,0xd7,0x49,0x89,0xf6}},
    {0xe43821,(void *)notetrack,(void **)&original_notetrack,{0x55,0x48,0x89,0xe5,0x48,0x63,0xc6,0x48,0x8d,0x04,0x40,0x48,0x8b,0x4c,0xc7,0x38,0x48,0x8b,0x81,0xc0}},
    {0xf47819,(void *)binding_text,(void **)&original_binding_text,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x38,0x14,0x00,0x00}},
    {0x1409b4c,(void *)exec_lua,NULL,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0xe8,0xf4,0xc4,0x8f,0xff,0x48,0x8b,0x40,0x18,0x48}},
    {0xe4971e,(void *)subscribe_ugc,(void **)&original_subscribe,{0x55,0x48,0x89,0xe5,0x53,0x50,0x31,0xdb,0x31,0xf6,0xba,0x0a,0x00,0x00,0x00,0xe8,0xe8,0x17,0x2b,0xff}},
    {0xf65fd8,(void *)text_width,(void **)&original_text_width,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58,0xf3,0x0f,0x11}},
    {0xf47c10,(void *)localize,(void **)&original_localize,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x68,0x20,0x00,0x00}},
    {0x134b1b9,(void *)accept_invite,(void **)&original_accept,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x48,0x89,0x55,0xd0,0x49,0x89}},
    {0x134bbf3,(void *)join_action,(void **)&original_join_action,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x70,0x49,0x89,0xd7,0x48,0x89}},
    {0x134bdb2,(void *)join_info,(void **)&original_join_info,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x81,0xec,0xa8,0x00,0x00,0x00,0x49,0x89,0xd7,0x41}},
    {0xf42b13,(void *)sl_command,(void **)&original_sl,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x81,0xec,0x10,0x01,0x00,0x00,0x48,0x8b,0x05,0x28,0x2e,0xa2}},
    {0xf6956d,(void *)steam_server_init,NULL,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58,0x48,0x8b,0x05}},
    {0xf6908e,(void *)steam_server_frame,NULL,{0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xe8,0x96,0xd9,0x6b,0x00,0xe8,0x76,0xb3,0x3a,0x00,0x3b}},
    {0xf41836,(void *)read_type,NULL,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xfc,0xe8,0x3d,0x61}},
    {0xe3528a,(void *)info_package,(void **)&original_info_package,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xf7,0x49,0x89,0xfe}},
    {0x116fa8e,(void *)state_package,(void **)&original_state_package,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xf6,0x49,0x89,0xff}},
    {0x116ffdc,(void *)game_package,(void **)&original_game_package,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xf4,0x49,0x89,0xfe}},
    {0x11709d0,(void *)heartbeat_package,(void **)&original_heartbeat_package,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xf6,0x49,0x89,0xfd}},
    {0x1234e06,(void *)join_package,(void **)&original_join_package,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89,0xf7,0x49,0x89,0xfe}},
};
/* Both T7 signed-byte copy fixes and the related Mac consumers. All seven
 * fields are encoded unsigned byte lengths, never signed character values. */
static const struct { size_t offset; unsigned char bytes[5]; } lengths[]={
    {0xd50e5e,{0x4c,0x0f,0xbe,0x76,0x04}},
    {0x11e57ac,{0x4c,0x0f,0xbe,0x76,0x04}},
    {0x11cbb5e,{0x49,0x0f,0xbe,0x55,0x04}},
    {0xf662c7,{0x48,0x0f,0xbe,0x48,0x03}},
    {0x143cdb9,{0x48,0x0f,0xbe,0x46,0x03}},
    {0x143e175,{0x48,0x0f,0xbe,0x5e,0x03}},
    {0x143f905,{0x48,0x0f,0xbe,0x48,0x03}},
};
int bo3_install_security_ui(uint64_t base) {
    for (size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)
        if (memcmp((void *)(uintptr_t)(base+hooks[i].offset),hooks[i].bytes,20)) return -1;
    static const unsigned char dep_f75cfb[]={0x55,0x48,0x89,0xe5,0x48,0x63,0xc2,0x48,0x8d,0x0d,0xb7,0x66,0x66,0x11,0x48,0x63,0x15,0xb0,0x66,0x66};
    if (memcmp((void *)(uintptr_t)(base+0xf75cfb),dep_f75cfb,20)) return -1;
    static const unsigned char dep_e3152b[]={0x55,0x48,0x89,0xe5,0xe8,0x53,0x6d,0x60,0x00,0x48,0x89,0xc7,0x5d,0xe9,0xf1,0x2c,0x48,0x00,0x55,0x48};
    if (memcmp((void *)(uintptr_t)(base+0xe3152b),dep_e3152b,20)) return -1;
    for (size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i)
        if (memcmp((void *)(uintptr_t)(base+lengths[i].offset),lengths[i].bytes,5)) return -1;
    static const unsigned char read_byte_sig[]={0x55,0x48,0x89,0xe5,0x48,0x63,0x47,0x24,0x8b,0x4f,0x1c,0x8b,0x57,0x20,0x01,0xca,0x39,0xd0,0x7d,0x0d};
    if (memcmp((void *)(uintptr_t)(base+0x1247989),read_byte_sig,20)) return -1;
    image=base;
    read_byte=(void *)(uintptr_t)(base+0x1247989);
    server_arg=(void *)(uintptr_t)(base+0xf75cfb);
    is_host=(void *)(uintptr_t)(base+0xe3152b);
    int count=0;
    unsigned char zero_extend=0xb6;
    for (size_t i=0;i<sizeof(lengths)/sizeof(*lengths);++i)
        if (!DobbyCodePatch((void *)(uintptr_t)(base+lengths[i].offset+2),&zero_extend,1)) ++count;
    for (size_t i=0;i<sizeof(hooks)/sizeof(*hooks);++i)
        if (!DobbyHook((void *)(uintptr_t)(base+hooks[i].offset),hooks[i].replacement,hooks[i].original)) ++count;
    return count;
}

static int apply_security_dvars(void *(*find)(const char *),void *(*set)(const char *,const char *,bool)) {
    static const char *const names[]={"g_allowvote","maxvoicepacketsperframe"};
    int count=0;
    for (size_t i=0;i<2;++i) {
        uint8_t *dvar=find(names[i]);
        if (!dvar) continue; /* Do not register an external dvar before the game. */
        uint32_t flags=0;memcpy(dvar+0x18,&flags,4);
        set(names[i],"0",false);
        int type,value;memcpy(&type,dvar+0x1c,4);memcpy(&value,dvar+0x28,4);
        if ((type==1 && dvar[0x28]==0) || ((type==6 || type==7) && value==0)) ++count;
    }
    return count;
}
int bo3_security_dvars(uint64_t base) {
    static const unsigned char dep_f9ff7c[]={0x48,0x85,0xff,0x74,0x32,0x0f,0xbe,0x07,0x85,0xc0,0x74,0x2b};
    if (memcmp((void *)(uintptr_t)(base+0xf9ff7c),dep_f9ff7c,12)) return -1;
    static const unsigned char dep_fa3386[]={0x55,0x48,0x89,0xe5,0x44,0x0f,0xb6,0xc2,0x31,0xd2,0x31,0xc9};
    if (memcmp((void *)(uintptr_t)(base+0xfa3386),dep_fa3386,12)) return -1;
    void *(*find)(const char *)=(void *)(uintptr_t)(base+0xf9ff7c);
    void *(*set)(const char *,const char *,bool)=(void *)(uintptr_t)(base+0xfa3386);
    return apply_security_dvars(find,set);
}
