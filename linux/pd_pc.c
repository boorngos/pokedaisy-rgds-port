// The game's own PC, the way a Pokémon Center script opens it:
// lockall; special ShowPokemonStorageSystemPC; waitstate; releaseall; end.
// Addresses and the first-32-byte CRCs are PokeDaisy's Pc.kt / FieldItems.kt
// (retail FireRed, LeafGreen, Emerald, and Lazarus). A ROM whose code moved
// is left alone.
#include "pd_pc.h"

#include "pd_game.h"

#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>

#include <stdio.h>
#include <string.h>

#define PC_SENTINEL 0x080000C0u
#define PC_SCRIPT 0x0203FFF4u

struct pc_rom {
    uint32_t setup;
    uint32_t lock;
    uint32_t specials;
    uint32_t pcFn;
    int special;
    uint32_t setupCrc;
    uint32_t pcCrc;
    uint32_t lockCrc;
};

static const struct pc_rom PC_FR1 = {
    0x08069AF8u, 0x0806996Cu, 0x0815FDD8u, 0x0808C6BDu, 60, 0xAAC00946u, 0x9662F022u, 0x9C26CCA4u,
};
static const struct pc_rom PC_FR0 = {
    0x08069AE4u, 0x08069958u, 0x0815FD60u, 0x0808C6A9u, 60, 0xAAC00946u, 0x9662F022u, 0x9C26CCA4u,
};
static const struct pc_rom PC_LG0 = {
    0x08069AE4u, 0x08069958u, 0x0815FD3Cu, 0x0808C67Du, 60, 0xAAC00946u, 0x5333E53Eu, 0x9C26CCA4u,
};
static const struct pc_rom PC_LG1 = {
    0x08069AF8u, 0x0806996Cu, 0x0815FDB4u, 0x0808C691u, 60, 0xAAC00946u, 0x5333E53Eu, 0x9C26CCA4u,
};
static const struct pc_rom PC_EM = {
    0x08098EF8u, 0x08098E6Cu, 0x081DBA64u, 0x080C7579u, 63, 0xEF3698BAu, 0x81188148u, 0x2532A407u,
};
static const struct pc_rom PC_LAZ = {
    0x0820B2B0u, 0x0820B218u, 0x0828CBF4u, 0x081D54D9u, 63, 0x16121208u, 0xDC62C107u, 0x75F73111u,
};

static uint32_t crc32_bytes(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t) -(int) (c & 1));
    }
    return ~c;
}

static bool rom_crc(const struct pd_game* game, uint32_t addr, uint32_t expect) {
    uint8_t buf[32];
    if (!pd_rom_read(game, addr & ~1u, buf, sizeof(buf))) return false;
    return crc32_bytes(buf, sizeof(buf)) == expect;
}

static const struct pc_rom* pc_for(const struct pd_game* game) {
    if (!game || !game->cfg) return NULL;
    if (game->title && strstr(game->title, "Lazarus")) return &PC_LAZ;
    if (!strcmp(game->code, "BPRE")) return game->revision == 1 ? &PC_FR1 : game->revision == 0 ? &PC_FR0 : NULL;
    if (!strcmp(game->code, "BPGE")) return game->revision == 1 ? &PC_LG1 : game->revision == 0 ? &PC_LG0 : NULL;
    if (!strcmp(game->code, "BPEE")) return &PC_EM;
    return NULL;
}

static bool pc_matches(const struct pd_game* game, const struct pc_rom* pc) {
    uint8_t ent[4];
    if (!pd_rom_read(game, pc->specials + 4u * (uint32_t) pc->special, ent, 4)) return false;
    uint32_t fn = (uint32_t) ent[0] | ((uint32_t) ent[1] << 8) | ((uint32_t) ent[2] << 16) | ((uint32_t) ent[3] << 24);
    if (fn != pc->pcFn) return false;
    return rom_crc(game, pc->setup, pc->setupCrc) && rom_crc(game, pc->pcFn, pc->pcCrc) &&
           rom_crc(game, pc->lock, pc->lockCrc);
}

// Runs a Thumb function and puts the CPU back. Memory writes stay.
static bool thumb_call(struct mCore* core, uint32_t fn, uint32_t arg0, uint32_t* r0) {
    if (!core || core->platform != mPLATFORM_GBA || !core->cpu || !core->board || !core->step) return false;
    struct ARMCore* cpu = core->cpu;
    struct GBA* gba = core->board;
    struct ARMRegisterFile saved = cpu->regs;
    uint32_t prefetch0 = cpu->prefetch[0], prefetch1 = cpu->prefetch[1];
    struct ARMMemory mem = cpu->memory;
    int region = gba->memory.activeRegion;
    uint32_t biosPrefetch = gba->memory.biosPrefetch;
    uint32_t idleLoop = gba->idleLoop, lastJump = gba->lastJump;
    bool haltPending = gba->haltPending;
    int idleStep = gba->idleDetectionStep, idleFailures = gba->idleDetectionFailures;
    int32_t cached[16];
    memcpy(cached, gba->cachedRegisters, sizeof(cached));
    enum ExecutionMode exec = cpu->executionMode;
    int halted = cpu->halted;
    enum PrivilegeMode priv = cpu->privilegeMode;
    int32_t savedCycles = cpu->cycles;
    int32_t savedNext = cpu->nextEvent;
    enum GBAIdleLoopOptimization savedIdle = gba->idleOptimization;
    bool savedBlocked = gba->cpuBlocked;
    // One instruction per step, and no idle-loop skip. Otherwise a step runs
    // the frame's events first and the game takes the CPU back mid-call.
    cpu->cycles = 0;
    cpu->nextEvent = 0x10000000;
    gba->idleOptimization = IDLE_LOOP_IGNORE;
    gba->cpuBlocked = false;
    gba->haltPending = false;
    cpu->halted = 0;
    cpu->cpsr.i = 1;
    if (priv != MODE_SYSTEM && priv != MODE_USER) {
        ARMSetPrivilegeMode(cpu, MODE_SYSTEM);
        cpu->cpsr.priv = MODE_SYSTEM;
    }
    cpu->gprs[0] = (int32_t) arg0;
    cpu->gprs[1] = cpu->gprs[2] = cpu->gprs[3] = 0;
    cpu->gprs[ARM_LR] = (int32_t) (PC_SENTINEL | 1u);
    cpu->gprs[ARM_PC] = (int32_t) (fn & ~1u);
    _ARMSetMode(cpu, MODE_THUMB);
    ThumbWritePC(cpu);
    int ok = 0;
    for (int i = 0; i < 2000000; i++) {
        if (cpu->executionMode == MODE_THUMB &&
            (uint32_t) cpu->gprs[ARM_PC] - WORD_SIZE_THUMB == PC_SENTINEL) {
            ok = 1;
            break;
        }
        core->step(core);
    }
    if (r0) *r0 = (uint32_t) cpu->gprs[0];
    cpu->regs = saved;
    cpu->privilegeMode = priv;
    cpu->executionMode = exec == MODE_ARM ? MODE_THUMB : MODE_ARM;
    _ARMSetMode(cpu, exec);
    cpu->prefetch[0] = prefetch0;
    cpu->prefetch[1] = prefetch1;
    cpu->cycles = savedCycles;
    cpu->nextEvent = savedNext;
    cpu->memory = mem;
    gba->memory.activeRegion = region;
    gba->memory.biosPrefetch = biosPrefetch;
    gba->idleLoop = idleLoop;
    gba->lastJump = lastJump;
    gba->haltPending = haltPending;
    gba->idleOptimization = savedIdle;
    gba->cpuBlocked = savedBlocked;
    gba->idleDetectionStep = idleStep;
    gba->idleDetectionFailures = idleFailures;
    memcpy(gba->cachedRegisters, cached, sizeof(cached));
    cpu->halted = halted;
    return ok;
}

bool pd_pc_open(struct mCore* core, const struct pd_game* game, uint8_t* ewram, size_t ewramSize, bool inBattle) {
    const struct pc_rom* pc = pc_for(game);
    if (!pc) {
        fprintf(stderr, "pokedaisy: PC: this ROM has no known box\n");
        return false;
    }
    if (inBattle) {
        fprintf(stderr, "pokedaisy: PC: not during a battle\n");
        return false;
    }
    if (!pc_matches(game, pc)) {
        fprintf(stderr, "pokedaisy: PC: this ROM's code is not the retail one (%s rev %d)\n", game->code, game->revision);
        return false;
    }
    if (!ewram || ewramSize < (PC_SCRIPT - 0x02000000u) + 7) return false;
    uint32_t locked = 1;
    if (!thumb_call(core, pc->lock, 0, &locked)) {
        fprintf(stderr, "pokedaisy: PC: could not ask if the field is free\n");
        return false;
    }
    if (locked) {
        fprintf(stderr, "pokedaisy: PC: the player is in a menu or a script\n");
        return false;
    }
    uint8_t script[7] = {
        0x69, 0x25, (uint8_t) pc->special, (uint8_t) (pc->special >> 8), 0x27, 0x6B, 0x02,
    };
    memcpy(ewram + (PC_SCRIPT - 0x02000000u), script, sizeof(script));
    if (!thumb_call(core, pc->setup, PC_SCRIPT, NULL)) {
        fprintf(stderr, "pokedaisy: PC: the game did not start the box\n");
        return false;
    }
    return true;
}
