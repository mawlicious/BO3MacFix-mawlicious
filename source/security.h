#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool bo3_security_command(const char *command, bool campaign);
bool bo3_security_im(const uint8_t *data, int length);
bool bo3_security_lobby_type(const char *key, uint32_t value);
int bo3_install_security(uint64_t base);
bool bo3_security_ui_text(const char *source, char output[4096]);
bool bo3_security_model_path(const char *path);
int bo3_install_security_ui(uint64_t base);
bool bo3_security_message_type(int type);

int bo3_install_security_steam(uint64_t base,void *matchmaking,void *networking);

int bo3_security_dvars(uint64_t base);
