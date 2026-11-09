#include "security.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../source/security_runtime.c"

static unsigned calls;
static bool success = true;
static bool engine_command(int client, void *msg) { (void)client;(void)msg;++calls;return false; }
static bool engine_scalar(void *msg, const char *key, int32_t *value) {
    (void)msg;(void)key;(void)value;++calls;return success;
}
static bool engine_unsigned(void *msg, const char *key, uint32_t *value) {
    (void)msg;(void)key;(void)value;++calls;return success;
}
static bool engine_byte(void *msg, const char *key, uint8_t *value) {
    (void)msg;(void)key;(void)value;++calls;return success;
}
static bool element_found;
static bool engine_element(void *msg,bool expected) { (void)msg;(void)expected;++calls;return element_found; }
static uint8_t tls[32], args[512];
static void *get_tls(void) { return tls; }
static bool engine_array(void *msg,const char *key) { (void)msg;(void)key;++calls;return success; }
int DobbyHook(void *address, void *replacement, void **original) {
    (void)address;
    if (replacement == (void *)command) *original = (void *)engine_command;
    else if (replacement == (void *)package_uint) *original = (void *)engine_unsigned;
    else if (replacement == (void *)package_uchar) *original = (void *)engine_byte;
    else if (replacement==(void *)package_array) *original=(void *)engine_array;
    else if (replacement==(void *)package_element) *original=(void *)engine_element;
    else *original = (void *)engine_scalar;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    assert(bo3_security_command("statresponse", false));
    assert(bo3_security_command("LM", false));
    assert(bo3_security_command("CONNECTRESPONSE", false));
    assert(bo3_security_command("connectResponseMigration", false));
    assert(!bo3_security_command("requeststats", false));
    assert(bo3_security_command("requeststats", true));
    assert(!bo3_security_command("rcon", true));
    assert(!bo3_security_command("loadingnewmap", false));
    assert(!bo3_security_command("statresponseEVIL", false));
    assert(!bo3_security_command(NULL, false));
    uint8_t im[2050] = {'1','h'};
    assert(!bo3_security_im(NULL,2));
    for (int n=-2;n<2;++n) assert(!bo3_security_im(im,n));
    assert(bo3_security_im(im,2) && bo3_security_im(im,2049));
    assert(!bo3_security_im(im,2050));
    im[0]=0;assert(!bo3_security_im(im,100));im[0]='1';
    im[1]='e';assert(!bo3_security_im(im,100));
    im[1]='m';assert(!bo3_security_im(im,100));
    for (unsigned i=0;i<3;i++) assert(bo3_security_lobby_type("lobbytype",i));
    assert(!bo3_security_lobby_type("SrcLobbyType",UINT32_MAX));
    assert(!bo3_security_lobby_type("destlobbytype",3));
    assert(bo3_security_lobby_type("other",UINT32_MAX));
    size_t size=0x6939a18;
    uint8_t *base=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(base != MAP_FAILED);
    if (argc == 2) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        size_t read=fread(base,1,size,f);fclose(f);assert(read>0xf42500);
    } else {
        const uint8_t entry[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x81,0xec,0x10,0x04,0,0};
        const uint8_t caller[]={0x44,0x89,0xff,0x4c,0x89,0xee,0xe8,0x6e,0xfb,0xff,0xff,0x84,0xc0};
        const uint8_t lookup[]={0xe8,0x0e,0xd0,0xee,0xff,0x48,0x8b,0x40,0x18,0x48,0x63,0x08};
        const uint8_t scalar[]={0x48,0x89,0xf0,0x8b,0x4f,0x40,0x85,0xc9,0x74};
        memcpy(base+0xe19016,entry,sizeof(entry));
        memcpy(base+0xe1949d,caller,sizeof(caller));
        memcpy(base+0xe1903c,lookup,sizeof(lookup));
        memcpy(base+0xf41d33,scalar,sizeof(scalar));
        memcpy(base+0xf41db0,scalar,sizeof(scalar));
        memcpy(base+0xf41fc0,scalar,sizeof(scalar));
        const uint8_t element[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x8b,0x43,0x40};
        memcpy(base+0xf4246d,element,sizeof(element));
        { const unsigned char array[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xf6,0x48,0x89,0xfb,0x8b,0x43,0x40,0x85,0xc0,0x74,0x17};memcpy(base+0xf423e3,array,sizeof(array)); }
    }
    assert(bo3_install_security((uintptr_t)base)==6);
    engine_tls=get_tls;
    uint8_t *args_pointer=args;memcpy(tls+24,&args_pointer,sizeof(args_pointer));
    int count=1;memcpy(args+0x64,&count,4);
    const char *tokens[]={"statresponse"};const char **token_pointer=tokens;
    memcpy(args+0x88,&token_pointer,sizeof(token_pointer));
    calls=0;assert(!checked_command(0,NULL) && calls==1);
    tokens[0]="rcon";assert(checked_command(0,NULL) && calls==1);
    assert(!command(0,NULL) && calls==2); --calls; /* unrelated caller forwards */
    tokens[0]="requeststats";assert(checked_command(0,NULL) && calls==1);
    base[0x6939a14]=2;assert(!checked_command(0,NULL) && calls==2);
    int nesting=-1;memcpy(args,&nesting,4);assert(checked_command(0,NULL) && calls==2);
    nesting=8;memcpy(args,&nesting,4);assert(checked_command(0,NULL) && calls==2);
    unsigned char lobby[0x44]={0};lobby[0x40]=2;
    int32_t value=-1;
    assert(!package_int(lobby,"lobbytype",&value));
    value=2;assert(package_int(lobby,"lobbytype",&value));
    uint32_t u=3;assert(!package_uint(lobby,"srclobbytype",&u));
    uint8_t nat=255;assert(package_uchar(NULL,"nattype",&nat) && nat==4);
    nat=255;assert(package_uchar(NULL,"other",&nat) && nat==255);
    success=false;nat=255;assert(!package_uchar(NULL,"nattype",&nat) && nat==255);
    success=true;
    int overflow=0;
    element_found=true;
    assert(package_element(lobby,true));
    assert(!package_element(lobby,false));
    memcpy(&overflow,lobby,4);assert(overflow==1);
    lobby[0x40]=1;memset(lobby,0,4);
    assert(package_element(lobby,false)); /* writer uses false to end array */
    memcpy(&overflow,lobby,4);assert(!overflow);
    value=200;assert(package_int(lobby,"clientcount",&value) && value==200);
    lobby[0x40]=2;value=19;assert(!package_int(lobby,"clientcount",&value) && !value);
    u=0;assert(!package_uint(lobby,"datamask",&u));
    base[0x6939a14]=0;u=512;assert(!package_uint(lobby,"datamask",&u));
    base[0x6939a14]=1;u=512;assert(package_uint(lobby,"datamask",&u));
    im[1]='f';memset(im+2,0,100);assert(!bo3_security_im(im,102));
    im[2]=1;assert(bo3_security_im(im,102));assert(!bo3_security_im(im,101));
    memset(lobby,0,4);lobby[0x40]=2;success=true;element_found=true;
    assert(package_array(lobby,"nomineelist"));
    for (int i=0;i<18;++i) assert(package_element(lobby,false)); /* zero-initialized destination */
    assert(!package_element(lobby,false) && lobby[0]==1);
    assert(package_array(lobby,"clientlist"));
    assert(!package_element(lobby,false)); /* counted arrays still enforce expected */
    base[0xe19016]^=1;assert(bo3_install_security((uintptr_t)base)==-1);
    munmap(base,size);
    puts("PASS: command/IM bounds, scalar validation, TLS command hook, original forwarding, executable guards");
}
