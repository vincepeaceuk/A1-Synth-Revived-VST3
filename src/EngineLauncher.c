/* A private, cooperative translator for this instance's original engine.
 * No debugger authorization, system runtime edits, or shared Wine shutdown. */
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "RuntimeBuild.h"

extern char **environ;

static int reject(int status, const char *message) {
    fprintf(stderr, "A1 engine launcher: %s\n", message);
    return status;
}

static int matchesBuild(const char *path, const char *expected) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    unsigned char bytes[16384], digest[CC_SHA256_DIGEST_LENGTH];
    size_t size;
    while ((size = fread(bytes, 1, sizeof(bytes), file)) != 0)
        CC_SHA256_Update(&context, bytes, (CC_LONG)size);
    int valid = !ferror(file);
    fclose(file);
    CC_SHA256_Final(digest, &context);
    char hex[2 * CC_SHA256_DIGEST_LENGTH + 1];
    for (unsigned i = 0; i < sizeof(digest); ++i) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return valid && !strcmp(hex, expected);
}

static int path(char *output, size_t size, const char *resources, const char *name) {
    int length = snprintf(output, size, "%s/%s", resources, name);
    return length > 0 && (size_t)length < size;
}

static int windowsPath(char *output, size_t size, const char *unixPath) {
    int length = snprintf(output, size, "Z:%s", unixPath);
    if (length < 0 || (size_t)length >= size) return 0;
    for (char *p = output + 2; *p; ++p) if (*p == '/') *p = '\\';
    return 1;
}

static int clearTranslatorEnvironment(void) {
    /* unsetenv can relocate environ. Restart iteration after each removal. */
    for (;;) {
        const char *found = NULL;
        for (char **p = environ; *p; ++p) {
            if (!strncmp(*p, "X87_", 4) || !strncmp(*p, "ROSETTA_", 8) ||
                !strncmp(*p, "A1_X87_", 7) || !strncmp(*p, "DYLD_INSERT_LIBRARIES=", 22)) {
                found = *p;
                break;
            }
        }
        if (!found) return 1;
        size_t length = strcspn(found, "=");
        char *key = strndup(found, length);
        if (!key) return 0;
        int result = unsetenv(key);
        free(key);
        if (result) return 0;
    }
}

int main(int argc, char **argv) {
    int run = argc > 1 && !strcmp(argv[1], "--run");
    if (argc != (run ? 4 : 3)) return reject(64, "invalid startup arguments");
    char executable[PATH_MAX], resolved[PATH_MAX], resources[PATH_MAX];
    uint32_t capacity = sizeof(executable);
    if (_NSGetExecutablePath(executable, &capacity) || !realpath(executable, resolved))
        return reject(70, "cannot locate private engine resources");
    strcpy(resources, resolved);
    char *slash = strrchr(resources, '/');
    if (!slash) return reject(70, "invalid resource location");
    *slash = 0;
    char helper[PATH_MAX], dll[PATH_MAX], helperWindows[PATH_MAX + 3], dllWindows[PATH_MAX + 3];
    char translator[PATH_MAX], library[PATH_MAX], privateWine[PATH_MAX];
    if (!path(helper, sizeof(helper), resources, "a1-helper.exe") ||
        !path(dll, sizeof(dll), resources, "a1.dll") ||
        !windowsPath(helperWindows, sizeof(helperWindows), helper) ||
        !windowsPath(dllWindows, sizeof(dllWindows), dll) ||
        !path(translator, sizeof(translator), resources, "x87sidecar") ||
        !path(library, sizeof(library), resources, "a1-coop.dylib") ||
        !path(privateWine, sizeof(privateWine), resources, "a1-wineloader"))
        return reject(64, "resource path is too long");
    if (strcmp(argv[run ? 2 : 1], helperWindows) || strcmp(argv[run ? 3 : 2], dllWindows))
        return reject(64, "startup arguments do not match this instance's resources");
    int socketType;
    socklen_t socketSize = sizeof(socketType);
    struct sockaddr_un address;
    socklen_t addressSize = sizeof(address);
    if (getsockopt(STDIN_FILENO, SOL_SOCKET, SO_TYPE, &socketType, &socketSize) ||
        socketType != SOCK_STREAM || getsockname(STDIN_FILENO, (struct sockaddr *)&address, &addressSize) ||
        address.sun_family != AF_UNIX) return reject(64, "a private engine socket is required");
    if (!matchesBuild(A1_WINE_SOURCE, A1_WINE_SHA256) ||
        !matchesBuild(A1_NTDLL_SOURCE, A1_NTDLL_SHA256))
        return reject(78, "CrossOver changed; rebuild this A1 preview for the installed version");
    if (access(translator, X_OK) || access(privateWine, X_OK) || access(library, R_OK) ||
        access(helper, R_OK) || access(dll, R_OK)) return reject(78, "private engine resources are missing");

    if (run) {
        char expected[64];
        snprintf(expected, sizeof(expected), "x87sidecar.%ld", (long)getpid());
        const char *bootstrap = getenv("X87_SIDECAR_BOOTSTRAP");
        const char *aot = getenv("ROSETTA_DISABLE_AOT");
        if (!bootstrap || strcmp(bootstrap, expected) || !aot || strcmp(aot, "1"))
            return reject(78, "accelerated engine startup was not established");
        if (unsetenv("A1_X87_READY") || setenv("DYLD_INSERT_LIBRARIES", library, 1) ||
            setenv("WINEARCH", "wow64", 1) ||
            setenv("WINELOADER", A1_WINE_SOURCE, 1) || setenv("CX_WINELOADER", A1_WINE_SOURCE, 1) ||
            dup2(STDIN_FILENO, STDOUT_FILENO) < 0) return reject(71, "cannot prepare engine startup");
        /* The prepared Wine image exports wine_main_preload_info, retaining
         * this PID and hook. argv[0] locates the installed adjacent ntdll.so. */
        char *target[] = {A1_WINE_SOURCE, helperWindows, dllWindows, NULL};
        execv(privateWine, target);
    } else {
        if (!clearTranslatorEnvironment() ||
            setenv("X87_ENABLE_FMA_CONTRACT", "0", 1) || setenv("X87_IGNORE_FTZ_DAZ", "1", 1) ||
            setenv("X87_NO_DECODE_HOOK", "1", 1) || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
            return reject(71, "cannot prepare translator startup");
        /* Its detached sidecar keeps this log descriptor. Only the original
         * PID restores binary stdout from its duplex socket in --run above. */
        char *target[] = {translator, "--cooperative", resolved, "--run", helperWindows, dllWindows, NULL};
        execv(translator, target);
    }
    return reject(72, "cannot execute the private engine runtime");
}
