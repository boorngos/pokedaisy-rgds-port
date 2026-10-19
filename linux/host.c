// PokeDaisy for Linux / Anbernic RG DS: libmgba on the top 640x480 panel,
// the portable companion core on the bottom 640x480 panel.
//
// Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy
#include <SDL.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>

#include <mgba-util/vfs.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/interface.h>

#include "pd_canvas.h"
#include "pd_game.h"
#include "pd_map.h"
#include "pd_menu.h"
#include "pd_png.h"
#include "pd_romart.h"
#include "pd_settings.h"
#include "pd_snapshot.h"
#include "pd_ui.h"

#define RG_W 640
#define RG_H 480
#define GBA_W 240
#define GBA_H 160
#define MAX_ROMS 256
#define MAX_BACKUPS 10
#define SNAPSHOT_EVERY 10
#define LEAVE_HOLD 60
#define STATE_FLAGS SAVESTATE_RTC
#define AUDIO_RATE 48000
#define AUDIO_SAMPLES 512

enum {
    GBA_A = 1 << 0, GBA_B = 1 << 1, GBA_SELECT = 1 << 2, GBA_START = 1 << 3,
    GBA_RIGHT = 1 << 4, GBA_LEFT = 1 << 5, GBA_UP = 1 << 6, GBA_DOWN = 1 << 7,
    GBA_R = 1 << 8, GBA_L = 1 << 9,
};

enum layout {
    LAYOUT_AUTO,
    LAYOUT_STACKED,
    LAYOUT_DUAL,
    LAYOUT_WINDOW,
};

struct paths {
    char home[PATH_MAX];
    char roms[PATH_MAX];
    char saves[PATH_MAX];
    char states[PATH_MAX];
    char backups[PATH_MAX];
    char settings[PATH_MAX];
    char share[PATH_MAX];
};

struct video {
    enum layout layout;
    SDL_Window* winTop;
    SDL_Window* winBot;
    SDL_Renderer* renTop;
    SDL_Renderer* renBot;
    SDL_Texture* texGame;
    SDL_Texture* texUi;
    int stacked;
    int rgdsTouch; // Wayland on the RG DS: the glass is one half of the 1280 desktop
    int gameBgr;   // mGBA pixels are 0xBBGGRR; the UI canvas is 0xRRGGBB
};

struct game_mem {
    uint8_t* ewram;
    size_t ewramSize;
    uint8_t* iwram;
    size_t iwramSize;
    const uint8_t* rom;
    size_t romSize;
};

struct demo_mem {
    uint8_t ewram[0x40000];
    uint8_t iwram[0x8000];
};

struct app {
    struct paths paths;
    struct video video;
    struct pd_settings settings;
    SDL_AudioDeviceID audio;
    SDL_GameController* pad;
    bool nintendo; // south = B, east = A (Anbernic / Nintendo layout)
    bool running;
    bool audioMuted;
    char shotPath[PATH_MAX];
};

static struct app g;
static uint32_t gamePx[GBA_W * GBA_H];
static uint32_t uiPx[PD_UI_WIDTH * PD_UI_HEIGHT];
static uint32_t uiArgb[PD_UI_WIDTH * PD_UI_HEIGHT];
static uint32_t topPx[RG_W * RG_H];
static struct mAVStream stream;

static void noop_log(struct mLogger* log, int category, enum mLogLevel level, const char* format, va_list args) {
    (void) log; (void) category; (void) level; (void) format; (void) args;
}
static struct mLogger silentLogger = { .log = noop_log, .filter = NULL };

static void die(const char* msg) {
    fprintf(stderr, "pokedaisy: %s\n", msg);
    exit(1);
}

static void mkdir_p(const char* path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static int ends_with(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

static void exe_dir(char* out, size_t n) {
    char buf[PATH_MAX];
    ssize_t k = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (k < 0) {
        snprintf(out, n, ".");
        return;
    }
    buf[k] = 0;
    char* slash = strrchr(buf, '/');
    if (slash) *slash = 0;
    snprintf(out, n, "%s", buf);
}

static int dir_exists(const char* p) {
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

static void find_share(char* out, size_t n) {
    char exe[PATH_MAX], cand[PATH_MAX];
    exe_dir(exe, sizeof(exe));
    const char* tries[] = { NULL, NULL, "/usr/local/share/pokedaisy", "/usr/share/pokedaisy" };
    snprintf(cand, sizeof(cand), "%s/share/pokedaisy", exe);
    tries[0] = cand;
    char cand2[PATH_MAX];
    snprintf(cand2, sizeof(cand2), "%s/../share/pokedaisy", exe);
    tries[1] = cand2;
    for (int i = 0; i < 4; i++) {
        if (tries[i] && dir_exists(tries[i])) {
            snprintf(out, n, "%s", tries[i]);
            return;
        }
    }
    snprintf(out, n, "%s", cand);
}

static void paths_init(struct paths* p, const char* homeOverride, const char* romsOverride) {
    if (homeOverride && homeOverride[0]) {
        snprintf(p->home, sizeof(p->home), "%s", homeOverride);
    } else {
        const char* env = getenv("POKEDAISY_HOME");
        if (env && env[0]) {
            snprintf(p->home, sizeof(p->home), "%s", env);
        } else {
            const char* h = getenv("HOME");
            snprintf(p->home, sizeof(p->home), "%s/.local/share/pokedaisy", h ? h : ".");
        }
    }
    mkdir_p(p->home);
    if (romsOverride && romsOverride[0]) snprintf(p->roms, sizeof(p->roms), "%s", romsOverride);
    else snprintf(p->roms, sizeof(p->roms), "%s/roms", p->home);
    snprintf(p->saves, sizeof(p->saves), "%s/saves", p->home);
    snprintf(p->states, sizeof(p->states), "%s/states", p->home);
    snprintf(p->backups, sizeof(p->backups), "%s/backups", p->home);
    snprintf(p->settings, sizeof(p->settings), "%s/settings.ini", p->home);
    mkdir_p(p->roms);
    mkdir_p(p->saves);
    mkdir_p(p->states);
    mkdir_p(p->backups);
    find_share(p->share, sizeof(p->share));
}

static int cmp_str(const void* a, const void* b) {
    return strcasecmp(*(const char* const*) a, *(const char* const*) b);
}

static int list_roms(char** out) {
    int n = 0;
    DIR* d = opendir(g.paths.roms);
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) && n < MAX_ROMS) {
        if (e->d_name[0] != '.' && ends_with(e->d_name, ".gba")) out[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(out, (size_t) n, sizeof(char*), cmp_str);
    return n;
}

static bool copy_file(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");
    if (!in) return false;
    FILE* out = fopen(to, "wb");
    if (!out) { fclose(in); return false; }
    char buf[16 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

static bool is_backup_of(const char* name, const char* base) {
    size_t baseLen = strlen(base);
    if (strncmp(name, base, baseLen)) return false;
    const char* rest = name + baseLen;
    static const char PATTERN[] = "-########-######.sav";
    if (strlen(rest) != sizeof(PATTERN) - 1) return false;
    for (size_t i = 0; PATTERN[i]; i++) {
        if (PATTERN[i] == '#' ? (rest[i] < '0' || rest[i] > '9') : rest[i] != PATTERN[i]) return false;
    }
    return true;
}

static void backup_save(const char* base, const char* savePath) {
    struct stat st;
    if (stat(savePath, &st) || st.st_size == 0) return;
    char stamp[32];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", gmtime(&now));
    char to[PATH_MAX];
    snprintf(to, sizeof(to), "%s/%s-%s.sav", g.paths.backups, base, stamp);
    copy_file(savePath, to);
    char* names[64];
    int n = 0;
    DIR* d = opendir(g.paths.backups);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && n < 64) {
        if (is_backup_of(e->d_name, base)) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t) n, sizeof(char*), cmp_str);
    for (int i = 0; i < n; i++) {
        if (i < n - MAX_BACKUPS) {
            char p[PATH_MAX];
            snprintf(p, sizeof(p), "%s/%s", g.paths.backups, names[i]);
            remove(p);
        }
        free(names[i]);
    }
}

static bool bus_read(void* ctx, uint32_t addr, void* out, size_t len) {
    const struct game_mem* m = ctx;
    if (addr >= 0x02000000 && addr - 0x02000000 + len <= m->ewramSize) {
        memcpy(out, m->ewram + (addr - 0x02000000), len);
        return true;
    }
    if (addr >= 0x03000000 && addr - 0x03000000 + len <= m->iwramSize) {
        memcpy(out, m->iwram + (addr - 0x03000000), len);
        return true;
    }
    if (addr >= 0x08000000 && addr - 0x08000000 + len <= m->romSize) {
        memcpy(out, m->rom + (addr - 0x08000000), len);
        return true;
    }
    return false;
}

static bool demo_read(void* ctx, uint32_t addr, void* out, size_t len) {
    struct demo_mem* m = ctx;
    if (addr >= 0x02000000 && addr + len <= 0x02040000) {
        memcpy(out, m->ewram + (addr - 0x02000000), len);
        return true;
    }
    if (addr >= 0x03000000 && addr + len <= 0x03008000) {
        memcpy(out, m->iwram + (addr - 0x03000000), len);
        return true;
    }
    return false;
}

static bool load_demo_fixture(struct demo_mem* m, const char* kind, struct pd_game* game) {
    const char* folder = "firered_vanilla";
    const char* code = "BPRE";
    int rev = 1;
    if (kind && (!strcmp(kind, "emerald") || !strcmp(kind, "BPEE"))) {
        folder = "emerald_vanilla";
        code = "BPEE";
        rev = 0;
    } else if (kind && (!strcmp(kind, "battle") || !strcmp(kind, "emerald-battle"))) {
        folder = "emerald_de_battle";
        code = "BPEE";
        rev = 0;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/demo/%s", g.paths.share, folder);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/ewram.bin", dir);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fread(m->ewram, 1, sizeof(m->ewram), f) != sizeof(m->ewram)) { fclose(f); return false; }
    fclose(f);
    snprintf(path, sizeof(path), "%s/iwram.bin", dir);
    f = fopen(path, "rb");
    if (!f) return false;
    if (fread(m->iwram, 1, sizeof(m->iwram), f) != sizeof(m->iwram)) { fclose(f); return false; }
    fclose(f);
    uint8_t header[0xC0] = { 0 };
    memcpy(header + 0xAC, code, 4);
    header[0xBC] = (uint8_t) rev;
    pd_game_detect(game, header, 0x800000);
    return true;
}

static void post_audio(struct mAVStream* s, blip_t* left, blip_t* right) {
    (void) s;
    if (!g.audio || g.audioMuted) {
        blip_clear(left);
        blip_clear(right);
        return;
    }
    if (SDL_GetQueuedAudioSize(g.audio) > AUDIO_RATE * 2 * sizeof(int16_t) / 4) {
        blip_clear(left);
        blip_clear(right);
        return;
    }
    int16_t buf[AUDIO_SAMPLES * 2];
    memset(buf, 0, sizeof(buf));
    blip_read_samples(left, buf, AUDIO_SAMPLES, true);
    blip_read_samples(right, buf + 1, AUDIO_SAMPLES, true);
    SDL_QueueAudio(g.audio, buf, sizeof(buf));
}

static void audio_init(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = AUDIO_SAMPLES;
    g.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g.audio) SDL_PauseAudioDevice(g.audio, 0);
}

static SDL_Renderer* make_renderer(SDL_Window* w) {
    SDL_Renderer* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(w, -1, 0);
    if (r) SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    return r;
}

static bool video_init(struct video* v, enum layout want) {
    int displays = SDL_GetNumVideoDisplays();
    const char* drv = getenv("SDL_VIDEODRIVER");
    v->rgdsTouch = (drv && !strcmp(drv, "wayland")) || getenv("WAYLAND_DISPLAY") != NULL;
    v->layout = want;
    if (want == LAYOUT_AUTO) {
        v->layout = (displays >= 2) ? LAYOUT_DUAL : LAYOUT_STACKED;
        SDL_DisplayMode dm;
        if (displays == 1 && SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            if (dm.w >= RG_W && dm.h >= RG_H * 2 - 40) v->layout = LAYOUT_STACKED;
            else v->layout = LAYOUT_WINDOW;
        }
    }
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (v->layout == LAYOUT_DUAL) {
        v->winTop = SDL_CreateWindow("PokeDaisy - game",
            SDL_WINDOWPOS_CENTERED_DISPLAY(0), SDL_WINDOWPOS_CENTERED_DISPLAY(0),
            RG_W, RG_H, flags | SDL_WINDOW_FULLSCREEN_DESKTOP);
        int bot = displays >= 2 ? 1 : 0;
        v->winBot = SDL_CreateWindow("PokeDaisy - companion",
            SDL_WINDOWPOS_CENTERED_DISPLAY(bot), SDL_WINDOWPOS_CENTERED_DISPLAY(bot),
            RG_W, RG_H, flags | SDL_WINDOW_FULLSCREEN_DESKTOP);
        v->stacked = 0;
    } else if (v->layout == LAYOUT_STACKED) {
        int h = RG_H * 2;
        Uint32 f = flags;
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w <= RG_W + 16 && dm.h <= h + 80)
            f |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        v->winTop = SDL_CreateWindow("PokeDaisy Linux - Anbernic RG DS",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, RG_W, h, f);
        v->winBot = v->winTop;
        v->stacked = 1;
    } else {
        v->winTop = SDL_CreateWindow("PokeDaisy Linux - Anbernic RG DS",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, RG_W, RG_H * 2, flags | SDL_WINDOW_RESIZABLE);
        v->winBot = v->winTop;
        v->stacked = 1;
    }
    if (!v->winTop || !v->winBot) return false;
    v->renTop = make_renderer(v->winTop);
    if (!v->renTop) return false;
    if (v->stacked) v->renBot = v->renTop;
    else {
        v->renBot = make_renderer(v->winBot);
        if (!v->renBot) return false;
    }
    v->texGame = SDL_CreateTexture(v->renTop, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
    v->texUi = SDL_CreateTexture(v->renBot, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, PD_UI_WIDTH, PD_UI_HEIGHT);
    SDL_SetTextureScaleMode(v->texGame, SDL_ScaleModeNearest);
    SDL_SetTextureScaleMode(v->texUi, SDL_ScaleModeNearest);
    return v->texGame && v->texUi;
}

static void video_quit(struct video* v) {
    if (v->texGame) SDL_DestroyTexture(v->texGame);
    if (v->texUi) SDL_DestroyTexture(v->texUi);
    if (v->renBot && v->renBot != v->renTop) SDL_DestroyRenderer(v->renBot);
    if (v->renTop) SDL_DestroyRenderer(v->renTop);
    if (v->winBot && v->winBot != v->winTop) SDL_DestroyWindow(v->winBot);
    if (v->winTop) SDL_DestroyWindow(v->winTop);
    memset(v, 0, sizeof(*v));
}

static uint32_t gameUpload[GBA_W * GBA_H];

static void present_screens(struct video* v, int screenMode, const uint32_t* game, const uint32_t* ui, int uiDirty) {
    if (game && v->texGame) {
        const uint32_t* src = game;
        if (v->gameBgr) {
            for (int i = 0; i < GBA_W * GBA_H; i++) {
                uint32_t p = game[i];
                gameUpload[i] = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
            }
            src = gameUpload;
        }
        SDL_UpdateTexture(v->texGame, NULL, src, GBA_W * 4);
    }
    if (ui && v->texUi && uiDirty) {
        for (int i = 0; i < PD_UI_WIDTH * PD_UI_HEIGHT; i++)
            uiArgb[i] = 0xFF000000u | ui[i];
        SDL_UpdateTexture(v->texUi, NULL, uiArgb, PD_UI_WIDTH * 4);
    }

    int tw, th;
    SDL_GetRendererOutputSize(v->renTop, &tw, &th);
    int topH = v->stacked ? th / 2 : th;
    int botY = v->stacked ? topH : 0;
    int botH = v->stacked ? th - topH : 0;
    if (!v->stacked) SDL_GetRendererOutputSize(v->renBot, &tw, &botH);

    SDL_SetRenderDrawColor(v->renTop, 16, 16, 24, 255);
    SDL_RenderClear(v->renTop);

    SDL_Rect dst;
    if (screenMode == PD_SCREEN_STRETCH) {
        dst.x = 0; dst.y = 0; dst.w = tw; dst.h = topH;
    } else if (screenMode == PD_SCREEN_PIXEL) {
        int s = 2;
        dst.w = GBA_W * s; dst.h = GBA_H * s;
        dst.x = (tw - dst.w) / 2;
        dst.y = (topH - dst.h) / 2;
    } else {
        float sx = (float) tw / GBA_W;
        float sy = (float) topH / GBA_H;
        float s = sx < sy ? sx : sy;
        dst.w = (int) (GBA_W * s);
        dst.h = (int) (GBA_H * s);
        dst.x = (tw - dst.w) / 2;
        dst.y = (topH - dst.h) / 2;
    }
    SDL_RenderCopy(v->renTop, v->texGame, NULL, &dst);

    if (v->stacked) {
        SDL_Rect uiDst = { 0, botY, tw, botH };
        SDL_RenderCopy(v->renTop, v->texUi, NULL, &uiDst);
        SDL_RenderPresent(v->renTop);
    } else {
        SDL_RenderPresent(v->renTop);
        SDL_SetRenderDrawColor(v->renBot, 16, 16, 24, 255);
        SDL_RenderClear(v->renBot);
        int bw, bh;
        SDL_GetRendererOutputSize(v->renBot, &bw, &bh);
        SDL_Rect uiDst = { 0, 0, bw, bh };
        SDL_RenderCopy(v->renBot, v->texUi, NULL, &uiDst);
        SDL_RenderPresent(v->renBot);
    }
}

static void write_stacked_png(const char* path, const uint32_t* game, const uint32_t* ui) {
    static uint32_t img[RG_W * RG_H * 2];
    for (int i = 0; i < RG_W * RG_H * 2; i++) img[i] = 0x101018;
    int gx = (RG_W - GBA_W * 2) / 2;
    int gy = (RG_H - GBA_H * 2) / 2;
    for (int y = 0; y < GBA_H; y++) {
        for (int x = 0; x < GBA_W; x++) {
            uint32_t p = game[y * GBA_W + x] & 0xFFFFFFu;
            int dx = gx + x * 2, dy = gy + y * 2;
            img[dy * RG_W + dx] = p;
            img[dy * RG_W + dx + 1] = p;
            img[(dy + 1) * RG_W + dx] = p;
            img[(dy + 1) * RG_W + dx + 1] = p;
        }
    }
    uint32_t* bot = img + RG_W * RG_H;
    for (int y = 0; y < PD_UI_HEIGHT; y++) {
        for (int x = 0; x < PD_UI_WIDTH; x++) {
            uint32_t p = ui[y * PD_UI_WIDTH + x] & 0xFFFFFFu;
            int dx = x * 2, dy = y * 2;
            bot[dy * RG_W + dx] = p;
            bot[dy * RG_W + dx + 1] = p;
            bot[(dy + 1) * RG_W + dx] = p;
            bot[(dy + 1) * RG_W + dx + 1] = p;
        }
    }
    pd_png_write(path, RG_W, RG_H * 2, img);
}

static void draw_fake_game(uint32_t* dst, const char* title, const char* line) {
    struct pd_canvas c;
    pd_canvas_init(&c, GBA_W, GBA_H, dst);
    pd_fill(&c, 0, 0, GBA_W, GBA_H, 0x38A038);
    pd_fill(&c, 0, 0, GBA_W, 16, 0x181830);
    pd_text(&c, 6, 2, title ? title : "PokeDaisy", 0xF8F8F8, 0x101018);
    pd_fill(&c, 12, 36, 216, 92, 0xF8F0D0);
    pd_text(&c, 20, 44, "Anbernic RG DS Linux", 0x181830, PD_NO_SHADOW);
    pd_text(&c, 20, 60, "top 640x480  game", 0x181830, PD_NO_SHADOW);
    pd_text(&c, 20, 76, "bot 640x480  companion", 0x181830, PD_NO_SHADOW);
    pd_text(&c, 20, 100, line ? line : "", 0x403020, PD_NO_SHADOW);
}

/* Goodix on the RG DS is calibrated onto one half of the 1280-wide Weston
   desktop. SDL still reports that half (0.5..1 or 0..0.5) for a 640 window. */
static float rgds_touch_x(float fx) {
    static int half;
    if (fx < 0.f) fx = 0.f;
    if (fx > 1.f) fx = 1.f;
    if (half == 0) {
        if (fx >= 0.62f) half = 1;
        else if (fx <= 0.38f) half = -1;
    }
    if (half > 0) {
        if (fx < 0.25f) return 1.f;
        if (fx < 0.5f) return 0.f;
        return (fx - 0.5f) * 2.f;
    }
    if (half < 0) {
        if (fx > 0.75f) return 0.f;
        if (fx > 0.5f) return 1.f;
        return fx * 2.f;
    }
    if (fx < 0.5f) return fx * 2.f;
    return (fx - 0.5f) * 2.f;
}

static void finger_to_ui(struct video* v, float fx, float fy, int* ox, int* oy, int* onUi) {
    *onUi = 0;
    *ox = 0;
    *oy = 0;
    if (v->rgdsTouch) fx = rgds_touch_x(fx);
    if (fx < 0.f) fx = 0.f;
    if (fx > 1.f) fx = 1.f;
    if (fy < 0.f) fy = 0.f;
    if (fy > 1.f) fy = 1.f;
    *ox = (int) (fx * PD_UI_WIDTH);
    *oy = (int) (fy * PD_UI_HEIGHT);
    if (*ox >= PD_UI_WIDTH) *ox = PD_UI_WIDTH - 1;
    if (*oy >= PD_UI_HEIGHT) *oy = PD_UI_HEIGHT - 1;
    *onUi = 1;
}

static void map_touch(struct video* v, SDL_Window* win, int x, int y, int* ox, int* oy, int* onUi) {
    *onUi = 0;
    *ox = 0; *oy = 0;
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);
    if (v->stacked) {
        int topH = wh / 2;
        if (y < topH) return;
        float sx = (float) PD_UI_WIDTH / ww;
        float sy = (float) PD_UI_HEIGHT / (wh - topH);
        *ox = (int) (x * sx);
        *oy = (int) ((y - topH) * sy);
        *onUi = 1;
    } else if (win == v->winBot) {
        float sx = (float) PD_UI_WIDTH / ww;
        float sy = (float) PD_UI_HEIGHT / wh;
        *ox = (int) (x * sx);
        *oy = (int) (y * sy);
        *onUi = 1;
    }
}

static uint32_t keys_from_keyboard(const Uint8* k) {
    uint32_t keys = 0;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) keys |= GBA_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) keys |= GBA_DOWN;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) keys |= GBA_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) keys |= GBA_RIGHT;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_K] || k[SDL_SCANCODE_PERIOD]) keys |= GBA_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_L] || k[SDL_SCANCODE_COMMA]) keys |= GBA_B;
    if (k[SDL_SCANCODE_RETURN]) keys |= GBA_START;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]) keys |= GBA_SELECT;
    if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_C]) keys |= GBA_L;
    if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_V]) keys |= GBA_R;
    return keys;
}

static uint32_t keys_from_pad(SDL_GameController* pad, bool nintendo) {
    if (!pad) return 0;
    uint32_t keys = 0;
    int south = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
    int east = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
    if (nintendo) {
        if (east) keys |= GBA_A;
        if (south) keys |= GBA_B;
    } else {
        if (south) keys |= GBA_A;
        if (east) keys |= GBA_B;
    }
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) keys |= GBA_START;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) keys |= GBA_SELECT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) keys |= GBA_UP;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) keys |= GBA_DOWN;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) keys |= GBA_LEFT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) keys |= GBA_RIGHT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) keys |= GBA_L;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) keys |= GBA_R;
    Sint16 ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    const int dead = 16000;
    if (ay < -dead) keys |= GBA_UP;
    if (ay > dead) keys |= GBA_DOWN;
    if (ax < -dead) keys |= GBA_LEFT;
    if (ax > dead) keys |= GBA_RIGHT;
    return keys;
}

static bool pad_ff(SDL_GameController* pad) {
    if (!pad) return false;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) return true;
    Sint16 t = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    return t > 8000;
}

static bool pad_pressed(SDL_GameController* pad, SDL_GameControllerButton b) {
    return pad && SDL_GameControllerGetButton(pad, b);
}

static void open_first_pad(void) {
    if (g.pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g.pad = SDL_GameControllerOpen(i);
            if (g.pad) break;
        }
    }
}

static bool state_info(const char* path, char* when, size_t len) {
    struct stat st;
    if (stat(path, &st) || st.st_size == 0) return false;
    strftime(when, len, "%m-%d %H:%M", localtime(&st.st_mtime));
    return true;
}

static bool save_state(struct mCore* core, const char* path) {
    struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
    if (!vf) return false;
    bool ok = mCoreSaveStateNamed(core, vf, STATE_FLAGS);
    vf->close(vf);
    return ok;
}

static bool load_state(struct mCore* core, const char* path) {
    struct VFile* vf = VFileOpen(path, O_RDONLY);
    if (!vf) return false;
    bool ok = mCoreLoadStateNamed(core, vf, STATE_FLAGS);
    vf->close(vf);
    return ok;
}

static void poll_companion_keys(struct pd_ui* ui, const Uint8* kb, SDL_GameController* pad, int* leaveHeld, bool* redraw) {
    static int prevX, prevY, prevTab, prevBack;
    int x = kb[SDL_SCANCODE_TAB] || kb[SDL_SCANCODE_RIGHTBRACKET] ||
            pad_pressed(pad, SDL_CONTROLLER_BUTTON_X);
    int y = kb[SDL_SCANCODE_ESCAPE] || kb[SDL_SCANCODE_LEFTBRACKET] ||
            pad_pressed(pad, SDL_CONTROLLER_BUTTON_Y);
    if (x && y) {
        if (++(*leaveHeld) >= LEAVE_HOLD) return;
    } else {
        *leaveHeld = 0;
        if (x && !prevX) { pd_ui_next_tab(ui, 1); *redraw = true; }
        if (y && !prevY) { pd_ui_back(ui); *redraw = true; }
    }
    int tab = kb[SDL_SCANCODE_PAGEDOWN];
    int back = kb[SDL_SCANCODE_PAGEUP];
    if (tab && !prevTab) { pd_ui_next_tab(ui, 1); *redraw = true; }
    if (back && !prevBack) { pd_ui_back(ui); *redraw = true; }
    prevX = x; prevY = y; prevTab = tab; prevBack = back;
}

static void apply_actions(struct pd_ui* ui, struct pd_host_info* host, bool* ffToggled, bool* leave,
                          struct mCore* core, const char* statePath, const char* savePath, const char* base) {
    switch (pd_ui_take_action(ui)) {
    case PD_ACTION_TOGGLE_FF: *ffToggled = !*ffToggled; break;
    case PD_ACTION_SAVE_STATE:
        if (core) save_state(core, statePath);
        host->hasState = state_info(statePath, host->stateWhen, sizeof(host->stateWhen));
        break;
    case PD_ACTION_LOAD_STATE:
        if (core) { backup_save(base, savePath); load_state(core, statePath); }
        break;
    case PD_ACTION_LEAVE_GAME: *leave = true; break;
    case PD_ACTION_SETTINGS_CHANGED: pd_settings_save(&g.settings, g.paths.settings); break;
    case PD_ACTION_NONE: break;
    }
}

static bool handle_global_event(SDL_Event* e, struct video* v, struct pd_ui* ui, bool* redraw, bool* touching) {
    if (e->type == SDL_QUIT) { g.running = false; return true; }
    if (e->type == SDL_CONTROLLERDEVICEADDED) open_first_pad();
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F11) {
        Uint32 f = SDL_GetWindowFlags(v->winTop);
        SDL_SetWindowFullscreen(v->winTop, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP || e->type == SDL_MOUSEMOTION) {
        if ((e->type == SDL_MOUSEMOTION ? e->motion.which : e->button.which) == SDL_TOUCH_MOUSEID)
            return false;
        int mx, my;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);
        SDL_Window* win = SDL_GetMouseFocus();
        if (!win) win = v->stacked ? v->winTop : v->winBot;
        int ox, oy, onUi;
        map_touch(v, win, mx, my, &ox, &oy, &onUi);
        if (onUi) {
            bool down = (buttons & SDL_BUTTON_LMASK) != 0;
            if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) down = true;
            if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) down = false;
            *redraw |= pd_ui_touch(ui, ox, oy, down);
            *touching = down;
        }
    }
    if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERMOTION || e->type == SDL_FINGERUP) {
        int ox, oy, onUi;
        if (v->rgdsTouch || !v->stacked) {
            /* The glass is the bottom LCD. Its events are not tied to winBot. */
            finger_to_ui(v, e->tfinger.x, e->tfinger.y, &ox, &oy, &onUi);
        } else {
            SDL_Window* win = SDL_GetWindowFromID((Uint32) e->tfinger.windowID);
            if (!win) win = v->winTop;
            int ww, wh;
            SDL_GetWindowSize(win, &ww, &wh);
            int mx = (int) (e->tfinger.x * ww);
            int my = (int) (e->tfinger.y * wh);
            map_touch(v, win, mx, my, &ox, &oy, &onUi);
        }
        if (onUi) {
            bool down = e->type != SDL_FINGERUP;
            *redraw |= pd_ui_touch(ui, ox, oy, down);
            *touching = down;
        }
    }
    return false;
}

static void fill_host(struct pd_host_info* host, const char* file) {
    memset(host, 0, sizeof(*host));
    host->count = 2;
    host->labels[0] = "FILE";
    snprintf(host->values[0], sizeof(host->values[0]), "%s", file);
    host->labels[1] = "SPEED";
    snprintf(host->values[1], sizeof(host->values[1]), "-");
}

static bool run_demo(const char* kind) {
    struct demo_mem mem;
    struct pd_game game;
    if (!load_demo_fixture(&mem, kind, &game)) {
        fprintf(stderr, "pokedaisy: demo fixtures not found in %s/demo\n", g.paths.share);
        return true;
    }
    struct pd_ui ui;
    pd_ui_init(&ui, &g.settings);
    struct pd_snapshot snap;
    pd_snapshot_read(&snap, &game, demo_read, &mem);
    pd_ui_update(&ui, &snap);
    struct pd_host_info host;
    fill_host(&host, "demo (no ROM)");
    snprintf(host.values[1], sizeof(host.values[1]), "fixture");
    struct pd_canvas bottom;
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, uiPx);
    draw_fake_game(gamePx, game.title, "Put a .gba in roms/ to play");
    pd_ui_draw(&ui, &bottom, &game, &snap, &host);
    if (g.shotPath[0]) {
        write_stacked_png(g.shotPath, gamePx, uiPx);
        return true;
    }
    bool redraw = true, touching = false, leave = false, ffToggled = false;
    int leaveHeld = 0;
    Uint32 lastTick = SDL_GetTicks();
    while (g.running && !leave) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (handle_global_event(&e, &g.video, &ui, &redraw, &touching)) break;
        }
        if (!g.running) break;
        const Uint8* kb = SDL_GetKeyboardState(NULL);
        poll_companion_keys(&ui, kb, g.pad, &leaveHeld, &redraw);
        if (leaveHeld >= LEAVE_HOLD) break;
        apply_actions(&ui, &host, &ffToggled, &leave, NULL, "", "", "demo");
        Uint32 now = SDL_GetTicks();
        redraw |= pd_ui_tick(&ui, now - lastTick);
        lastTick = now;
        if (redraw) {
            pd_ui_draw(&ui, &bottom, &game, &snap, &host);
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 1);
            redraw = false;
        } else {
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 0);
        }
        SDL_Delay(16);
    }
    return g.running;
}

static bool run_game(const char* file) {
    char romPath[PATH_MAX], savePath[PATH_MAX], statePath[PATH_MAX], base[256];
    snprintf(romPath, sizeof(romPath), "%s/%s", g.paths.roms, file);
    if (file[0] == '/' || (file[0] == '.' && file[1] == '/')) snprintf(romPath, sizeof(romPath), "%s", file);
    snprintf(base, sizeof(base), "%s", strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
    size_t bl = strlen(base);
    if (bl > 4) base[bl - 4] = 0;
    snprintf(savePath, sizeof(savePath), "%s/%s.sav", g.paths.saves, base);
    snprintf(statePath, sizeof(statePath), "%s/%s.ss1", g.paths.states, base);

    struct VFile* rom = VFileOpen(romPath, O_RDONLY);
    if (!rom) {
        fprintf(stderr, "pokedaisy: cannot open %s\n", romPath);
        return true;
    }
    uint8_t header[0xC0] = { 0 };
    rom->seek(rom, 0, SEEK_SET);
    rom->read(rom, header, sizeof(header));
    rom->seek(rom, 0, SEEK_SET);
    struct pd_game game;
    pd_game_detect(&game, header, (size_t) rom->size(rom));

    struct mCore* core = mCoreFindVF(rom);
    if (!core || !core->init(core)) {
        rom->close(rom);
        fprintf(stderr, "pokedaisy: not a GBA game: %s\n", file);
        return true;
    }
    mCoreInitConfig(core, NULL);
    core->setVideoBuffer(core, (color_t*) gamePx, GBA_W);
    core->setAudioBufferSize(core, AUDIO_SAMPLES);
    if (!core->loadROM(core, rom)) {
        core->deinit(core);
        fprintf(stderr, "pokedaisy: ROM failed to load\n");
        return true;
    }
    backup_save(base, savePath);
    struct VFile* save = VFileOpen(savePath, O_CREAT | O_RDWR);
    if (save) core->loadSave(core, save);
    stream.postAudioBuffer = post_audio;
    core->setAVStream(core, &stream);
    if (g.audio) {
        blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), AUDIO_RATE);
        blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), AUDIO_RATE);
    }
    core->reset(core);

    struct game_mem mem = { 0 };
    mem.ewram = core->getMemoryBlock(core, 0x02, &mem.ewramSize);
    mem.iwram = core->getMemoryBlock(core, 0x03, &mem.iwramSize);
    size_t romSize = 0;
    game.rom = core->getMemoryBlock(core, 0x08, &romSize);
    game.romSize = game.rom ? romSize : 0;
    mem.rom = game.rom;
    mem.romSize = game.romSize;
    pd_romart_scan(&game);
    pd_map_art(&game);

    struct pd_ui ui;
    pd_ui_init(&ui, &g.settings);
    struct pd_snapshot snap, shown;
    pd_snapshot_read(&shown, &game, bus_read, &mem);
    shown.frame = 0;
    struct pd_host_info host;
    fill_host(&host, file);
    host.hasState = state_info(statePath, host.stateWhen, sizeof(host.stateWhen));
    struct pd_canvas bottom;
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, uiPx);
    pd_ui_draw(&ui, &bottom, &game, &shown, &host);

    if (g.shotPath[0]) {
        write_stacked_png(g.shotPath, gamePx, uiPx);
        core->deinit(core);
        return true;
    }

    g.video.gameBgr = 1;
    bool redraw = true, touching = false, leave = false, ffToggled = false;
    int leaveHeld = 0;
    unsigned frame = 0, fpsFrames = 0;
    Uint32 fpsStart = SDL_GetTicks();
    Uint32 lastTick = fpsStart;

    while (g.running && !leave) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (handle_global_event(&e, &g.video, &ui, &redraw, &touching)) break;
        }
        if (!g.running) break;
        const Uint8* kb = SDL_GetKeyboardState(NULL);
        poll_companion_keys(&ui, kb, g.pad, &leaveHeld, &redraw);
        if (leaveHeld >= LEAVE_HOLD) break;
        apply_actions(&ui, &host, &ffToggled, &leave, core, statePath, savePath, base);
        if (leave) break;

        bool ff = ffToggled || kb[SDL_SCANCODE_SPACE] || pad_ff(g.pad);
        if (ff != host.ffOn) {
            host.ffOn = ff;
            g.audioMuted = ff;
            redraw = true;
        }
        uint32_t keys = keys_from_keyboard(kb) | keys_from_pad(g.pad, g.nintendo);
        int frames = ff ? g.settings.ffSpeed : 1;
        for (int i = 0; i < frames; i++) {
            core->setKeys(core, keys);
            core->runFrame(core);
            frame++;
        }
        if (frame / SNAPSHOT_EVERY != (frame - (unsigned) frames) / SNAPSHOT_EVERY && mem.ewram && mem.iwram) {
            pd_snapshot_read(&snap, &game, bus_read, &mem);
            pd_ui_update(&ui, &snap);
            snap.frame = 0;
            if (memcmp(&snap, &shown, sizeof(snap))) {
                shown = snap;
                redraw = true;
            }
        }
        fpsFrames += (unsigned) frames;
        Uint32 now = SDL_GetTicks();
        redraw |= pd_ui_tick(&ui, now - lastTick);
        lastTick = now;
        if (now - fpsStart >= 1000) {
            snprintf(host.values[1], sizeof(host.values[1]), "%u FPS", fpsFrames);
            fpsFrames = 0;
            fpsStart = now;
            if (ui.tab == PD_TAB_SETTINGS) redraw = true;
        }
        if (redraw) {
            pd_ui_draw(&ui, &bottom, &game, &shown, &host);
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 1);
            redraw = false;
        } else {
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 0);
        }
    }
    g.video.gameBgr = 0;
    g.audioMuted = false;
    if (g.audio) SDL_ClearQueuedAudio(g.audio);
    core->deinit(core);
    return g.running;
}

static bool pick_rom(char* out, size_t outLen) {
    char* names[MAX_ROMS + 4];
    int n = list_roms(names);
    int extra = 0;
    names[n + extra++] = strdup("[Demo] FireRed party (no ROM)");
    names[n + extra++] = strdup("[Demo] Emerald party (no ROM)");
    names[n + extra++] = strdup("[Demo] Emerald battle (no ROM)");
    int total = n + extra;
    static char labels[MAX_ROMS + 4][128];
    const char* items[MAX_ROMS + 4];
    for (int i = 0; i < total; i++) {
        snprintf(labels[i], sizeof(labels[i]), "%s", names[i]);
        size_t L = strlen(labels[i]);
        if (i < n && L > 4 && !strcasecmp(labels[i] + L - 4, ".gba")) labels[i][L - 4] = 0;
        items[i] = labels[i];
    }
    struct pd_menu menu;
    pd_menu_init(&menu, "CHOOSE A GAME", items, total);
    struct pd_canvas top, bottom;
    pd_canvas_init(&top, RG_W, RG_H, topPx);
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, uiPx);
    pd_splash_draw(&top, "PokeDaisy on the RG DS.", "A: play   START: quit",
                   "In game: X tabs, Y back, R2 fast-forward.");
    for (int y = 0; y < GBA_H; y++)
        for (int x = 0; x < GBA_W; x++)
            gamePx[y * GBA_W + x] = topPx[(y * RG_H / GBA_H) * RG_W + (x * RG_W / GBA_W)];
    draw_fake_game(gamePx, "PokeDaisy Linux", "Choose a game on the bottom screen");

    bool dirty = true, picked = false, touching = false;
    while (g.running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g.running = false; break; }
            if (e.type == SDL_CONTROLLERDEVICEADDED) open_first_pad();
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
                int ox, oy, onUi;
                finger_to_ui(&g.video, e.tfinger.x, e.tfinger.y, &ox, &oy, &onUi);
                if (onUi) {
                    bool down = e.type != SDL_FINGERUP;
                    int row = pd_menu_touch(&menu, ox, oy, down);
                    touching = down;
                    dirty = true;
                    if (!down && row >= 0) { picked = true; goto done; }
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
                if ((e.type == SDL_MOUSEMOTION ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID)
                    continue;
                int mx, my;
                Uint32 buttons = SDL_GetMouseState(&mx, &my);
                SDL_Window* win = SDL_GetMouseFocus();
                if (!win) win = g.video.winTop;
                int ox, oy, onUi;
                map_touch(&g.video, win, mx, my, &ox, &oy, &onUi);
                if (onUi) {
                    bool down = (buttons & SDL_BUTTON_LMASK) != 0;
                    if (e.type == SDL_MOUSEBUTTONDOWN) down = true;
                    if (e.type == SDL_MOUSEBUTTONUP) down = false;
                    int row = pd_menu_touch(&menu, ox, oy, down);
                    touching = down;
                    dirty = true;
                    if (!down && row >= 0) { picked = true; goto done; }
                }
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_F4) {
                    g.running = false;
                    goto done;
                }
            }
        }
        const Uint8* kb = SDL_GetKeyboardState(NULL);
        static int prevUp, prevDown, prevA, prevStart;
        int up = kb[SDL_SCANCODE_UP] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        int down = kb[SDL_SCANCODE_DOWN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        int a = kb[SDL_SCANCODE_RETURN] || kb[SDL_SCANCODE_Z] || kb[SDL_SCANCODE_K] ||
                pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_A) || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_B);
        int start = kb[SDL_SCANCODE_F4];
        if (up && !prevUp) { pd_menu_move(&menu, -1); dirty = true; }
        if (down && !prevDown) { pd_menu_move(&menu, 1); dirty = true; }
        if (a && !prevA && total > 0) { picked = true; break; }
        if (start && !prevStart) { g.running = false; break; }
        prevUp = up; prevDown = down; prevA = a; prevStart = start;
        if (dirty) {
            pd_menu_draw(&menu, &bottom);
            present_screens(&g.video, PD_SCREEN_PIXEL, gamePx, uiPx, 1);
            dirty = false;
        } else {
            present_screens(&g.video, PD_SCREEN_PIXEL, gamePx, uiPx, 0);
        }
        SDL_Delay(16);
    }
done:
    if (picked) {
        int sel = menu.selected;
        if (sel < n) snprintf(out, outLen, "%s", names[sel]);
        else if (sel == n) snprintf(out, outLen, "::demo:firered");
        else if (sel == n + 1) snprintf(out, outLen, "::demo:emerald");
        else snprintf(out, outLen, "::demo:battle");
    }
    for (int i = 0; i < total; i++) free(names[i]);
    return g.running && picked;
}

static void usage(void) {
    puts("PokeDaisy Linux for the Anbernic RG DS");
    puts("Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy");
    puts("");
    puts("usage: pokedaisy [options] [rom.gba]");
    puts("  --layout auto|stacked|dual|window");
    puts("  --home DIR          data dir (default ~/.local/share/pokedaisy)");
    puts("  --roms DIR          where to look for .gba files");
    puts("  --demo [kind]       companion demo without a ROM");
    puts("                      kind: firered | emerald | battle");
    puts("  --shot FILE.png     draw one dual-screen frame and exit");
    puts("  --nintendo          face buttons: east=A, south=B (default, RG DS)");
    puts("  --xbox              face buttons: south=A, east=B");
    puts("  --help");
    puts("");
    puts("Controls: D-pad / left stick, A/B, Start, Select, L/R.");
    puts("  X next companion tab, Y back, R2 or Space fast-forward.");
    puts("  Hold X+Y to leave the game. F11 fullscreen. Esc quits the list.");
}

int main(int argc, char** argv) {
    enum layout layout = LAYOUT_AUTO;
    const char* home = NULL;
    const char* roms = NULL;
    const char* demoKind = NULL;
    const char* launchRom = NULL;
    bool wantDemo = false;
    g.nintendo = true;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--layout") && i + 1 < argc) {
            const char* v = argv[++i];
            if (!strcmp(v, "dual")) layout = LAYOUT_DUAL;
            else if (!strcmp(v, "stacked")) layout = LAYOUT_STACKED;
            else if (!strcmp(v, "window")) layout = LAYOUT_WINDOW;
            else layout = LAYOUT_AUTO;
        } else if (!strcmp(argv[i], "--home") && i + 1 < argc) home = argv[++i];
        else if (!strcmp(argv[i], "--roms") && i + 1 < argc) roms = argv[++i];
        else if (!strcmp(argv[i], "--demo")) {
            wantDemo = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') demoKind = argv[++i];
            else demoKind = "firered";
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            snprintf(g.shotPath, sizeof(g.shotPath), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--nintendo")) g.nintendo = true;
        else if (!strcmp(argv[i], "--xbox")) g.nintendo = false;
        else if (argv[i][0] != '-') launchRom = argv[i];
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    paths_init(&g.paths, home, roms);
    pd_settings_load(&g.settings, g.paths.settings);
    mLogSetDefaultLogger(&silentLogger);

    bool needVideo = !g.shotPath[0];
    if (needVideo) {
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        SDL_GameControllerAddMappingsFromFile("/usr/share/sdl2/gamecontrollerdb.txt");
        if (!video_init(&g.video, layout)) {
            fprintf(stderr, "video: %s\n", SDL_GetError());
            return 1;
        }
        audio_init();
        open_first_pad();
        g.running = true;
    } else {
        if (SDL_Init(SDL_INIT_TIMER) != 0) { /* optional */ }
        g.running = true;
        if (!wantDemo && !launchRom) { wantDemo = true; demoKind = "firered"; }
    }

    if (launchRom) {
        char tmp[PATH_MAX];
        if (strchr(launchRom, '/')) {
            char copy[PATH_MAX];
            snprintf(copy, sizeof(copy), "%s", launchRom);
            char* dir = dirname(copy);
            snprintf(g.paths.roms, sizeof(g.paths.roms), "%s", dir);
            snprintf(tmp, sizeof(tmp), "%s", basename(launchRom));
            run_game(launchRom);
        } else {
            run_game(launchRom);
        }
    } else if (wantDemo) {
        run_demo(demoKind);
    } else if (needVideo) {
        char file[512];
        while (g.running && pick_rom(file, sizeof(file))) {
            if (!strncmp(file, "::demo:", 7)) {
                if (!run_demo(file + 7)) break;
            } else if (!run_game(file)) break;
        }
    }

    if (g.audio) SDL_CloseAudioDevice(g.audio);
    if (g.pad) SDL_GameControllerClose(g.pad);
    video_quit(&g.video);
    SDL_Quit();
    return 0;
}
