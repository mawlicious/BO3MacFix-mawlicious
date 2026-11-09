#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool bo3_security_command(const char *command, bool campaign);
bool bo3_security_im(const uint8_t *data, int length);
bool bo3_security_lobby_type(const char *key, uint32_t value);
int bo3_install_security(uint64_t base);
