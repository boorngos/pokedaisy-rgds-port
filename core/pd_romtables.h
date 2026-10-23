// Species, move, item and map-section names read from the ROM that is running,
// the way pd_lazarus.c reads Lazarus. Addresses come from PokeDaisy's
// gen_*_tables.py (lidor30/pokedaisy). A null profile means the bundled
// FireRed / Emerald tables.
#ifndef PD_ROMTABLES_H
#define PD_ROMTABLES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pd_game.h"
#include "pd_map.h"

struct pd_rt_profile {
    const char* region;
    uint8_t header; // fill zeroed table pointers from the GF ROM header
    uint8_t species_mode; // 1 inline (species = name of species 1), 2 flat array, 3 pointer per species
    uint32_t species;
    uint16_t species_stride, species_len, species_max;
    int16_t types_off, gender_off; // relative to the inline name; unused when stats != 0
    uint32_t stats;
    uint16_t stats_stride;
    uint8_t stats_type, stats_gender;
    uint8_t item_inline; // name is text, not a pointer
    uint8_t item_array;  // a name array, not a struct per item
    uint32_t items;      // item 0
    uint16_t item_stride, item_len, item_max;
    uint8_t item_name_off; // name's offset inside the record
    int16_t item_key;  // -1 = the slot is the id; else a u16 id at this offset
    int16_t item_desc; // pointer offset from the name; -32768 = none
    uint32_t items_extra;
    uint16_t extra_first, extra_count, extra_stride;
    uint8_t move_inline;
    uint32_t moves; // move 0
    uint16_t move_stride, move_len, move_max;
    uint32_t battle; // move 0 of gBattleMoves; 0 = the move record itself
    uint16_t battle_stride;
    int8_t bits_off; // >= 0: u16 type:5 | category:2 | power:9. -1 = power/type bytes
    int8_t power_off, type_off;
    uint8_t type_mask;
    uint8_t chart_kind; // 0 none, 1 u32 / chart_one, 2 u16 / chart_one
    uint32_t chart;
    uint16_t chart_n, chart_one;
    uint8_t type_set; // 0 vanilla, 1 expansion (1 = Normal), 2 CFRU (Fairy 23), 3 Too Many Types 2
    uint8_t fairy_id; // extra name on the vanilla set (0 = none)
    const char* fairy_name;
    int egg;
    uint32_t map_base;
    uint16_t map_count;
    uint8_t map_name_off, map_xy_off, map_deref;
    uint32_t map2_base;
    uint16_t map2_first, map2_count;
    uint8_t map2_name_off;
    uint32_t map_grid;
    uint8_t map_w, map_h, map_ox, map_oy, map_cell;
    uint16_t map_none;
    uint32_t map_ptrs;
    uint16_t map_ptr_first, map_ptr_count;
};

// The profile after a GF-header fill, or g->rt itself.
const struct pd_rt_profile* pd_rt_get(const struct pd_game* g);

bool pd_rt_species(const struct pd_game* g, int species, char* buf, size_t len);
void pd_rt_species_types(const struct pd_game* g, int species, int* t1, int* t2);
int pd_rt_gender(const struct pd_game* g, int species); // 255 = unknown / genderless
bool pd_rt_move(const struct pd_game* g, int move, char* name, size_t len, int* type, int* power);
bool pd_rt_item(const struct pd_game* g, int item, char* buf, size_t len);
bool pd_rt_item_desc(const struct pd_game* g, int item, char* buf, size_t len);
const char* pd_rt_type_name(const struct pd_rt_profile* rt, int type);
uint32_t pd_rt_type_color(const struct pd_rt_profile* rt, int type);
int pd_rt_chart_pct(const struct pd_game* g, int atk, int def); // 100 when this profile has no chart
bool pd_rt_has_map(const struct pd_game* g);
const char* pd_rt_mapsec(const struct pd_game* g, int mapsec);
// Fills rects (page -1 = not on the map) and the cursor grid. False when this profile has none.
bool pd_rt_map_prepare(const struct pd_game* g, struct pd_mapsec_rect* rects, int nrects, uint8_t* grid);

// Lazarus v2.0's tables (the same addresses pd_lazarus.c reads).
extern const struct pd_rt_profile pd_rt_lazarus;

// sha1 is 40 lowercase hex digits. Installs title, RAM and rt. False = not a known hack.
bool pd_hack_apply(struct pd_game* g, const char* sha1, const struct pd_config* firered, const struct pd_config* emerald);

#endif
