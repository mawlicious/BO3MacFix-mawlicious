#ifndef BO3MACFIX_HAVOK_CONVERT_H
#define BO3MACFIX_HAVOK_CONVERT_H

#include <stddef.h>
#include <stdint.h>

/* Explicit wire schemas: inherited members must be flattened. No C++ ABI or
 * Havok SDK objects are used. Every serialized pointer requires a member. */
typedef enum {
    HKCONV_BYTES = 1, /* element_size bytes, including enum storage */
    HKCONV_STRUCT,
    HKCONV_POINTER,
    HKCONV_STRING,
    HKCONV_ARRAY,       /* hkArray: pointer, uint32 count, uint32 capacity */
    HKCONV_SIMPLE_ARRAY /* pointer, uint32 count (16 byte aligned wire slot) */
} hkconv_kind;

typedef struct {
    uint32_t source_offset, target_offset;
    hkconv_kind kind;
    hkconv_kind element_kind; /* ARRAY/SIMPLE_ARRAY elements */
    uint32_t element_size;    /* BYTES size; never inferred */
    uint32_t count;           /* fixed inline repetition; zero means one */
    const char *class_name;  /* STRUCT, typed POINTER or struct/pointer array */
} hkconv_member;

typedef struct {
    const char *name;
    uint32_t source_size, target_size;
    uint32_t source_alignment, target_alignment;
    const hkconv_member *members;
    size_t member_count;
} hkconv_class;

/* Converts v11 hk_2014.2.0-r1 Windows x64 packfiles (8,1,0,1) to Mac
 * x64 (8,1,1,1). Unknown classes, unmodeled pointers, external imports,
 * relative arrays and unsupported layouts fail without returning output.
 * Output owns no input/schema memory; release it with hkconv_free().
 * Returns 1 on success, 0 on failure; error receives a bounded explanation. */
int hkconv_convert(const void *input, size_t input_size,
                   const hkconv_class *classes, size_t class_count,
                   void **output, size_t *output_size,
                   char *error, size_t error_size);
void hkconv_free(void *output);

#endif
