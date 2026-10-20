// Opens the game's own PC (WITHDRAW / DEPOSIT) where the player stands.
#ifndef PD_PC_H
#define PD_PC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct mCore;
struct pd_game;

// False when this ROM has no known PC, a menu or battle is up, or the call
// did not return. The script stays in EWRAM until the player leaves the PC.
bool pd_pc_open(struct mCore* core, const struct pd_game* game, uint8_t* ewram, size_t ewramSize, bool inBattle);

#endif
