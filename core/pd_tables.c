#include "pd_tables.h"

#include <stdio.h>
#include <string.h>

#include "pd_lazarus.h"

// Upper-cases ASCII letters, leaving é (UTF-8 C3 A9) and the rest alone.
static const char* game_case(const char* src, char* buf, size_t len) {
    size_t i = 0;
    for (; src[i] && i + 1 < len; i++) {
        char c = src[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
    }
    buf[i] = 0;
    return buf;
}

const char* pd_species_name(int species, char* buf, size_t len) {
    if (species == 0) return "-";
    if (species == PD_SPECIES_EGG) return "EGG";
    if (pd_is_lazarus(pd_bound_game())) {
        if (pd_laz_species_name(pd_bound_game(), species, buf, len)) return game_case(buf, buf, len);
        snprintf(buf, len, "#%d", species);
        return buf;
    }
    if (species > 0 && species < pd_species_names_count && pd_species_names[species]) {
        return game_case(pd_species_names[species], buf, len);
    }
    snprintf(buf, len, "#%d", species);
    return buf;
}

const char* pd_move_name(int move, char* buf, size_t len) {
    if (move <= 0) return "-";
    if (pd_is_lazarus(pd_bound_game()) && pd_laz_move(pd_bound_game(), move, buf, len, NULL, NULL)) {
        return game_case(buf, buf, len);
    }
    if (move < pd_moves_count && pd_moves[move].name) return game_case(pd_moves[move].name, buf, len);
    snprintf(buf, len, "MOVE#%d", move);
    return buf;
}

const char* pd_type_name(int type) {
    if (pd_is_lazarus(pd_bound_game())) {
        const char* n = pd_laz_type_name(type);
        if (n[0]) return n;
    }
    if (type >= 0 && type < pd_type_names_count && pd_type_names[type]) return pd_type_names[type];
    return "";
}

uint32_t pd_type_color(int type) {
    static const uint32_t VANILLA[] = {
        0xA8A878, 0xC03028, 0xA890F0, 0xA040A0, 0xE0C068, 0xB8A038, 0xA8B820, 0x705898, 0xB8B8D0, 0x68A090,
        0xF08030, 0x6890F0, 0x78C850, 0xF8D030, 0xF85888, 0x98D8D8, 0x7038F8, 0x705848,
    };
    if (pd_is_lazarus(pd_bound_game())) {
        uint32_t c = pd_laz_type_color(type);
        return c ? c : 0x888888;
    }
    return type >= 0 && type < (int) (sizeof(VANILLA) / sizeof(VANILLA[0])) ? VANILLA[type] : 0x888888;
}

int pd_move_type(int move) {
    int type = PD_TYPE_NONE, power = 0;
    if (pd_is_lazarus(pd_bound_game()) && pd_laz_move(pd_bound_game(), move, NULL, 0, &type, &power)) return type;
    if (move > 0 && move < pd_moves_count && pd_moves[move].name) return pd_moves[move].type;
    return PD_TYPE_NONE;
}

int pd_move_power(int move) {
    int type = PD_TYPE_NONE, power = 0;
    if (pd_is_lazarus(pd_bound_game()) && pd_laz_move(pd_bound_game(), move, NULL, 0, &type, &power)) return power;
    if (move > 0 && move < pd_moves_count && pd_moves[move].name) return pd_moves[move].power;
    return 0;
}

static int single_pct(int atk, int def) {
    for (int i = 0; i < pd_type_chart_count; i++) {
        if (pd_type_chart[i].atk == atk && pd_type_chart[i].def == def) return pd_type_chart[i].pct;
    }
    return 100;
}

int pd_type_multiplier_pct(int atk, int def1, int def2) {
    if (pd_is_lazarus(pd_bound_game())) {
        int pct = pd_laz_chart_pct(pd_bound_game(), atk, def1);
        if (def2 != PD_TYPE_NONE && def2 != def1) pct = pct * pd_laz_chart_pct(pd_bound_game(), atk, def2) / 100;
        return pct;
    }
    int pct = single_pct(atk, def1);
    if (def2 != PD_TYPE_NONE && def2 != def1) pct = pct * single_pct(atk, def2) / 100;
    return pct;
}

void pd_species_type_pair(int species, int* t1, int* t2) {
    *t1 = *t2 = PD_TYPE_NONE;
    if (pd_is_lazarus(pd_bound_game())) {
        pd_laz_species_types(pd_bound_game(), species, t1, t2);
        return;
    }
    if (species <= 0 || species >= pd_species_type_table_count) return;
    *t1 = pd_species_type_table[species][0];
    *t2 = pd_species_type_table[species][1];
}
