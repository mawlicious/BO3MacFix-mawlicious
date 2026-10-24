#include "memory_io.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BO3_MEMORY_IO_TEST
#include "dyld-interposing.h"
#endif

/* Large cluster sizes on external volumes become equally large libc stream
 * buffers. Asset archives are already read in engine-sized blocks. Keep a
 * normal fully buffered stream, without a megabyte of read-ahead per handle. */
int bo3_limit_read_buffer(FILE *stream, const char *mode) {
    int saved_errno = errno, changed = 0;
    if (stream && mode && (!strcmp(mode, "r") || !strcmp(mode, "rb"))) {
        struct stat st;
        if (!fstat(fileno(stream), &st) && S_ISREG(st.st_mode) &&
            st.st_blksize > 65536)
            changed = setvbuf(stream, NULL, _IOFBF, 65536) == 0;
    }
    errno = saved_errno;
    return changed;
}

#ifndef BO3_MEMORY_IO_TEST
static uintptr_t archive_open_return;

static FILE *buffered_fdopen(int descriptor, const char *mode) {
    FILE *stream = fdopen(descriptor, mode);
    /* Only the game's verified archive-opening call site is changed. Other
     * users of fdopen (including libraries) retain their original buffering. */
    if ((uintptr_t)__builtin_return_address(0) == archive_open_return)
        bo3_limit_read_buffer(stream, mode);
    return stream;
}
DYLD_INTERPOSE(buffered_fdopen, fdopen)

int bo3_install_memory_io(uint64_t base) {
    const char *enabled = getenv("BO3MACFIX_MEMORY_IO");
    if (enabled && !strcmp(enabled, "0")) return 0;
    /* This call is made immediately after opening the descriptor, before
     * the FILE is exposed to any read or seek. Fail closed on other builds. */
    static const unsigned char expected[] = {0xe8, 0xa4, 0xb6, 0x51, 0x01};
    if (memcmp((const void *)(base + 0x10be0f), expected, sizeof(expected)))
        return -1;
    archive_open_return = base + 0x10be14;
    return 1;
}
#endif
