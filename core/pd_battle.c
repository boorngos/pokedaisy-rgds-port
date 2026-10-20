// gBattlerControllerFuncs[], scanned the way readNativeBattleInputFast does:
// the active-battler index is a loop variable and is stale by the time we look.
// Addresses are PokeDaisy's NativeConfig (retail FireRed, LeafGreen, Emerald,
// Lazarus). A handler match ignores the Thumb bit.
#include "pd_battle.h"

#include <string.h>

struct battle_ctrl {
    uint32_t funcs;
    uint32_t action, move, bag, party, target;
};

static const struct battle_ctrl FR0 = {
    0x03004FE0, 0x0802E438, 0x0802EA10, 0x0803073C, 0x08030684, 0x0802E674,
};
static const struct battle_ctrl FR1 = {
    0x03004FE0, 0x0802E44C, 0x0802EA24, 0x08030750, 0x08030698, 0x0802E688,
};
static const struct battle_ctrl EM = {
    0x03005D60, 0x08057588, 0x08057BFC, 0x080598E0, 0x08059828, 0x08057824,
};
static const struct battle_ctrl LAZ = {
    0x03002248, 0x0805D649, 0x0805DF0D, 0x0805FD79, 0x0805FCA9, 0x0805D8A5,
};

static const struct battle_ctrl* ctrl_for(const struct pd_game* g) {
    if (!g || !g->cfg) return NULL;
    if (g->title && strstr(g->title, "Lazarus")) return &LAZ;
    if (!strcmp(g->code, "BPRE") || !strcmp(g->code, "BPGE"))
        return g->revision == 1 ? &FR1 : g->revision == 0 ? &FR0 : NULL;
    if (!strcmp(g->code, "BPEE")) return &EM;
    return NULL;
}

static uint32_t ru32(const uint8_t* p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

int pd_battle_menu(const struct pd_game* g, pd_read_fn read, void* ctx) {
    const struct battle_ctrl* c = ctrl_for(g);
    if (!c || !g->cfg) return PD_BMENU_NONE;
    uint8_t flags = 0;
    if (!read(ctx, g->cfg->gMain + 0x439, &flags, 1) || (flags & 0x02) == 0) return PD_BMENU_NONE;
    uint8_t raw[16];
    if (!read(ctx, c->funcs, raw, sizeof(raw))) return PD_BMENU_BUSY;
    uint32_t want[5] = { c->action, c->move, c->bag, c->party, c->target };
    int menu[5] = { PD_BMENU_ACTION, PD_BMENU_MOVE, PD_BMENU_BAG, PD_BMENU_PARTY, PD_BMENU_TARGET };
    for (int i = 0; i < 4; i++) {
        uint32_t fn = ru32(raw + i * 4) & ~1u;
        for (int k = 0; k < 5; k++) {
            if (want[k] && fn == (want[k] & ~1u)) return menu[k];
        }
    }
    return PD_BMENU_BUSY;
}
