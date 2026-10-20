#include "pd_style.h"

// PokeDaisy's teal, then one accent per game colour. The greys stay put so
// the windows still read as the OPTION screen; the stripes and the red value
// carry the theme.
static const struct pd_palette THEMES[PD_THEMES] = {
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0xCB0707, 0xE0A564,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0x4AADA5, 0x398C8C, 0x216B63, "POKEDAISY" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0xCB0707, 0xE0A564,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0xC45858, 0xA83838, 0x6E2020, "FIRERED" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0x1E7A32, 0x8CC888,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0x58B068, 0x388848, 0x1E5830, "LEAFGREEN" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0x0E8A62, 0x88C8B0,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0x3AAA8C, 0x288870, 0x145848, "EMERALD" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0xC4203A, 0xE09898,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0xC05068, 0x983048, 0x681828, "RUBY" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0x2060C8, 0x98B4E0,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0x4880C8, 0x3060A0, 0x183868, "SAPPHIRE" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0xB88810, 0xE0C878,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0xD0B050, 0xB09030, 0x705818, "GOLD" },
    { 0xFFFFFF, 0x636363, 0xD6D6CE, 0xE0DFDF, 0xFFFFFF, 0x575656, 0xBCBBB4, 0x6840B0, 0xC0A8E0,
      0x8C8C94, 0xD6D6D6, 0xC6C5C5, 0x8870C0, 0x6850A0, 0x403068, "UNBOUND" },
    { 0xE8EEF4, 0x3A4450, 0xB0B8C4, 0xD8DEE6, 0xF4F7FB, 0x3A4450, 0xB0B8C4, 0xC8D4E4, 0x8090A8,
      0x708090, 0xC8D0D8, 0xB8C0C8, 0x2A3440, 0x1C2632, 0x101820, "NIGHT" },
};

static int themeId;

const struct pd_palette* pd_palette(void) {
    if (themeId < 0 || themeId >= PD_THEMES) return &THEMES[0];
    return &THEMES[themeId];
}

void pd_style_set_theme(int theme) {
    themeId = theme >= 0 && theme < PD_THEMES ? theme : 0;
}

const char* pd_theme_name(int theme) {
    if (theme < 0 || theme >= PD_THEMES) theme = 0;
    return THEMES[theme].name;
}

static const struct pd_layer TITLE_LAYERS[] = { { 0x63737B, 2 }, { 0xCED6D6, 1 } };
static const struct pd_layer LIST_LAYERS[] = {
    { 0x293131, 1 }, { 0x8C8CCE, 1 }, { 0x736B84, 2 }, { 0xDED6DE, 1 }, { 0xFFFFFF, 2 },
};
#define N_LAYERS(a) ((int) (sizeof(a) / sizeof((a)[0])))

int pd_title_box(struct pd_canvas* c, int x, int y, int w, int h, uint32_t fill) {
    return pd_layered_box(c, x, y, w, h, 3, TITLE_LAYERS, N_LAYERS(TITLE_LAYERS), fill);
}

int pd_list_box(struct pd_canvas* c, int x, int y, int w, int h) {
    return pd_layered_box(c, x, y, w, h, 3, LIST_LAYERS, N_LAYERS(LIST_LAYERS), PD_LIST_FILL);
}

void pd_backdrop(struct pd_canvas* c) {
    for (int y = 0; y < c->h; y += 8) pd_fill(c, 0, y, c->w, 8, (y / 8) % 2 ? PD_BACKDROP_B : PD_BACKDROP_A);
    pd_fill(c, 0, c->h - 4, c->w, 4, PD_BACKDROP_BAND);
}
