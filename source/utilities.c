#include <CommonCrypto/CommonDigest.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include "utilities.h"
#include "dobby.h"
#ifndef NO_OSX
#include <CommonCrypto/CommonCrypto.h>
#endif

// Gets the base address of the first loaded dyld image (the game binary)
uint64_t get_base_address() {
    // getsegbyname is deprecated since 13.0, is there an alternative?
    const struct segment_command_64* segment = getsegbyname("__TEXT");
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    return segment->vmaddr + slide;
}

// Gets just the filename of a given path
const char *filename_from_path(const char *path) {
    size_t len = strlen(path);
    for (size_t i = len - 1; i > 0; i--) {
        if (path[i] == '/')
            return path + i + 1;
    }
    return path;
}

// Detects if the process is running under Rosetta2 or not (find out if we're on ASi or Intel)
bool rosetta_translated_process() {
    int ret = 0;
    size_t size = sizeof(ret);
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1)
        return false;
    return ret;
}

// Hash function used to generate network passwords on t7patch
uint64_t gscu_canon_hash64(const char *input) {
    static const uint64_t offset = 14695981039346656037ULL;
	static const uint64_t prime = 1099511628211ULL;
	uint64_t hash = offset;
	const char* _input = input;
	while (*_input) {
		hash = (hash ^ tolower(*_input)) * prime;
		_input++;
	}
	return 0x7FFFFFFFFFFFFFFF & hash;
}

// Replaces a vtable entry with the address of a function
void replace_vtable_entry(void *entry, void *replacement) {
    uint8_t replacement_bytes[8];
    uint64_t replacement_addy = (uint64_t)replacement;
    memcpy(replacement_bytes, &replacement_addy, sizeof(uint64_t));
    DobbyCodePatch(entry, replacement_bytes, 8);
}

// Checks if a file at a given path is a macOS binary
bool is_macos_dylib(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp != NULL) {
        uint8_t file_magic[4] = {0};
        if (fread(file_magic, 1, 4, fp) == 4) {
            // 0xFEEDFACF - 64-bit Mach-O
            static const uint8_t macho_header_64bit[4] = {0xcf, 0xfa, 0xed, 0xfe};
            if (memcmp(file_magic, macho_header_64bit, 4) == 0)
                return true;
            // 0xCAFEBABE - fat Mach-O
            static const uint8_t macho_header_fat[4] = {0xca, 0xfe, 0xba, 0xbe};
            if (memcmp(file_magic, macho_header_fat, 4) == 0)
                return true;
        }
        fclose(fp);
    }
    return false;
}

// Gets the SHA-1 hash of a file
bool get_file_sha1(const char *path, uint8_t *output_hash) {
    FILE *fp = fopen(path, "rb");
    if (fp != NULL) {
        CC_SHA1_CTX ctx;
        CC_SHA1_Init(&ctx);
        uint8_t file_read_buffer[512] = {0};
        int r = 0;
        while ((r = fread(file_read_buffer, 1, sizeof(file_read_buffer), fp)) > 0) {
            CC_SHA1_Update(&ctx, file_read_buffer, r);
        }
        CC_SHA1_Final(output_hash, &ctx);
        fclose(fp);
        return true;
    }
    return false;
}

bool bytes_to_hex(const uint8_t *bytes, size_t bytes_len, char *out_string, size_t out_string_len) {
    if ((bytes_len * 2) >= out_string_len)
        return false;
    out_string[0] = '\0';
    for (size_t i = 0; i < bytes_len; i++) {
        snprintf(out_string, out_string_len, "%s%02x", out_string, bytes[i]);
    }
    return true;
}
