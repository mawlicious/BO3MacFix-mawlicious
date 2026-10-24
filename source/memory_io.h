#pragma once
#include <stdint.h>
#include <stdio.h>

/* Configure newly opened, read-only regular streams before their first I/O. */
int bo3_limit_read_buffer(FILE *stream, const char *mode);
int bo3_install_memory_io(uint64_t base);
