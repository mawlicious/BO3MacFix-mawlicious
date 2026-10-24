#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exercise the policy on any filesystem, using the real stream operations. */
static int block_hint = 1024 * 1024;
static int fake_stat(int fd, struct stat *st) {
    int result = fstat(fd, st);
    if (!result) st->st_blksize = block_hint;
    return result;
}
#define fstat fake_stat
#define BO3_MEMORY_IO_TEST
#include "../source/memory_io.c"
#undef fstat

int main(void) {
    char path[] = "/tmp/bo3-memory-io-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    unsigned char data[100003];
    for (size_t i=0;i<sizeof(data);i++) data[i]=(unsigned char)(i*37+11);
    assert(write(fd,data,sizeof(data)) == sizeof(data));
    close(fd);
    FILE *stream=fopen(path,"rb");
    assert(stream);
    errno=EDOM;
    assert(bo3_limit_read_buffer(stream,"rb") == 1 && errno == EDOM);
#ifdef __APPLE__
    assert(stream->_bf._size == 65536);
#endif
    unsigned char output[100003];
    assert(fread(output,1,sizeof(output),stream) == sizeof(output));
    assert(!memcmp(output,data,sizeof(data)));
    assert(fseeko(stream,65000,SEEK_SET) == 0);
    assert(fread(output,1,1234,stream) == 1234);
    assert(!memcmp(output,data+65000,1234));
    assert(fseeko(stream,-20,SEEK_END) == 0);
    assert(fread(output,1,30,stream) == 20 && feof(stream));
    fclose(stream);
    stream=fopen(path,"r+");
    assert(stream && bo3_limit_read_buffer(stream,"r+") == 0);
    assert(bo3_limit_read_buffer(stream,"w") == 0);
    assert(bo3_limit_read_buffer(stream,NULL) == 0);
    assert(bo3_limit_read_buffer(NULL,"r") == 0);
    block_hint=4096;
    assert(bo3_limit_read_buffer(stream,"r") == 0);
    fclose(stream);
    int pipes[2]; assert(pipe(pipes) == 0);
    stream=fdopen(pipes[0],"r"); block_hint=1024*1024;
    assert(stream && bo3_limit_read_buffer(stream,"r") == 0);
    fclose(stream);close(pipes[1]);unlink(path);
    puts("memory_io: bounded buffers preserve reads, seeks, EOF and errno; other streams unchanged");
}
