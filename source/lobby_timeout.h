#ifndef BO3MACFIX_LOBBY_TIMEOUT_H
#define BO3MACFIX_LOBBY_TIMEOUT_H
#include <stdint.h>

/* 1: applied, -1: unsupported instructions, -2: write/verification failed. */
int install_lobby_info_timeout(uint64_t game_base);
#endif
