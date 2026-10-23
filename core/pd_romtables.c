#include "pd_romtables.h"

#include <stdio.h>
#include <string.h>

#include "pd_snapshot.h"

#define SP_INLINE 1
#define SP_FLAT 2
#define SP_PTR 3
#define TY_EXP 1
#define TY_CFRU 2
#define TY_TMT 3

static uint16_t u16(const uint8_t* p) {
    return (uint16_t) (p[0] | p[1] << 8);
}

static uint32_t u32(const uint8_t* p) {
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static uint32_t ru32(const struct pd_game* g, uint32_t addr) {
    const uint8_t* p = pd_rom_at(g, addr, 4);
    return p ? u32(p) : 0;
}

static bool text_at(const struct pd_game* g, uint32_t addr, int maxLen, char* buf, size_t len) {
    const uint8_t* p = pd_rom_at(g, addr, (size_t) maxLen);
    if (!p || !buf || !len) return false;
    pd_gen3_text(p, (size_t) maxLen, buf, len);
    return buf[0] != 0;
}

static struct pd_rt_profile live;
static unsigned liveRom;
static const struct pd_rt_profile* liveSrc;

const struct pd_rt_profile* pd_rt_get(const struct pd_game* g) {
    if (!g || !g->rt) return NULL;
    if (!g->rt->header) return g->rt;
    if (liveSrc == g->rt && liveRom == pd_rom_id(g)) return &live;
    live = *g->rt;
    liveSrc = g->rt;
    liveRom = pd_rom_id(g);
    // pokefirered / pokeemerald GFRomHeader at 0x08000100.
    if (!live.species) live.species = ru32(g, 0x08000144);
    if (!live.moves) live.moves = ru32(g, 0x08000148);
    if (!live.stats) live.stats = ru32(g, 0x080001BC);
    if (!live.items && !live.item_array) live.items = ru32(g, 0x080001C8);
    if (!live.battle) live.battle = ru32(g, 0x080001CC);
    return &live;
}

static const uint8_t* species_name_addr(const struct pd_game* g, const struct pd_rt_profile* rt, int species, int* len) {
    if (species < 1 || (rt->species_max && species >= rt->species_max)) return NULL;
    *len = rt->species_len ? rt->species_len : 13;
    if (rt->species_mode == SP_INLINE) {
        return pd_rom_at(g, rt->species + (uint32_t) (species - 1) * rt->species_stride, (size_t) *len);
    }
    if (rt->species_mode == SP_FLAT) {
        return pd_rom_at(g, rt->species + (uint32_t) species * rt->species_stride, (size_t) *len);
    }
    if (rt->species_mode == SP_PTR) {
        const uint8_t* e = pd_rom_at(g, rt->species + (uint32_t) species * 4, 4);
        if (!e) return NULL;
        *len = rt->species_len ? rt->species_len : 20;
        return pd_rom_at(g, u32(e), (size_t) *len);
    }
    return NULL;
}

bool pd_rt_species(const struct pd_game* g, int species, char* buf, size_t len) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (buf && len) buf[0] = 0;
    if (!rt || !rt->species) return false;
    if (rt->egg && species == rt->egg) {
        snprintf(buf, len, "EGG");
        return true;
    }
    int n = 13;
    const uint8_t* p = species_name_addr(g, rt, species, &n);
    if (!p) return false;
    pd_gen3_text(p, (size_t) n, buf, len);
    return buf[0] != 0;
}

static int gender_at(const struct pd_game* g, const struct pd_rt_profile* rt, int species) {
    if (rt->stats) {
        const uint8_t* e = pd_rom_at(g, rt->stats + (uint32_t) species * rt->stats_stride, rt->stats_stride);
        return e ? e[rt->stats_gender] : 255;
    }
    if (rt->species_mode == SP_INLINE && rt->gender_off != 0) {
        uint32_t addr = rt->species + (uint32_t) (species - 1) * rt->species_stride + (uint32_t) rt->gender_off;
        const uint8_t* p = pd_rom_at(g, addr, 1);
        return p ? *p : 255;
    }
    return 255;
}

void pd_rt_species_types(const struct pd_game* g, int species, int* t1, int* t2) {
    *t1 = *t2 = 255;
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (!rt) return;
    if (rt->stats) {
        const uint8_t* e = pd_rom_at(g, rt->stats + (uint32_t) species * rt->stats_stride, rt->stats_stride);
        if (!e) return;
        *t1 = e[rt->stats_type];
        *t2 = e[rt->stats_type + 1];
        return;
    }
    if (rt->species_mode != SP_INLINE || rt->types_off == 0) return;
    uint32_t addr = rt->species + (uint32_t) (species - 1) * rt->species_stride + (uint32_t) rt->types_off;
    const uint8_t* p = pd_rom_at(g, addr, 2);
    if (!p) return;
    *t1 = p[0];
    *t2 = p[1];
}

int pd_rt_gender(const struct pd_game* g, int species) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (!rt) return 255;
    return gender_at(g, rt, species);
}

bool pd_rt_move(const struct pd_game* g, int move, char* name, size_t len, int* type, int* power) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (name && len) name[0] = 0;
    if (type) *type = 255;
    if (power) *power = 0;
    if (!rt || !rt->moves || move <= 0 || (rt->move_max && move > rt->move_max)) return false;
    char tmp[24];
    char* out = name && len ? name : tmp;
    size_t outLen = name && len ? len : sizeof(tmp);
    if (rt->move_inline) {
        if (!text_at(g, rt->moves + (uint32_t) move * (rt->move_stride ? rt->move_stride : rt->move_len), rt->move_len, out, outLen))
            return true;
    } else {
        const uint8_t* e = pd_rom_at(g, rt->moves + (uint32_t) move * rt->move_stride, 4);
        if (!e || !text_at(g, u32(e), rt->move_len ? rt->move_len : 20, out, outLen)) return true;
    }
    const uint8_t* rec = NULL;
    uint32_t base = rt->battle ? rt->battle : rt->moves;
    uint16_t stride = rt->battle ? rt->battle_stride : rt->move_stride;
    if (!rt->move_inline && rt->bits_off > 0) {
        rec = pd_rom_at(g, base + (uint32_t) move * stride + (uint32_t) rt->bits_off, 2);
        if (rec) {
            uint16_t bits = u16(rec);
            if (type) *type = bits & 0x1F;
            if (power) *power = bits >> 7;
        }
    } else if (rt->battle && rt->bits_off < 0) {
        uint32_t at = base + (uint32_t) move * stride;
        const uint8_t* tp = pd_rom_at(g, at + (uint32_t) rt->type_off, 1);
        const uint8_t* pp = pd_rom_at(g, (uint32_t) ((int) at + rt->power_off), 1);
        uint8_t mask = rt->type_mask ? rt->type_mask : 0xFF;
        if (type && tp) *type = *tp & mask;
        if (power && pp) *power = *pp;
    }
    return true;
}

static int item_slot_of(const struct pd_game* g, const struct pd_rt_profile* rt, int item) {
    if (item <= 0 || item >= 4096 || !rt->items) return -1;
    if (rt->item_array || rt->item_key < 0) return item < (rt->item_max ? rt->item_max : 4000) ? item : -1;
    static int16_t slot[4096];
    static unsigned rom;
    static const struct pd_rt_profile* src;
    if (rom != pd_rom_id(g) || src != rt) {
        rom = pd_rom_id(g);
        src = rt;
        for (int i = 0; i < 4096; i++) slot[i] = -1;
        int max = rt->item_max ? rt->item_max : 2000;
        for (int s = 1; s < max && s < 4096; s++) {
            const uint8_t* e = pd_rom_at(g, rt->items + (uint32_t) s * rt->item_stride, (size_t) rt->item_key + 2);
            if (!e) break;
            int id = u16(e + rt->item_key);
            if (id > 0 && id < 4096 && slot[id] < 0) slot[id] = (int16_t) s;
        }
    }
    return slot[item];
}

static uint32_t item_name_addr(const struct pd_rt_profile* rt, int slot) {
    if (rt->items_extra && slot >= rt->extra_first && slot < rt->extra_first + rt->extra_count)
        return rt->items_extra + (uint32_t) (slot - rt->extra_first) * rt->extra_stride;
    return rt->items + (uint32_t) slot * rt->item_stride + rt->item_name_off;
}

bool pd_rt_item(const struct pd_game* g, int item, char* buf, size_t len) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (buf && len) buf[0] = 0;
    if (!rt || !rt->items) return false;
    int slot = item;
    int lenN = rt->item_len ? rt->item_len : 20;
    if (rt->items_extra && item >= rt->extra_first && item < rt->extra_first + rt->extra_count) {
        return text_at(g, item_name_addr(rt, item), lenN, buf, len);
    }
    if (!rt->item_array) slot = item_slot_of(g, rt, item);
    if (slot <= 0) return false;
    uint32_t addr = item_name_addr(rt, slot);
    if (rt->item_inline || rt->item_array) return text_at(g, addr, lenN, buf, len);
    const uint8_t* e = pd_rom_at(g, addr, 4);
    return e && text_at(g, u32(e), lenN, buf, len);
}

bool pd_rt_item_desc(const struct pd_game* g, int item, char* buf, size_t len) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (buf && len) buf[0] = 0;
    if (!rt || !rt->item_desc || rt->item_array) return false;
    int slot = item_slot_of(g, rt, item);
    if (slot <= 0) return false;
    uint32_t name = item_name_addr(rt, slot);
    const uint8_t* p = pd_rom_at(g, (uint32_t) ((int) name + rt->item_desc), 4);
    return p && text_at(g, u32(p), 180, buf, len);
}

static const char* const EXP_TYPES[] = {
    "",        "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug",     "Ghost", "Steel", "",
    "Fire",    "Water",  "Grass",    "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy",
};

static const char* const TMT_EXTRA[] = {
    "Ancient", "Anime", "Baby", "Ball", "Bean", "Bird", "Brat", "Boring", "Cat", "Crab", "Dance", "Deeznuts",
    "Emerald", "Family", "Fast", "FireFht", "Florida", "Fluffy", "Food", "Freak", "Friend", "Frog", "Furry",
    "Future", "Gamer", "Gaslight", "Gatekeep", "Gender", "Girlboss", "Goblin", "Goku", "Gun", "Guys", "Hater",
    "Holy", "Kaiju", "little", "Magic", "Mario", "Monke", "Ninja", "Pikachu", "Rat", "Reddit", "Sans", "Sharp",
    "Silly", "Song", "Space", "Stinky", "Sus", "Thing", "Ugly", "Unalive", "Reverse", "Vgc", "Vibes", "Wealth",
    "Yapper", "Every",
};

static const char* const VANILLA[] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "???",
    "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark",
};

const char* pd_rt_type_name(const struct pd_rt_profile* rt, int type) {
    if (!rt || type < 0) return "";
    if (rt->type_set == TY_EXP || rt->type_set == TY_TMT) {
        if (type < (int) (sizeof(EXP_TYPES) / sizeof(EXP_TYPES[0])) && EXP_TYPES[type][0]) return EXP_TYPES[type];
        if (rt->type_set == TY_TMT && type >= 22 && type - 22 < (int) (sizeof(TMT_EXTRA) / sizeof(TMT_EXTRA[0])))
            return TMT_EXTRA[type - 22];
        return "";
    }
    if (rt->fairy_id && type == rt->fairy_id && rt->fairy_name) return rt->fairy_name;
    if (rt->type_set == TY_CFRU && type == 23) return "Fairy";
    if (type < (int) (sizeof(VANILLA) / sizeof(VANILLA[0]))) return VANILLA[type];
    return "";
}

static const uint32_t EXP_COL[] = {
    0,         0xA8A878, 0xC03028, 0xA890F0, 0xA040A0, 0xE0C068, 0xB8A038, 0xA8B820, 0x705898, 0xB8B8D0, 0,
    0xF08030,  0x6890F0, 0x78C850, 0xF8D030, 0xF85888, 0x98D8D8, 0x7038F8, 0x705848, 0xEE99AC,
};

static const uint32_t VAN_COL[] = {
    0xA8A878, 0xC03028, 0xA890F0, 0xA040A0, 0xE0C068, 0xB8A038, 0xA8B820, 0x705898, 0xB8B8D0, 0x68A090,
    0xF08030, 0x6890F0, 0x78C850, 0xF8D030, 0xF85888, 0x98D8D8, 0x7038F8, 0x705848,
};

uint32_t pd_rt_type_color(const struct pd_rt_profile* rt, int type) {
    if (!rt || type < 0) return 0;
    if (rt->type_set == TY_EXP || rt->type_set == TY_TMT) {
        if (type < (int) (sizeof(EXP_COL) / sizeof(EXP_COL[0])) && EXP_COL[type]) return EXP_COL[type];
        return 0x888888;
    }
    if ((rt->fairy_id && type == rt->fairy_id) || (rt->type_set == TY_CFRU && type == 23)) return 0xEE99AC;
    if (type < (int) (sizeof(VAN_COL) / sizeof(VAN_COL[0]))) return VAN_COL[type];
    return 0x888888;
}

int pd_rt_chart_pct(const struct pd_game* g, int atk, int def) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (!rt || !rt->chart_kind || !rt->chart || atk < 0 || def < 0 || atk >= rt->chart_n || def >= rt->chart_n) return -1;
    uint32_t one = rt->chart_one ? rt->chart_one : 0x1000;
    if (rt->chart_kind == 1) {
        const uint8_t* p = pd_rom_at(g, rt->chart + (uint32_t) (atk * rt->chart_n + def) * 4, 4);
        return p ? (int) (u32(p) * 100u / one) : -1;
    }
    const uint8_t* p = pd_rom_at(g, rt->chart + (uint32_t) (atk * rt->chart_n + def) * 2, 2);
    return p ? (int) (u16(p) * 100u / one) : -1;
}

// --- map names -----------------------------------------------------------

#define MAP_N 512
#define MAP_NAME 28

static unsigned mapRom;
static const struct pd_rt_profile* mapSrc;
static char mapNames[MAP_N][MAP_NAME];
static struct pd_mapsec_rect mapRects[MAP_N];
static uint8_t mapGrid[28 * 15];
static int mapGridOk;

static void name_from_entry(const struct pd_game* g, uint32_t base, int id, int nameOff, int xyOff, int deref, int ox, int oy) {
    if (id < 0 || id >= MAP_N) return;
    const uint8_t* e = pd_rom_at(g, base + (uint32_t) id * 8, 8);
    if (!e) return;
    uint32_t np = u32(e + nameOff);
    if (deref) {
        const uint8_t* p = pd_rom_at(g, np, 4);
        if (!p) return;
        np = u32(p);
    }
    if (!text_at(g, np, 24, mapNames[id], MAP_NAME)) return;
    int x = e[xyOff], y = e[xyOff + 1], w = e[xyOff + 2], h = e[xyOff + 3];
    if ((x || y) && w && h)
        mapRects[id] = (struct pd_mapsec_rect) { 0, (uint8_t) (x + ox), (uint8_t) (y + oy), (uint8_t) w, (uint8_t) h };
}

static void prepare_map(const struct pd_game* g, const struct pd_rt_profile* rt) {
    if (mapSrc == rt && mapRom == pd_rom_id(g)) return;
    mapSrc = rt;
    mapRom = pd_rom_id(g);
    mapGridOk = 0;
    memset(mapNames, 0, sizeof(mapNames));
    for (int i = 0; i < MAP_N; i++) mapRects[i] = (struct pd_mapsec_rect) { -1, 0, 0, 0, 0 };
    if (rt->map_ptrs && rt->map_ptr_count) {
        for (int i = 0; i < rt->map_ptr_count && rt->map_ptr_first + i < MAP_N; i++) {
            uint32_t np = ru32(g, rt->map_ptrs + (uint32_t) i * 4);
            text_at(g, np, 24, mapNames[rt->map_ptr_first + i], MAP_NAME);
        }
    }
    int count = rt->map_count ? rt->map_count : (rt->map_base ? 256 : 0);
    if (count > MAP_N) count = MAP_N;
    for (int id = 0; id < count; id++)
        name_from_entry(g, rt->map_base, id, rt->map_name_off, rt->map_xy_off, rt->map_deref, rt->map_ox, rt->map_oy);
    if (rt->map2_base) {
        int n2 = rt->map2_count ? rt->map2_count : 0;
        for (int i = 0; i < n2; i++) {
            int id = rt->map2_first + i;
            if (id < 0 || id >= MAP_N) continue;
            const uint8_t* e = pd_rom_at(g, rt->map2_base + (uint32_t) i * 8, 8);
            if (!e) continue;
            uint32_t np = u32(e + rt->map2_name_off);
            if (rt->map_deref) {
                const uint8_t* p = pd_rom_at(g, np, 4);
                if (!p) continue;
                np = u32(p);
            }
            text_at(g, np, 24, mapNames[id], MAP_NAME);
        }
    }
    if (rt->map_grid && rt->map_cell == 1 && rt->map_none <= 255 && rt->map_w > 0 && rt->map_h > 0 &&
        rt->map_w <= 28 && rt->map_h <= 15) {
        size_t bytes = (size_t) rt->map_w * rt->map_h;
        const uint8_t* grid = pd_rom_at(g, rt->map_grid, bytes);
        if (grid) {
            memset(mapGrid, (int) rt->map_none, sizeof(mapGrid));
            memcpy(mapGrid, grid, bytes);
            mapGridOk = 1;
        }
    }
}

bool pd_rt_has_map(const struct pd_game* g) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    return rt && (rt->map_base || rt->map_ptrs);
}

const char* pd_rt_mapsec(const struct pd_game* g, int mapsec) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (!rt || mapsec < 0 || mapsec >= MAP_N) return "";
    prepare_map(g, rt);
    return mapNames[mapsec];
}

bool pd_rt_map_prepare(const struct pd_game* g, struct pd_mapsec_rect* rects, int nrects, uint8_t* grid) {
    const struct pd_rt_profile* rt = pd_rt_get(g);
    if (!rt) return false;
    prepare_map(g, rt);
    if (!mapGridOk) return false;
    if (rects) {
        int n = nrects < MAP_N ? nrects : MAP_N;
        memcpy(rects, mapRects, (size_t) n * sizeof(*rects));
    }
    if (grid) memcpy(grid, mapGrid, (size_t) rt->map_w * rt->map_h);
    return true;
}

// --- profiles ------------------------------------------------------------
// Addresses are the ones gen_*_tables.py reads. species_mode 1: `species` is
// species 1's name. Mode 2/3: index 0.

const struct pd_rt_profile pd_rt_lazarus = {
    .region = "ILIOS", .species_mode = SP_INLINE, .species = 0x08C7A438, .species_stride = 0xD4, .species_len = 13,
    .species_max = 1562, .types_off = -0x26, .gender_off = -0x1A, .item_inline = 1, .items = 0x0886855C,
    .item_stride = 0x50, .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x088CFAC8,
    .move_stride = 0x34, .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x08571B78,
    .chart_n = 21, .chart_one = 0x1000, .type_set = TY_EXP, .egg = 1561, .map_base = 0x08CE3E2C, .map_count = 256,
    .map_name_off = 4, .map_grid = 0x08CE35C8, .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2, .map_cell = 1,
    .map_none = 0xD5,
};

static const struct pd_rt_profile RT_HNS = {
    .region = "JOHTO", .species_mode = SP_INLINE, .species = 0x087E667C, .species_stride = 0x10C, .species_len = 13,
    .species_max = 1600, .types_off = -0x26, .gender_off = -0x1A, .items = 0x0878EFD4, .item_stride = 0x2C,
    .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x087B5C68, .move_stride = 0x44,
    .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x08475D1C, .chart_n = 21,
    .chart_one = 0x1000, .type_set = TY_EXP, .map_base = 0x08D553D4, .map_count = 256, .map_name_off = 4,
    .map_grid = 0x08D56858, .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0x7D,
};

static const struct pd_rt_profile RT_SEAGLASS = {
    .region = "HOENN", .species_mode = SP_INLINE, .species = 0x088F087C, .species_stride = 0xD0, .species_len = 13,
    .species_max = 1489, .types_off = -0x26, .gender_off = -0x1A, .item_inline = 1, .items = 0x0867E77C,
    .item_stride = 0x54, .item_len = 21, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x086D29E0,
    .move_stride = 0x38, .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .type_set = TY_EXP,
};

static const struct pd_rt_profile RT_IMPERIUM = {
    .region = "HOENN", .species_mode = SP_INLINE, .species = 0x08D5DB08, .species_stride = 0x104, .species_len = 13,
    .species_max = 1536, .types_off = -0x26, .gender_off = -0x1A, .egg = 1536, .item_inline = 1, .items = 0x086C7A78,
    .item_stride = 0x50, .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x08704A74,
    .move_stride = 0x34, .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x084EA6DC,
    .chart_n = 21, .chart_one = 0x1000, .type_set = TY_EXP, .map_base = 0x08DDA3E4, .map_count = 256, .map_name_off = 4,
    .map_grid = 0x08DD975C, .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xDA,
};

static const struct pd_rt_profile RT_TMT2 = {
    .region = "HOENN", .species_mode = SP_INLINE, .species = 0x08712B78, .species_stride = 0x104, .species_len = 13,
    .species_max = 1651, .types_off = -0x26, .gender_off = -0x18, .item_inline = 1, .items = 0x086B99B0,
    .item_stride = 0x54, .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x086DFB2C,
    .move_stride = 0x38, .move_len = 20, .move_max = 1165, .battle = 0x086DFB2C, .battle_stride = 0x38, .bits_off = -1,
    .power_off = 0x0C, .type_off = 0x0A, .type_mask = 0x7F, .chart_kind = 1, .chart = 0x084EAC18, .chart_n = 82,
    .chart_one = 0x1000, .type_set = TY_TMT,
};

static const struct pd_rt_profile RT_SG = {
    .region = "JOHTO", .species_mode = SP_INLINE, .species = 0x087D45F3, .species_stride = 0x118, .species_len = 13,
    .species_max = 1579, .types_off = -0x2D, .gender_off = -0x21, .items = 0x087520A0, .item_stride = 0x2C,
    .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x08776778, .move_stride = 0x48,
    .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x0843DBB4, .chart_n = 21,
    .chart_one = 0x1000, .type_set = TY_EXP, .egg = 1578, .map_base = 0x08F3782C, .map_count = 0x13A, .map_name_off = 4,
};

static const struct pd_rt_profile RT_SG12 = {
    .region = "JOHTO", .species_mode = SP_INLINE, .species = 0x087D5E23, .species_stride = 0x120, .species_len = 13,
    .species_max = 1579, .types_off = -0x2D, .gender_off = -0x21, .items = 0x08753834, .item_stride = 0x2C,
    .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x08777EFC, .move_stride = 0x48,
    .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x0843E6E4, .chart_n = 21,
    .chart_one = 0x1000, .type_set = TY_EXP, .egg = 1578, .map_base = 0x08F42978, .map_count = 0x13A, .map_name_off = 4,
};

static const struct pd_rt_profile RT_SG12B = {
    .region = "JOHTO", .species_mode = SP_INLINE, .species = 0x087D5D8B, .species_stride = 0x120, .species_len = 13,
    .species_max = 1579, .types_off = -0x2D, .gender_off = -0x21, .items = 0x0875379C, .item_stride = 0x2C,
    .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .moves = 0x08777E64, .move_stride = 0x48,
    .move_len = 20, .move_max = 1000, .bits_off = 0x0A, .chart_kind = 1, .chart = 0x0843E64C, .chart_n = 21,
    .chart_one = 0x1000, .type_set = TY_EXP, .egg = 1578, .map_base = 0x08F428D4, .map_count = 0x13A, .map_name_off = 4,
};

static const struct pd_rt_profile RT_SG12C = {
    .region = "JOHTO", .species_mode = SP_INLINE, .species = 0x087D5FAF, .species_stride = 0x120, .species_len = 13,
    .species_max = 1579, .types_off = -0x2D, .gender_off = -0x21, .items = 0x08753928, .item_stride = 0x2C,
    .item_len = 20, .item_max = 2000, .item_key = -1, .item_desc = -8, .type_set = TY_EXP, .egg = 1578,
};

// FireRed rev 0's own tables, read from the hacked ROM (in-place edits show up).
static const struct pd_rt_profile RT_FR = {
    .species_mode = SP_FLAT, .species = 0x08245EE0, .species_stride = 11, .species_len = 11, .species_max = 440,
    .stats = 0x08254784, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x083DB028, .item_stride = 44, .item_len = 14, .item_max = 400, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x08247094, .move_stride = 13, .move_len = 13, .move_max = 355, .battle = 0x08250C04,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_ptrs = 0x083F1CAC, .map_ptr_first = 0x58,
    .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_ROCKET = {
    .species_mode = SP_FLAT, .species = 0x08245EE0, .species_stride = 11, .species_len = 11, .species_max = 440,
    .stats = 0x08254784, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x083DB028, .item_stride = 44, .item_len = 14, .item_max = 400, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x08901800, .move_stride = 13, .move_len = 13, .move_max = 487, .battle = 0x08900000,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU, .map_ptrs = 0x083F1CAC,
    .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_ODYSSEY = {
    .species_mode = SP_FLAT, .species = 0x08245EE0, .species_stride = 11, .species_len = 11, .species_max = 440,
    .stats = 0x08254784, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x083DB028, .item_stride = 44, .item_len = 14, .item_max = 400, .item_key = 14, .item_desc = 20,
    .fairy_id = 9, .fairy_name = "Aether", .map_ptrs = 0x083F1CAC, .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_EM = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x083185C8, .species_stride = 11, .species_len = 11,
    .species_max = 412, .stats = 0x083203CC, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10,
    .item_inline = 1, .items = 0x085839A0, .item_stride = 44, .item_len = 14, .item_max = 377, .item_key = 14,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x0831977C, .move_stride = 13, .move_len = 13, .move_max = 355,
    .battle = 0x0831C898, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_base = 0x085A147C,
    .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x085A096C, .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2,
    .map_cell = 1, .map_none = 0xD5,
};

static const struct pd_rt_profile RT_GLAZED = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x083185C8, .species_stride = 11, .species_len = 11,
    .species_max = 412, .stats = 0x083203CC, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10,
    .item_inline = 1, .items = 0x085839A0, .item_stride = 44, .item_len = 14, .item_max = 377, .item_key = 14,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x0831977C, .move_stride = 13, .move_len = 13, .move_max = 355,
    .battle = 0x0831C898, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .fairy_id = 9,
    .fairy_name = "Fairy", .map_base = 0x085A147C, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x085A096C,
    .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5,
};

static const struct pd_rt_profile RT_GAIA = {
    .species_mode = SP_FLAT, .species = 0x08745850, .species_stride = 11, .species_len = 11, .species_max = 935,
    .stats = 0x08ACD000, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x083DB028, .item_stride = 44, .item_len = 14, .item_max = 400, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x08247094, .move_stride = 13, .move_len = 13, .move_max = 355, .battle = 0x08250C04,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2,
};

static const struct pd_rt_profile RT_UNBOUND = {
    .region = "BORRIUS", .species_mode = SP_FLAT, .species = 0x0966A98C, .species_stride = 11, .species_len = 11,
    .species_max = 1294, .stats = 0x099E0C9C, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10,
    .item_inline = 1, .items = 0x08876200, .item_stride = 44, .item_len = 14, .item_max = 800, .item_key = 14,
    .item_desc = 20, .move_inline = 1, .moves = 0x08A40A10, .move_stride = 13, .move_len = 13, .move_max = 893,
    .type_set = TY_CFRU, .map_ptrs = 0x083F1CAC, .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_UNBOUND_FR = {
    .region = "BORRIUS", .species_mode = SP_FLAT, .species = 0x0966A98C, .species_stride = 11, .species_len = 11,
    .species_max = 1294, .stats = 0x099E0C9C, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10,
    .item_inline = 1, .items = 0x08876200, .item_stride = 44, .item_len = 14, .item_max = 900, .item_key = 14,
    .item_desc = 20, .move_inline = 1, .moves = 0x081B2980, .move_stride = 13, .move_len = 13, .move_max = 893,
    .type_set = TY_CFRU, .map_ptrs = 0x083F1CAC, .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_RADICAL = {
    .species_mode = SP_FLAT, .species = 0x094042CC, .species_stride = 11, .species_len = 11, .species_max = 1400,
    .stats = 0x097B98EC, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x093C0000, .item_stride = 44, .item_len = 14, .item_max = 2000, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x090EEEDC, .move_stride = 17, .move_len = 17, .move_max = 1003, .battle = 0x091521D0,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU,
};

static const struct pd_rt_profile RT_SWORD = {
    .species_mode = SP_FLAT, .species = 0x096184B8, .species_stride = 11, .species_len = 11, .species_max = 1268,
    .stats = 0x099AFBEC, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x093C0000, .item_stride = 44, .item_len = 14, .item_max = 2000, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x094B277C, .move_stride = 13, .move_len = 13, .move_max = 895, .battle = 0x094BFA7C,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU, .map_ptrs = 0x083F1CAC,
    .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_GS = {
    .species_mode = SP_FLAT, .species = 0x0941995C, .species_stride = 11, .species_len = 11, .species_max = 1313,
    .stats = 0x09798E18, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x08F70000, .item_stride = 44, .item_len = 14, .item_max = 2000, .item_key = -1, .item_desc = 20,
    .move_inline = 1, .moves = 0x09CF820C, .move_stride = 17, .move_len = 17, .move_max = 884, .battle = 0x09D10BE3,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU, .map_ptrs = 0x083F1CAC,
    .map_ptr_first = 0x58, .map_ptr_count = 109,
};

static const struct pd_rt_profile RT_AME = {
    .species_mode = SP_FLAT, .species = 0x0986315C, .species_stride = 11, .species_len = 11, .species_max = 1268,
    .stats = 0x09BC257C, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x0872DB0C, .item_stride = 44, .item_len = 14, .item_max = 723, .item_key = -1, .item_desc = 20,
    .move_inline = 1, .moves = 0x08931964, .move_stride = 13, .move_len = 13, .move_max = 828, .battle = 0x089B6358,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU,
};

static const struct pd_rt_profile RT_AME141 = {
    .species_mode = SP_FLAT, .species = 0x09864F38, .species_stride = 11, .species_len = 11, .species_max = 1294,
    .stats = 0x09A98390, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x0872DB0C, .item_stride = 44, .item_len = 14, .item_max = 723, .item_key = -1, .item_desc = 20,
    .move_inline = 1, .moves = 0x08958398, .move_stride = 13, .move_len = 13, .move_max = 828, .battle = 0x089FFCA8,
    .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .type_set = TY_CFRU,
};

static const struct pd_rt_profile RT_CELIA = {
    .species_mode = SP_FLAT, .species = 0x087270B8, .species_stride = 11, .species_len = 11, .species_max = 411,
    .stats = 0x0874B058, .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1,
    .items = 0x08C1D0BC, .item_stride = 44, .item_len = 14, .item_max = 600, .item_key = 14, .item_desc = 20,
    .move_inline = 1, .moves = 0x0872A89F, .move_stride = 13, .move_len = 13, .move_max = 1189, .battle = 0x0873E7F0,
    .battle_stride = 16, .bits_off = -1, .power_off = -2, .type_off = 0, .type_mask = 0x3F,
};

static const struct pd_rt_profile RT_RR = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 11, .species_len = 11, .species_max = 412,
    .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1, .item_stride = 48, .item_len = 18,
    .item_max = 409, .item_key = -1, .item_desc = 0x18, .move_inline = 1, .move_stride = 13, .move_len = 13,
    .move_max = 355, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2,
};

static const struct pd_rt_profile RT_RY180 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 11, .species_len = 11, .species_max = 412,
    .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1, .item_stride = 48, .item_len = 18,
    .item_max = 409, .item_key = -1, .item_desc = 0x18, .move_inline = 1, .move_stride = 13, .move_len = 13,
    .move_max = 355, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_ptrs = 0x08891BC8,
    .map_ptr_first = 0x58, .map_ptr_count = 0x72,
};
static const struct pd_rt_profile RT_RY190 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 11, .species_len = 11, .species_max = 412,
    .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1, .item_stride = 48, .item_len = 18,
    .item_max = 409, .item_key = -1, .item_desc = 0x18, .move_inline = 1, .move_stride = 13, .move_len = 13,
    .move_max = 355, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_ptrs = 0x08891684,
    .map_ptr_first = 0x58, .map_ptr_count = 0x72,
};
static const struct pd_rt_profile RT_RY194 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 11, .species_len = 11, .species_max = 412,
    .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1, .item_stride = 48, .item_len = 18,
    .item_max = 409, .item_key = -1, .item_desc = 0x18, .move_inline = 1, .move_stride = 13, .move_len = 13,
    .move_max = 355, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_ptrs = 0x08971688,
    .map_ptr_first = 0x58, .map_ptr_count = 0x72,
};
static const struct pd_rt_profile RT_RY197 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 11, .species_len = 11, .species_max = 412,
    .stats_stride = 28, .stats_type = 6, .stats_gender = 0x10, .item_inline = 1, .item_stride = 48, .item_len = 18,
    .item_max = 409, .item_key = -1, .item_desc = 0x18, .move_inline = 1, .move_stride = 13, .move_len = 13,
    .move_max = 355, .battle_stride = 12, .bits_off = -1, .power_off = 1, .type_off = 2, .map_ptrs = 0x0899A498,
    .map_ptr_first = 0x58, .map_ptr_count = 0x72,
};

static const struct pd_rt_profile RT_MARIO15 = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x0841F5A8, .species_stride = 11, .species_len = 11,
    .species_max = 1402, .stats = 0x08439060, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12,
    .item_inline = 1, .items = 0x0880C8DC, .item_stride = 0x28, .item_len = 14, .item_max = 1000, .item_key = -1,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x08426E24, .move_stride = 17, .move_len = 17, .move_max = 840,
    .battle = 0x08431730, .battle_stride = 0x14, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1,
    .chart = 0x0842EA4C, .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08836BAC, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x088360F6, .map_w = 28, .map_h = 15,
    .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5, .egg = 1402,
};
static const struct pd_rt_profile RT_MARIO152 = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x0841FA38, .species_stride = 11, .species_len = 11,
    .species_max = 1402, .stats = 0x084394F0, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12,
    .item_inline = 1, .items = 0x0880CD90, .item_stride = 0x28, .item_len = 14, .item_max = 1000, .item_key = -1,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x084272B4, .move_stride = 17, .move_len = 17, .move_max = 840,
    .battle = 0x08431BC0, .battle_stride = 0x14, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1,
    .chart = 0x0842EEDC, .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08837060, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x088365AA, .map_w = 28, .map_h = 15,
    .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5, .egg = 1402,
};
static const struct pd_rt_profile RT_MARIO121 = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x0840E1A8, .species_stride = 11, .species_len = 11,
    .species_max = 1402, .stats = 0x08427A6C, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12,
    .item_inline = 1, .items = 0x087EE200, .item_stride = 0x28, .item_len = 14, .item_max = 1000, .item_key = -1,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x08415A0E, .move_stride = 17, .move_len = 17, .move_max = 833,
    .battle = 0x08420220, .battle_stride = 0x14, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1,
    .chart = 0x0841D570, .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08817934, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x08816E8A, .map_w = 28, .map_h = 15,
    .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5, .egg = 1401,
};
static const struct pd_rt_profile RT_MARIO122 = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x0840E188, .species_stride = 11, .species_len = 11,
    .species_max = 1402, .stats = 0x08427A4C, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12,
    .item_inline = 1, .items = 0x087EE1E4, .item_stride = 0x28, .item_len = 14, .item_max = 1000, .item_key = -1,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x084159EE, .move_stride = 17, .move_len = 17, .move_max = 833,
    .battle = 0x08420200, .battle_stride = 0x14, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1,
    .chart = 0x0841D550, .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08817924, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x08816E7A, .map_w = 28, .map_h = 15,
    .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5, .egg = 1401,
};
static const struct pd_rt_profile RT_MARIO13 = {
    .region = "HOENN", .species_mode = SP_FLAT, .species = 0x0840E148, .species_stride = 11, .species_len = 11,
    .species_max = 1402, .stats = 0x08427A0C, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12,
    .item_inline = 1, .items = 0x087EE1C8, .item_stride = 0x28, .item_len = 14, .item_max = 1000, .item_key = -1,
    .item_desc = 0x14, .move_inline = 1, .moves = 0x084159AE, .move_stride = 17, .move_len = 17, .move_max = 833,
    .battle = 0x084201C0, .battle_stride = 0x14, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1,
    .chart = 0x0841D510, .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08817908, .map_count = 0xD5, .map_name_off = 4, .map_grid = 0x08816E5E, .map_w = 28, .map_h = 15,
    .map_ox = 1, .map_oy = 2, .map_cell = 1, .map_none = 0xD5, .egg = 1401,
};

static const struct pd_rt_profile RT_QZ = {
    .species_mode = SP_FLAT, .species = 0x08504670, .species_stride = 13, .species_len = 13, .species_max = 1529,
    .stats = 0x0853E560, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1,
    .items = 0x091E66EC, .item_stride = 20, .item_len = 20, .item_max = 890, .move_inline = 1, .moves = 0x0850E1BA,
    .move_stride = 17, .move_len = 17, .move_max = 1000, .battle = 0x08530690, .battle_stride = 0x18, .bits_off = -1,
    .power_off = 2, .type_off = 3,     .chart_kind = 1, .chart = 0x0852DA8C, .chart_n = 20, .chart_one = 0x1000,
    .fairy_id = 18, .fairy_name = "Fairy", .egg = 1529, .map_base = 0x09229558, .map_count = 0xD5, .map_deref = 1, .map_xy_off = 4,
    .map2_base = 0x0922A7E8, .map2_first = 0x100, .map2_count = 0xD5, .map2_name_off = 4,
};
static const struct pd_rt_profile RT_QZ_ES = {
    .species_mode = SP_FLAT, .species = 0x08514E25, .species_stride = 13, .species_len = 13, .species_max = 1529,
    .stats = 0x0853E560, .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1,
    .items = 0x091EDC7C, .item_stride = 20, .item_len = 20, .item_max = 890, .move_inline = 1, .moves = 0x0851D9F2,
    .move_stride = 17, .move_len = 17, .move_max = 1000, .battle = 0x08530690, .battle_stride = 0x18, .bits_off = -1,
    .power_off = 2, .type_off = 3, .fairy_id = 18, .fairy_name = "Fairy", .egg = 1529, .map_base = 0x0922C7D4, .map_count = 0xD5,
    .map_deref = 1, .map_xy_off = 4, .map2_base = 0x0922DA64, .map2_first = 0x100, .map2_count = 0xD5, .map2_name_off = 4,
};
static const struct pd_rt_profile RT_QZ_V1 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1529,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x091EFDC4,
    .item_stride = 20, .item_len = 20, .item_max = 890, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 1000, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1529, .map_base = 0x09233AE0, .map_count = 0xD5, .map_deref = 1, .map_xy_off = 4,
    .map2_base = 0x09234D70, .map2_first = 0x100, .map2_count = 0xD5, .map2_name_off = 4,
};
static const struct pd_rt_profile RT_QZ_V1_ES = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1529,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x091F2E08,
    .item_stride = 20, .item_len = 20, .item_max = 890, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 1000, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1529, .map_base = 0x09236DA4, .map_count = 0xD5, .map_deref = 1, .map_xy_off = 4,
    .map2_base = 0x09238034, .map2_first = 0x100, .map2_count = 0xD5, .map2_name_off = 4,
};

static const struct pd_rt_profile RT_QZ084 = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1520,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x08F9B248,
    .item_stride = 20, .item_len = 20, .item_max = 879, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 849, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1520, .map_base = 0x08FD0B18, .map_count = 0xD5, .map_name_off = 4, .map_ox = 1, .map_oy = 2,
};
static const struct pd_rt_profile RT_QZ084_ES = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1520,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x08F9A968,
    .item_stride = 20, .item_len = 20, .item_max = 879, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 849, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1520, .map_base = 0x08FD043C, .map_count = 0xD5, .map_name_off = 4, .map_ox = 1, .map_oy = 2,
};
static const struct pd_rt_profile RT_MOE = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1520,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x08F9B22C,
    .item_stride = 20, .item_len = 20, .item_max = 879, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 849, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1520, .map_base = 0x08FD0AFC, .map_count = 0xD5, .map_name_off = 4, .map_ox = 1, .map_oy = 2,
};
static const struct pd_rt_profile RT_MOE_ES = {
    .header = 1, .species_mode = SP_FLAT, .species_stride = 13, .species_len = 13, .species_max = 1520,
    .stats_stride = 0x24, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .item_array = 1, .items = 0x08F9A94C,
    .item_stride = 20, .item_len = 20, .item_max = 879, .move_inline = 1, .move_stride = 17, .move_len = 17,
    .move_max = 849, .battle_stride = 0x18, .bits_off = -1, .power_off = 2, .type_off = 3, .fairy_id = 18,
    .fairy_name = "Fairy", .egg = 1520, .map_base = 0x08FD0420, .map_count = 0xD5, .map_name_off = 4, .map_ox = 1, .map_oy = 2,
};

static const struct pd_rt_profile RT_ROGUE = {
    .region = "HOENN", .species_mode = SP_INLINE, .species = 0x0905BD04, .species_stride = 0x98, .species_len = 13,
    .species_max = 2000, .types_off = -38, .item_inline = 1, .items = 0x08B935AC,
    .item_stride = 0x28, .item_len = 16, .item_max = 827, .item_name_off = 0x10, .item_key = -1, .item_desc = -4,
    .items_extra = 0x08B9DD54, .extra_first = 827, .extra_count = 201, .extra_stride = 0x30, .move_inline = 1,
    .moves = 0x084A4008, .move_stride = 13, .move_len = 13, .move_max = 800, .battle = 0x08BB5EA8, .battle_stride = 28,
    .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 1, .chart = 0x08496DC4, .chart_n = 20,
    .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy", .map_base = 0x090BADD4, .map_count = 0xD5,
    .map_name_off = 4, .map_grid = 0x090BA19C, .map_w = 28, .map_h = 15, .map_ox = 1, .map_oy = 2, .map_cell = 1,
    .map_none = 0xD5,
};

static const struct pd_rt_profile RT_ROWE = {
    .species_mode = SP_FLAT, .species = 0x084180A4, .species_stride = 13, .species_len = 13, .species_max = 1960,
    .stats = 0x08502C54, .stats_stride = 0x40, .stats_type = 0x0C, .stats_gender = 0x18, .item_inline = 1,
    .items = 0x08F76D78, .item_stride = 0x38, .item_len = 20, .item_max = 1000, .item_key = -1, .item_desc = 0x1C,
    .move_inline = 1, .moves = 0x0841E42C, .move_stride = 17, .move_len = 17, .move_max = 900, .battle = 0x08435298,
    .battle_stride = 0x38, .bits_off = -1, .power_off = 2, .type_off = 3, .chart_kind = 2, .chart = 0x0842C182,
    .chart_n = 19, .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy", .map_base = 0x08FD278C,
    .map_count = 0xD5, .map_name_off = 4, .map_ox = 1, .map_oy = 2,
};

static const struct pd_rt_profile RT_ELITE = {
    .species_mode = SP_PTR, .species = 0x089CEAFC, .species_len = 20, .species_max = 2901, .stats = 0x08C033E4,
    .stats_stride = 0x2C, .stats_type = 6, .stats_gender = 0x12, .items = 0x08AC6B24, .item_stride = 40, .item_len = 24,
    .item_max = 440, .item_key = -1, .move_inline = 1, .moves = 0x089D82C0, .move_stride = 19, .move_len = 19,
    .move_max = 1032, .battle = 0x08BF9884, .battle_stride = 24, .bits_off = -1, .power_off = 0x0A, .type_off = 0x0B,
    .chart_kind = 2, .chart = 0x08545A04, .chart_n = 20, .chart_one = 0x400, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08EFA32C, .map_count = 220, .map_name_off = 4, .map_ox = 1, .map_oy = 2, .egg = 2901,
};
static const struct pd_rt_profile RT_ELITE3 = {
    .species_mode = SP_PTR, .species = 0x089C1614, .species_len = 20, .species_max = 2901, .stats = 0x08BF5B84,
    .stats_stride = 0x2C, .stats_type = 6, .stats_gender = 0x12, .items = 0x08AB9640, .item_stride = 40, .item_len = 24,
    .item_max = 440, .item_key = -1, .move_inline = 1, .moves = 0x089CADD8, .move_stride = 19, .move_len = 19,
    .move_max = 1032, .battle = 0x08BEC024, .battle_stride = 24, .bits_off = -1, .power_off = 0x0A, .type_off = 0x0B,
    .chart_kind = 2, .chart = 0x08540E40, .chart_n = 20, .chart_one = 0x400, .fairy_id = 18, .fairy_name = "Fairy",
    .map_base = 0x08EE7EC0, .map_count = 220, .map_name_off = 4, .map_ox = 1, .map_oy = 2, .egg = 2901,
};

static const struct pd_rt_profile RT_ROCKET_DS = {
    .species_mode = SP_FLAT, .species = 0x08598560, .species_stride = 11, .species_len = 11, .species_max = 1395,
    .stats = 0x085AD228, .stats_stride = 36, .stats_type = 6, .stats_gender = 0x12, .item_inline = 1, .items = 0x08C833B0,
    .item_stride = 44, .item_len = 14, .item_max = 923, .item_key = 14, .item_desc = 20, .move_inline = 1,
    .moves = 0x0859C151, .move_stride = 13, .move_len = 13, .move_max = 755, .battle = 0x085A5820, .battle_stride = 20,
    .bits_off = -1, .power_off = 2, .type_off = 4, .chart_kind = 2, .chart = 0x085A29F2, .chart_n = 19,
    .chart_one = 0x1000, .fairy_id = 18, .fairy_name = "Fairy", .map_base = 0x08CB0C68, .map_count = 0xFC,
    .map_name_off = 4, .egg = 1395,
};

// --- SHA-1 catalog -------------------------------------------------------

enum {
    RAM_NONE = 0,
    RAM_HNS,
    RAM_SEAGLASS,
    RAM_IMPERIUM,
    RAM_TMT2,
    RAM_MARIO15,
    RAM_MARIO13,
    RAM_CELIA,
    RAM_RY180,
    RAM_RY190,
    RAM_RY194,
    RAM_RY197,
    RAM_RE225,
    RAM_RE228,
    RAM_REBAL,
    RAM_SG,
    RAM_SG12,
    RAM_ROGUE,
    RAM_QUETZAL,
    RAM_ELITE,
    RAM_ELITE3,
    RAM_TEAM,
    RAM_ROWE,
};

struct hack {
    const char* sha;
    const char* title;
    int8_t base; // 1 FireRed rev 0, 2 Emerald
    int8_t ram;
    const struct pd_rt_profile* rt;
};

static const struct hack HACKS[] = {
    { "79ee6df0869c1773c8c6a5f764afc1f8d833d8bb", "Pokémon Heart and Soul", 2, RAM_HNS, &RT_HNS },
    { "b9f4d332d30fc88c379f9e037f9eae3b2755ead4", "Pokémon Emerald Seaglass", 2, RAM_SEAGLASS, &RT_SEAGLASS },
    { "1d20091c4d936f5eb122db8780554dd0829ffb63", "Pokémon Emerald Imperium", 2, RAM_IMPERIUM, &RT_IMPERIUM },
    { "3fa8e61ec1727cc51540c54677623f736a446e1b", "Pokémon Too Many Types 2", 2, RAM_TMT2, &RT_TMT2 },
    { "ea5d369cc8a31cbf1cfacb7c9470ea670f08957b", "Pokémon SoulGold", 2, RAM_SG, &RT_SG },
    { "805d880ee229fb6dc3ce03d7b03baf48f0d759d0", "Pokémon SoulGold", 2, RAM_SG12, &RT_SG12 },
    { "5d6a036260fdbde96f85b5c1b92d0256d3aebafb", "Pokémon SoulGold", 2, RAM_SG12, &RT_SG12B },
    { "2be7c99aa473308debe1d03ff2015bab7d9addb5", "Pokémon SoulGold", 2, RAM_SG12, &RT_SG12C },
    { "e10105d8544469c6a11ca2cf510289981df3c3b0", "Pokémon Glazed", 2, RAM_NONE, &RT_GLAZED },
    { "a7c4e34fafb53f2d5283eb6d43dad285c0dd40a8", "Pokémon Emerald Kaizo", 2, RAM_NONE, &RT_EM },
    { "7600af1fe08444c850c3c1227fd7dfd81336ae8e", "Pokémon Emerald Rogue", 2, RAM_ROGUE, &RT_ROGUE },
    { "81bd0f4bfa1c04ab2c6faab1bddd10e8a390ea77", "Pokémon R.O.W.E.", 2, RAM_ROWE, &RT_ROWE },
    { "2f3e17e55c6afe23da97ec0f96a067eeb142ad15", "Pokémon Elite Redux", 2, RAM_ELITE, &RT_ELITE },
    { "5b38a681b6924ef3e0bb9f6cfbf22426c4c4e523", "Pokémon Elite Redux", 2, RAM_ELITE3, &RT_ELITE3 },
    { "40780347d0b4df6bf491eaf12603e5422caee677", "Pokémon Team Rocket Dragonsden", 2, RAM_TEAM, &RT_ROCKET_DS },
    { "d0658315da1e8827f66f15c3d3a000fe747e163e", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ },
    { "fe346b5b0eb022e3a103f81e7dcefba6f1dc542f", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ_ES },
    { "c108cec750a35775084c0806cc704d8abd1994e7", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ_V1 },
    { "ff008b70b6f2b330da9ffa8a405fc951a9e54147", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ_V1_ES },
    { "6806c7146aff457a4be85ef2030e9ddcdb17b665", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ084 },
    { "b6542c7dbb40cf6ea96714a7a0e30cb468102861", "Pokémon Quetzal", 2, RAM_QUETZAL, &RT_QZ084_ES },
    { "f1035f615c13c46394a18fa99181c24c3cd15e12", "Pokémon Moemon Quetzal", 2, RAM_QUETZAL, &RT_MOE },
    { "ae5d88bb823a8f9d0d07e8784da2376ff7c1ae43", "Pokémon Moemon Quetzal", 2, RAM_QUETZAL, &RT_MOE_ES },
    { "831fe0f97301f17b24a9cd4a64ef0520835c4dde", "Super Mariomon", 2, RAM_MARIO15, &RT_MARIO15 },
    { "06e79d12afa578db97c23c1f7ba771fe2f8fe905", "Super Mariomon", 2, RAM_MARIO15, &RT_MARIO152 },
    { "43dd208b23eade69aa3384301e934ecafbf6c2b3", "Super Mariomon", 2, RAM_MARIO13, &RT_MARIO121 },
    { "08d1c275c0ba4d559b33427292702d61ad49fb5c", "Super Mariomon", 2, RAM_MARIO13, &RT_MARIO122 },
    { "043512c2497871c018df606fb90ffbd4c81430f4", "Super Mariomon", 2, RAM_MARIO13, &RT_MARIO13 },
    { "67f44fb4cdbb16c0d1a495ac219f2979b1e3005f", "Pokémon Recharged Emerald", 2, RAM_RE225, &RT_RR },
    { "56c9777215f5be627f7c3099e68985aefc10f2b7", "Pokémon Recharged Emerald", 2, RAM_RE228, &RT_RR },
    { "08b9c7903ae4c4e26d80ef945a90c004ae1e0ebb", "Pokémon Recharged Emerald", 2, RAM_REBAL, &RT_RR },
    { "b4776b82a4c7915d0fadeaa27e013523f99dfd94", "Pokémon Unbound", 1, RAM_NONE, &RT_UNBOUND },
    { "0ce2a880aa097f1dce4e1db8ee513d0e82d15859", "Pokémon Unbound", 1, RAM_NONE, &RT_UNBOUND_FR },
    { "d5b1e77975fcda831e0e9a7b527906bf3f40ecd0", "Pokémon Gaia", 1, RAM_NONE, &RT_GAIA },
    { "964f951a0fdaf209e4ea1344883ef0d557bb3a80", "Pokémon Radical Red", 1, RAM_NONE, &RT_RADICAL },
    { "8745ddbdbfadf6abaf66de4e9055923b62eb4668", "Pokémon Odyssey", 1, RAM_NONE, &RT_ODYSSEY },
    { "00e70c0384a5f1698588034201fd5b849d3542e2", "Pokémon Amethyst", 1, RAM_NONE, &RT_AME },
    { "91291aade04b4b111cd03ae7b6e2ff460e1edd8a", "Pokémon Amethyst", 1, RAM_NONE, &RT_AME141 },
    { "3cbd5a2e72ce60cfae1e0c28d83e01f855c51537", "Celia's Stupid Romhack", 1, RAM_CELIA, &RT_CELIA },
    { "afe4c8e0f9c7186860fcea5f25d8318f5d7929b9", "Sword and Shield Ultimate Plus", 1, RAM_NONE, &RT_SWORD },
    { "daa2f89934d2c0447d5271dabeba86bfba04e1fd", "Sword and Shield Ultimate Plus", 1, RAM_NONE, &RT_SWORD },
    { "4858cc3d7b335bb5eed553861d7a4a17bad9ce2c", "Sword and Shield Ultimate Plus", 1, RAM_NONE, &RT_SWORD },
    { "932323d1fa3732d8673d1319fa86ed4df41087b9", "Sword and Shield Ultimate Plus", 1, RAM_NONE, &RT_SWORD },
    { "25f8840ecb58e584edf18ffde35f845e94f3f17c", "Pokémon GS Chronicles", 1, RAM_NONE, &RT_GS },
    { "8bac897de515f88e6ce92d0712ce2c018a394b37", "Pokémon Orange Islands", 1, RAM_NONE, &RT_FR },
    { "d2e3800e69e44c9649c3d9a15dc1cd12c1d90501", "Pokémon Orange Islands", 1, RAM_NONE, &RT_FR },
    { "3b6c51d8fb6b513bdd01b7bda3ed8965bc985d58", "Pokémon Ultra Violet", 1, RAM_NONE, &RT_FR },
    { "033d05374ae34eb84fae62c693ca8f3da23dd1bd", "Pokémon FireRed Rocket Edition", 1, RAM_NONE, &RT_ROCKET },
    { "1ee1557db1e4c2e0e94c6306efcc40506bc40cc9", "Pokémon Ash Gray", 1, RAM_NONE, &RT_FR },
    { "92ed82145c013069472153391c7aab1cfbf37b75", "Pokémon Ash Gray", 1, RAM_NONE, &RT_FR },
    { "74337272566a74625d6dedefe47d2d153eb48ec1", "Pokémon Ghost Grey", 1, RAM_NONE, &RT_ROCKET },
    { "0dafc0116b74406a2df491e26ccb40dd362fb0b7", "Pokémon Ghost Grey", 1, RAM_NONE, &RT_ROCKET },
    { "20dfaea1d5317cf95fe0b493208edce7d50b865f", "Pokémon Ghost Grey", 1, RAM_NONE, &RT_ROCKET },
    { "be5033598e91d0e80fa93e551406f02a7876d7c2", "Pokémon Recharged Yellow", 1, RAM_RY180, &RT_RY180 },
    { "ac30d1ed80e75104a3fb92f71e2b9a51c26e80c8", "Pokémon Recharged Yellow", 1, RAM_RY190, &RT_RY190 },
    { "7d9fae6cf31c07bc7fa5cf7afe137ad5385cefb1", "Pokémon Recharged Yellow", 1, RAM_RY194, &RT_RY194 },
    { "ed60654d7525ba005bfc8f799bf0ecba2ef08a2e", "Pokémon Recharged Yellow", 1, RAM_RY197, &RT_RY197 },
};

static void battle_exp(struct pd_config* c, int size, int pp) {
    c->battleMonSize = size;
    c->battleMoveOff = 0x0C;
    c->battleType1 = 0x22;
    c->battleType2 = 0x23;
    c->battlePp = pp;
    c->battleHp = 0x2A;
    c->battleLevel = 0x2C;
    c->battleMaxHp = 0x2E;
    c->battleStatus = 0x50;
}

static void bag_med(struct pd_config* c) {
    c->bagPocketsN = 6;
    c->bagOrder[0] = PD_POCKET_ITEMS;
    c->bagOrder[1] = PD_POCKET_ITEMS;
    c->bagOrder[2] = PD_POCKET_BALLS;
    c->bagOrder[3] = PD_POCKET_TMHM;
    c->bagOrder[4] = PD_POCKET_BERRIES;
    c->bagOrder[5] = PD_POCKET_KEY;
}

static void icons(struct pd_config* c, uint32_t table, uint32_t idx, uint32_t pal, uint32_t stride, uint8_t mask) {
    c->monIconTable = table;
    c->monIconPaletteIndices = idx;
    c->monIconPaletteTable = pal;
    c->monIconStride = stride;
    c->monPalIdxStride = stride;
    c->monPalIdxMask = mask;
}

static void apply_ram(struct pd_config* c, int id) {
    switch (id) {
    case RAM_HNS:
        c->playerParty = 0x02034764;
        c->playerPartyCount = 0x020342A4;
        c->battleMons = 0x02000420;
        c->battlerPositions = 0x02000238;
        c->battlersCount = 0x020000B0;
        c->battleTypeFlags = 0x020000AC;
        c->gMain = 0x03005BD8;
        c->saveBlock1Ptr = 0x030041D8;
        c->saveBlock2Ptr = 0x030041D4;
        c->mapHeader = 0x02001D0C;
        c->moneyOff = 0x494;
        c->bagPockets = 0x02006DD0;
        bag_med(c);
        c->speciesMask = c->moveMask = 0x07FF;
        c->enemyParty = 0;
        battle_exp(c, 0x88, 0x25);
        icons(c, 0x087E65B0, 0x087E65D2, 0x08D39CC0, 0x10C, 0x07);
        break;
    case RAM_SEAGLASS:
        c->playerParty = 0x02019C20;
        c->playerPartyCount = 0x02019C1D;
        c->gMain = 0x030014B4;
        c->saveBlock1Ptr = 0x030051B8;
        c->saveBlock2Ptr = 0x030051BC;
        c->mapHeader = 0x0200B04C;
        c->bagPockets = 0x0200B0B8;
        c->enemyParty = 0x02019E78;
        c->battleTypeFlags = 0x020002C0;
        c->battlersCount = 0x02000344;
        c->battlerPartyIndexes = 0x02000348;
        c->battlerPositions = 0x02000350;
        c->battleMons = 0x02000360;
        c->battleStructPtr = 0x02000824;
        c->monToSwitchIntoOff = 0x3C;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x088F07F8, 0x088F0806, 0x0893F0B8, 0xD0, 0x07);
        break;
    case RAM_IMPERIUM:
        c->playerParty = 0x020375F8;
        c->playerPartyCount = 0x020375F5;
        c->gMain = 0x03004218;
        c->saveBlock1Ptr = 0x03006218;
        c->saveBlock2Ptr = 0x0300621C;
        c->encryptionKeyOff = 0x44;
        c->mapHeader = 0x0200A68C;
        c->bagPockets = 0x0200AC44;
        bag_med(c);
        c->speciesMask = c->moveMask = 0x07FF;
        c->enemyParty = 0x02037850;
        c->battleTypeFlags = 0x020002C0;
        c->battlersCount = 0x02000344;
        c->battlerPartyIndexes = 0x02000348;
        c->battlerPositions = 0x02000350;
        c->battleMons = 0x02000360;
        c->battleStructPtr = 0x02000830;
        c->monToSwitchIntoOff = 0x3C;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x08D5DA40, 0x08D5DA62, 0x08DC0350, 0x104, 0x07);
        break;
    case RAM_TMT2:
        c->playerParty = 0x02032C94;
        c->playerPartyCount = 0x02032715;
        c->gMain = 0x03004084;
        c->saveBlock1Ptr = 0x030057D8;
        c->saveBlock2Ptr = 0x030057D4;
        c->encryptionKeyOff = 0x44;
        c->mapHeader = 0x02001BBC;
        c->bagPockets = 0x02006C28;
        c->bagPocketsN = 7;
        c->bagOrder[0] = c->bagOrder[1] = c->bagOrder[2] = PD_POCKET_ITEMS;
        c->bagOrder[3] = PD_POCKET_BALLS;
        c->bagOrder[4] = PD_POCKET_TMHM;
        c->bagOrder[5] = PD_POCKET_BERRIES;
        c->bagOrder[6] = PD_POCKET_KEY;
        c->speciesMask = c->moveMask = 0x07FF;
        c->enemyParty = 0x02032A3C;
        c->battleTypeFlags = 0x020000A8;
        c->battlersCount = 0x020000AC;
        c->battlerPartyIndexes = 0x02000298;
        c->battlerPositions = 0x020002A8;
        c->battleMons = 0x020005AC;
        c->battleStructPtr = 0x020000B0;
        c->monToSwitchIntoOff = 0x8B;
        battle_exp(c, 0x60, 0x26);
        icons(c, 0x08712AB0, 0x08712AD2, 0x08DF976C, 0x104, 0x07);
        break;
    case RAM_MARIO15:
        c->playerParty = 0x02025158;
        c->playerPartyCount = 0x02025155;
        c->gMain = 0x03002C40;
        c->saveBlock1Ptr = 0x03004BCC;
        c->saveBlock2Ptr = 0x03004BD0;
        c->mapHeader = 0x02038D9C;
        c->bagPockets = 0x0203B8BC;
        bag_med(c);
        c->enemyParty = 0x02025860;
        c->battleTypeFlags = 0x02024B5C;
        c->battlersCount = 0x02024BD8;
        c->battlerPartyIndexes = 0x02024BDA;
        c->battlerPositions = 0x02024BE2;
        c->battleMons = 0x02024BF0;
        c->battleStructPtr = 0x020250BC;
        c->monToSwitchIntoOff = 0x5C;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x087FC70C, 0x087FF2E4, 0x087FFC4C, 0, 0);
        break;
    case RAM_MARIO13:
        c->playerParty = 0x02025038;
        c->playerPartyCount = 0x02025035;
        c->gMain = 0x03002C40;
        c->saveBlock1Ptr = 0x03004BCC;
        c->saveBlock2Ptr = 0x03004BD0;
        c->mapHeader = 0x020383B8;
        c->bagPockets = 0x0203AEC0;
        c->bagPocketsN = 5;
        c->enemyParty = 0x020254E8;
        c->battleTypeFlags = 0x02024A5C;
        c->battlersCount = 0x02024AD8;
        c->battlerPartyIndexes = 0x02024ADA;
        c->battlerPositions = 0x02024AE2;
        c->battleMons = 0x02024AF0;
        c->battleStructPtr = 0x02024F9C;
        c->monToSwitchIntoOff = 0x3C;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x087DE064, 0x087E0C34, 0x087E159C, 0, 0);
        break;
    case RAM_CELIA:
        c->playerParty = 0x0202433C;
        c->playerPartyCount = 0x020240E1;
        c->battleMons = 0x02023BF8;
        c->battlerPositions = 0x02023BEA;
        c->battlersCount = 0x02023BE0;
        c->battleTypeFlags = 0x02022B60;
        c->gMain = 0x03003490;
        c->saveBlock1Ptr = 0x030053A8;
        c->saveBlock2Ptr = 0x030053AC;
        c->encryptionKeyOff = 0xB18;
        c->mapHeader = 0x02036FF8;
        c->bagPockets = 0x02039B44;
        c->battlerPartyIndexes = 0x02023BE2;
        c->enemyParty = 0;
        icons(c, 0x08C12A48, 0x08C13F0C, 0x08C14440, 0, 0);
        break;
    case RAM_RY180:
    case RAM_RY190:
    case RAM_RY194:
    case RAM_RY197:
        c->battleMons = 0x02001460;
        c->battlerPositions = 0x02001450;
        c->battlersCount = 0x02001444;
        c->battleTypeFlags = 0x020003C0;
        c->gMain = 0x03003B64;
        c->battlerPartyIndexes = 0x02001448;
        c->battleStructPtr = 0x02001880;
        c->monToSwitchIntoOff = 0x5C;
        c->moneyOff = 0x29C;
        c->bagPocketsN = 6;
        bag_med(c);
        if (id == RAM_RY180) {
            c->playerParty = 0x02038554;
            c->playerPartyCount = 0x02038551;
            c->saveBlock1Ptr = 0x03005AB4;
            c->saveBlock2Ptr = 0x03005AB8;
            c->mapHeader = 0x0200B6CC;
            c->enemyParty = 0x020387AC;
            c->bagPockets = 0x0200B768;
            icons(c, 0x0886B7F0, 0x0886BED0, 0x0886C088, 0, 0);
        } else if (id == RAM_RY197) {
            c->playerParty = 0x0203855C;
            c->playerPartyCount = 0x02038559;
            c->saveBlock1Ptr = 0x03005AD0;
            c->saveBlock2Ptr = 0x03005AD4;
            c->mapHeader = 0x0200B6D4;
            c->enemyParty = 0x020387B4;
            c->bagPockets = 0x0200B770;
            icons(c, 0x089740C4, 0x089747A4, 0x0897495C, 0, 0);
        } else {
            c->playerParty = 0x0203855C;
            c->playerPartyCount = 0x02038559;
            c->saveBlock1Ptr = 0x03005AB4;
            c->saveBlock2Ptr = 0x03005AB8;
            c->mapHeader = 0x0200B6D4;
            c->enemyParty = 0x020387B4;
            c->bagPockets = 0x0200B770;
            if (id == RAM_RY190) icons(c, 0x0886B2B0, 0x0886B990, 0x0886BB48, 0, 0);
            else icons(c, 0x0894B2B4, 0x0894B994, 0x0894BB4C, 0, 0);
        }
        break;
    case RAM_RE225:
    case RAM_RE228:
    case RAM_REBAL:
        c->battleMons = 0x02001458;
        c->battlerPositions = 0x02001448;
        c->battlersCount = 0x0200143C;
        c->battleTypeFlags = 0x020003B8;
        c->gMain = 0x030036D4;
        c->mapHeader = 0x0200C1AC;
        c->bagPockets = 0x0200C218;
        bag_med(c);
        c->battlerPartyIndexes = 0x02001440;
        c->battleStructPtr = 0x02001878;
        c->monToSwitchIntoOff = 0x5C;
        if (id == RAM_REBAL) {
            c->playerParty = 0x020373DC;
            c->playerPartyCount = 0x020373D9;
            c->enemyParty = 0x02037634;
            c->saveBlock1Ptr = 0x03007220;
            c->saveBlock2Ptr = 0x03007224;
            c->encryptionKeyOff = 0xB0;
            c->moneyOff = 0x494;
            icons(c, 0x088DD3F0, 0x088DDAD0, 0x088DDC88, 0, 0);
        } else {
            c->playerParty = 0x020373C8;
            c->playerPartyCount = 0x020373C5;
            c->enemyParty = 0x02037620;
            if (id == RAM_RE225) {
                c->saveBlock1Ptr = 0x030071F8;
                c->saveBlock2Ptr = 0x030071FC;
                icons(c, 0x088DADD4, 0x088DB4B4, 0x088DB66C, 0, 0);
            } else {
                c->saveBlock1Ptr = 0x03007214;
                c->saveBlock2Ptr = 0x03007218;
                icons(c, 0x088DCC6C, 0x088DD34C, 0x088DD504, 0, 0);
            }
        }
        break;
    case RAM_SG:
    case RAM_SG12:
        c->battleMons = 0x02001ECC;
        c->battlerPositions = 0x02001B58;
        c->battlersCount = 0x02001AB8;
        c->battleTypeFlags = 0x02001AB4;
        c->gMain = 0x030055C0;
        c->saveBlock1Ptr = 0x030040C4;
        c->saveBlock2Ptr = 0x030040C0;
        c->mapHeader = 0x02003828;
        c->encryptionKeyOff = 0xB4;
        c->moneyOff = 0x478;
        c->bagPockets = 0x020088D4;
        c->bagPocketsN = 8;
        c->bagOrder[0] = c->bagOrder[1] = c->bagOrder[2] = PD_POCKET_ITEMS;
        c->bagOrder[3] = PD_POCKET_BALLS;
        c->bagOrder[4] = PD_POCKET_TMHM;
        c->bagOrder[5] = PD_POCKET_ITEMS;
        c->bagOrder[6] = PD_POCKET_BERRIES;
        c->bagOrder[7] = PD_POCKET_KEY;
        c->speciesMask = c->moveMask = 0x07FF;
        c->monStride = 96;
        c->mapSecWide = 1;
        c->battlerPartyIndexes = 0x02001B5C;
        c->battleStructPtr = 0x02001ABC;
        c->monToSwitchIntoOff = 0x115;
        battle_exp(c, 0x98, 0x25);
        if (id == RAM_SG) {
            c->playerParty = 0x0203901C;
            c->playerPartyCount = 0x02038DD5;
            c->enemyParty = 0x02038DDC;
            icons(c, 0x087D4514, 0x087D453A, 0x08F15970, 0x118, 0x07);
        } else {
            c->playerParty = 0x02039024;
            c->playerPartyCount = 0x02038DDD;
            c->enemyParty = 0x02038DE4;
            icons(c, 0x087D5D3C, 0x087D5D62, 0x08F20ABC, 0x120, 0x07);
        }
        break;
    case RAM_ROGUE:
        c->playerParty = 0x02036C7C;
        c->playerPartyCount = 0x02036C79;
        c->monStride = 104;
        c->speciesMask = 0x07FF;
        c->gMain = 0x030014B4;
        c->saveBlock1Ptr = 0x03003614;
        c->saveBlock2Ptr = 0x03003618;
        c->encryptionKeyOff = 0x4C;
        c->moneyOff = 0x4A8;
        c->mapHeader = 0x020264B4;
        c->bagPockets = 0x02026524;
        c->bagPocketsN = 9;
        c->bagOrder[0] = c->bagOrder[1] = c->bagOrder[2] = c->bagOrder[3] = PD_POCKET_ITEMS;
        c->bagOrder[4] = PD_POCKET_BALLS;
        c->bagOrder[5] = PD_POCKET_TMHM;
        c->bagOrder[6] = PD_POCKET_BERRIES;
        c->bagOrder[7] = PD_POCKET_ITEMS;
        c->bagOrder[8] = PD_POCKET_KEY;
        c->enemyParty = 0x02036EEC;
        c->battleTypeFlags = 0x0201C2C0;
        c->battlersCount = 0x0201C33C;
        c->battlerPartyIndexes = 0x0201C340;
        c->battlerPositions = 0x0201C348;
        c->battleMons = 0x0201C358;
        c->battleStructPtr = 0x0201C83C;
        c->monToSwitchIntoOff = 0x3C;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x0905BCB4, 0x0905BCC6, 0x0909713C, 0x98, 0x07);
        break;
    case RAM_QUETZAL:
        c->playerParty = 0x020235CC;
        c->playerPartyCount = 0x020235C9;
        c->monStride = 0x68;
        c->gMain = 0x03002660;
        c->saveBlock1Ptr = 0x030055BC;
        c->saveBlock2Ptr = 0x030055C0;
        c->encryptionKeyOff = 0x2C;
        c->moneyOff = 0x918;
        c->mapHeader = 0x02038CB0;
        c->enemyParty = 0x02023AAC;
        c->battleTypeFlags = 0x02022FBC;
        c->battlersCount = 0x02023044;
        c->battlerPartyIndexes = 0x02023046;
        c->battlerPositions = 0x0202304E;
        c->battleMons = 0x0202305C;
        c->battleStructPtr = 0x0202352C;
        c->monToSwitchIntoOff = 0x3C;
        c->bagPockets = 0;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x091CFB80, 0x091D1DDC, 0x091D2674, 0, 0);
        break;
    case RAM_ELITE:
    case RAM_ELITE3:
        c->gMain = 0x03003420;
        c->saveBlock1Ptr = 0x0300340C;
        c->saveBlock2Ptr = 0x03003410;
        c->moneyOff = 0x400;
        c->bagPockets = 0;
        c->monStride = 0x4C;
        c->battlerPositions = 0x0201C490;
        c->battlersCount = 0x0201C484;
        c->battleTypeFlags = 0x0201C3F4;
        c->battlerPartyIndexes = 0x0201C488;
        c->monToSwitchIntoOff = 0x5E;
        c->battleMonSize = 0x68;
        c->battleSpeciesOff = 0x14;
        c->battleMoveOff = 0x20;
        c->battleType1 = 0x44;
        c->battleType2 = 0x45;
        c->battlePp = 0x47;
        c->battleHp = 0x36;
        c->battleLevel = 0x4B;
        c->battleMaxHp = 0x38;
        c->battleStatus = 0x08;
        if (id == RAM_ELITE) {
            c->playerParty = 0x02039910;
            c->playerPartyCount = 0x0203990D;
            c->battleMons = 0x0201C514;
            c->mapHeader = 0x020277C0;
            c->enemyParty = 0x02039AD8;
            c->battleStructPtr = 0x0201CB10;
            icons(c, 0x08E31FA0, 0x08E37A50, 0x08E38848, 0, 0);
        } else {
            c->playerParty = 0x02039908;
            c->playerPartyCount = 0x02039905;
            c->battleMons = 0x0201C50C;
            c->mapHeader = 0x020277B8;
            c->enemyParty = 0x02039AD0;
            c->battleStructPtr = 0x0201CB08;
            icons(c, 0x08E1FB28, 0x08E255D8, 0x08E263D0, 0, 0);
        }
        break;
    case RAM_TEAM:
        c->playerParty = 0x02025170;
        c->playerPartyCount = 0x0202516D;
        c->gMain = 0x030032E0;
        c->saveBlock1Ptr = 0x0300524C;
        c->saveBlock2Ptr = 0x03005250;
        c->mapHeader = 0x020382D8;
        c->bagPockets = 0x0203ADDC;
        c->bagPocketsN = 8;
        c->encryptionKeyOff = 0xAC;
        c->moneyOff = 0x490;
        c->bagOrder[0] = c->bagOrder[1] = PD_POCKET_ITEMS;
        c->bagOrder[2] = PD_POCKET_BALLS;
        c->bagOrder[3] = PD_POCKET_ITEMS;
        c->bagOrder[4] = PD_POCKET_BERRIES;
        c->bagOrder[5] = PD_POCKET_ITEMS;
        c->bagOrder[6] = PD_POCKET_TMHM;
        c->bagOrder[7] = PD_POCKET_KEY;
        c->enemyParty = 0x020253C8;
        c->battleTypeFlags = 0x02024BB8;
        c->battlersCount = 0x02024C38;
        c->battlerPartyIndexes = 0x02024C3A;
        c->battlerPositions = 0x02024C42;
        c->battleMons = 0x02024C50;
        c->battleStructPtr = 0x020250EC;
        c->monToSwitchIntoOff = 0x62;
        battle_exp(c, 0x5C, 0x25);
        icons(c, 0x08C722E8, 0x08C74754, 0x08C75070, 0, 0);
        break;
    case RAM_ROWE:
        c->playerParty = 0x02025128;
        c->playerPartyCount = 0x02025125;
        c->monStride = 0x4C;
        c->gMain = 0x03002B90;
        c->saveBlock1Ptr = 0x03004AFC;
        c->saveBlock2Ptr = 0x03004B00;
        c->encryptionKeyOff = 0x40;
        c->moneyOff = 0x400;
        c->mapHeader = 0x02037BF4;
        c->bagPockets = 0x0203B3B4;
        c->bagPocketsN = 10;
        c->bagOrder[0] = c->bagOrder[1] = PD_POCKET_ITEMS;
        c->bagOrder[2] = PD_POCKET_BALLS;
        c->bagOrder[3] = c->bagOrder[4] = c->bagOrder[5] = PD_POCKET_ITEMS;
        c->bagOrder[6] = PD_POCKET_BERRIES;
        c->bagOrder[7] = PD_POCKET_ITEMS;
        c->bagOrder[8] = PD_POCKET_TMHM;
        c->bagOrder[9] = PD_POCKET_KEY;
        c->enemyParty = 0x020252F0;
        c->battleTypeFlags = 0x02024B6C;
        c->battlersCount = 0x02024BF8;
        c->battlerPartyIndexes = 0x02024BFA;
        c->battlerPositions = 0x02024C02;
        c->battleMons = 0x02024C10;
        c->battleStructPtr = 0x020250A4;
        c->monToSwitchIntoOff = 0x5E;
        battle_exp(c, 0x60, 0x25);
        icons(c, 0x08F6F910, 0x08F72778, 0x08F73314, 0, 0);
        break;
    default: break;
    }
}

bool pd_hack_apply(struct pd_game* g, const char* sha1, const struct pd_config* firered, const struct pd_config* emerald) {
    if (!g || !sha1) return false;
    const struct hack* h = NULL;
    for (size_t i = 0; i < sizeof(HACKS) / sizeof(HACKS[0]); i++) {
        if (!strcmp(sha1, HACKS[i].sha)) {
            h = &HACKS[i];
            break;
        }
    }
    if (!h) return false;
    g->kind = h->base == 1 ? PD_GAME_FIRERED : PD_GAME_EMERALD;
    g->title = h->title;
    g->unsupported = NULL;
    g->rt = h->rt;
    const struct pd_config* base = h->base == 1 ? firered : emerald;
    if (!base) return true;
    static struct pd_config liveCfg;
    liveCfg = *base;
    if (h->ram) apply_ram(&liveCfg, h->ram);
    // These two builds share a RAM map and move their icon tables.
    if (!strcmp(sha1, "06e79d12afa578db97c23c1f7ba771fe2f8fe905"))
        icons(&liveCfg, 0x087FCBC0, 0x087FF798, 0x08800100, 0, 0);
    if (!strcmp(sha1, "5d6a036260fdbde96f85b5c1b92d0256d3aebafb"))
        icons(&liveCfg, 0x087D5CA4, 0x087D5CCA, 0x08F20A18, 0x120, 0x07);
    g->cfg = &liveCfg;
    return true;
}
