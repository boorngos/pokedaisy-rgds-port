// Which of the game's own battle menus is waiting. Same values as the app's
// BATTLE_INPUT_* (Telemetry.kt): the bottom screen follows them.
#ifndef PD_BATTLE_H
#define PD_BATTLE_H

#include "pd_snapshot.h"

enum pd_battle_menu {
    PD_BMENU_NONE = 0,
    PD_BMENU_BUSY = 1,   // a hit, a message, an animation
    PD_BMENU_ACTION = 2, // FIGHT / BAG / POKéMON / RUN
    PD_BMENU_MOVE = 3,
    PD_BMENU_BAG = 4,
    PD_BMENU_PARTY = 5,
    PD_BMENU_TARGET = 6,
};

// NONE outside a battle, or when this ROM has no known handlers. BUSY while
// the battle is up but no player menu is (dialogue, an attack, a switch).
int pd_battle_menu(const struct pd_game* g, pd_read_fn read, void* ctx);

#endif
