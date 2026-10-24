#include "../source/havok_convert.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(unsigned char *p,uint32_t v) {
    p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void section(unsigned char *p,const char *name,uint32_t start,uint32_t local,uint32_t global,uint32_t virt,uint32_t end) {
    memset(p,255,64);memcpy(p,name,strlen(name)+1);
    put32(p+20,start);put32(p+24,local);put32(p+28,global);put32(p+32,virt);
    put32(p+36,end);put32(p+40,end);put32(p+44,end);
}
static const hkconv_member graph_members[]={
    {.source_offset=16,.target_offset=12,.kind=HKCONV_BYTES,.element_size=4},
    {.source_offset=24,.target_offset=16,.kind=HKCONV_POINTER,.class_name="Graph"},
    {.source_offset=32,.target_offset=24,.kind=HKCONV_POINTER,.class_name="Graph"},
    {.source_offset=40,.target_offset=32,.kind=HKCONV_STRING},
    {.source_offset=48,.target_offset=40,.kind=HKCONV_ARRAY,.element_kind=HKCONV_STRUCT,.class_name="Element"},
};
static const hkconv_member element_members[]={
    {.source_offset=16,.target_offset=12,.kind=HKCONV_BYTES,.element_size=4},
};
static const hkconv_class classes[]={
    {"Graph",64,56,8,8,graph_members,5},
    {"Element",24,16,8,8,element_members,1},
};
enum { INPUT_SIZE=448, DATA=272 };
static void fixture(unsigned char p[INPUT_SIZE]) {
    memset(p,255,INPUT_SIZE);memset(p,0,64);
    put32(p,0x57e0e057);put32(p+4,0x10c0c010);put32(p+12,11);
    memcpy(p+16,"\x08\x01\x00\x01",4);
    put32(p+20,3);put32(p+24,2);put32(p+28,0);put32(p+32,0);put32(p+36,5);
    memcpy(p+40,"hk_2014.2.0-r1",14);memset(p+60,255,4);
    section(p+64,"__classnames__",256,16,16,16,16);
    section(p+128,"__types__",272,0,0,0,0);
    section(p+192,"__data__",DATA,128,160,160,176);
    put32(p+256,0x12345678);p[260]=9;memcpy(p+261,"Graph",6);
    memset(p+DATA,0,128);put32(p+DATA+16,0xabcdef01);
    put32(p+DATA+48+8,2);put32(p+DATA+48+12,0x80000002);
    put32(p+DATA+64+16,123);put32(p+DATA+88+16,456);memcpy(p+DATA+112,"hello",6);
    const uint32_t from[]={24,32,40,48},to[]={0,0,112,64};
    for (int i=0;i<4;i++) {put32(p+DATA+128+i*8,from[i]);put32(p+DATA+132+i*8,to[i]);}
    put32(p+DATA+160,0);put32(p+DATA+164,0);put32(p+DATA+168,5);
}
static uint32_t fixup_target(const unsigned char *output,uint32_t from) {
    const unsigned char *section=output+192;
    uint32_t start=read32(section+20),end=read32(section+28);
    for (uint32_t at=read32(section+24);at+8<=end;at+=8)
        if (read32(output+start+at)==from) return read32(output+start+at+4);
    assert(!"Missing fixup");return 0;
}
static void expect_failure(unsigned char *input,size_t size,const hkconv_class *schema,size_t count,const char *reason) {
    void *out=(void *)1;size_t out_size=1;char error[256];
    assert(!hkconv_convert(input,size,schema,count,&out,&out_size,error,sizeof(error)));
    assert(out==NULL && out_size==0 && error[0]);
    if (reason && !strstr(error,reason)) {fprintf(stderr,"Expected %s, received %s\n",reason,error);abort();}
}
int main(void) {
    unsigned char input[INPUT_SIZE];fixture(input);void *result=NULL;size_t size=0;char error[256];
    if (!hkconv_convert(input,sizeof(input),classes,2,&result,&size,error,sizeof(error))) {
        fprintf(stderr,"Conversion failed: %s\n",error);return 1;
    }
    const unsigned char *out=result;assert(size>256);
    assert(!memcmp(out+16,"\x08\x01\x01\x01",4));
    assert(read32(out+256)==0x12345678); /* Original class signature is preserved. */
    uint32_t data=read32(out+192+20);assert(read32(out+data+12)==0xabcdef01);
    assert(fixup_target(out,16)==0 && fixup_target(out,24)==0); /* cycle and alias */
    uint32_t text=fixup_target(out,32),array=fixup_target(out,40);
    assert(!strcmp((const char *)out+data+text,"hello"));
    assert(!(array&15));assert(read32(out+data+48)==2);
    assert(read32(out+data+array+12)==123 && read32(out+data+array+16+12)==456);
    assert(read32(out+data+52)==0x80000002);
    uint32_t virt=read32(out+192+32);assert(read32(out+data+virt)==0);
    /* Input fixup order is not semantic. Sorting must preserve output bytes. */
    unsigned char shuffled[INPUT_SIZE];memcpy(shuffled,input,sizeof(shuffled));
    for (unsigned i=0;i<4;i++)
        memcpy(shuffled+DATA+128+i*8,input+DATA+128+(3-i)*8,8);
    void *shuffled_result=NULL;size_t shuffled_size=0;
    assert(hkconv_convert(shuffled,sizeof(shuffled),classes,2,&shuffled_result,&shuffled_size,error,sizeof(error)));
    assert(shuffled_size==size && !memcmp(result,shuffled_result,size));
    hkconv_free(shuffled_result);
    memcpy(shuffled+DATA+136,shuffled+DATA+128,8);
    expect_failure(shuffled,sizeof(shuffled),classes,2,"Duplicate pointer fixup");
    hkconv_free(result);

    /* Opaque arrays retain every byte and interior pointer across elements. */
    hkconv_member opaque_members[5];memcpy(opaque_members,graph_members,sizeof(opaque_members));
    opaque_members[1].class_name=NULL;
    opaque_members[4].element_kind=HKCONV_BYTES;
    opaque_members[4].element_size=24;
    opaque_members[4].class_name=NULL;
    hkconv_class opaque_classes[2];memcpy(opaque_classes,classes,sizeof(opaque_classes));
    opaque_classes[0].members=opaque_members;
    fixture(input);put32(input+DATA+132,64+29);
    assert(hkconv_convert(input,sizeof(input),opaque_classes,2,&result,&size,error,sizeof(error)));
    out=result;data=read32(out+192+20);array=fixup_target(out,40);
    assert(!memcmp(out+data+array,input+DATA+64,48));
    assert(fixup_target(out,16)==array+29);
    hkconv_free(result);fixture(input);

    /* Every truncation, including a valid-looking partial section, fails. */
    for (size_t n=0;n<sizeof(input);n++) expect_failure(input,n,classes,2,NULL);
    fixture(input);input[18]=1;expect_failure(input,sizeof(input),classes,2,"source ABI");
    /* The game's loader also follows the header's root name and object, so
     * valid virtual fixups cannot excuse an invalid or inconsistent header. */
    fixture(input);put32(input+36,UINT32_MAX);expect_failure(input,sizeof(input),classes,2,"range");
    fixture(input);put32(input+36,6);expect_failure(input,sizeof(input),classes,2,"Root class");
    fixture(input);put32(input+28,16);expect_failure(input,sizeof(input),classes,2,"Root object");
    fixture(input);put32(input+DATA+56,UINT32_MAX);expect_failure(input,sizeof(input),classes,2,"array length");
    fixture(input);put32(input+DATA+132,9999);expect_failure(input,sizeof(input),classes,2,"range");
    fixture(input);expect_failure(input,sizeof(input),classes,1,"struct schema");
    fixture(input);hkconv_class incomplete[2];memcpy(incomplete,classes,sizeof(classes));
    incomplete[0].members=graph_members+1;incomplete[0].member_count=4;
    /* Omitting a scalar is permitted; omitting any pointer is rejected. */
    incomplete[0].members=graph_members+2;incomplete[0].member_count=3;
    expect_failure(input,sizeof(input),incomplete,2,"Unmodeled pointer");
    fixture(input);put32(input+DATA+160,8);expect_failure(input,sizeof(input),classes,2,NULL);

    /* An untyped interior pointer into a moved array follows the element's
     * scalar mapping rather than assuming the old Windows byte stride. */
    fixture(input);hkconv_member interior_members[5];memcpy(interior_members,graph_members,sizeof(graph_members));
    interior_members[1].class_name=NULL;put32(input+DATA+132,88+16);
    memcpy(incomplete,classes,sizeof(classes));incomplete[0].members=interior_members;
    assert(hkconv_convert(input,sizeof(input),incomplete,2,&result,&size,error,sizeof(error)));
    out=result;array=fixup_target(out,40);assert(fixup_target(out,16)==array+16+12);hkconv_free(result);

    /* Primitive hkSimpleArray storage and its count survive relocation. */
    fixture(input);memcpy(interior_members,graph_members,sizeof(graph_members));
    interior_members[4].kind=HKCONV_SIMPLE_ARRAY;interior_members[4].element_kind=HKCONV_BYTES;
    interior_members[4].element_size=4;interior_members[4].class_name=NULL;
    put32(input+DATA+64,17);put32(input+DATA+68,23);
    assert(hkconv_convert(input,sizeof(input),incomplete,2,&result,&size,error,sizeof(error)));
    out=result;data=read32(out+192+20);array=fixup_target(out,40);
    assert(read32(out+data+array)==17 && read32(out+data+array+4)==23);hkconv_free(result);

    /* Strings aliasing an earlier opaque array still require a terminator
     * inside the same copied byte range; adjacent source bytes may move. */
    fixture(input);
    hkconv_member alias_members[5];memcpy(alias_members,graph_members,sizeof(graph_members));
    alias_members[3]=graph_members[4];alias_members[4]=graph_members[3];
    alias_members[3].element_kind=HKCONV_BYTES;alias_members[3].element_size=4;
    alias_members[3].class_name=NULL;
    hkconv_class alias_classes[2];memcpy(alias_classes,classes,sizeof(classes));
    alias_classes[0].members=alias_members;
    memset(input+DATA+64,'X',64);put32(input+DATA+148,64);
    expect_failure(input,sizeof(input),alias_classes,2,"Unterminated string");
    input[DATA+72]=0;
    expect_failure(input,sizeof(input),alias_classes,2,"String alias");
    memcpy(input+DATA+64,"hi",3);
    assert(hkconv_convert(input,sizeof(input),alias_classes,2,&result,&size,error,sizeof(error)));
    out=result;data=read32(out+192+20);array=fixup_target(out,40);
    assert(fixup_target(out,32)==array && !strcmp((const char *)out+data+array,"hi"));
    hkconv_free(result);

    /* Global string fixups into the name section are copied into output data. */
    unsigned char extended[480];memset(extended,255,sizeof(extended));fixture(extended);
    memset(extended+DATA+144,255,8);
    section(extended+192,"__data__",DATA,128,160,176,208);
    put32(extended+DATA+160,40);put32(extended+DATA+164,0);put32(extended+DATA+168,5);
    memset(extended+DATA+172,255,36);
    put32(extended+DATA+176,0);put32(extended+DATA+180,0);put32(extended+DATA+184,5);
    assert(hkconv_convert(extended,sizeof(extended),classes,2,&result,&size,error,sizeof(error)));
    out=result;data=read32(out+192+20);text=fixup_target(out,32);
    assert(!strcmp((const char *)out+data+text,"Graph"));hkconv_free(result);

    /* An array of pointers retains two references to the existing root. */
    memset(extended,255,sizeof(extended));fixture(extended);
    section(extended+192,"__data__",DATA,128,176,176,208);
    memset(extended+DATA+64,0,16);
    put32(extended+DATA+160,64);put32(extended+DATA+164,0);
    put32(extended+DATA+168,72);put32(extended+DATA+172,0);
    put32(extended+DATA+176,0);put32(extended+DATA+180,0);put32(extended+DATA+184,5);
    memset(extended+DATA+188,255,20);
    memcpy(interior_members,graph_members,sizeof(graph_members));
    interior_members[4].element_kind=HKCONV_POINTER;interior_members[4].class_name="Graph";
    assert(hkconv_convert(extended,sizeof(extended),incomplete,2,&result,&size,error,sizeof(error)));
    out=result;array=fixup_target(out,40);assert(fixup_target(out,array)==0 && fixup_target(out,array+8)==0);
    hkconv_free(result);

    /* Unsupported relative/nested layouts cannot silently leave pointers. */
    fixture(input);interior_members[4].element_kind=HKCONV_ARRAY;
    expect_failure(input,sizeof(input),incomplete,2,"Nested array");

    /* Deterministic corruptions exercise parser error paths under sanitizers. */
    uint32_t random=0x12345678;
    for (unsigned round=0;round<2000;round++) {
        fixture(input);
        for (unsigned edit=0;edit<4;edit++) {
            random=random*1664525u+1013904223u;size_t at=random%sizeof(input);
            random=random*1664525u+1013904223u;input[at]=(unsigned char)(random>>24);
        }
        int ok=hkconv_convert(input,sizeof(input),classes,2,&result,&size,error,sizeof(error));
        if (ok) hkconv_free(result);else assert(result==NULL && size==0 && error[0]);
    }
    puts("havok_convert: all tests passed");return 0;
}
