// Pokémon Lazarus v2.0 (sha1 7dcdc7e2), read from the player's ROM the way
// PokeDaisy's gen_expansion_tables.py / NATIVE_LAZARUS do. Retail FireRed and
// Emerald keep using the bundled tables.
#ifndef PD_LAZARUS_H
#define PD_LAZARUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pd_game.h"
#include "pd_map.h"

#define PD_LAZ_SPECIES_MAX 1561
#define PD_LAZ_EGG 1561
#define PD_LAZ_MAP_W 28
#define PD_LAZ_MAP_H 15
#define PD_LAZ_MAP_OX 1
#define PD_LAZ_MAP_OY 2
#define PD_LAZ_MAP_NONE 0xD5
#define PD_LAZ_DEX_BYTES 129

bool pd_is_lazarus(const struct pd_game* g);

// The game the name / type lookups without a game argument belong to.
// pd_snapshot_read and the companion frame set it.
void pd_bind_game(const struct pd_game* g);
const struct pd_game* pd_bound_game(void);

// False when this id has no text of its own (a zeroed species, an empty item).
bool pd_laz_species_name(const struct pd_game* g, int species, char* buf, size_t len);
bool pd_laz_species_named(const struct pd_game* g, int species);
void pd_laz_species_types(const struct pd_game* g, int species, int* t1, int* t2);
int pd_laz_gender_ratio(const struct pd_game* g, int species);
// The 0xD4-byte gSpeciesInfo record, or NULL.
const uint8_t* pd_laz_species(const struct pd_game* g, int species);
void pd_laz_ability(const struct pd_game* g, int ability, char* buf, size_t len);

bool pd_laz_item_name(const struct pd_game* g, int item, char* buf, size_t len);
bool pd_laz_item_desc(const struct pd_game* g, int item, char* buf, size_t len);

bool pd_laz_move(const struct pd_game* g, int move, char* name, size_t len, int* type, int* power);
const char* pd_laz_type_name(int type);
uint32_t pd_laz_type_color(int type);
int pd_laz_chart_pct(const struct pd_game* g, int atk, int def);

// Stable for the life of this ROM (the MAP tab keeps the pointer).
const char* pd_laz_mapsec_name(const struct pd_game* g, int mapsec);
// Fills rects (page -1 = not on the map) and copies the cursor grid. Once per ROM.
void pd_laz_map_prepare(const struct pd_game* g, struct pd_mapsec_rect* rects, uint8_t* grid);

#endif
