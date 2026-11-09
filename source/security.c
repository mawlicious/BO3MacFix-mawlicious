#include "security.h"
#include <string.h>
#include <strings.h>

/* T7Patch's connectionless allowlist, evaluated on the ENGINE token rather
 * than a second tokenizer that might disagree about quotes or comments. */
bool bo3_security_command(const char *command, bool campaign) {
    static const char *const allowed[] = {
        "connectResponse", "statresponse", "LM", "disconnect", "loadoutResponse",
        "infoResponse", "statusResponse", "keyAuthorize", "error", "print",
        "fastrestart", "ping", "pinga", "steamAuthReq", "cfl",
        /* Mac has a distinct host-migration response handler. */
        "connectResponseMigration"
    };
    if (!command) return false;
    if (campaign && !strcasecmp(command, "requeststats")) return true;
    for (size_t i = 0; i < sizeof(allowed)/sizeof(*allowed); ++i)
        if (!strcasecmp(command, allowed[i])) return true;
    return false;
}

bool bo3_security_im(const uint8_t *data, int length) {
    /* Existing MacFix blocked e/m. Also enforce the header and T7's <2048
     * payload limit before the engine reads anything. Join request layout is
     * verified separately on Mac; don't reinterpret Windows structs here. */
    if (!data || length<2 || length-2>=2048 || data[0]!='1' || data[1]=='e' || data[1]=='m') return false;
    if (data[1]=='f') {
        uint32_t kind;
        if (length!=102) return false;
        memcpy(&kind,data+2,4);
        if (kind==0) return false;
    }
    return true;
}

bool bo3_security_lobby_type(const char *key, uint32_t value) {
    if (!key) return false;
    if (!strcasecmp(key, "lobbytype") || !strcasecmp(key, "srclobbytype") ||
        !strcasecmp(key, "destlobbytype")) return value <= 2;
    if (!strcasecmp(key,"clientcount") || !strcasecmp(key,"membercount")) return value<=18;
    if (!strcasecmp(key,"votecount")) return value<=216;
    if (!strcasecmp(key,"settingssize")) return value>0 && value<=0xc000;
    return true;
}

/* Bounds are checked before scanning; callers supply a private 4096-byte copy. */
bool bo3_security_ui_text(const char *source, char output[4096]) {
    if (!source) source = "";
    size_t n = strnlen(source, 4096);
    if (n == 4096) return false;
    memmove(output, source, n + 1);
    for (size_t i=0; i<n; ++i) {
        if (output[i]=='^' && output[i+1]=='B') {
            size_t j=i+2;
            while (j<n && output[j]!='^') ++j;
            if (j==n || j-i-2>=64) output[i]='.';
            else i=j;
        }
    }
    for (size_t i=0; i+1<n; ++i) {
        if (output[i]=='[' && output[i+1]=='{') {
            char *end=strstr(output+i+2,"}]");
            if (!end || (size_t)(end-output-i)>=256) output[i]=output[i+1]='.';
        }
    }
    return true;
}
bool bo3_security_model_path(const char *path) {
    if (!path) return false;
    size_t length=strnlen(path,4096);
    if (length==4096) return false;
    size_t segment=0;
    for (size_t i=0; i<length; ++i) {
        if (path[i]=='.') segment=0;
        else if (++segment>=64) return false;
    }
    return true;
}

bool bo3_security_message_type(int type) {
    return type>=0 && type<=32 && type!=15 && type!=22 && type!=23 && type!=24 && type!=30;
}
