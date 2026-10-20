#include "pd_ra.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <stdio.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

static void copy_token(const char* json, char* token, size_t tokenLen) {
    const char* p = strstr(json, "\"Token\"");
    if (!p) return;
    p = strchr(p, ':');
    if (!p) return;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < tokenLen) token[i++] = *p++;
    token[i] = 0;
}

static int success_true(const char* json) {
    const char* p = strstr(json, "\"Success\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ') p++;
    return !strncmp(p, "true", 4);
}

static void copy_error(const char* json, char* err, size_t errLen) {
    const char* p = strstr(json, "\"Error\"");
    if (!p) {
        snprintf(err, errLen, "RetroAchievements refused the sign-in.");
        return;
    }
    p = strchr(p, ':');
    if (!p) return;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < errLen) err[i++] = *p++;
    err[i] = 0;
}

#ifndef _WIN32
static void url_encode(const char* src, char* dst, size_t dstLen) {
    static const char* hex = "0123456789ABCDEF";
    size_t i = 0;
    for (; src && *src && i + 4 < dstLen; src++) {
        unsigned char c = (unsigned char) *src;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') dst[i++] = (char) c;
        else {
            dst[i++] = '%';
            dst[i++] = hex[c >> 4];
            dst[i++] = hex[c & 15];
        }
    }
    dst[i] = 0;
}
#endif

static bool finish_login(const char* buf, char* token, size_t tokenLen, char* err, size_t errLen) {
    token[0] = 0;
    if (!success_true(buf)) {
        if (!buf[0]) snprintf(err, errLen, "No answer from RetroAchievements.");
        else copy_error(buf, err, errLen);
        return false;
    }
    copy_token(buf, token, tokenLen);
    if (!token[0]) {
        snprintf(err, errLen, "RetroAchievements sent no token.");
        return false;
    }
    snprintf(err, errLen, "Signed in.");
    return true;
}

#ifdef _WIN32
bool pd_ra_login(const char* user, const char* pass, char* token, size_t tokenLen, char* err, size_t errLen) {
    char uarg[96], parg[96];
    snprintf(uarg, sizeof(uarg), "u=%s", user);
    snprintf(parg, sizeof(parg), "p=%s", pass);
    char cmd[640];
    snprintf(cmd, sizeof(cmd),
             "curl -sS -m 20 -A \"PokeDaisy/1 (Windows) rcheevos/12.5\" --data-urlencode r=login --data-urlencode \"%s\" "
             "--data-urlencode \"%s\" https://retroachievements.org/dorequest.php",
             uarg, parg);
    FILE* pipe = _popen(cmd, "r");
    if (!pipe) {
        snprintf(err, errLen, "Sign-in needs curl on this console.");
        return false;
    }
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, pipe);
    buf[n] = 0;
    _pclose(pipe);
    return finish_login(buf, token, tokenLen, err, errLen);
}
#else

// libcurl is on the console (/usr/lib). The curl program is not.
#define CURLOPT_TIMEOUT 13
#define CURLOPT_POSTFIELDS 10015
#define CURLOPT_USERAGENT 10018
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_POST 47
#define CURLOPT_FOLLOWLOCATION 52
#define CURLOPT_URL 10002
#define CURLOPT_NOSIGNAL 99
#define CURLOPT_ERRORBUFFER 10010

struct curl_easy;
typedef size_t (*pd_curl_write)(char* ptr, size_t size, size_t nmemb, void* userdata);

struct curl_api {
    void* lib;
    struct curl_easy* (*easy_init)(void);
    int (*easy_setopt)(struct curl_easy*, int, ...);
    int (*easy_perform)(struct curl_easy*);
    void (*easy_cleanup)(struct curl_easy*);
    const char* (*easy_strerror)(int);
    int (*global_init)(long);
};

struct curl_body {
    size_t used;
    char buf[1024];
};

static size_t curl_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct curl_body* out = userdata;
    size_t n = size * nmemb;
    size_t room = sizeof(out->buf) - 1 - out->used;
    if (n > room) n = room;
    if (n) memcpy(out->buf + out->used, ptr, n);
    out->used += n;
    out->buf[out->used] = 0;
    return size * nmemb;
}

static struct curl_api curlApi;
static int curlLoaded;

static int load_curl(void) {
    if (curlLoaded) return curlApi.lib != NULL;
    curlLoaded = 1;
    const char* names[] = { "libcurl.so.4", "libcurl.so.3", "libcurl.so", NULL };
    for (int i = 0; names[i] && !curlApi.lib; i++) curlApi.lib = dlopen(names[i], RTLD_NOW);
    if (!curlApi.lib) return 0;
    curlApi.easy_init = dlsym(curlApi.lib, "curl_easy_init");
    curlApi.easy_setopt = dlsym(curlApi.lib, "curl_easy_setopt");
    curlApi.easy_perform = dlsym(curlApi.lib, "curl_easy_perform");
    curlApi.easy_cleanup = dlsym(curlApi.lib, "curl_easy_cleanup");
    curlApi.easy_strerror = dlsym(curlApi.lib, "curl_easy_strerror");
    curlApi.global_init = dlsym(curlApi.lib, "curl_global_init");
    if (!curlApi.easy_init || !curlApi.easy_setopt || !curlApi.easy_perform || !curlApi.easy_cleanup) {
        dlclose(curlApi.lib);
        curlApi.lib = NULL;
        return 0;
    }
    if (curlApi.global_init) curlApi.global_init(3);
    return 1;
}

// 1 = signed in, 0 = libcurl answered and refused, -1 = no libcurl.
static int login_libcurl(const char* user, const char* pass, char* token, size_t tokenLen, char* err, size_t errLen) {
    if (!load_curl()) return -1;
    char userEnc[160], passEnc[160], body[360];
    url_encode(user, userEnc, sizeof(userEnc));
    url_encode(pass, passEnc, sizeof(passEnc));
    snprintf(body, sizeof(body), "r=login&u=%s&p=%s", userEnc, passEnc);
    struct curl_easy* easy = curlApi.easy_init();
    if (!easy) {
        snprintf(err, errLen, "Could not start sign-in.");
        return 0;
    }
    struct curl_body out = { 0 };
    char curlErr[256];
    curlErr[0] = 0;
    curlApi.easy_setopt(easy, CURLOPT_URL, "https://retroachievements.org/dorequest.php");
    curlApi.easy_setopt(easy, CURLOPT_POST, 1L);
    curlApi.easy_setopt(easy, CURLOPT_POSTFIELDS, body);
    curlApi.easy_setopt(easy, CURLOPT_USERAGENT, "PokeDaisy/1 (Linux) rcheevos/12.5");
    curlApi.easy_setopt(easy, CURLOPT_TIMEOUT, 20L);
    curlApi.easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curlApi.easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curlApi.easy_setopt(easy, CURLOPT_WRITEFUNCTION, (pd_curl_write) curl_write);
    curlApi.easy_setopt(easy, CURLOPT_WRITEDATA, &out);
    curlApi.easy_setopt(easy, CURLOPT_ERRORBUFFER, curlErr);
    int rc = curlApi.easy_perform(easy);
    curlApi.easy_cleanup(easy);
    if (rc != 0) {
        if (curlErr[0]) snprintf(err, errLen, "%s", curlErr);
        else if (curlApi.easy_strerror) snprintf(err, errLen, "%s", curlApi.easy_strerror(rc));
        else snprintf(err, errLen, "No answer from RetroAchievements.");
        return 0;
    }
    return finish_login(out.buf, token, tokenLen, err, errLen) ? 1 : 0;
}

bool pd_ra_login(const char* user, const char* pass, char* token, size_t tokenLen, char* err, size_t errLen) {
    int viaLib = login_libcurl(user, pass, token, tokenLen, err, errLen);
    if (viaLib >= 0) return viaLib == 1;
    char uarg[96], parg[96];
    snprintf(uarg, sizeof(uarg), "u=%s", user);
    snprintf(parg, sizeof(parg), "p=%s", pass);
    int fds[2];
    if (pipe(fds) != 0) {
        snprintf(err, errLen, "Could not start sign-in.");
        return false;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    char* argv[] = {
        "curl", "-sS", "-m", "20", "-A", "PokeDaisy/1 (Linux) rcheevos/12.5", "--data-urlencode", "r=login",
        "--data-urlencode", uarg, "--data-urlencode", parg, "https://retroachievements.org/dorequest.php", NULL,
    };
    pid_t pid;
    int rc = posix_spawnp(&pid, "curl", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        snprintf(err, errLen, "Sign-in needs libcurl on this console.");
        return false;
    }
    char buf[1024];
    size_t n = 0;
    while (n + 1 < sizeof(buf)) {
        ssize_t r = read(fds[0], buf + n, sizeof(buf) - 1 - n);
        if (r <= 0) break;
        n += (size_t) r;
    }
    buf[n] = 0;
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return finish_login(buf, token, tokenLen, err, errLen);
}
#endif
