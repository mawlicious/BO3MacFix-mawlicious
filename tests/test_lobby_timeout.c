#include "lobby_timeout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef REAL_DOBBY
static int fail_write;
int DobbyCodePatch(void *address, unsigned char *buffer, unsigned int length) {
    assert(length == 4);
    if (fail_write) return -1;
    memcpy(address, buffer, length);
    return 0;
}
#endif
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *fp = fopen(argv[1], "rb"); assert(fp);
    assert(fseek(fp, 0, SEEK_END) == 0);
    long length = ftell(fp); assert(length > 0x131601a);
    rewind(fp);
    unsigned char *original = malloc(length); assert(original);
    assert(fread(original, 1, length, fp) == (size_t)length); fclose(fp);
    unsigned char *image = mmap(NULL, length, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0); assert(image != MAP_FAILED);
    memcpy(image, original, length);
    image[0xda1930] ^= 1;
    assert(install_lobby_info_timeout((uintptr_t)image) == -1);
    assert(memcmp(image + 0x1315ff6, original + 0x1315ff6, 36) == 0);
    memcpy(image, original, length);
    image[0x1315ff6] ^= 1;
    assert(install_lobby_info_timeout((uintptr_t)image) == -1);
    memcpy(image, original, length);
#ifndef REAL_DOBBY
    fail_write = 1;
    assert(install_lobby_info_timeout((uintptr_t)image) == -2);
    assert(memcmp(image, original, length) == 0);
    fail_write = 0;
#else
    assert(mprotect(image, (length + getpagesize()-1) & ~(getpagesize()-1), PROT_READ|PROT_EXEC) == 0);
#endif
    assert(install_lobby_info_timeout((uintptr_t)image) == 1);
    unsigned int ms; memcpy(&ms, image + 0x1315ffc, sizeof(ms)); assert(ms == 30000);
    memcpy(original + 0x1315ffc, &ms, sizeof(ms));
    assert(memcmp(image, original, length) == 0);
    puts("PASS: actual executable matched; only join-info default changed to 30000; unsupported bytes rejected.");
    munmap(image, length); free(original);
}
