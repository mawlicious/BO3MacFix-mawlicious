#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <Foundation/Foundation.h>

int main(int argc, char **argv) {
    NSTask *gameProc = [[NSTask alloc] init];
    // copy our current working directory and pass it onto the child process
    NSProcessInfo *currentProcess = [NSProcessInfo processInfo];
    NSString *pwd = [currentProcess arguments][0];
    gameProc.currentDirectoryURL = [[NSURL fileURLWithPath:pwd] URLByDeletingLastPathComponent];
    // set the game executable path
    gameProc.executableURL = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"CoDBlkOps3_Exe"];
    
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
            NSString *header = [NSString stringWithFormat:@"BO3MacFix-mawlicious session started %@\nNetwork password: %@; friends-only setting: %@\n", [NSDate date], [newEnvironment[@"BO3MACFIX_NETWORKPASSWORD"] length] ? @"enabled" : @"disabled", newEnvironment[@"BO3MACFIX_FRIENDSONLY"] ?: @"false"];
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
