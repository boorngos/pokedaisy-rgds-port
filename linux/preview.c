// Software dual-screen preview: 640x480 game + 640x480 companion (RG DS).
// Used to generate web/shots and --shot PNGs without a display.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pd_canvas.h"
#include "pd_game.h"
#include "pd_snapshot.h"
#include "pd_ui.h"
#include "pd_png.h"
#include "pd_menu.h"
#include "pd_settings.h"

#define RG_W 640
#define RG_H 480
#define GBA_W 240
#define GBA_H 160

static uint8_t ewram[0x40000];
static uint8_t iwram[0x8000];

static bool load_bin(const char* dir, const char* name, uint8_t* buf, size_t len) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len;
}

static bool fixture_read(void* ctx, uint32_t addr, void* out, size_t len) {
    if (pd_rom_read(ctx, addr, out, len)) return true;
    if (addr >= 0x02000000 && addr + len <= 0x02040000) {
        memcpy(out, ewram + (addr - 0x02000000), len);
        return true;
    }
    if (addr >= 0x03000000 && addr + len <= 0x03008000) {
        memcpy(out, iwram + (addr - 0x03000000), len);
        return true;
    }
    return false;
}

static void scale2x(uint32_t* dst, int dw, const uint32_t* src, int sw, int sh, int ox, int oy) {
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            uint32_t p = src[y * sw + x];
            int dx = ox + x * 2, dy = oy + y * 2;
            dst[dy * dw + dx] = p;
            dst[dy * dw + dx + 1] = p;
            dst[(dy + 1) * dw + dx] = p;
            dst[(dy + 1) * dw + dx + 1] = p;
        }
    }
}

static void draw_fake_game(uint32_t* top, const char* title, const char* line) {
    struct pd_canvas c;
    pd_canvas_init(&c, GBA_W, GBA_H, top);
    pd_fill(&c, 0, 0, GBA_W, GBA_H, 0x38A038);
    pd_fill(&c, 0, 0, GBA_W, 16, 0x181830);
    pd_text(&c, 8, 2, title, 0xF8F8F8, 0x101018);
    pd_fill(&c, 16, 40, 208, 80, 0xF8F0D0);
    pd_text(&c, 24, 48, "Anbernic RG DS", 0x181830, PD_NO_SHADOW);
    pd_text(&c, 24, 64, "640 x 480 x 2", 0x181830, PD_NO_SHADOW);
    pd_text(&c, 24, 88, line, 0x403020, PD_NO_SHADOW);
}

static void compose_rgds(uint32_t* out, const uint32_t* game, const uint32_t* companion) {
    for (int i = 0; i < RG_W * RG_H * 2; i++) out[i] = 0x101018;
    int gx = (RG_W - GBA_W * 2) / 2;
    int gy = (RG_H - GBA_H * 2) / 2;
    scale2x(out, RG_W, game, GBA_W, GBA_H, gx, gy);
    scale2x(out + RG_W * RG_H, RG_W, companion, PD_UI_WIDTH, PD_UI_HEIGHT, 0, 0);
}

static void shot(const char* dir, const char* name, const uint32_t* game, const uint32_t* bottom) {
    static uint32_t img[RG_W * RG_H * 2];
    compose_rgds(img, game, bottom);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    if (!pd_png_write(path, RG_W, RG_H * 2, img)) {
        fprintf(stderr, "preview: cannot write %s\n", path);
        exit(1);
    }
    printf("%s\n", path);
}

static void fake_header(uint8_t* header, const char* code, int rev) {
    memset(header, 0, 0xC0);
    memcpy(header + 0xAC, code, 4);
    header[0xBC] = (uint8_t) rev;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: pokedaisy-preview <fixture-dir> <BPRE|BPEE> <out-dir> [name]\n");
        return 1;
    }
    const char* fix = argv[1];
    const char* code = argv[2];
    const char* out = argv[3];
    const char* name = argc > 4 ? argv[4] : "shot";

    if (!load_bin(fix, "ewram.bin", ewram, sizeof(ewram)) ||
        !load_bin(fix, "iwram.bin", iwram, sizeof(iwram))) {
        fprintf(stderr, "preview: missing ewram.bin / iwram.bin in %s\n", fix);
        return 1;
    }

    uint8_t header[0xC0];
    fake_header(header, code, strcmp(code, "BPRE") == 0 ? 1 : 0);
    struct pd_game game;
    pd_game_detect(&game, header, 0x800000);

    struct pd_snapshot snap;
    pd_snapshot_read(&snap, &game, fixture_read, &game);

    struct pd_settings settings;
    pd_settings_defaults(&settings);
    struct pd_ui ui;
    pd_ui_init(&ui, &settings);
    pd_ui_update(&ui, &snap);

    struct pd_host_info host = { .count = 2, .ffOn = false, .hasState = false };
    host.labels[0] = "FILE";
    snprintf(host.values[0], sizeof(host.values[0]), "demo");
    host.labels[1] = "SPEED";
    snprintf(host.values[1], sizeof(host.values[1]), "RG DS");

    static uint32_t gba[GBA_W * GBA_H];
    static uint32_t bottom[PD_UI_WIDTH * PD_UI_HEIGHT];
    struct pd_canvas bc;
    pd_canvas_init(&bc, PD_UI_WIDTH, PD_UI_HEIGHT, bottom);

    draw_fake_game(gba, game.title ? game.title : "PokeDaisy", "fixture RAM, no ROM");

    pd_ui_draw(&ui, &bc, &game, &snap, &host);
    shot(out, name, gba, bottom);

    // Second shot: SETTINGS
    ui.tab = PD_TAB_SETTINGS;
    pd_ui_draw(&ui, &bc, &game, &snap, &host);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s-settings", name);
    shot(out, buf, gba, bottom);

    if (snap.inBattle) {
        ui.tab = PD_TAB_BATTLE;
        pd_ui_update(&ui, &snap);
        pd_ui_draw(&ui, &bc, &game, &snap, &host);
        snprintf(buf, sizeof(buf), "%s-battle", name);
        shot(out, buf, gba, bottom);
    } else {
        ui.tab = PD_TAB_BAG;
        pd_ui_draw(&ui, &bc, &game, &snap, &host);
        snprintf(buf, sizeof(buf), "%s-bag", name);
        shot(out, buf, gba, bottom);
    }
    return 0;
}
