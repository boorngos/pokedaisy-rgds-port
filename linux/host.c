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

#include "pd_battle.h"
#include "pd_canvas.h"
#include "pd_game.h"
#include "pd_map.h"
#include "pd_menu.h"
#include "pd_pc.h"
#include "pd_png.h"
#include "pd_ra.h"
#include "pd_romart.h"
#include "pd_settings.h"
#include "pd_setup.h"
#include "pd_sha1.h"
#include "pd_tables.h"
#include "pd_snapshot.h"
#include "pd_style.h"
#include "pd_ui.h"
#include "pd_zip.h"

#define QUICK_N 6

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
    SDL_Texture* texMenu;
    SDL_Texture* texStatus;
    SDL_Texture* texHud;
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
    int quick;
    int quickSel;
    int quickFire;
    int wildOpen;
    int wildSel;
    char shotPath[PATH_MAX];
};

static struct app g;
static uint32_t gamePx[GBA_W * GBA_H];
static uint32_t uiPx[PD_UI_WIDTH * PD_UI_HEIGHT];
static uint32_t uiArgb[PD_UI_WIDTH * PD_UI_HEIGHT];
static uint32_t splashPx[PD_UI_WIDTH * PD_UI_HEIGHT];
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
        if (e->d_name[0] == '.' || n >= MAX_ROMS) continue;
        if (ends_with(e->d_name, ".gba") || ends_with(e->d_name, ".zip")) out[n++] = strdup(e->d_name);
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

static void refresh_views(struct mCore* core, struct game_mem* mem, struct pd_game* game) {
    // mGBA copies the ROM the first time something writes the cart (a 32MB
    // image always trips this) and unmaps the old one. Icons and the map
    // read that pointer, so it has to be fetched again after the frame.
    mem->ewram = core->getMemoryBlock(core, 0x02, &mem->ewramSize);
    mem->iwram = core->getMemoryBlock(core, 0x03, &mem->iwramSize);
    size_t romSize = 0;
    game->rom = core->getMemoryBlock(core, 0x08, &romSize);
    game->romSize = game->rom ? romSize : 0;
    mem->rom = game->rom;
    mem->romSize = game->romSize;
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
    v->texMenu = SDL_CreateTexture(v->renTop, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PD_UI_WIDTH, PD_UI_HEIGHT);
    v->texStatus = SDL_CreateTexture(v->renTop, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PD_UI_WIDTH, 16);
    v->texHud = SDL_CreateTexture(v->renTop, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 72, 120);
    SDL_SetTextureScaleMode(v->texGame, SDL_ScaleModeNearest);
    SDL_SetTextureScaleMode(v->texUi, SDL_ScaleModeNearest);
    if (v->texMenu) SDL_SetTextureScaleMode(v->texMenu, SDL_ScaleModeNearest);
    if (v->texStatus) SDL_SetTextureScaleMode(v->texStatus, SDL_ScaleModeNearest);
    if (v->texHud) SDL_SetTextureScaleMode(v->texHud, SDL_ScaleModeNearest);
    return v->texGame && v->texUi && v->texMenu;
}

static void video_quit(struct video* v) {
    if (v->texGame) SDL_DestroyTexture(v->texGame);
    if (v->texUi) SDL_DestroyTexture(v->texUi);
    if (v->texMenu) SDL_DestroyTexture(v->texMenu);
    if (v->texStatus) SDL_DestroyTexture(v->texStatus);
    if (v->texHud) SDL_DestroyTexture(v->texHud);
    if (v->renBot && v->renBot != v->renTop) SDL_DestroyRenderer(v->renBot);
    if (v->renTop) SDL_DestroyRenderer(v->renTop);
    if (v->winBot && v->winBot != v->winTop) SDL_DestroyWindow(v->winBot);
    if (v->winTop) SDL_DestroyWindow(v->winTop);
    memset(v, 0, sizeof(*v));
}

static uint32_t gameUpload[GBA_W * GBA_H];

struct hud_view {
    int count;
    char line[6][16];
    int hp[6], maxHp[6];
};

static uint32_t shade_pixel(uint32_t p, int mode, int x, int y) {
    int r = (int) ((p >> 16) & 255), g = (int) ((p >> 8) & 255), b = (int) (p & 255);
    if (mode == PD_SHADER_GBA) {
        int nr = (r * 230 + g * 18 + b * 8) / 256;
        int ng = (r * 12 + g * 214 + b * 18) / 256;
        int nb = (r * 10 + g * 28 + b * 200) / 256;
        r = nr > 255 ? 255 : nr;
        g = ng > 255 ? 255 : ng;
        b = nb > 255 ? 255 : nb;
    }
    if (mode == PD_SHADER_LCD && ((x % 3) == 2 || (y % 3) == 2)) {
        r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4;
    }
    if ((mode == PD_SHADER_SCANLINES || mode == PD_SHADER_CRT) && (y & 1)) {
        r = r * 5 / 8; g = g * 5 / 8; b = b * 5 / 8;
    }
    if (mode == PD_SHADER_CRT) {
        int dx = x - GBA_W / 2, dy = y - GBA_H / 2;
        int d = dx * dx / 6 + dy * dy;
        if (d > 500) {
            int k = 255 - (d - 500) / 16;
            if (k < 120) k = 120;
            r = r * k / 255; g = g * k / 255; b = b * k / 255;
        }
    }
    return 0xFF000000u | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

static void present_screens(struct video* v, int screenMode, const uint32_t* game, const uint32_t* ui, int uiDirty,
                            const char* status, int shader, const struct hud_view* hud, const uint32_t* cover) {
    if (game && v->texGame) {
        for (int y = 0; y < GBA_H; y++) {
            for (int x = 0; x < GBA_W; x++) {
                uint32_t p = game[y * GBA_W + x];
                if (v->gameBgr) p = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
                gameUpload[y * GBA_W + x] = shade_pixel(p, shader, x, y);
            }
        }
        SDL_UpdateTexture(v->texGame, NULL, gameUpload, GBA_W * 4);
    }
    if (ui && v->texUi && uiDirty) {
        for (int i = 0; i < PD_UI_WIDTH * PD_UI_HEIGHT; i++)
            uiArgb[i] = 0xFF000000u | ui[i];
        SDL_UpdateTexture(v->texUi, NULL, uiArgb, PD_UI_WIDTH * 4);
    }

    int topW, topHpx;
    SDL_GetRendererOutputSize(v->renTop, &topW, &topHpx);
    int topH = v->stacked ? topHpx / 2 : topHpx;
    int botY = v->stacked ? topH : 0;
    int botH = v->stacked ? topHpx - topH : 0;
    int botW = topW;
    if (!v->stacked) SDL_GetRendererOutputSize(v->renBot, &botW, &botH);

    int bar = 0;
    if (status && status[0] && v->texStatus) {
        static char shown[96];
        static uint32_t barPx[PD_UI_WIDTH * 16];
        if (strcmp(shown, status)) {
            snprintf(shown, sizeof(shown), "%s", status);
            struct pd_canvas bc;
            pd_canvas_init(&bc, PD_UI_WIDTH, 16, barPx);
            pd_fill(&bc, 0, 0, PD_UI_WIDTH, 16, 0x182028);
            pd_text_fit(&bc, 4, 0, PD_UI_WIDTH - 8, status, 0xF4F1E8, 0x000000);
            uint32_t argb[PD_UI_WIDTH * 16];
            for (int i = 0; i < PD_UI_WIDTH * 16; i++) argb[i] = 0xFF000000u | barPx[i];
            SDL_UpdateTexture(v->texStatus, NULL, argb, PD_UI_WIDTH * 4);
        }
        bar = 22;
    }

    SDL_SetRenderDrawColor(v->renTop, 16, 16, 24, 255);
    SDL_RenderClear(v->renTop);
    if (bar) {
        SDL_Rect barDst = { 0, 0, topW, bar };
        SDL_RenderCopy(v->renTop, v->texStatus, NULL, &barDst);
    }

    int areaY = bar, areaH = topH - bar;
    if (areaH < 8) areaH = topH, areaY = 0;
    SDL_Rect dst;
    if (screenMode == PD_SCREEN_STRETCH) {
        dst.x = 0; dst.y = areaY; dst.w = topW; dst.h = areaH;
    } else if (screenMode == PD_SCREEN_PIXEL) {
        int s = 2;
        while (s > 1 && (GBA_W * s > topW || GBA_H * s > areaH)) s--;
        dst.w = GBA_W * s; dst.h = GBA_H * s;
        dst.x = (topW - dst.w) / 2;
        dst.y = areaY + (areaH - dst.h) / 2;
    } else {
        float sx = (float) topW / GBA_W;
        float sy = (float) areaH / GBA_H;
        float s = sx < sy ? sx : sy;
        dst.w = (int) (GBA_W * s);
        dst.h = (int) (GBA_H * s);
        dst.x = (topW - dst.w) / 2;
        dst.y = areaY + (areaH - dst.h) / 2;
    }
    if (cover && v->texMenu) {
        static uint32_t argb[PD_UI_WIDTH * PD_UI_HEIGHT];
        for (int i = 0; i < PD_UI_WIDTH * PD_UI_HEIGHT; i++) argb[i] = 0xFF000000u | cover[i];
        SDL_UpdateTexture(v->texMenu, NULL, argb, PD_UI_WIDTH * 4);
        SDL_Rect coverDst = { 0, areaY, topW, areaH };
        SDL_RenderCopy(v->renTop, v->texMenu, NULL, &coverDst);
    } else SDL_RenderCopy(v->renTop, v->texGame, NULL, &dst);
    if (!cover && hud && hud->count > 0 && v->texHud && dst.x >= 76) {
        static uint32_t hudPx[72 * 120];
        struct pd_canvas hc;
        pd_canvas_init(&hc, 72, 120, hudPx);
        pd_fill(&hc, 0, 0, 72, 120, 0x182028);
        int rows = hud->count > 6 ? 6 : hud->count;
        for (int i = 0; i < rows; i++) {
            pd_text_fit(&hc, 2, i * 20, 68, hud->line[i], 0xF4F1E8, PD_NO_SHADOW);
            int bw = 64, fill = hud->maxHp[i] > 0 ? bw * hud->hp[i] / hud->maxHp[i] : 0;
            if (fill < 0) fill = 0;
            if (fill > bw) fill = bw;
            uint32_t col = hud->maxHp[i] && hud->hp[i] * 5 < hud->maxHp[i] ? 0xE03030 :
                           hud->maxHp[i] && hud->hp[i] * 2 < hud->maxHp[i] ? 0xE0C030 : 0x40B050;
            pd_fill(&hc, 2, i * 20 + 14, bw, 3, 0x101418);
            if (fill) pd_fill(&hc, 2, i * 20 + 14, fill, 3, col);
        }
        uint32_t argb[72 * 120];
        for (int i = 0; i < 72 * 120; i++) argb[i] = 0xFF000000u | hudPx[i];
        SDL_UpdateTexture(v->texHud, NULL, argb, 72 * 4);
        int hh = rows * 20;
        SDL_Rect src = { 0, 0, 72, hh };
        SDL_Rect hd = { 4, dst.y + (dst.h - hh) / 2, 72, hh };
        if (hd.y < areaY) hd.y = areaY;
        SDL_RenderCopy(v->renTop, v->texHud, &src, &hd);
    }

    if (v->stacked) {
        SDL_Rect uiDst = { 0, botY, topW, botH };
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

// The idle top screen is the same 320x240 canvas as the companion, scaled up.
static void present_menu(struct video* v, const uint32_t* splash, const uint32_t* ui, int dirty) {
    if (splash && v->texMenu) {
        static uint32_t argb[PD_UI_WIDTH * PD_UI_HEIGHT];
        for (int i = 0; i < PD_UI_WIDTH * PD_UI_HEIGHT; i++) argb[i] = 0xFF000000u | splash[i];
        SDL_UpdateTexture(v->texMenu, NULL, argb, PD_UI_WIDTH * 4);
    }
    if (ui && v->texUi && dirty) {
        for (int i = 0; i < PD_UI_WIDTH * PD_UI_HEIGHT; i++)
            uiArgb[i] = 0xFF000000u | ui[i];
        SDL_UpdateTexture(v->texUi, NULL, uiArgb, PD_UI_WIDTH * 4);
    }
    int topW, topHpx;
    SDL_GetRendererOutputSize(v->renTop, &topW, &topHpx);
    int topH = v->stacked ? topHpx / 2 : topHpx;
    int botY = v->stacked ? topH : 0;
    int botH = v->stacked ? topHpx - topH : 0;
    int botW = topW;
    if (!v->stacked) SDL_GetRendererOutputSize(v->renBot, &botW, &botH);
    SDL_SetRenderDrawColor(v->renTop, 0x24, 0x28, 0x2C, 255);
    SDL_RenderClear(v->renTop);
    SDL_Rect topDst = { 0, 0, topW, topH };
    SDL_RenderCopy(v->renTop, v->texMenu, NULL, &topDst);
    if (v->stacked) {
        SDL_Rect uiDst = { 0, botY, topW, botH };
        SDL_RenderCopy(v->renTop, v->texUi, NULL, &uiDst);
        SDL_RenderPresent(v->renTop);
    } else {
        SDL_RenderPresent(v->renTop);
        SDL_SetRenderDrawColor(v->renBot, 16, 16, 24, 255);
        SDL_RenderClear(v->renBot);
        SDL_Rect uiDst = { 0, 0, botW, botH };
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

static bool pad_labeled_nintendo(SDL_GameController* pad) {
    const char* n = SDL_GameControllerName(pad);
    return n && (strstr(n, "ANBERNIC") || strstr(n, "rk3568"));
}

static uint32_t keys_from_pad(SDL_GameController* pad, bool nintendo) {
    if (!pad) return 0;
    uint32_t keys = 0;
    int a = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
    int b = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
    /* SDL's standard map is positional (A = south). The RG DS map names the
       buttons by the letter printed on them, so A is already the east button. */
    if (nintendo && !pad_labeled_nintendo(pad)) {
        if (b) keys |= GBA_A;
        if (a) keys |= GBA_B;
    } else {
        if (a) keys |= GBA_A;
        if (b) keys |= GBA_B;
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
    Sint16 t = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    return t > 8000;
}

struct key_step {
    uint32_t keys;
    int hold;
};

// A battle tap plays a short script (the 2x2 menu's LEFT/UP walk, which is a
// no-op on a bit already clear), then a closed loop that reads the party or
// bag cursor. FireRed's gBagMenuState / gPartyMenu; Emerald's party menu.
#define MACRO_MAX 48
enum {
    FOL_NONE,
    FOL_SWITCH,
    FOL_ITEM,
    PH_WAIT_PARTY,
    PH_PARTY,
    PH_WAIT_BAG,
    PH_BAG_POCKET,
    PH_BAG_ITEM,
    PH_BAG_CONFIRM,
    PH_BERRY,
    PH_SECOND_A,
    PH_RUN_B,
};

static struct {
    struct key_step seq[MACRO_MAX];
    int n, i, left;
    int follow, phase, timer, presses, last, holdoff;
    int slot, pocket, index, berry, mon, confirms;
    const struct pd_game* game;
    struct game_mem* mem;
} drv;

static void drv_stop(void) {
    drv.n = drv.i = drv.left = drv.follow = drv.phase = drv.holdoff = 0;
}

// The script already queued stays; nothing follows it.
static void drv_finish(void) {
    drv.follow = drv.phase = 0;
}

static void drv_press(uint32_t keys) {
    if (drv.n + 2 >= MACRO_MAX) return;
    if (drv.n == 0) drv.i = 0;
    drv.seq[drv.n++] = (struct key_step) { keys, 2 };
    drv.seq[drv.n++] = (struct key_step) { 0, 16 };
    if (drv.left <= 0) drv.left = drv.seq[drv.i].hold;
}

static void drv_wait(int frames) {
    if (drv.n + 1 >= MACRO_MAX) return;
    if (drv.n == 0) drv.i = 0;
    drv.seq[drv.n++] = (struct key_step) { 0, frames };
    if (drv.left <= 0) drv.left = drv.seq[drv.i].hold;
}

static void drv_cell(int index) {
    drv_press(GBA_LEFT);
    drv_press(GBA_UP);
    if (index & 1) drv_press(GBA_RIGHT);
    if (index & 2) drv_press(GBA_DOWN);
    drv_press(GBA_A);
}

static int drv_u8(uint32_t addr) {
    uint8_t b;
    if (!drv.mem || !bus_read(drv.mem, addr, &b, 1)) return -1;
    return b;
}

static int drv_u16(uint32_t addr) {
    uint8_t b[2];
    if (!drv.mem || !bus_read(drv.mem, addr, b, 2)) return -1;
    return b[0] | b[1] << 8;
}

static uint32_t party_menu_addr(const struct pd_game* g) {
    if (!g) return 0;
    if (g->title && strstr(g->title, "Lazarus")) return 0x0201B67C;
    if (!strcmp(g->code, "BPEJ")) return 0x0203CB94;
    if (g->kind == PD_GAME_EMERALD) return 0x0203CEC8;
    if (g->kind == PD_GAME_FIRERED) return 0x0203B0A0;
    return 0;
}

static uint32_t bag_state_addr(const struct pd_game* g) {
    if (g && g->kind == PD_GAME_FIRERED) return 0x0203ACFC;
    return 0;
}

static int party_len(const struct pd_game* g) {
    if (!g || !g->cfg || !g->cfg->playerParty) return 6;
    int n = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t b[4];
        if (!bus_read(drv.mem, g->cfg->playerParty + (uint32_t) i * 100, b, 4)) return 6;
        if (b[0] | b[1] | b[2] | b[3]) n++;
    }
    return n > 0 ? n : 1;
}

// Shorter way around the party list (slots, then CANCEL at 7).
static uint32_t party_key(int slot, int want, int count) {
    int ring[8], n = 0, i = -1, j = -1;
    for (int k = 0; k < count && k < 6; k++) ring[n++] = k;
    ring[n++] = 7;
    for (int k = 0; k < n; k++) {
        if (ring[k] == slot) i = k;
        if (ring[k] == want) j = k;
    }
    if (i < 0 || j < 0) return GBA_DOWN;
    int down = (j - i + n) % n;
    return down <= n - down ? GBA_DOWN : GBA_UP;
}

static void drv_arm(int phase) {
    drv.phase = phase;
    drv.timer = drv.presses = 0;
    drv.last = -1;
    drv.holdoff = 40;
}

static void drv_step(void) {
    if (drv.n || drv.holdoff > 0) {
        if (!drv.n && drv.holdoff > 0) drv.holdoff--;
        return;
    }
    if (drv.phase == PH_SECOND_A) {
        drv_press(GBA_A);
        drv_finish();
        return;
    }
    if (drv.phase == PH_RUN_B) {
        if (drv.confirms <= 0) { drv_stop(); return; }
        drv_press(GBA_B);
        drv.confirms--;
        drv.holdoff = 20;
        return;
    }
    if (drv.phase == PH_WAIT_PARTY || drv.phase == PH_PARTY) {
        uint32_t menu = party_menu_addr(drv.game);
        int slot = menu ? drv_u8(menu + 9) : -1;
        if (drv.phase == PH_WAIT_PARTY) {
            if (slot >= 0 && slot <= 7) {
                drv.phase = PH_PARTY;
                drv.timer = drv.presses = 0;
                drv.last = -1;
            } else if (++drv.timer > 200) {
                drv_stop();
            }
            return;
        }
        if (slot < 0 || slot > 7) {
            if (++drv.timer > 40) drv_stop();
            return;
        }
        if (slot != drv.last) {
            if (drv.last >= 0) drv.presses++;
            drv.last = slot;
        }
        if (slot == drv.slot) {
            drv_press(GBA_A);
            if (drv.confirms > 1) {
                drv.phase = PH_SECOND_A;
                drv.holdoff = 20;
            } else drv_finish();
            return;
        }
        if (drv.presses > 16 || ++drv.timer > 240) { drv_stop(); return; }
        drv_press(party_key(slot, drv.slot, party_len(drv.game)));
        return;
    }
    if (drv.phase == PH_WAIT_BAG || drv.phase == PH_BAG_POCKET || drv.phase == PH_BAG_ITEM ||
        drv.phase == PH_BAG_CONFIRM || drv.phase == PH_BERRY) {
        uint32_t bag = bag_state_addr(drv.game);
        if (!bag) {
            // No cursor address (Emerald): the bag opens on the top screen and
            // the script already pressed A. Stop rather than walk blind.
            drv_stop();
            return;
        }
        int open = drv_u8(bag + 5);
        int pocket = drv_u16(bag + 6);
        if (drv.phase == PH_WAIT_BAG) {
            if ((open == 1 && pocket >= 0 && pocket <= 4) || ++drv.timer > 90) {
                drv.phase = PH_BAG_POCKET;
                drv.timer = drv.presses = 0;
            }
            return;
        }
        if (pocket < 0) { drv_stop(); return; }
        if (drv.phase == PH_BAG_POCKET) {
            if (pocket == drv.pocket) {
                drv.phase = PH_BAG_ITEM;
                drv.presses = 0;
                return;
            }
            if (++drv.presses > 8) { drv_stop(); return; }
            drv_press(pocket < drv.pocket ? GBA_RIGHT : GBA_LEFT);
            drv.holdoff = 10;
            return;
        }
        int row = drv_u16(bag + 14 + pocket * 2);
        int above = drv_u16(bag + 8 + pocket * 2);
        int cur = row >= 0 && above >= 0 ? row + above : -1;
        if (drv.phase == PH_BAG_ITEM) {
            if (cur == drv.index) {
                drv_press(GBA_A);
                drv.phase = PH_BAG_CONFIRM;
                drv.holdoff = 10;
                drv.confirms = 1;
                return;
            }
            if (cur < 0 || ++drv.presses > 40) { drv_stop(); return; }
            drv_press(cur < drv.index ? GBA_DOWN : GBA_UP);
            return;
        }
        if (drv.phase == PH_BAG_CONFIRM) {
            drv_press(GBA_A);
            if (drv.berry >= 0) {
                drv.phase = PH_BERRY;
                drv.presses = 0;
                drv.holdoff = 20;
            } else if (drv.mon >= 0) {
                drv.phase = PH_WAIT_PARTY;
                drv.slot = drv.mon;
                drv.confirms = 1;
                drv.timer = 0;
                drv.holdoff = 8;
            } else drv_finish();
            return;
        }
        // Berry pouch: it opens on the first berry. Walk down, then A.
        if (drv.presses < drv.berry) {
            drv_press(GBA_DOWN);
            drv.presses++;
            return;
        }
        drv_press(GBA_A);
        if (drv.mon >= 0) {
            drv.phase = PH_WAIT_PARTY;
            drv.slot = drv.mon;
            drv.confirms = 1;
            drv.timer = 0;
            drv.holdoff = 12;
        } else drv_finish();
    }
}

static uint32_t drv_keys(void) {
    drv_step();
    if (!drv.n) return 0;
    uint32_t keys = drv.seq[drv.i].keys;
    if (--drv.left <= 0) {
        drv.i++;
        if (drv.i >= drv.n) drv.n = 0;
        else drv.left = drv.seq[drv.i].hold;
    }
    return keys;
}

static bool pad_pressed(SDL_GameController* pad, SDL_GameControllerButton b) {
    return pad && SDL_GameControllerGetButton(pad, b);
}

static void install_anbernic_pad(void) {
    /* Same letter map SimpleOS uses: b0 is the button labeled A. */
    static const char bind[] =
        "a:b0,b:b1,x:b3,y:b2,back:b6,guide:b8,start:b7,"
        "leftstick:b9,rightstick:b12,leftshoulder:b4,rightshoulder:b5,"
        "dpup:h0.1,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:b10,righttrigger:b11,"
        "platform:Linux,";
    char line[512];
    snprintf(line, sizeof(line), "1900b655010000000100000000010000,ANBERNIC-rk3568-keys,%s", bind);
    SDL_GameControllerAddMapping(line);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        const char* nm = SDL_JoystickNameForIndex(i);
        if (!nm || (!strstr(nm, "ANBERNIC") && !strstr(nm, "rk3568"))) continue;
        char guid[64];
        SDL_JoystickGUID g = SDL_JoystickGetDeviceGUID(i);
        SDL_JoystickGetGUIDString(g, guid, sizeof(guid));
        snprintf(line, sizeof(line), "%s,%s,%s", guid, nm, bind);
        SDL_GameControllerAddMapping(line);
    }
}

static void open_first_pad(void) {
    if (g.pad) return;
    install_anbernic_pad();
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

static bool apply_actions(struct pd_ui* ui, struct pd_host_info* host, bool* ffToggled, bool* leave,
                          struct mCore* core, const char* statePath, const char* savePath, const char* base) {
    enum pd_action action = pd_ui_take_action(ui);
    switch (action) {
    case PD_ACTION_TOGGLE_FF: *ffToggled = !*ffToggled; break;
    case PD_ACTION_SAVE_STATE:
        if (core) save_state(core, statePath);
        host->hasState = state_info(statePath, host->stateWhen, sizeof(host->stateWhen));
        break;
    case PD_ACTION_LOAD_STATE:
        if (core) { backup_save(base, savePath); load_state(core, statePath); }
        break;
    case PD_ACTION_LEAVE_GAME: *leave = true; break;
    case PD_ACTION_SETTINGS_CHANGED:
        pd_style_set_theme(g.settings.theme);
        pd_settings_save(&g.settings, g.paths.settings);
        break;
    case PD_ACTION_BATTLE_FIGHT:
        drv_stop();
        drv_cell(0);
        break;
    case PD_ACTION_BATTLE_MOVE:
        drv_stop();
        drv_cell(ui->actionArg);
        break;
    case PD_ACTION_BATTLE_BACK:
        drv_stop();
        drv_press(GBA_B);
        break;
    case PD_ACTION_BATTLE_PKMN:
    case PD_ACTION_BATTLE_SWITCH:
        drv_stop();
        if (ui->battleMenu != PD_BMENU_PARTY) drv_cell(2);
        drv.slot = ui->actionArg;
        drv.confirms = 2;
        drv_arm(PH_WAIT_PARTY);
        break;
    case PD_ACTION_BATTLE_RUN:
        drv_stop();
        drv_cell(3);
        drv.confirms = 4;
        drv_arm(PH_RUN_B);
        break;
    case PD_ACTION_BATTLE_ITEM:
        drv_stop();
        if (ui->battleMenu != PD_BMENU_BAG) drv_cell(1);
        drv.pocket = ui->actionArg;
        drv.index = ui->actionArg2;
        drv.berry = ui->actionArg3;
        drv.mon = ui->actionArg4;
        drv_arm(PH_WAIT_BAG);
        break;
    case PD_ACTION_RA_LOGIN: {
        char token[48], err[64];
        if (pd_ra_login(ui->settings->raUser, ui->settings->raPass, token, sizeof(token), err, sizeof(err))) {
            snprintf(ui->settings->raToken, sizeof(ui->settings->raToken), "%s", token);
            snprintf(ui->settings->raNote, sizeof(ui->settings->raNote), "Signed in.");
            pd_settings_save(ui->settings, g.paths.settings);
        } else {
            snprintf(ui->settings->raNote, sizeof(ui->settings->raNote), "%s", err);
        }
        ui->settings->raPass[0] = 0;
        break;
    }
    case PD_ACTION_NONE: break;
    }
    return action != PD_ACTION_NONE;
}

static bool handle_global_event(SDL_Event* e, struct video* v, struct pd_ui* ui, bool* redraw, bool* touching) {
    if (e->type == SDL_QUIT) { g.running = false; return true; }
    if (e->type == SDL_CONTROLLERDEVICEADDED) open_first_pad();
    if (e->type == SDL_TEXTINPUT && ui && ui->tab == PD_TAB_SETTINGS &&
        (ui->overlay == PD_OVERLAY_NONE || ui->overlay == PD_OVERLAY_RA)) {
        if (e->text.text[0]) pd_ui_search_type(ui, (unsigned char) e->text.text[0]);
        *redraw = true;
    }
    if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_BACKSPACE && ui && ui->tab == PD_TAB_SETTINGS &&
        (ui->overlay == PD_OVERLAY_NONE || ui->overlay == PD_OVERLAY_RA)) {
        pd_ui_search_type(ui, -1);
        *redraw = true;
    }
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
        if (g.quick && g.wildOpen && onUi) {
            bool down = (buttons & SDL_BUTTON_LMASK) != 0;
            if (e->type == SDL_MOUSEBUTTONUP) down = false;
            if (e->type == SDL_MOUSEBUTTONDOWN) down = true;
            *redraw = true;
            *touching = down;
            return false;
        }
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
            if (g.quick && g.wildOpen && onUi) {
                *redraw = true;
                *touching = e->type != SDL_FINGERUP;
                return false;
            }
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
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 1, NULL, 0, NULL, NULL);
            redraw = false;
        } else {
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 0, NULL, 0, NULL, NULL);
        }
        SDL_Delay(16);
    }
    return g.running;
}

static int battery_pct(void) {
    static const char* paths[] = {
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/BAT0/capacity",
        "/sys/class/power_supply/axp2202-battery/capacity",
    };
    for (int i = 0; i < 3; i++) {
        FILE* f = fopen(paths[i], "r");
        if (!f) continue;
        int n = -1;
        if (fscanf(f, "%d", &n) != 1) n = -1;
        fclose(f);
        if (n >= 0 && n <= 100) return n;
    }
    return -1;
}

static void fill_status(char* out, size_t len, const struct pd_game* game, const struct pd_snapshot* s) {
    char money[16] = "";
    if (s->money >= 0) snprintf(money, sizeof(money), "$%ld", s->money);
    const char* place = s->valid ? pd_mapsec_name(game, s->mapsec) : "";
    char clock[8];
    time_t now = time(NULL);
    strftime(clock, sizeof(clock), "%H:%M", localtime(&now));
    int bat = battery_pct();
    char batText[8] = "";
    if (bat >= 0) snprintf(batText, sizeof(batText), "%d%%", bat);
    snprintf(out, len, "%s  %s  %s  %s  %s", game->title ? game->title : "", place && *place ? place : "", money, clock,
             batText);
}

static void fill_hud(struct hud_view* hud, const struct pd_snapshot* s) {
    hud->count = 0;
    if (!s->valid || s->inBattle) return;
    for (int i = 0; i < s->partyCount && i < 6; i++) {
        snprintf(hud->line[hud->count], sizeof(hud->line[0]), "L%d", s->party[i].level);
        hud->hp[hud->count] = s->party[i].hp;
        hud->maxHp[hud->count] = s->party[i].maxHp;
        hud->count++;
    }
}

enum { Q_PC, Q_FLY, Q_WILD, Q_SAVE, Q_LOAD, Q_FF };

static const char* const QUICK_ITEMS[] = {
    "PC", "FLY", "WILD", "SAVE STATE", "LOAD STATE", "FF",
};
_Static_assert(sizeof(QUICK_ITEMS) / sizeof(QUICK_ITEMS[0]) == QUICK_N, "quick menu size");

static int party_knows_fly(const struct pd_snapshot* snap) {
    if (!snap || !snap->valid) return 0;
    for (int i = 0; i < snap->partyCount && i < 6; i++) {
        if (snap->party[i].species <= 0 || snap->party[i].species == PD_SPECIES_EGG) continue;
        for (int m = 0; m < 4; m++)
            if (snap->party[i].moves[m] == 19) return 1;
    }
    return 0;
}

// Why the aimed tile can't run, or the WILD banner. NULL hides the middle.
static const char* quick_note(int row, const struct pd_snapshot* snap, int hasState) {
    int battle = snap && snap->inBattle;
    switch (row) {
    case Q_PC: return battle ? "Not during a battle" : NULL;
    case Q_FLY:
        if (battle) return "Not during a battle";
        if (!party_knows_fly(snap)) return "No one knows FLY";
        return NULL;
    case Q_WILD: return "WILD ENCOUNTERS";
    case Q_LOAD: return hasState ? NULL : "none yet";
    default: return NULL;
    }
}

static unsigned quick_blocked(const struct pd_snapshot* snap, int hasState) {
    unsigned b = 0;
    if (quick_note(Q_PC, snap, hasState)) b |= 1u << Q_PC;
    if (quick_note(Q_FLY, snap, hasState)) b |= 1u << Q_FLY;
    if (quick_note(Q_LOAD, snap, hasState)) b |= 1u << Q_LOAD;
    return b;
}

// False when the tile could not run, so the wheel stays up.
static bool quick_do(struct pd_ui* ui, int row, bool* ffToggled, struct mCore* core, struct game_mem* mem,
                     const struct pd_game* game, const struct pd_snapshot* snap, int hasState) {
    if (quick_blocked(snap, hasState) & (1u << row)) return false;
    switch (row) {
    case Q_PC:
        if (!pd_pc_open(core, game, mem->ewram, mem->ewramSize, snap && snap->inBattle)) {
            fprintf(stderr, "pokedaisy: the PC isn't available right now\n");
            return false;
        }
        break;
    case Q_FLY: pd_ui_open_tab(ui, PD_TAB_MAP); break;
    case Q_WILD: return false;
    case Q_SAVE: ui->action = PD_ACTION_SAVE_STATE; break;
    case Q_LOAD: ui->action = PD_ACTION_LOAD_STATE; break;
    case Q_FF: *ffToggled = !*ffToggled; break;
    default: break;
    }
    return true;
}

// The wheel sits on the top screen, over a dimmed copy of the game, the way
// the Android ring does. 320x240 stretched across that screen.
static void paint_wheel(uint32_t* dst, const uint32_t* game, int bgr, int shader, const char* const* items, int n,
                        int sel, const char* note, unsigned blocked) {
    for (int y = 0; y < PD_UI_HEIGHT; y++) {
        int sy = y * GBA_H / PD_UI_HEIGHT;
        for (int x = 0; x < PD_UI_WIDTH; x++) {
            int sx = x * GBA_W / PD_UI_WIDTH;
            uint32_t p = game[sy * GBA_W + sx];
            if (bgr) p = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
            p = shade_pixel(p, shader, sx, sy);
            dst[y * PD_UI_WIDTH + x] = p & 0xFFFFFFu;
        }
    }
    struct pd_canvas top;
    pd_canvas_init(&top, PD_UI_WIDTH, PD_UI_HEIGHT, dst);
    pd_quick_draw(&top, items, n, sel, note, blocked);
}

static bool run_game(const char* file) {
    char romPath[PATH_MAX], loadPath[PATH_MAX], savePath[PATH_MAX], statePath[PATH_MAX], base[256];
    snprintf(romPath, sizeof(romPath), "%s/%s", g.paths.roms, file);
    if (file[0] == '/' || (file[0] == '.' && file[1] == '/')) snprintf(romPath, sizeof(romPath), "%s", file);
    snprintf(base, sizeof(base), "%s", strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
    char* dot = strrchr(base, '.');
    if (dot && dot != base) *dot = 0;
    snprintf(savePath, sizeof(savePath), "%s/%s.sav", g.paths.saves, base);
    snprintf(statePath, sizeof(statePath), "%s/%s.ss1", g.paths.states, base);
    snprintf(loadPath, sizeof(loadPath), "%s", romPath);
    if (ends_with(romPath, ".zip")) {
        snprintf(loadPath, sizeof(loadPath), "%s/unpacked.gba", g.paths.home);
        if (!pd_zip_extract_rom(romPath, loadPath)) {
            fprintf(stderr, "pokedaisy: no GBA game inside %s\n", romPath);
            return true;
        }
    }

    struct VFile* rom = VFileOpen(loadPath, O_RDONLY);
    if (!rom) {
        fprintf(stderr, "pokedaisy: cannot open %s\n", loadPath);
        return true;
    }
    uint8_t header[0xC0] = { 0 };
    rom->seek(rom, 0, SEEK_SET);
    rom->read(rom, header, sizeof(header));
    rom->seek(rom, 0, SEEK_SET);
    struct pd_game game;
    pd_game_detect(&game, header, (size_t) rom->size(rom));
    if (!strcmp(game.code, "BPEE") && rom->size(rom) > 0x1000000) {
        char hex[41];
        if (pd_sha1_file(loadPath, hex)) {
            pd_game_apply_sha1(&game, hex);
            if (!game.title || strcmp(game.title, "Pokémon Lazarus"))
                fprintf(stderr, "pokedaisy: large Emerald hack %s uses retail addresses\n", hex);
        }
    }

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
    refresh_views(core, &mem, &game);
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
    g.quick = 0;
    g.quickFire = -1;
    drv_stop();
    bool redraw = true, touching = false, leave = false, ffToggled = false, quickRelease = false;
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
        static int prevL3, prevR3, prevQuickUp, prevQuickDown, prevQuickA, prevQuickB;
        int l3 = pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_LEFTSTICK) || kb[SDL_SCANCODE_F1];
        int r3 = pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_RIGHTSTICK) || kb[SDL_SCANCODE_F2];
        if (l3 && !prevL3) {
            g.quick = !g.quick;
            g.quickSel = 0;
            g.wildOpen = 0;
            redraw = true;
        }
        if (r3 && !prevR3 && !g.quick) {
            g.settings.screenMode = (g.settings.screenMode + 1) % PD_SCREEN_MODES;
            pd_settings_save(&g.settings, g.paths.settings);
            redraw = true;
        }
        prevL3 = l3;
        prevR3 = r3;
        if (g.quick) {
            int up = kb[SDL_SCANCODE_UP] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
            int down = kb[SDL_SCANCODE_DOWN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            int left = kb[SDL_SCANCODE_LEFT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            int right = kb[SDL_SCANCODE_RIGHT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            int a = kb[SDL_SCANCODE_RETURN] || kb[SDL_SCANCODE_Z] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_A);
            int b = kb[SDL_SCANCODE_X] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_B) ||
                    pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_Y);
            if (g.wildOpen) {
                int dir = 0;
                if ((up || left) && !prevQuickUp) dir = -1;
                if ((down || right) && !prevQuickDown) dir = 1;
                if (g.pad) {
                    Sint16 ay = SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_LEFTY);
                    static Uint32 wildNext;
                    if (ay < -12000 || ay > 12000) {
                        Uint32 now = SDL_GetTicks();
                        if (now >= wildNext) {
                            dir = ay > 0 ? 1 : -1;
                            wildNext = now + 160;
                        }
                    } else {
                        wildNext = 0;
                    }
                }
                if (dir) {
                    g.wildSel = pd_wild_move(&game, &shown, g.wildSel, dir);
                    redraw = true;
                }
                if (b && !prevQuickB) { g.wildOpen = 0; redraw = true; }
            } else {
                if ((up || left) && !prevQuickUp) {
                    g.quickSel = (g.quickSel + QUICK_N - 1) % QUICK_N;
                    redraw = true;
                }
                if ((down || right) && !prevQuickDown) {
                    g.quickSel = (g.quickSel + 1) % QUICK_N;
                    redraw = true;
                }
                if (g.pad) {
                    int aim = pd_quick_aim(SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_LEFTX),
                                           SDL_GameControllerGetAxis(g.pad, SDL_CONTROLLER_AXIS_LEFTY), QUICK_N);
                    if (aim >= 0 && aim != g.quickSel) {
                        g.quickSel = aim;
                        redraw = true;
                    }
                }
                if (a && !prevQuickA) g.quickFire = g.quickSel;
                if (b && !prevQuickB) { g.quick = 0; redraw = true; }
            }
            prevQuickUp = up || left;
            prevQuickDown = down || right;
            prevQuickA = a;
            prevQuickB = b;
        } else {
            prevQuickUp = prevQuickDown = prevQuickA = prevQuickB = 0;
        }
        if (g.quickFire >= 0) {
            int row = g.quickFire;
            g.quickFire = -1;
            if (row == Q_WILD) {
                g.wildOpen = 1;
                g.wildSel = pd_wild_move(&game, &shown, 0, 0);
            } else if (quick_do(&ui, row, &ffToggled, core, &mem, &game, &shown, host.hasState)) {
                g.quick = 0;
                // The press that chose the row is still down. Held into the game,
                // it confirms the first PC line (or the START menu) on the same frame.
                quickRelease = true;
            }
            redraw = true;
        }
        if (!g.quick) poll_companion_keys(&ui, kb, g.pad, &leaveHeld, &redraw);
        if (leaveHeld >= LEAVE_HOLD) break;
        if (ui.overlay == PD_OVERLAY_RA && !g.quick) {
            static int raU, raD, raL, raR, raA;
            int ru = kb[SDL_SCANCODE_UP] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
            int rd = kb[SDL_SCANCODE_DOWN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            int rl = kb[SDL_SCANCODE_LEFT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            int rr = kb[SDL_SCANCODE_RIGHT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            int ra = kb[SDL_SCANCODE_RETURN] || kb[SDL_SCANCODE_Z] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_A);
            int dir = 0;
            if (ru && !raU) dir = 1;
            else if (rd && !raD) dir = 2;
            else if (rl && !raL) dir = 3;
            else if (rr && !raR) dir = 4;
            else if (ra && !raA) dir = 5;
            if (dir && pd_ui_ra_key(&ui, dir)) redraw = true;
            else if (dir) redraw = true;
            raU = ru;
            raD = rd;
            raL = rl;
            raR = rr;
            raA = ra;
        }
        if (apply_actions(&ui, &host, &ffToggled, &leave, core, statePath, savePath, base)) redraw = true;
        if (leave) break;

        bool ff = ffToggled || kb[SDL_SCANCODE_SPACE] || pad_ff(g.pad);
        if (ff != host.ffOn) {
            host.ffOn = ff;
            g.audioMuted = ff;
            redraw = true;
        }
        uint32_t rawKeys = keys_from_keyboard(kb) | keys_from_pad(g.pad, g.nintendo);
        if (quickRelease && (rawKeys & (GBA_A | GBA_B)) == 0) quickRelease = false;
        uint32_t player = (g.quick || quickRelease || ui.overlay == PD_OVERLAY_RA) ? 0 : rawKeys;
        drv.game = &game;
        drv.mem = &mem;
        if (player && (drv.n || drv.phase)) drv_stop();
        int frames = ff ? g.settings.ffSpeed : 1;
        for (int i = 0; i < frames; i++) {
            uint32_t keys = player;
            if (!player && (drv.n || drv.phase)) keys = drv_keys();
            core->setKeys(core, keys);
            core->runFrame(core);
            frame++;
        }
        refresh_views(core, &mem, &game);
        if (mem.iwram && pd_ui_set_battle_menu(&ui, pd_battle_menu(&game, bus_read, &mem))) redraw = true;
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
        char status[96];
        status[0] = 0;
        if (g.settings.statusBar) fill_status(status, sizeof(status), &game, &shown);
        struct hud_view hud;
        fill_hud(&hud, &shown);
        const struct hud_view* hudPtr = g.settings.hud ? &hud : NULL;
        const uint32_t* cover = NULL;
        if (g.quick && !g.wildOpen) {
            paint_wheel(splashPx, gamePx, g.video.gameBgr, g.settings.shader, QUICK_ITEMS, QUICK_N, g.quickSel,
                        quick_note(g.quickSel, &shown, host.hasState), quick_blocked(&shown, host.hasState));
            cover = splashPx;
        }
        if (redraw) {
            pd_ui_draw(&ui, &bottom, &game, &shown, &host);
            if (g.quick && g.wildOpen) pd_wild_draw(&bottom, &game, &shown, g.wildSel);
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 1, status, g.settings.shader, hudPtr, cover);
            redraw = false;
        } else {
            present_screens(&g.video, g.settings.screenMode, gamePx, uiPx, 0, status, g.settings.shader, hudPtr, cover);
        }
    }
    g.quick = 0;
    g.video.gameBgr = 0;
    g.audioMuted = false;
    if (g.audio) SDL_ClearQueuedAudio(g.audio);
    core->deinit(core);
    return g.running;
}

static void ra_try_login(void) {
    char token[48], err[64];
    if (pd_ra_login(g.settings.raUser, g.settings.raPass, token, sizeof(token), err, sizeof(err))) {
        snprintf(g.settings.raToken, sizeof(g.settings.raToken), "%s", token);
        snprintf(g.settings.raNote, sizeof(g.settings.raNote), "Signed in.");
        pd_settings_save(&g.settings, g.paths.settings);
    } else {
        snprintf(g.settings.raNote, sizeof(g.settings.raNote), "%s", err);
    }
    g.settings.raPass[0] = 0;
}

static void run_setup(void) {
    struct pd_setup setup;
    pd_setup_init(&setup);
    struct pd_canvas bottom;
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, uiPx);
    struct pd_canvas top;
    pd_canvas_init(&top, PD_UI_WIDTH, PD_UI_HEIGHT, splashPx);
    pd_splash_draw(&top, "Settings are on", "the bottom screen.", "Y or B returns to the list.");
    bool dirty = true;
    int armed = 0;
    SDL_StartTextInput();
    while (g.running) {
        int leave = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { g.running = false; break; }
            if (e.type == SDL_CONTROLLERDEVICEADDED) open_first_pad();
            if (e.type == SDL_TEXTINPUT && e.text.text[0]) {
                if (pd_setup_ra_open(&setup)) pd_setup_ra_type(&setup, &g.settings, (unsigned char) e.text.text[0]);
                else pd_setup_type(&setup, (unsigned char) e.text.text[0]);
                dirty = true;
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE) {
                    if (pd_setup_ra_open(&setup)) pd_setup_ra_type(&setup, &g.settings, -1);
                    else pd_setup_type(&setup, -1);
                    dirty = true;
                }
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_F4) {
                    if (pd_setup_ra_open(&setup)) pd_setup_ra_close(&setup, &g.settings);
                    else leave = 1;
                    dirty = true;
                }
            }
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP) {
                int ox, oy, onUi;
                finger_to_ui(&g.video, e.tfinger.x, e.tfinger.y, &ox, &oy, &onUi);
                if (onUi && !pd_setup_ra_open(&setup) &&
                    pd_setup_touch(&setup, &g.settings, ox, oy, e.type != SDL_FINGERUP)) {
                    pd_setup_adjust(&setup, &g.settings, 1);
                    dirty = true;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.which == SDL_TOUCH_MOUSEID) continue;
                int ox, oy, onUi;
                SDL_Window* win = SDL_GetMouseFocus();
                if (!win) win = g.video.winTop;
                map_touch(&g.video, win, e.button.x, e.button.y, &ox, &oy, &onUi);
                if (onUi && !pd_setup_ra_open(&setup) &&
                    pd_setup_touch(&setup, &g.settings, ox, oy, e.type == SDL_MOUSEBUTTONDOWN)) {
                    pd_setup_adjust(&setup, &g.settings, 1);
                    dirty = true;
                }
            }
        }
        const Uint8* kb = SDL_GetKeyboardState(NULL);
        static int prevUp, prevDown, prevLeft, prevRight, prevB, prevA;
        int up = kb[SDL_SCANCODE_UP] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        int down = kb[SDL_SCANCODE_DOWN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        int left = kb[SDL_SCANCODE_LEFT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        int right = kb[SDL_SCANCODE_RIGHT] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        int a = kb[SDL_SCANCODE_RETURN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_A);
        int b = kb[SDL_SCANCODE_X] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_B) ||
                pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_Y);
        if (!b) armed = 1;
        if (pd_setup_ra_open(&setup)) {
            int dir = 0;
            if (up && !prevUp) dir = 1;
            else if (down && !prevDown) dir = 2;
            else if (left && !prevLeft) dir = 3;
            else if (right && !prevRight) dir = 4;
            else if (a && !prevA) dir = 5;
            if (dir) {
                int r = pd_setup_ra_key(&setup, &g.settings, dir);
                if (r == 1) ra_try_login();
                else if (r == 2) pd_settings_save(&g.settings, g.paths.settings);
                dirty = true;
            }
            if (armed && b && !prevB) {
                pd_setup_ra_close(&setup, &g.settings);
                dirty = true;
            }
        } else {
            if (up && !prevUp) { pd_setup_move(&setup, &g.settings, -1); dirty = true; }
            if (down && !prevDown) { pd_setup_move(&setup, &g.settings, 1); dirty = true; }
            if (left && !prevLeft) { pd_setup_adjust(&setup, &g.settings, -1); dirty = true; }
            if ((right && !prevRight) || (a && !prevA)) { pd_setup_adjust(&setup, &g.settings, 1); dirty = true; }
            if (leave || (armed && b && !prevB)) break;
        }
        if (leave && !pd_setup_ra_open(&setup)) break;
        prevUp = up; prevDown = down; prevLeft = left; prevRight = right; prevA = a; prevB = b;
        if (dirty) {
            pd_setup_draw(&setup, &bottom, &g.settings);
            present_menu(&g.video, splashPx, uiPx, 1);
            dirty = false;
        }
        SDL_Delay(16);
    }
    pd_style_set_theme(g.settings.theme);
    pd_settings_save(&g.settings, g.paths.settings);
}

static bool pick_rom(char* out, size_t outLen) {
    char* names[MAX_ROMS];
    int n = list_roms(names);
    int total = n;
    static char labels[MAX_ROMS][128];
    const char* items[MAX_ROMS];
    for (int i = 0; i < total; i++) {
        snprintf(labels[i], sizeof(labels[i]), "%s", names[i]);
        size_t L = strlen(labels[i]);
        if (L > 4 && !strcasecmp(labels[i] + L - 4, ".zip")) { labels[i][L - 4] = 0; L -= 4; }
        if (L > 4 && !strcasecmp(labels[i] + L - 4, ".gba")) labels[i][L - 4] = 0;
        items[i] = labels[i];
    }
    struct pd_menu menu;
    pd_menu_init(&menu, "CHOOSE A GAME", items, total);
    struct pd_canvas top, bottom;
    pd_canvas_init(&top, PD_UI_WIDTH, PD_UI_HEIGHT, splashPx);
    pd_canvas_init(&bottom, PD_UI_WIDTH, PD_UI_HEIGHT, uiPx);
    pd_splash_draw(&top, NULL, NULL, "Y opens settings.");

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
                    if (!down && row == -2) { run_setup(); dirty = true; }
                    else if (!down && row >= 0) { picked = true; goto done; }
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
                    if (!down && row == -2) { run_setup(); dirty = true; }
                    else if (!down && row >= 0) { picked = true; goto done; }
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
        static int prevUp, prevDown, prevA, prevStart, prevY;
        int up = kb[SDL_SCANCODE_UP] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        int down = kb[SDL_SCANCODE_DOWN] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        int a = kb[SDL_SCANCODE_RETURN] || kb[SDL_SCANCODE_Z] || kb[SDL_SCANCODE_K] ||
                pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_A);
        int y = kb[SDL_SCANCODE_TAB] || pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_Y) ||
                pad_pressed(g.pad, SDL_CONTROLLER_BUTTON_X);
        int start = kb[SDL_SCANCODE_F4];
        if (up && !prevUp) { pd_menu_move(&menu, -1); dirty = true; }
        if (down && !prevDown) { pd_menu_move(&menu, 1); dirty = true; }
        if (y && !prevY) { run_setup(); dirty = true; }
        if (a && !prevA && total > 0) { picked = true; break; }
        if (start && !prevStart) { g.running = false; break; }
        prevUp = up; prevDown = down; prevA = a; prevStart = start; prevY = y;
        if (dirty) {
            pd_menu_draw(&menu, &bottom);
            present_menu(&g.video, splashPx, uiPx, 1);
            dirty = false;
        }
        SDL_Delay(16);
    }
done:
    if (picked && menu.selected >= 0 && menu.selected < n) snprintf(out, outLen, "%s", names[menu.selected]);
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
    puts("  --roms DIR          where to look for .gba and .zip files");
    puts("  --demo [kind]       companion demo without a ROM");
    puts("                      kind: firered | emerald | battle");
    puts("  --shot FILE.png     draw one dual-screen frame and exit");
    puts("  --nintendo          face buttons: east=A, south=B (default, RG DS)");
    puts("  --xbox              face buttons: south=A, east=B");
    puts("  --help");
    puts("");
    puts("Controls: D-pad / left stick, A/B, Start, Select, L/R.");
    puts("  X next companion tab, Y back, R2 or Space fast-forward.");
    puts("  L3 or F1 opens the wheel on the top screen. The left stick aims, A runs the tile.");
    puts("  R3 or F2 cycles ORIGINAL / FIT / STRETCH.");
    puts("  On the game list, Y or the SETTINGS gear opens settings.");
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
    pd_style_set_theme(g.settings.theme);
    g.quickFire = -1;
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
        SDL_StartTextInput();
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
