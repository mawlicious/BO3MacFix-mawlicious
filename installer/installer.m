#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/file.h>

static NSString *const Name = @"BO3MacFix-mawlicious";

static BOOL regularFile(NSString *path) {
    struct stat st;
    return lstat(path.fileSystemRepresentation, &st) == 0 && S_ISREG(st.st_mode);
}

static BOOL clearQuarantine(NSString *path, NSError **error) {
    if (removexattr(path.fileSystemRepresentation, "com.apple.quarantine", 0) == 0 || errno == ENOATTR)
        return YES;
    if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
    return NO;
}

static NSString *digest(NSString *path) {
    FILE *file = fopen(path.fileSystemRepresentation, "rb");
    if (!file) return nil;
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buffer[16384], result[CC_SHA256_DIGEST_LENGTH];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file))) CC_SHA256_Update(&ctx, buffer, (CC_LONG)count);
    BOOL ok = !ferror(file);
    fclose(file);
    if (!ok) return nil;
    CC_SHA256_Final(result, &ctx);
    NSMutableString *hex = [NSMutableString string];
    for (size_t i = 0; i < sizeof(result); i++) [hex appendFormat:@"%02x", result[i]];
    return hex;
}

static BOOL validatePayload(NSString *directory, NSError **error) {
    for (NSString *name in @[@"AppBundleExe", @"BO3MacFix-mawlicious.dylib"])
        if (!regularFile([directory stringByAppendingPathComponent:name])) return NO;
    NSString *manifest = [NSString stringWithContentsOfFile:[directory stringByAppendingPathComponent:@"ASSET_SHA256SUMS"] encoding:NSUTF8StringEncoding error:error];
    if (!manifest) return NO;
    NSMutableSet *required = [NSMutableSet setWithArray:@[@"zone/core_patch.ff", @"zone/mp_patch.ff", @"zone/zm_patch.ff"]];
    for (NSString *line in [manifest componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]]) {
        if (!line.length) continue;
        if (line.length < 67 || ![[line substringWithRange:NSMakeRange(64, 2)] isEqualToString:@"  "]) return NO;
        NSString *relative = [line substringFromIndex:66];
        if (![required containsObject:relative]) return NO;
        NSString *path = [directory stringByAppendingPathComponent:relative];
        if (!regularFile(path) || ![[line substringToIndex:64] isEqualToString:digest(path)]) return NO;
        [required removeObject:relative];
    }
    return required.count == 0;
}

/* Stage next to the destination and rename only when the copy, permissions,
 * quarantine removal and backup have succeeded. The active launcher remains
 * usable if any preflight operation fails. */
static BOOL replaceLauncher(NSString *source, NSString *target, NSError **error) {
    NSFileManager *files = [NSFileManager defaultManager];
    NSString *temporary = [[target stringByDeletingLastPathComponent] stringByAppendingPathComponent:[@".mawlicious-launcher-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    BOOL ok = [files copyItemAtPath:source toPath:temporary error:error];
    if (ok && chmod(temporary.fileSystemRepresentation, 0755) != 0) {
        if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
        ok = NO;
    }
    if (ok) ok = clearQuarantine(temporary, error);
    if (ok && rename(temporary.fileSystemRepresentation, target.fileSystemRepresentation) != 0) {
        if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
        ok = NO;
    }
    if (!ok) [files removeItemAtPath:temporary error:nil];
    return ok;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        BOOL uninstall = argc == 2 && strcmp(argv[1], "--uninstall") == 0;
        if (argc > 2 || (argc == 2 && !uninstall && strcmp(argv[1], "--install") != 0)) {
            fprintf(stderr, "Usage: Install_BO3MacFix-mawlicious [--install|--uninstall]\n");
            return 2;
        }
        char executable[PATH_MAX], resolved[PATH_MAX];
        uint32_t size = sizeof(executable);
        if (_NSGetExecutablePath(executable, &size) || !realpath(executable, resolved)) {
            perror("Finding installer location");
            return 1;
        }
        NSString *directory = [[NSString stringWithUTF8String:resolved] stringByDeletingLastPathComponent];
        if (![directory.lastPathComponent isEqualToString:Name]) {
            fprintf(stderr, "Keep the complete BO3MacFix-mawlicious folder beside CoDBlkOps3.app.\n");
            return 1;
        }
        NSString *macos = [[directory stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"CoDBlkOps3.app/Contents/MacOS"];
        NSString *target = [macos stringByAppendingPathComponent:@"AppBundleExe"];
        NSString *backup = [macos stringByAppendingPathComponent:@"AppBundleExe_before_mawlicious"];
        if (!uninstall && !regularFile(target)) {
            fprintf(stderr, "Game launcher not found. Place BO3MacFix-mawlicious beside CoDBlkOps3.app and quit the game before installing.\n");
            return 1;
        }
        NSString *lockPath = [macos stringByAppendingPathComponent:@".mawlicious-install.lock"];
        int lock = open(lockPath.fileSystemRepresentation, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) {
            if (lock >= 0) close(lock);
            fprintf(stderr, "Another installer is running or the game directory is not writable.\n");
            return 1;
        }
        /* The descriptor stays open until process exit, including error paths. */
        NSError *error = nil;
        NSFileManager *files = [NSFileManager defaultManager];
        if (uninstall) {
            if (!regularFile(backup)) {
                fprintf(stderr, "No pre-mawlicious launcher backup found; nothing was changed.\n");
                return 1;
            }
            if (!replaceLauncher(backup, target, &error)) {
                fprintf(stderr, "Could not restore launcher: %s\n", error.localizedDescription.UTF8String);
                return 1;
            }
            puts("Previous launcher restored. Mod files, caches and logs were retained.");
            return 0;
        }
        if (!validatePayload(directory, &error)) {
            fprintf(stderr, "Release files are missing or damaged. Extract the entire release folder again. %s\n", error ? error.localizedDescription.UTF8String : "");
            return 1;
        }
        for (NSString *name in @[@"AppBundleExe", @"BO3MacFix-mawlicious.dylib"]) {
            NSString *path = [directory stringByAppendingPathComponent:name];
            mode_t mode = [name isEqualToString:@"AppBundleExe"] ? 0755 : 0644;
            if (!clearQuarantine(path, &error) || chmod(path.fileSystemRepresentation, mode)) {
                fprintf(stderr, "Could not prepare native binaries; the game launcher was not changed.\n");
                return 1;
            }
        }
        /* The first backup is immutable across reinstalls. Refuse non-regular
         * backup paths instead of following links or silently losing rollback. */
        if ([files fileExistsAtPath:backup]) {
            if (!regularFile(backup)) {
                fprintf(stderr, "Launcher backup is not a regular file; nothing was changed.\n");
                return 1;
            }
        } else {
            NSString *legacy = [macos stringByAppendingPathComponent:@"AppBundleExe_backup"];
            NSString *source = regularFile(legacy) ? legacy : target;
            NSString *stagedBackup = [macos stringByAppendingPathComponent:[@".mawlicious-backup-" stringByAppendingString:NSUUID.UUID.UUIDString]];
            if (![files copyItemAtPath:source toPath:stagedBackup error:&error]) {
                fprintf(stderr, "Could not back up launcher: %s\n", error.localizedDescription.UTF8String);
                return 1;
            }
            /* Publish only a complete backup. The installation lock prevents
             * concurrent installers from overwriting this initial baseline.
             * Rename also works on external volumes without hard-link support. */
            if (rename(stagedBackup.fileSystemRepresentation, backup.fileSystemRepresentation) != 0) {
                int saved = errno;
                [files removeItemAtPath:stagedBackup error:nil];
                fprintf(stderr, "Could not publish launcher backup: %s\n", strerror(saved));
                return 1;
            }
        }
        if (!replaceLauncher([directory stringByAppendingPathComponent:@"AppBundleExe"], target, &error)) {
            fprintf(stderr, "Could not install launcher: %s\n", error.localizedDescription.UTF8String);
            return 1;
        }
        puts("BO3MacFix-mawlicious installed. Launch Black Ops III normally through Steam.");
        printf("Session logs: %s/logs\n", directory.fileSystemRepresentation);
        return 0;
    }
}
