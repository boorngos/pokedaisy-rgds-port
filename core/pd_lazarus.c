#include "pd_lazarus.h"

#include <stdio.h>
#include <string.h>

#include "pd_snapshot.h"

// gSpeciesInfo. Entry 0 is at this address; speciesName is +0x2C.
#define SPECIES_BASE 0x08C7A338u
#define SPECIES_STRIDE 0xD4u
#define NAME_OFF 0x2Cu
#define TYPES_OFF 0x06
#define GENDER_OFF 0x12
#define ABILITY_OFF 0x18
#define ABILITY_NAMES 0x088DD968u
#define ABILITY_STRIDE 0x1Cu
// gItemsInfo[0].name, inline, description pointer 8 bytes before it.
#define ITEM_NAME0 0x0886855Cu
#define ITEM_STRIDE 0x50u
// gMovesInfo[0]: name pointer at +0, type:5 | category:2 | power:9 at +0x0A.
#define MOVE_INFO0 0x088CFAC8u
#define MOVE_STRIDE 0x34u
#define TYPE_CHART 0x08571B78u
#define CHART_N 21
// gRegionMapEntries: {u8 x, y, w, h, name*}.
#define MAPSEC_BASE 0x08CE3E2Cu
#define MAP_LAYOUT 0x08CE35C8u

static const struct pd_game* bound;

bool pd_is_lazarus(const struct pd_game* g) {
    return g && g->title && strstr(g->title, "Lazarus");
}

void pd_bind_game(const struct pd_game* g) {
    bound = g;
}

const struct pd_game* pd_bound_game(void) {
    return bound;
}

static uint16_t u16(const uint8_t* p) {
    return (uint16_t) (p[0] | p[1] << 8);
}

static uint32_t u32(const uint8_t* p) {
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static bool text_at(const struct pd_game* g, uint32_t addr, int maxLen, char* buf, size_t len) {
    const uint8_t* p = pd_rom_at(g, addr, (size_t) maxLen);
    if (!p) {
        if (len) buf[0] = 0;
        return false;
    }
    pd_gen3_text(p, (size_t) maxLen, buf, len);
    return buf[0] != 0;
}

const uint8_t* pd_laz_species(const struct pd_game* g, int species) {
    if (!g || species < 0 || species > PD_LAZ_SPECIES_MAX) return NULL;
    return pd_rom_at(g, SPECIES_BASE + (uint32_t) species * SPECIES_STRIDE, SPECIES_STRIDE);
}

bool pd_laz_species_name(const struct pd_game* g, int species, char* buf, size_t len) {
    if (species == PD_LAZ_EGG) {
        snprintf(buf, len, "EGG");
        return true;
    }
    const uint8_t* e = pd_laz_species(g, species);
    if (!e) {
        if (len) buf[0] = 0;
        return false;
    }
    pd_gen3_text(e + NAME_OFF, 13, buf, len);
    return buf[0] != 0;
}

static unsigned namedRom;
static uint8_t named[(PD_LAZ_SPECIES_MAX + 8) / 8];

bool pd_laz_species_named(const struct pd_game* g, int species) {
    if (!g || !g->rom || species < 1 || species > PD_LAZ_SPECIES_MAX) return false;
    if (namedRom != pd_rom_id(g)) {
        namedRom = pd_rom_id(g);
        memset(named, 0, sizeof(named));
        char buf[16];
        for (int i = 1; i <= PD_LAZ_SPECIES_MAX; i++) {
            if (!pd_laz_species_name(g, i, buf, sizeof(buf))) continue;
            named[i / 8] |= (uint8_t) (1u << (i % 8));
        }
    }
    return (named[species / 8] >> (species % 8)) & 1;
}

void pd_laz_species_types(const struct pd_game* g, int species, int* t1, int* t2) {
    *t1 = *t2 = 255;
    const uint8_t* e = pd_laz_species(g, species);
    if (!e || !e[NAME_OFF]) return;
    *t1 = e[TYPES_OFF];
    *t2 = e[TYPES_OFF + 1];
}

int pd_laz_gender_ratio(const struct pd_game* g, int species) {
    const uint8_t* e = pd_laz_species(g, species);
    return e && e[NAME_OFF] ? e[GENDER_OFF] : 255;
}

void pd_laz_ability(const struct pd_game* g, int ability, char* buf, size_t len) {
    if (len) buf[0] = 0;
    if (ability <= 0) return;
    text_at(g, ABILITY_NAMES + (uint32_t) ability * ABILITY_STRIDE, 17, buf, len);
}

bool pd_laz_item_name(const struct pd_game* g, int item, char* buf, size_t len) {
    if (item <= 0 || item > 2000) {
        if (len) buf[0] = 0;
        return false;
    }
    return text_at(g, ITEM_NAME0 + (uint32_t) item * ITEM_STRIDE, 20, buf, len);
}

bool pd_laz_item_desc(const struct pd_game* g, int item, char* buf, size_t len) {
    if (len) buf[0] = 0;
    if (item <= 0 || item > 2000) return false;
    const uint8_t* p = pd_rom_at(g, ITEM_NAME0 + (uint32_t) item * ITEM_STRIDE - 8, 4);
    if (!p) return false;
    uint32_t desc = u32(p);
    return text_at(g, desc, 180, buf, len);
}

bool pd_laz_move(const struct pd_game* g, int move, char* name, size_t len, int* type, int* power) {
    char tmp[24];
    if (name && len) name[0] = 0;
    if (type) *type = 255;
    if (power) *power = 0;
    if (move <= 0 || move > 1000) return false;
    const uint8_t* e = pd_rom_at(g, MOVE_INFO0 + (uint32_t) move * MOVE_STRIDE, MOVE_STRIDE);
    if (!e || !text_at(g, u32(e), 20, name && len ? name : tmp, name && len ? len : sizeof(tmp))) return false;
    uint16_t bits = u16(e + 0x0A);
    if (type) *type = bits & 0x1F;
    if (power) *power = bits >> 7;
    return true;
}

const char* pd_laz_type_name(int type) {
    static const char* const NAMES[] = {
        "",        "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug",  "Ghost", "Steel", "",
        "Fire",    "Water",  "Grass",    "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy",
    };
    return type >= 0 && type < (int) (sizeof(NAMES) / sizeof(NAMES[0])) && NAMES[type][0] ? NAMES[type] : "";
}

uint32_t pd_laz_type_color(int type) {
    static const uint32_t COL[] = {
        0,         0xA8A878, 0xC03028, 0xA890F0, 0xA040A0, 0xE0C068, 0xB8A038, 0xA8B820, 0x705898, 0xB8B8D0, 0,
        0xF08030,  0x6890F0, 0x78C850, 0xF8D030, 0xF85888, 0x98D8D8, 0x7038F8, 0x705848, 0xEE99AC,
    };
    return type >= 0 && type < (int) (sizeof(COL) / sizeof(COL[0])) ? COL[type] : 0;
}

int pd_laz_chart_pct(const struct pd_game* g, int atk, int def) {
    if (atk < 0 || def < 0 || atk >= CHART_N || def >= CHART_N) return 100;
    const uint8_t* p = pd_rom_at(g, TYPE_CHART + (uint32_t) (atk * CHART_N + def) * 4, 4);
    if (!p) return 100;
    return (int) (u32(p) * 100u / 0x1000u);
}

static unsigned mapRom;
static char mapNames[256][28];
static struct pd_mapsec_rect mapRects[256];
static uint8_t mapGrid[PD_LAZ_MAP_W * PD_LAZ_MAP_H];

static void prepare_map(const struct pd_game* g) {
    if (!g || mapRom == pd_rom_id(g)) return;
    mapRom = pd_rom_id(g);
    memset(mapNames, 0, sizeof(mapNames));
    memset(mapGrid, PD_LAZ_MAP_NONE, sizeof(mapGrid));
    for (int i = 0; i < 256; i++) mapRects[i] = (struct pd_mapsec_rect) { -1, 0, 0, 0, 0 };
    const uint8_t* grid = pd_rom_at(g, MAP_LAYOUT, sizeof(mapGrid));
    if (grid) memcpy(mapGrid, grid, sizeof(mapGrid));
    bool on[256];
    memset(on, 0, sizeof(on));
    for (size_t i = 0; i < sizeof(mapGrid); i++) {
        if (mapGrid[i] != PD_LAZ_MAP_NONE) on[mapGrid[i]] = true;
    }
    for (int id = 0; id < 256; id++) {
        const uint8_t* e = pd_rom_at(g, MAPSEC_BASE + (uint32_t) id * 8, 8);
        if (!e) continue;
        uint32_t np = u32(e + 4);
        if (!text_at(g, np, 24, mapNames[id], sizeof(mapNames[id]))) continue;
        int x = e[0], y = e[1], w = e[2], h = e[3];
        if ((x || y) && w && h) {
            mapRects[id] = (struct pd_mapsec_rect) { 0, (uint8_t) (x + PD_LAZ_MAP_OX), (uint8_t) (y + PD_LAZ_MAP_OY),
                                                    (uint8_t) w, (uint8_t) h };
        } else if (on[id]) {
            mapRects[id] = (struct pd_mapsec_rect) { 0, 0, 0, 0, 0 };
        }
    }
}

const char* pd_laz_mapsec_name(const struct pd_game* g, int mapsec) {
    prepare_map(g);
    if (mapsec < 0 || mapsec >= 256 || !mapNames[mapsec][0]) return "";
    return mapNames[mapsec];
}

void pd_laz_map_prepare(const struct pd_game* g, struct pd_mapsec_rect* rects, uint8_t* grid) {
    prepare_map(g);
    if (rects) memcpy(rects, mapRects, sizeof(mapRects));
    if (grid) memcpy(grid, mapGrid, sizeof(mapGrid));
}
