#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>

static NSString *sha256Hex(NSData *data) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:64];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) [hex appendFormat:@"%02x", digest[i]];
    return hex;
}

static unsigned long long hexValue(NSString *s, BOOL *ok) {
    NSScanner *scanner = [NSScanner scannerWithString:s];
    unsigned long long v = 0;
    *ok = [scanner scanHexLongLong:&v] && scanner.atEnd;
    return v;
}

/* Rosetta 2 discards its ahead-of-time translation of the whole game as soon
 * as the mod patches game code at runtime, leaving every frame on its JIT.
 * Instead, run a copy with all patches applied on disk (generated here from
 * the user's own executable and BO3MacFix-mawlicious/static-patch.txt), so
 * Rosetta translates it ahead of time once and keeps that translation.
 * Returns the executable name to launch; falls back to the original. */
static NSString *prepareStaticExecutable(NSURL *macos, NSURL *fix, NSMutableString *notes) {
    NSString *env = [NSProcessInfo processInfo].environment[@"BO3MACFIX_STATIC"];
    if ([env isEqualToString:@"0"]) { [notes appendString:@"Static executable: disabled by BO3MACFIX_STATIC=0.\n"]; return @"CoDBlkOps3_Exe"; }
    NSString *patchPath = [fix URLByAppendingPathComponent:@"static-patch.txt"].path;
    NSString *patch = [NSString stringWithContentsOfFile:patchPath encoding:NSUTF8StringEncoding error:nil];
    if (!patch) { [notes appendString:@"Static executable: no static-patch.txt; using runtime patching.\n"]; return @"CoDBlkOps3_Exe"; }
    NSString *originalPath = [macos URLByAppendingPathComponent:@"CoDBlkOps3_Exe"].path;
    NSString *staticPath = [macos URLByAppendingPathComponent:@"CoDBlkOps3_Exe_mawlicious"].path;
    NSString *stampPath = [staticPath stringByAppendingString:@".stamp"];
    NSData *original = [NSData dataWithContentsOfFile:originalPath options:NSDataReadingMappedIfSafe error:nil];
    if (!original) { [notes appendString:@"Static executable: cannot read CoDBlkOps3_Exe.\n"]; return @"CoDBlkOps3_Exe"; }
    NSString *originalSha = sha256Hex(original), *wantOriginal = nil, *wantPatched = nil;
    NSMutableArray<NSArray<NSString *> *> *ops = [NSMutableArray array];
    for (NSString *line in [patch componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
        NSArray<NSString *> *f = [line componentsSeparatedByString:@" "];
        if (line.length == 0 || [line hasPrefix:@"#"]) continue;
        if ([f[0] isEqualToString:@"original-sha256"] && f.count == 2) wantOriginal = f[1];
        else if ([f[0] isEqualToString:@"patched-sha256"] && f.count == 2) wantPatched = f[1];
        else [ops addObject:f];
    }
    if (![originalSha isEqualToString:wantOriginal]) {
        [notes appendFormat:@"Static executable: CoDBlkOps3_Exe changed (sha256 %@); using runtime patching.\n", originalSha];
        return @"CoDBlkOps3_Exe";
    }
    NSString *stamp = [NSString stringWithContentsOfFile:stampPath encoding:NSUTF8StringEncoding error:nil];
    NSArray<NSString *> *stampFields = [stamp componentsSeparatedByString:@" "];
    if (stampFields.count == 3 && [stampFields[0] isEqualToString:originalSha] && [stampFields[1] isEqualToString:wantPatched]) {
        NSData *existing = [NSData dataWithContentsOfFile:staticPath options:NSDataReadingMappedIfSafe error:nil];
        if (existing && [sha256Hex(existing) isEqualToString:[stampFields[2] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]]) {
            [notes appendString:@"Static executable: verified CoDBlkOps3_Exe_mawlicious (Rosetta AOT mode).\n"];
            return @"CoDBlkOps3_Exe_mawlicious";
        }
    }
    /* (Re)build: apply writes and copies from the original, verify, sign. */
    NSMutableData *image = [original mutableCopy];
    uint8_t *bytes = image.mutableBytes;
    const uint8_t *source = original.bytes;
    for (NSArray<NSString *> *f in ops) {
        BOOL ok1 = NO, ok2 = YES, ok3 = YES;
        if ([f[0] isEqualToString:@"w"] && f.count == 3 && f[2].length % 2 == 0) {
            unsigned long long off = hexValue(f[1], &ok1), n = f[2].length / 2;
            if (!ok1 || off + n > image.length) goto bad;
            for (unsigned long long i = 0; i < n; i++) {
                unsigned int b; if (sscanf([f[2] substringWithRange:NSMakeRange(i * 2, 2)].UTF8String, "%2x", &b) != 1) goto bad;
                bytes[off + i] = (uint8_t)b;
            }
        } else if ([f[0] isEqualToString:@"c"] && f.count == 4) {
            unsigned long long dst = hexValue(f[1], &ok1), src = hexValue(f[2], &ok2), n = hexValue(f[3], &ok3);
            if (!ok1 || !ok2 || !ok3 || dst + n > image.length || src + n > original.length) goto bad;
            memcpy(bytes + dst, source + src, n);
        } else goto bad;
    }
    if (![sha256Hex(image) isEqualToString:wantPatched]) {
        [notes appendString:@"Static executable: patched image hash mismatch; using runtime patching.\n"];
        return @"CoDBlkOps3_Exe";
    }
    {
        NSString *temporary = [staticPath stringByAppendingFormat:@".tmp-%d", getpid()];
        NSFileManager *files = [NSFileManager defaultManager];
        [files removeItemAtPath:temporary error:nil];
        if (![image writeToFile:temporary atomically:NO] ||
            ![files setAttributes:@{NSFilePosixPermissions: @0755} ofItemAtPath:temporary error:nil]) {
            [files removeItemAtPath:temporary error:nil];
            [notes appendString:@"Static executable: cannot write the patched copy; using runtime patching.\n"];
            return @"CoDBlkOps3_Exe";
        }
        /* The original's Developer ID signature no longer matches; sign ad hoc. */
        NSTask *codesign = [[NSTask alloc] init];
        codesign.executableURL = [NSURL fileURLWithPath:@"/usr/bin/codesign"];
        codesign.arguments = @[@"--force", @"--sign", @"-", temporary];
        codesign.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        codesign.standardError = [NSFileHandle fileHandleWithNullDevice];
        if (![codesign launchAndReturnError:nil]) { [files removeItemAtPath:temporary error:nil]; goto signfail; }
        [codesign waitUntilExit];
        if (codesign.terminationStatus != 0) { [files removeItemAtPath:temporary error:nil]; goto signfail; }
        NSData *signedImage = [NSData dataWithContentsOfFile:temporary];
        if (!signedImage || rename(temporary.fileSystemRepresentation, staticPath.fileSystemRepresentation) != 0) {
            [files removeItemAtPath:temporary error:nil];
            [notes appendString:@"Static executable: cannot install the patched copy; using runtime patching.\n"];
            return @"CoDBlkOps3_Exe";
        }
        NSString *newStamp = [NSString stringWithFormat:@"%@ %@ %@\n", originalSha, wantPatched, sha256Hex(signedImage)];
        [newStamp writeToFile:stampPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
        [notes appendString:@"Static executable: built CoDBlkOps3_Exe_mawlicious (Rosetta translates it once on this launch).\n"];
        return @"CoDBlkOps3_Exe_mawlicious";
    }
signfail:
    [notes appendString:@"Static executable: ad hoc signing failed; using runtime patching.\n"];
    return @"CoDBlkOps3_Exe";
bad:
    [notes appendString:@"Static executable: malformed static-patch.txt; using runtime patching.\n"];
    return @"CoDBlkOps3_Exe";
}

int main(int argc, char **argv) {
    NSTask *gameProc = [[NSTask alloc] init];
    // copy our current working directory and pass it onto the child process
    NSProcessInfo *currentProcess = [NSProcessInfo processInfo];
    NSString *pwd = [currentProcess arguments][0];
    gameProc.currentDirectoryURL = [[NSURL fileURLWithPath:pwd] URLByDeletingLastPathComponent];
    NSMutableString *launchNotes = [NSMutableString string];
    NSURL *fixURL = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix-mawlicious" isDirectory:YES];
    NSString *executableName = prepareStaticExecutable(gameProc.currentDirectoryURL, fixURL, launchNotes);
    // set the game executable path
    gameProc.executableURL = [gameProc.currentDirectoryURL URLByAppendingPathComponent:executableName];
    
    // add the fix dylib to DYLD_INSERT_LIBRARIES of the new process
    NSURL *dylibUrl = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix-mawlicious/BO3MacFix-mawlicious.dylib"];
    NSMutableDictionary *newEnvironment = [[currentProcess environment] mutableCopy];
    // if we already have dyld_insert_libraries set (by Steam), append our dylib to that
    if ([[currentProcess environment] objectForKey:@"DYLD_INSERT_LIBRARIES"]) {
        newEnvironment[@"DYLD_INSERT_LIBRARIES"] = [NSString stringWithFormat:@"%@:%@",[dylibUrl path],[currentProcess environment][@"DYLD_INSERT_LIBRARIES"]];
    } else { // otherwise just set it by itself
        newEnvironment[@"DYLD_INSERT_LIBRARIES"] = [dylibUrl path];
    }
    newEnvironment[@"SteamAppId"] = @"311210";
    // parse our arguments and figure out which ones we send to the new process vs which ones we 
    NSMutableArray *newArguments = [[NSMutableArray alloc] init];
    for (int i = 1; i < argc; i++) {
        if (strcasecmp(argv[i], "--password") == 0 && i + 1 < argc)
            newEnvironment[@"BO3MACFIX_NETWORKPASSWORD"] = [NSString stringWithUTF8String:argv[++i]];
        else if (strcasecmp(argv[i], "--playername") == 0 && i + 1 < argc)
            newEnvironment[@"BO3MACFIX_PLAYERNAME"] = [NSString stringWithUTF8String:argv[++i]];
        else if (strcasecmp(argv[i], "--friendsonly") == 0)
            newEnvironment[@"BO3MACFIX_FRIENDSONLY"] = @"true";
        else
            [newArguments addObject:[NSString stringWithUTF8String:argv[i]]];
    }
    gameProc.environment = newEnvironment;
    gameProc.arguments = newArguments;

    // The game's console log is overwritten on each launch. Capture both game
    // logging (forwarded by the dylib) and mod diagnostics in a unique session.
    NSURL *logsURL = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix-mawlicious/logs" isDirectory:YES];
    NSError *logError = nil;
    NSFileManager *files = [NSFileManager defaultManager];
    NSFileHandle *sessionLog = nil;
    if ([files createDirectoryAtURL:logsURL withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions: @0700} error:&logError]) {
        NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
        formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        formatter.dateFormat = @"yyyy-MM-dd_HH-mm-ss";
        NSString *logName = [NSString stringWithFormat:@"session-%@-%d.log", [formatter stringFromDate:[NSDate date]], getpid()];
        NSURL *logURL = [logsURL URLByAppendingPathComponent:logName];
        if ([files createFileAtPath:logURL.path contents:nil attributes:@{NSFilePosixPermissions: @0600}])
            sessionLog = [NSFileHandle fileHandleForWritingAtPath:logURL.path];
        if (sessionLog) {
            NSString *header = [NSString stringWithFormat:@"BO3MacFix-mawlicious session started %@\n%@Network password: %@; friends-only setting: %@\n", [NSDate date], launchNotes, [newEnvironment[@"BO3MACFIX_NETWORKPASSWORD"] length] ? @"enabled" : @"disabled", newEnvironment[@"BO3MACFIX_FRIENDSONLY"] ?: @"false"];
            [sessionLog writeData:[header dataUsingEncoding:NSUTF8StringEncoding]];
            gameProc.standardOutput = sessionLog;
            gameProc.standardError = sessionLog;
            NSLog(@"BO3MacFix-mawlicious session log: %@", logURL.path);
        }
    }
    if (!sessionLog)
        NSLog(@"Could not open BO3MacFix-mawlicious session log: %@", logError ?: @"file creation failed");
    
    //NSLog(@"Launching game...\n");
    //NSLog(@"  Executable: %@\n", gameProc.executableURL);
    //NSLog(@"  Working Directory: %@\n", gameProc.currentDirectoryURL);
    //NSLog(@"  DYLD_INSERT_LIBRARIES: %@\n", newEnvironment[@"DYLD_INSERT_LIBRARIES"]);
    // launch the game executable
    [gameProc launch];
    [gameProc waitUntilExit];
    if (sessionLog) {
        NSString *footer = [NSString stringWithFormat:@"\nBO3MacFix-mawlicious session ended %@; game exit status %d\n", [NSDate date], gameProc.terminationStatus];
        [sessionLog writeData:[footer dataUsingEncoding:NSUTF8StringEncoding]];
        [sessionLog closeFile];
    }
    return gameProc.terminationStatus;
}
