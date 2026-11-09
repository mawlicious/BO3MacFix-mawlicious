#include "security.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    char out[4096], text[4097];
    assert(bo3_security_ui_text("normal ^Bjump^ [{+use}]",out));
    assert(!strcmp(out,"normal ^Bjump^ [{+use}]"));
    assert(bo3_security_ui_text("^Bunterminated",out) && out[0]=='.');
    assert(bo3_security_ui_text("[{unterminated",out) && out[0]=='.' && out[1]=='.');
    memset(text,'x',sizeof(text));text[4096]=0;
    assert(!bo3_security_ui_text(text,out));
    text[4095]=0;assert(bo3_security_ui_text(text,out));
    memset(text,'a',64);text[64]=0;assert(!bo3_security_model_path(text));
    text[63]=0;assert(bo3_security_model_path(text));
    assert(bo3_security_model_path("player.score.value"));
    assert(!bo3_security_model_path(NULL));
    assert(bo3_security_lobby_type("clientcount",18));
    assert(!bo3_security_lobby_type("clientcount",19));
    assert(!bo3_security_lobby_type("membercount",UINT32_MAX));
    assert(!bo3_security_lobby_type("votecount",217));
    assert(!bo3_security_lobby_type("settingssize",0));
    assert(bo3_security_lobby_type("settingssize",0xc000));
    assert(!bo3_security_lobby_type("settingssize",0xc001));
    assert(bo3_security_message_type(0) && bo3_security_message_type(16));
    assert(!bo3_security_message_type(-1) && !bo3_security_message_type(33));
    assert(!bo3_security_message_type(15) && !bo3_security_message_type(30));
    puts("PASS: UI directives, model path boundaries, lobby count limits");
}
