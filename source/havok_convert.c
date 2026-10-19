#include "havok_convert.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Packfiles describe a graph, not a sequence of C structs. Keep source
 * addresses until traversal finishes, then resolve aliases and interior
 * pointers through the field mappings produced during relayout. */
#define LIMIT ((size_t)512 * 1024 * 1024)
#define DEPTH_LIMIT 128
typedef struct { uint32_t start, local, global, virt, exports, imports, end; } Section;
typedef struct { uint32_t sec, from, to_sec, to; int used; } Fixup;
typedef struct { uint32_t sec, at, name_sec, name_at; } Virtual;
typedef struct { uint32_t sec, from, size, to; } Map;
typedef struct { uint32_t sec, at, out; const hkconv_class *cls; } Object;
typedef struct { uint32_t at, sec, to; } Patch;
typedef struct { uint8_t *p; size_t n, cap; } Buffer;
typedef struct {
    const uint8_t *input; size_t input_size;
    const hkconv_class *classes; size_t class_count;
    Section *sections; uint32_t section_count, data_sec, names_sec;
    Fixup *fixups; size_t fixup_count, fixup_cap;
    Virtual *virtuals; size_t virtual_count, virtual_cap;
    Map *maps; size_t map_count, map_cap;
    Object *objects; size_t object_count, object_cap;
    Patch *patches; size_t patch_count, patch_cap;
    Buffer data;
    char *error; size_t error_size; int failed;
} Context;

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void w32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static int fail(Context *c, const char *fmt, ...) {
    if (!c->failed && c->error && c->error_size) {
        va_list ap; va_start(ap,fmt); vsnprintf(c->error,c->error_size,fmt,ap); va_end(ap);
    }
    c->failed=1; return 0;
}
static int grow(Context *c, void **ptr, size_t *cap, size_t count, size_t size) {
    if (count<=*cap) return 1;
    size_t next=*cap ? *cap*2 : 16;
    if (next<count) next=count;
    if (!size || next>LIMIT/size) return fail(c,"Allocation limit exceeded");
    void *p=realloc(*ptr,next*size);
    if (!p) return fail(c,"Out of memory");
    *ptr=p; *cap=next; return 1;
}
#define APPEND(c, field, value) \
    (grow((c),(void **)&(c)->field,&(c)->field##_cap,(c)->field##_count+1,sizeof(*(c)->field)) ? \
     ((c)->field[(c)->field##_count++]=(value),1) : 0)
/* Use matching singular stems for the generic append helper. */
#define fixups_cap fixup_cap
#define fixups_count fixup_count
#define virtuals_cap virtual_cap
#define virtuals_count virtual_count
#define maps_cap map_cap
#define maps_count map_count
#define objects_cap object_cap
#define objects_count object_count
#define patches_cap patch_cap
#define patches_count patch_count

static int range(size_t at, size_t n, size_t size) { return at<=size && n<=size-at; }
static const uint8_t *source(Context *c, uint32_t sec, uint32_t at, size_t n) {
    if (sec>=c->section_count || !range(at,n,c->sections[sec].local)) {
        fail(c,"Source range outside section (%u:%u, %zu bytes)",sec,at,n); return NULL;
    }
    return c->input+c->sections[sec].start+at;
}
static uint32_t alloc_data(Context *c, size_t n, uint32_t alignment) {
    if (!alignment || alignment>16 || (alignment&(alignment-1))) {
        fail(c,"Unsupported alignment %u",alignment); return 0;
    }
    size_t at=(c->data.n+alignment-1)&~(size_t)(alignment-1);
    if (at>LIMIT || n>LIMIT-at ||
        !grow(c,(void **)&c->data.p,&c->data.cap,at+n,1)) {
        if (!c->failed) fail(c,"Output exceeds size limit"); return 0;
    }
    memset(c->data.p+c->data.n,0,at+n-c->data.n);
    c->data.n=at+n; return (uint32_t)at;
}
static const hkconv_class *find_class(Context *c, const char *name) {
    if (!name) return NULL;
    for (size_t i=0;i<c->class_count;i++)
        if (!strcmp(c->classes[i].name,name)) return &c->classes[i];
    return NULL;
}
static const char *string_at(Context *c, uint32_t sec, uint32_t at, size_t *length) {
    const uint8_t *p=source(c,sec,at,1); if (!p) return NULL;
    const uint8_t *end=memchr(p,0,c->sections[sec].local-at);
    if (!end) { fail(c,"Unterminated string at %u:%u",sec,at); return NULL; }
    *length=(size_t)(end-p)+1; return (const char *)p;
}
static const hkconv_class *virtual_class(Context *c,uint32_t sec,uint32_t at) {
    for (size_t i=0;i<c->virtual_count;i++) {
        Virtual *v=&c->virtuals[i];
        if (v->sec==sec && v->at==at) {
            size_t n; const char *name=string_at(c,v->name_sec,v->name_at,&n);
            if (!name) return NULL;
            const hkconv_class *cls=find_class(c,name);
            if (!cls) fail(c,"Missing class schema: %s",name);
            return cls;
        }
    }
    return NULL;
}
static int add_map(Context *c,uint32_t sec,uint32_t from,uint32_t size,uint32_t to) {
    Map m={sec,from,size,to}; return APPEND(c,maps,m);
}
static int mapped(Context *c,uint32_t sec,uint32_t from,uint32_t *to) {
    /* Exact object/field starts take precedence over containing byte ranges. */
    for (size_t i=0;i<c->map_count;i++) if (c->maps[i].sec==sec && c->maps[i].from==from) {
        *to=c->maps[i].to; return 1;
    }
    for (size_t i=0;i<c->map_count;i++) {
        Map *m=&c->maps[i];
        if (m->sec==sec && from>=m->from && from-m->from<m->size) {
            *to=m->to+from-m->from; return 1;
        }
    }
    return 0;
}
static Fixup *pointer_at(Context *c,uint32_t sec,uint32_t at) {
    for (size_t i=0;i<c->fixup_count;i++) if (c->fixups[i].sec==sec && c->fixups[i].from==at) {
        c->fixups[i].used=1; return &c->fixups[i];
    }
    const uint8_t *p=source(c,sec,at,8);
    if (p) for (int i=0;i<8;i++) if (p[i]) { fail(c,"Pointer without fixup at %u:%u",sec,at); break; }
    return NULL;
}
static int object(Context *,uint32_t,uint32_t,const hkconv_class *,uint32_t *,unsigned);
static int fields(Context *,uint32_t,uint32_t,uint32_t,const hkconv_class *,unsigned);

static int value(Context *c,uint32_t sec,uint32_t at,uint32_t out,hkconv_kind kind,
                 uint32_t bytes,const char *class_name,unsigned depth) {
    if (depth>DEPTH_LIMIT) return fail(c,"Object nesting limit exceeded");
    if (kind==HKCONV_BYTES) {
        const uint8_t *p=source(c,sec,at,bytes); if (!p) return 0;
        memcpy(c->data.p+out,p,bytes); return add_map(c,sec,at,bytes,out);
    }
    if (kind==HKCONV_STRUCT) {
        const hkconv_class *cls=find_class(c,class_name);
        if (!cls) return fail(c,"Missing embedded class schema");
        if (!source(c,sec,at,cls->source_size)) return 0;
        if (!add_map(c,sec,at,0,out)) return 0;
        return fields(c,sec,at,out,cls,depth+1);
    }
    if (kind!=HKCONV_POINTER && kind!=HKCONV_STRING) return fail(c,"Unsupported element kind %d",kind);
    if (!add_map(c,sec,at,8,out)) return 0;
    Fixup *f=pointer_at(c,sec,at); if (!f) return !c->failed;
    /* Recursive calls grow fixup-independent arrays; copy source address now. */
    uint32_t to_sec=f->to_sec,to=f->to;
    if (kind==HKCONV_STRING) {
        size_t n; const char *s=string_at(c,to_sec,to,&n); if (!s) return 0;
        uint32_t dest;
        if (!mapped(c,to_sec,to,&dest)) {
            dest=alloc_data(c,n,1); if (c->failed) return 0;
            memcpy(c->data.p+dest,s,n);
            if (!add_map(c,to_sec,to,(uint32_t)n,dest)) return 0;
        } else {
            /* An alias must include its terminator inside a copied byte
             * range. Relayout may otherwise place unrelated fields after the
             * mapped start, even when the source string itself was valid. */
            int covered=0;
            for (size_t i=0;i<c->map_count;i++) {
                Map *m=&c->maps[i];
                if (m->sec==to_sec && to>=m->from &&
                    range(to-m->from,n,m->size) &&
                    m->to+to-m->from==dest) { covered=1; break; }
            }
            if (!covered || !range(dest,n,c->data.n) || memcmp(c->data.p+dest,s,n))
                return fail(c,"String alias is not a contiguous copied byte range");
        }
    } else {
        if (to_sec!=c->data_sec) return fail(c,"Object pointer targets a non-data section");
        const hkconv_class *cls=virtual_class(c,to_sec,to);
        if (c->failed) return 0;
        if (!cls && class_name) cls=find_class(c,class_name);
        if (class_name && !cls) return fail(c,"Missing pointer class schema: %s",class_name);
        uint32_t dest;
        if (cls && !object(c,to_sec,to,cls,&dest,depth+1)) return 0;
        /* Untyped pointers resolve after all fields have been discovered. */
    }
    Patch p={out,to_sec,to}; return APPEND(c,patches,p);
}

static int element_shape(Context *c,hkconv_kind kind,uint32_t bytes,const char *name,
                         uint32_t *src_size,uint32_t *dst_size,uint32_t *align) {
    if (kind==HKCONV_BYTES) {
        if (!bytes) return fail(c,"Zero-sized byte field");
        *src_size=*dst_size=bytes; *align=1; return 1;
    }
    if (kind==HKCONV_STRUCT) {
        const hkconv_class *cls=find_class(c,name);
        if (!cls) return fail(c,"Missing struct schema: %s",name?name:"(null)");
        *src_size=cls->source_size; *dst_size=cls->target_size; *align=cls->target_alignment; return 1;
    }
    if (kind==HKCONV_POINTER || kind==HKCONV_STRING) { *src_size=*dst_size=*align=8; return 1; }
    if (kind==HKCONV_ARRAY || kind==HKCONV_SIMPLE_ARRAY) { *src_size=*dst_size=16; *align=8; return 1; }
    return fail(c,"Unknown schema kind %d",kind);
}

static int array(Context *c,uint32_t sec,uint32_t at,uint32_t out,const hkconv_member *m,unsigned depth) {
    const uint8_t *header=source(c,sec,at,16); if (!header) return 0;
    uint32_t count=u32(header+8),src_stride,dst_stride,align;
    if (count>INT_MAX) return fail(c,"Invalid array length");
    if (!element_shape(c,m->element_kind,m->element_size,m->class_name,&src_stride,&dst_stride,&align)) return 0;
    if (m->element_kind==HKCONV_ARRAY || m->element_kind==HKCONV_SIMPLE_ARRAY)
        return fail(c,"Nested array schemas are unsupported");
    if ((uint64_t)count*src_stride>LIMIT || (uint64_t)count*dst_stride>LIMIT)
        return fail(c,"Array size exceeds limit");
    w32(c->data.p+out+8,count);
    if (m->kind==HKCONV_ARRAY) w32(c->data.p+out+12,count|0x80000000u);
    if (!add_map(c,sec,at,16,out)) return 0;
    Fixup *f=pointer_at(c,sec,at); if (c->failed) return 0;
    if (!f) return count ? fail(c,"Nonempty array has no data fixup") : 1;
    if (!count) return fail(c,"Empty array has a data fixup");
    uint32_t to_sec=f->to_sec,to=f->to;
    if (to_sec!=c->data_sec) return fail(c,"Array targets a non-data section");
    if (!source(c,to_sec,to,(size_t)count*src_stride)) return 0;
    uint32_t dest;
    if (mapped(c,to_sec,to,&dest)) return fail(c,"Overlapping/shared array storage is unsupported");
    /* Havok arrays can contain SIMD values represented by opaque byte fields.
     * 16-byte storage satisfies their alignment without guessing their type. */
    dest=alloc_data(c,(size_t)count*dst_stride,16); if (c->failed) return 0;
    if (!add_map(c,to_sec,to,0,dest)) return 0;
    for (uint32_t i=0;i<count;i++)
        if (!value(c,to_sec,to+i*src_stride,dest+i*dst_stride,m->element_kind,m->element_size,m->class_name,depth+1)) return 0;
    Patch p={out,to_sec,to}; return APPEND(c,patches,p);
}

static int fields(Context *c,uint32_t sec,uint32_t at,uint32_t out,const hkconv_class *cls,unsigned depth) {
    if (depth>DEPTH_LIMIT) return fail(c,"Object nesting limit exceeded");
    for (size_t i=0;i<cls->member_count;i++) {
        const hkconv_member *m=&cls->members[i]; uint32_t ss,ds,align;
        if (!element_shape(c,m->kind,m->element_size,m->class_name,&ss,&ds,&align)) return 0;
        uint32_t count=m->count?m->count:1;
        if (!range(m->source_offset,(size_t)ss*count,cls->source_size) ||
            !range(m->target_offset,(size_t)ds*count,cls->target_size)) return fail(c,"Member outside class %s",cls->name);
        for (uint32_t j=0;j<count;j++) {
            uint32_t from=at+m->source_offset+j*ss, dest=out+m->target_offset+j*ds;
            if (m->kind==HKCONV_ARRAY || m->kind==HKCONV_SIMPLE_ARRAY) {
                if (!array(c,sec,from,dest,m,depth+1)) return 0;
            } else if (!value(c,sec,from,dest,m->kind,m->element_size,m->class_name,depth+1)) return 0;
        }
    }
    return 1;
}

static int object(Context *c,uint32_t sec,uint32_t at,const hkconv_class *cls,uint32_t *dest,unsigned depth) {
    for (size_t i=0;i<c->object_count;i++) if (c->objects[i].sec==sec && c->objects[i].at==at) {
        if (c->objects[i].cls!=cls) return fail(c,"Conflicting object schemas");
        *dest=c->objects[i].out; return 1;
    }
    if (!source(c,sec,at,cls->source_size)) return 0;
    uint32_t out=alloc_data(c,cls->target_size,cls->target_alignment); if (c->failed) return 0;
    Object o={sec,at,out,cls};
    if (!APPEND(c,objects,o) || !add_map(c,sec,at,0,out)) return 0;
    *dest=out; return fields(c,sec,at,out,cls,depth+1);
}

static int parse(Context *c) {
    const uint8_t *p=c->input;
    if (c->input_size<64 || c->input_size>LIMIT) return fail(c,"Invalid input size");
    if (u32(p)!=0x57e0e057u || u32(p+4)!=0x10c0c010u || u32(p+12)!=11)
        return fail(c,"Expected a v11 Havok packfile");
    if (memcmp(p+16,"\x08\x01\x00\x01",4) || memcmp(p+40,"hk_2014.2.0-r1\0",14))
        return fail(c,"Unsupported Havok version or source ABI");
    c->section_count=u32(p+20); c->data_sec=u32(p+24); c->names_sec=u32(p+32);
    if (!c->section_count || c->section_count>32 || c->data_sec>=c->section_count || c->names_sec>=c->section_count)
        return fail(c,"Invalid section indices");
    uint32_t extra=(uint32_t)p[62]|(uint32_t)p[63]<<8;
    if (extra!=0xffff && extra!=16 && extra!=0) return fail(c,"Unsupported header extension");
    size_t table=64+(extra==16?16:0);
    if (!range(table,(size_t)c->section_count*64,c->input_size)) return fail(c,"Truncated section table");
    c->sections=calloc(c->section_count,sizeof(*c->sections));
    if (!c->sections) return fail(c,"Out of memory");
    for (uint32_t i=0;i<c->section_count;i++) {
        const uint8_t *h=p+table+i*64; Section *s=&c->sections[i];
        s->start=u32(h+20); s->local=u32(h+24); s->global=u32(h+28); s->virt=u32(h+32);
        s->exports=u32(h+36); s->imports=u32(h+40); s->end=u32(h+44);
        if (s->start<table+c->section_count*64 || !range(s->start,s->end,c->input_size) ||
            s->local>s->global || s->global>s->virt || s->virt>s->exports ||
            s->exports>s->imports || s->imports>s->end) return fail(c,"Invalid section bounds");
        if (s->exports!=s->end || s->imports!=s->end) return fail(c,"Packfile imports/exports are unsupported");
    }
    for (uint32_t i=0;i<c->section_count;i++) {
        Section *s=&c->sections[i];
        /* Class/type metadata is discarded, but object data references into
         * names are understood for strings. Other live sections are unsafe. */
        if (i!=c->data_sec) continue;
        uint32_t starts[2]={s->local,s->global},ends[2]={s->global,s->virt};
        for (int global=0;global<2;global++) {
            uint32_t stride=global?12:8;
            for (uint32_t at=starts[global];at+4<=ends[global];) {
                const uint8_t *f=p+s->start+at;
                if (u32(f)==UINT32_MAX) { at+=4; continue; }
                if (!range(at,stride,ends[global])) return fail(c,"Truncated pointer fixup");
                Fixup v={i,u32(f),global?u32(f+4):i,u32(f+(global?8:4)),0};
                if (!source(c,v.sec,v.from,8) || !source(c,v.to_sec,v.to,1)) return 0;
                for (size_t k=0;k<c->fixup_count;k++) if (c->fixups[k].from==v.from)
                    return fail(c,"Duplicate pointer fixup");
                if (!APPEND(c,fixups,v)) return 0;
                at+=stride;
            }
        }
        for (uint32_t at=s->virt;at+4<=s->exports;) {
            const uint8_t *f=p+s->start+at;
            if (u32(f)==UINT32_MAX) { at+=4; continue; }
            if (!range(at,12,s->exports)) return fail(c,"Truncated virtual fixup");
            Virtual v={i,u32(f),u32(f+4),u32(f+8)}; size_t n;
            if (!source(c,v.sec,v.at,1) || !string_at(c,v.name_sec,v.name_at,&n)) return 0;
            for (size_t k=0;k<c->virtual_count;k++) if (c->virtuals[k].at==v.at)
                return fail(c,"Duplicate virtual fixup");
            if (!APPEND(c,virtuals,v)) return 0;
            at+=12;
        }
    }
    if (!c->virtual_count) return fail(c,"No class objects in packfile");
    /* The loader uses the header's root class name independently of virtual
     * fixups. Preserve it only when it is bounded and describes that object. */
    size_t root_name_length;
    const char *root_name=string_at(c,c->names_sec,u32(p+36),&root_name_length);
    if (!root_name) return 0;
    uint32_t root_at=u32(p+28);
    for (size_t i=0;i<c->virtual_count;i++) {
        Virtual *v=&c->virtuals[i];
        if (v->sec!=c->data_sec || v->at!=root_at) continue;
        size_t name_length;
        const char *name=string_at(c,v->name_sec,v->name_at,&name_length);
        if (!name) return 0;
        if (strcmp(root_name,name)) return fail(c,"Root class does not match virtual fixup");
        return 1;
    }
    return fail(c,"Root object has no virtual fixup");
}

static size_t align16(size_t v) { return (v+15)&~(size_t)15; }
static void section_header(uint8_t *h,const char *name,uint32_t start,uint32_t local,
                           uint32_t global,uint32_t virt,uint32_t end) {
    memset(h,0xff,64); memcpy(h,name,strlen(name)+1);
    w32(h+20,start); w32(h+24,local); w32(h+28,global); w32(h+32,virt);
    w32(h+36,end); w32(h+40,end); w32(h+44,end);
}
static int write_output(Context *c,void **output,size_t *output_size) {
    uint32_t root;
    if (!mapped(c,c->data_sec,u32(c->input+28),&root)) return fail(c,"Root object is not mapped");
    size_t names_size=c->sections[c->names_sec].local;
    size_t data_start=256+align16(names_size),local=align16(c->data.n);
    size_t global=align16(local+c->patch_count*8),virt=global;
    size_t end=align16(virt+c->virtual_count*12),total=data_start+end;
    if (total>LIMIT) return fail(c,"Output exceeds size limit");
    uint8_t *p=malloc(total); if (!p) return fail(c,"Out of memory");
    memset(p,0xff,total); memcpy(p,c->input,64);
    p[18]=1; w32(p+20,3); w32(p+24,2); w32(p+28,root); w32(p+32,0);
    /* Preserve the SDK's predicate marker (BO3 uses 21,0). If an extended
     * source header was accepted, its extension is deliberately omitted. */
    if (p[62]==16 && p[63]==0) { p[62]=0; p[63]=0; }
    section_header(p+64,"__classnames__",256,(uint32_t)align16(names_size),(uint32_t)align16(names_size),(uint32_t)align16(names_size),(uint32_t)align16(names_size));
    section_header(p+128,"__types__",(uint32_t)data_start,0,0,0,0);
    section_header(p+192,"__data__",(uint32_t)data_start,(uint32_t)local,(uint32_t)global,(uint32_t)virt,(uint32_t)end);
    memcpy(p+256,c->input+c->sections[c->names_sec].start,names_size);
    memcpy(p+data_start,c->data.p,c->data.n);
    for (size_t i=0;i<c->patch_count;i++) {
        Patch *patch=&c->patches[i]; uint32_t dest;
        if (!mapped(c,patch->sec,patch->to,&dest)) { free(p); return fail(c,"Unmapped pointer target %u:%u",patch->sec,patch->to); }
        w32(p+data_start+local+i*8,patch->at); w32(p+data_start+local+i*8+4,dest);
    }
    for (size_t i=0;i<c->virtual_count;i++) {
        Virtual *v=&c->virtuals[i]; uint32_t dest;
        if (v->name_sec!=c->names_sec || !mapped(c,v->sec,v->at,&dest)) {
            free(p); return fail(c,"Unsupported virtual fixup target");
        }
        w32(p+data_start+virt+i*12,dest); w32(p+data_start+virt+i*12+4,0);
        w32(p+data_start+virt+i*12+8,v->name_at);
    }
    *output=p; *output_size=total; return 1;
}

int hkconv_convert(const void *input,size_t input_size,const hkconv_class *classes,size_t class_count,
                   void **output,size_t *output_size,char *error,size_t error_size) {
    Context c={0}; c.input=input; c.input_size=input_size; c.classes=classes;
    c.class_count=class_count; c.error=error; c.error_size=error_size;
    if (error && error_size) error[0]=0;
    if (output) *output=NULL;
    if (output_size) *output_size=0;
    if (!input || !classes || !class_count || !output || !output_size) return fail(&c,"Invalid arguments");
    for (size_t i=0;i<class_count;i++) {
        const hkconv_class *cls=&classes[i];
        if (!cls->name || !cls->source_size || !cls->target_size ||
            !cls->source_alignment || cls->source_alignment>16 || (cls->source_alignment&(cls->source_alignment-1)) ||
            !cls->target_alignment || cls->target_alignment>16 || (cls->target_alignment&(cls->target_alignment-1)) ||
            (cls->member_count && !cls->members)) { fail(&c,"Invalid class schema"); break; }
        for (size_t j=0;j<i;j++) if (!strcmp(classes[j].name,cls->name)) { fail(&c,"Duplicate class schema"); break; }
    }
    if (!c.failed && parse(&c)) {
        for (size_t i=0;i<c.virtual_count && !c.failed;i++) {
            Virtual *v=&c.virtuals[i]; const hkconv_class *cls=virtual_class(&c,v->sec,v->at); uint32_t dest;
            if (!cls || !object(&c,v->sec,v->at,cls,&dest,0)) break;
        }
        for (size_t i=0;i<c.fixup_count && !c.failed;i++) if (!c.fixups[i].used)
            fail(&c,"Unmodeled pointer fixup at %u:%u",c.fixups[i].sec,c.fixups[i].from);
        if (!c.failed) write_output(&c,output,output_size);
    }
    free(c.sections); free(c.fixups); free(c.virtuals); free(c.maps);
    free(c.objects); free(c.patches); free(c.data.p);
    return !c.failed;
}
void hkconv_free(void *output) { free(output); }
