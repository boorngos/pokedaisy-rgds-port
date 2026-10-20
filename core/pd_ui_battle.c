// BATTLE: INFO (BattleInfoScreen.kt) and SUGGESTIONS (SuggestionsScreen.kt).
//
// INFO: one white window split by rules - the foe, you, then your moves with
// their plain-words verdicts against the foe. SUGGESTIONS: the foe on a VS
// line, then the party's three best picks against it, each with its best
// move (the app's rankSuggestions). Under either, a bar: the button to the
// other pane and, in a trainer battle, the FOE TEAM - the trainer's party in
// order, Poké Balls until sent out or announced, fainted ones faded, the one
// the panes are about framed in red. Tapping one shows it (NEXT / FAINTED /
// RESERVE / UNSEEN), tapping it again follows the battle.
#include <stdio.h>
#include <stdlib.h>

#include "pd_icon.h"
#include "pd_tables.h"
#include "pd_ui_internal.h"

#define BAR_H 34
#define ROW 34      // a card's row: the icon and two lines of text
#define RULE 5      // a separator and the space around it
#define FRAME 3     // the white window's frame
#define LINE 16     // PD_FONT_HEIGHT

// BattleControlsScreen's button colours.
#define SUGGEST_GOLD 0xE8B830
#define SUGGEST_GOLD_DARK 0xA87C10
#define INFO_PURPLE 0x9068D0
#define INFO_PURPLE_DARK 0x5C3C98
#define VERDICT_GREY 0x8C8C94
#define OUT_GREEN 0x48A858

// What a card shows: a battler (types from the battle) or a party Pokémon.
struct mon_view {
    int species, level, hp, maxHp;
    int type1, type2;
    uint32_t status;
    const int* moves;
    const int* pp;
};

static struct mon_view from_battler(const struct pd_battle_mon* b) {
    return (struct mon_view) { b->species, b->level, b->hp, b->maxHp, b->type1, b->type2, b->status, b->moves, b->pp };
}

static struct mon_view from_party(const struct pd_mon* m) {
    struct mon_view v = { m->species, m->level, m->hp, m->maxHp, PD_TYPE_NONE, PD_TYPE_NONE, m->status, m->moves, m->pp };
    pd_species_type_pair(m->species, &v.type1, &v.type2);
    return v;
}

static void types_text(int t1, int t2, char* buf, size_t len) {
    if (t1 == PD_TYPE_NONE) snprintf(buf, len, "-");
    else if (t2 != t1 && t2 != PD_TYPE_NONE) snprintf(buf, len, "%s/%s", pd_type_name(t1), pd_type_name(t2));
    else snprintf(buf, len, "%s", pd_type_name(t1));
}

// A small pill of white text (VerdictPill): returns its width.
static int pill(struct pd_canvas* c, int x, int y, const char* text, uint32_t fill) {
    int w = pd_text_width(text) + 6;
    pd_round_rect(c, x, y + 1, w, 14, 2, fill);
    pd_text(c, x + 3, y - 1, text, 0xFFFFFF, PD_NO_SHADOW);
    return w;
}

// The icon on the window's grey, or a ball when the ROM has no icon for it.
static void icon(struct pd_canvas* c, const struct pd_game* g, int species, int x, int y, bool faded) {
    pd_round_rect(c, x, y, PD_ICON + 2, PD_ICON + 2, 3, PD_LIST_FILL);
    if (!pd_draw_mon_icon(c, g, species, x + 1, y + 1, faded)) ui_ball(c, x + 12, y + 12, true);
}

// A card across w: icon, then [side] name ... [state] Lv on the first line,
// types, HP bar and HP on the second.
static void card(struct pd_canvas* c, const struct pd_game* g, int x, int y, int w, const struct mon_view* m,
                 const char* side, const char* state) {
    char buf[40];
    icon(c, g, m->species, x, y, false);
    int tx = x + PD_ICON + 8, right = x + w;
    int ty = y + 1;
    int nx = tx;
    if (side) nx += pd_text(c, nx, ty, side, PD_VALUE, PD_VALUE_SHADOW) + 5;
    char lv[12];
    snprintf(lv, sizeof(lv), "Lv%d", m->level);
    int lvx = right - pd_text_width(lv);
    pd_text(c, lvx, ty, lv, PD_LABEL, PD_LABEL_SHADOW);
    int stateW = state && *state ? pd_text_width(state) + 6 + 4 : 0;
    const char* status = pd_status_label(m->status);
    int statusW = *status ? pd_text_width(status) + 6 + 4 : 0;
    int nameEnd = nx + pd_text_fit(c, nx, ty, lvx - 6 - stateW - statusW - nx,
                                   pd_species_name(m->species, buf, sizeof(buf)), PD_LABEL, PD_LABEL_SHADOW);
    if (*status) {
        ui_status_badge(c, nameEnd + 4, ty + 1, m->status);
        nameEnd += statusW;
    }
    if (stateW) pill(c, nameEnd + 4, ty, state, VERDICT_GREY);

    ty += LINE + 1;
    types_text(m->type1, m->type2, buf, sizeof(buf));
    int typesW = pd_text_fit(c, tx, ty, 92, buf, PD_MUTED, PD_MUTED_SHADOW);
    char hp[16];
    snprintf(hp, sizeof(hp), "%d/%d", m->hp, m->maxHp);
    int hpx = right - pd_text_width(hp);
    pd_text(c, hpx, ty, hp, PD_LABEL, PD_LABEL_SHADOW);
    int barX = tx + (typesW > 92 ? typesW : 92) + 6;
    int barW = hpx - 6 - barX - 18;
    if (barW >= 24) ui_hp_bar(c, barX, ty + 5, barW, m->hp, m->maxHp);
}

// The plain-words verdict of BattleInfoScreen: SUPER 2x / RESISTED ½x / ...
static void verdict(int pct, const char** text, uint32_t* color, uint32_t* shadow) {
    if (pct == 0) { *text = "IMMUNE 0x"; *color = PD_MUTED; *shadow = PD_MUTED_SHADOW; }
    else if (pct >= 400) { *text = "SUPER 4x"; *color = PD_VALUE; *shadow = PD_VALUE_SHADOW; }
    else if (pct >= 200) { *text = "SUPER 2x"; *color = PD_VALUE; *shadow = PD_VALUE_SHADOW; }
    else if (pct <= 25) { *text = "RESISTED 1/4x"; *color = PD_MUTED; *shadow = PD_MUTED_SHADOW; }
    else if (pct < 100) { *text = "RESISTED 1/2x"; *color = PD_MUTED; *shadow = PD_MUTED_SHADOW; }
    else { *text = "NEUTRAL 1x"; *color = PD_LABEL; *shadow = PD_LABEL_SHADOW; }
}

// A move on the window's grey: its name and PP, then its verdict on foe
// (NULL = no foe: no verdict).
static void move_cell(struct pd_canvas* c, int x, int y, int w, int h, int move, int pp, const struct mon_view* foe) {
    char buf[40];
    pd_round_rect(c, x, y, w, h, 3, PD_LIST_FILL);
    if (!move) return;
    int ix = x + 5, iw = w - 10;
    int ty = y + (h - 2 * LINE - 1) / 2;
    char ppText[12];
    snprintf(ppText, sizeof(ppText), "PP %d", pp);
    int ppW = pd_text_width(ppText);
    pd_text_fit(c, ix, ty, iw - ppW - 6, pd_move_name(move, buf, sizeof(buf)), PD_LABEL, PD_LABEL_SHADOW);
    pd_text_right(c, ix + iw, ty, ppText, PD_MUTED, PD_MUTED_SHADOW);
    const char* text = "";
    uint32_t color = PD_LABEL, shadow = PD_LABEL_SHADOW;
    if (pd_move_power(move) == 0) {
        text = "STATUS";
        color = PD_MUTED;
        shadow = PD_MUTED_SHADOW;
    } else if (foe) {
        verdict(pd_type_multiplier_pct(pd_move_type(move), foe->type1, foe->type2), &text, &color, &shadow);
    }
    pd_text(c, ix, ty + LINE + 1, text, color, shadow);
}

static void rule(struct pd_canvas* c, int x, int y, int w) {
    pd_fill(c, x, y + RULE / 2, w, 1, PD_DIVIDER);
}

// A coloured square-cornered button (PlatinumButton / TextNavButton).
static void nav_button(struct pd_ui* ui, struct pd_canvas* c, int x, int y, int w, int h, const char* label,
                       uint32_t fill, uint32_t dark, int id) {
    bool pressed = ui->pressedId == id;
    static const struct pd_layer FRAME_LAYERS[1] = { { 0, 2 } };
    struct pd_layer frame = FRAME_LAYERS[0];
    frame.color = dark;
    pd_layered_box(c, x, y, w, h, 3, &frame, 1, pressed ? dark : fill);
    pd_text(c, x + (w - pd_text_width(label)) / 2, pd_text_y(y, h), label, 0xFFFFFF, dark);
    ui_add_hit(ui, x, y, w, h, id);
}

// The FOE TEAM, from x to the right edge.
static void foe_strip(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                      int x, int y, int right) {
    if (s->foeCount < 2) return;
    int cell = BAR_H, gap = 3;
    int need = s->foeCount * cell + (s->foeCount - 1) * gap;
    if (right - x < need) cell = (right - x - (s->foeCount - 1) * gap) / s->foeCount;
    int sx = right - (s->foeCount * cell + (s->foeCount - 1) * gap);
    for (int i = 0; i < s->foeCount; i++, sx += cell + gap) {
        bool shown = (ui->foeSeen >> i & 1) || i == ui->foeShown;
        bool current = i == ui->foeShown;
        struct pd_layer layers[2] = { { PD_VALUE, 2 }, { PD_VALUE_SHADOW, 1 } };
        struct pd_layer plain[2] = { { 0x63737B, 2 }, { 0xCED6D6, 1 } };
        pd_layered_box(c, sx, y, cell, cell, 3, current ? layers : plain, 2, shown ? PD_TITLE_FILL : PD_LIST_FILL);
        const struct pd_mon* m = &s->foes[i];
        if (!shown) {
            ui_ball(c, sx + (cell - 10) / 2, y + (cell - 10) / 2, true);
        } else if (!pd_draw_mon_icon(c, g, m->species, sx + (cell - PD_ICON) / 2, y + (cell - PD_ICON) / 2,
                                     m->hp == 0)) {
            char lv[8];
            snprintf(lv, sizeof(lv), "%d", m->level);
            pd_text(c, sx + (cell - pd_text_width(lv)) / 2, pd_text_y(y, cell), lv,
                    m->hp ? PD_LABEL : PD_MUTED, PD_NO_SHADOW);
        }
        ui_add_hit(ui, sx, y, cell, cell, HIT_FOE_SLOT + i);
    }
}

// The foe the panes are about: the tapped one, else the one about to come
// in, else the one out (CompanionScreen's shownFoe). A party Pokémon only
// when it isn't the one out - that one is the battler, with its live types.
static const struct pd_mon* shown_foe(struct pd_ui* ui, const struct pd_snapshot* s, const char** state) {
    int shown = ui->foeSelected >= 0 ? ui->foeSelected : s->foeNext >= 0 ? s->foeNext : s->foeActive;
    ui->foeShown = shown;
    *state = "";
    if (shown < 0 || shown >= s->foeCount || shown == s->foeActive) return NULL;
    const struct pd_mon* m = &s->foes[shown];
    if (shown == s->foeNext) *state = "NEXT";
    else if (m->hp == 0) *state = "FAINTED";
    else if (ui->foeSeen >> shown & 1) *state = "RESERVE";
    else *state = "UNSEEN";
    return m;
}

// --- SUGGESTIONS ---

struct pick {
    const struct pd_mon* mon;
    int move, pp, pct, score;
};

// Whether party Pokémon m is one of yours on the field (isOut: battlers
// carry no personality to match on).
static bool is_out(const struct pd_mon* m, const struct pd_snapshot* s) {
    for (int p = 0; p < 4; p += 2) {
        const struct pd_battle_mon* b = &s->battlers[p];
        if (b->species && b->species == m->species && b->level == m->level && b->maxHp == m->maxHp) return true;
    }
    return false;
}

static int by_score(const void* a, const void* b) {
    const struct pick *x = a, *y = b;
    if (x->score != y->score) return y->score - x->score;
    return (int) (x->mon - y->mon); // stable: party order on ties
}

// rankSuggestions: every living party Pokémon with a usable damaging move,
// by (its best move's power x multiplier on the foe) minus the worst the
// foe's own types do to it.
static int rank(const struct pd_snapshot* s, const struct mon_view* foe, struct pick* out) {
    int n = 0;
    for (int i = 0; i < s->partyCount; i++) {
        const struct pd_mon* m = &s->party[i];
        if (m->hp <= 0 || m->species == PD_SPECIES_EGG) continue;
        int best = -1, bestValue = -1;
        for (int k = 0; k < PD_NUM_MOVES; k++) {
            int power = m->moves[k] ? pd_move_power(m->moves[k]) : 0;
            if (power <= 0 || m->pp[k] <= 0) continue;
            int value = power * pd_type_multiplier_pct(pd_move_type(m->moves[k]), foe->type1, foe->type2);
            if (value > bestValue) {
                bestValue = value;
                best = k;
            }
        }
        if (best < 0) continue;
        int t1, t2;
        pd_species_type_pair(m->species, &t1, &t2);
        int risk = 100;
        if (foe->type1 != PD_TYPE_NONE) {
            risk = pd_type_multiplier_pct(foe->type1, t1, t2);
            if (foe->type2 != PD_TYPE_NONE && foe->type2 != foe->type1) {
                int r2 = pd_type_multiplier_pct(foe->type2, t1, t2);
                if (r2 > risk) risk = r2;
            }
        }
        int pct = pd_type_multiplier_pct(pd_move_type(m->moves[best]), foe->type1, foe->type2);
        out[n++] = (struct pick) { m, m->moves[best], m->pp[best], pct, pd_move_power(m->moves[best]) * pct / 100 - risk };
    }
    qsort(out, (size_t) n, sizeof(out[0]), by_score);
    return n;
}

static void suggestions(struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                        const struct mon_view* foe, const char* foeState, int ix, int iy, int iw, int ih) {
    if (!foe) {
        pd_text(c, ix, iy, "NO FOE ON THE FIELD", PD_MUTED, PD_MUTED_SHADOW);
        return;
    }
    card(c, g, ix, iy, iw, foe, "VS", foeState);
    int y = iy + ROW;
    rule(c, ix, y, iw);
    y += RULE;
    struct pick picks[PD_PARTY_SIZE];
    int n = rank(s, foe, picks);
    if (!n) {
        pd_text_wrap(c, ix, y + 2, iw, LINE, 2, "NO USABLE DAMAGING MOVES IN YOUR PARTY", PD_MUTED, PD_MUTED_SHADOW);
        return;
    }
    static const char* const RANKS[3] = { "1ST", "2ND", "3RD" };
    int rowH = (iy + ih - y - 2 * RULE) / 3;
    int half = (iw - RULE) / 2;
    for (int i = 0; i < n && i < 3; i++) {
        if (i) {
            rule(c, ix, y, iw);
            y += RULE;
        }
        const struct pick* p = &picks[i];
        int ry = y + (rowH - ROW) / 2;
        // The Pokémon: rank and name, then its level, HP and OUT when it's in battle.
        char buf[40];
        icon(c, g, p->mon->species, ix, ry, false);
        int tx = ix + PD_ICON + 8, right = ix + half;
        int nx = tx + pd_text(c, tx, ry + 1, RANKS[i], PD_VALUE, PD_VALUE_SHADOW) + 5;
        pd_text_fit(c, nx, ry + 1, right - nx, pd_species_name(p->mon->species, buf, sizeof(buf)), PD_LABEL,
                    PD_LABEL_SHADOW);
        int ty = ry + LINE + 1;
        char lv[12];
        snprintf(lv, sizeof(lv), "Lv%d", p->mon->level);
        int bx = tx + pd_text(c, tx, ty, lv, PD_LABEL, PD_LABEL_SHADOW) + 5;
        int outW = is_out(p->mon, s) ? pd_text_width("OUT") + 6 : 0;
        if (outW) pill(c, right - outW, ty, "OUT", OUT_GREEN);
        int barW = right - (outW ? outW + 5 : 0) - bx - 18;
        if (barW > 48) barW = 48;
        if (barW >= 16) ui_hp_bar(c, bx, ty + 5, barW, p->mon->hp, p->mon->maxHp);
        // Its best move against the foe.
        pd_fill(c, ix + half + RULE / 2, y + 2, 1, rowH - 4, PD_DIVIDER);
        move_cell(c, ix + half + RULE, y + 1, iw - half - RULE, rowH - 2, p->move, p->pp, foe);
        y += rowH;
    }
}

// --- INFO ---

static void info(struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                 const struct mon_view* foe, const char* foeState, const struct mon_view* you, int ix, int iy,
                 int iw, int ih) {
    int y = iy;
    if (foe) card(c, g, ix, y, iw, foe, s->isTrainer ? "FOE" : "WILD", foeState);
    else pd_text(c, ix, y + 1, "NO FOE ON THE FIELD", PD_MUTED, PD_MUTED_SHADOW);
    y += ROW;
    rule(c, ix, y, iw);
    y += RULE;
    card(c, g, ix, y, iw, you, "YOU", "");
    y += ROW;
    rule(c, ix, y, iw);
    y += RULE;
    int gap = 4;
    int bw = (iw - gap) / 2, bh = (iy + ih - y - gap) / 2;
    for (int i = 0; i < PD_NUM_MOVES; i++) {
        move_cell(c, ix + (i % 2) * (bw + gap), y + (i / 2) * (bh + gap), bw, bh, you->moves[i], you->pp[i], foe);
    }
}

// Platinum's battle buttons (BattleControlsScreen): a light top band and
// darker side bands inside a near-black outline.
static void plat(struct pd_ui* ui, struct pd_canvas* c, int x, int y, int w, int h, uint32_t fill, uint32_t dark,
                 int id) {
    bool pressed = id >= 0 && ui->pressedId == id;
    uint32_t face = pressed ? dark : fill;
    pd_round_rect(c, x, y, w, h, 4, 0x202020);
    if (w <= 6 || h <= 6) return;
    pd_round_rect(c, x + 2, y + 2, w - 4, h - 4, 3, face);
    int side = w / 14;
    if (side < 4) side = 4;
    if (side > 10) side = 10;
    pd_fill(c, x + 2, y + 5, side, h - 8, dark);
    pd_fill(c, x + w - 2 - side, y + 5, side, h - 8, dark);
    pd_fill(c, x + 4, y + 2, w - 8, 3, 0xFFFFFF);
    if (id >= 0) ui_add_hit(ui, x, y, w, h, id);
}

static void plat_label(struct pd_canvas* c, int x, int y, int w, int h, const char* label) {
    pd_text(c, x + (w - pd_text_width(label)) / 2, pd_text_y(y, h), label, 0xFFFFFF, 0x383838);
}

static bool buttons_on(const struct pd_ui* ui) {
    return ui->settings && ui->settings->battleButtons;
}

static void ball_row(struct pd_canvas* c, const struct pd_snapshot* s, int x, int y, int right) {
    int i;
    for (i = 0; i < PD_PARTY_SIZE; i++) {
        bool alive = i < s->partyCount && s->party[i].species && s->party[i].species != PD_SPECIES_EGG && s->party[i].hp > 0;
        bool present = i < s->partyCount && s->party[i].species;
        ui_ball(c, x + i * 16, y, present && alive);
        if (!present) pd_round_rect(c, x + i * 16, y, 10, 10, 5, 0x8A8A8A);
    }
    if (!s->isTrainer || s->foeCount < 1) return;
    int fx = right - s->foeCount * 14;
    for (i = 0; i < s->foeCount; i++) {
        bool alive = s->foes[i].species && s->foes[i].hp > 0;
        ui_ball(c, fx + i * 14, y + 1, alive);
    }
}

static const uint32_t TYPE_COLOR[] = {
    0xA8A878, 0xC03028, 0xA890F0, 0xA040A0, 0xE0C068, 0xB8A038, 0xA8B820, 0x705898, 0xB8B8D0, 0x68A090,
    0xF08030, 0x6890F0, 0x78C850, 0xF8D030, 0xF85888, 0x98D8D8, 0x7038F8, 0x705848,
};

static void mult_text(int pct, char* buf, size_t len) {
    if (pct <= 0) snprintf(buf, len, "0x");
    else if (pct >= 400) snprintf(buf, len, "4x");
    else if (pct >= 200) snprintf(buf, len, "2x");
    else if (pct <= 25) snprintf(buf, len, "1/4x");
    else if (pct < 100) snprintf(buf, len, "1/2x");
    else snprintf(buf, len, "1x");
}

static void move_grid(struct pd_ui* ui, struct pd_canvas* c, const struct mon_view* you, const struct mon_view* foe,
                      int x, int y, int w, int h) {
    int gap = 4;
    int cancelH = 28;
    int gridH = h - cancelH - gap;
    int bw = (w - gap) / 2, bh = (gridH - gap) / 2;
    for (int i = 0; i < PD_NUM_MOVES; i++) {
        int mx = x + (i % 2) * (bw + gap);
        int my = y + (i / 2) * (bh + gap);
        int move = you->moves[i];
        int pp = you->pp[i];
        uint32_t frame = move && (unsigned) pd_move_type(move) < sizeof(TYPE_COLOR) / sizeof(TYPE_COLOR[0])
            ? TYPE_COLOR[pd_move_type(move)] : 0x888888;
        bool usable = move && pp > 0;
        plat(ui, c, mx, my, bw, bh, usable ? 0xE8E8B0 : 0xC8C8A0, frame, usable ? HIT_MOVE + i : -1);
        if (!move) {
            plat_label(c, mx, my, bw, bh, "-");
            continue;
        }
        char buf[40];
        int ix = mx + 8, iw = bw - 16;
        int ty = my + 6;
        pd_text_fit(c, ix, ty, iw, pd_move_name(move, buf, sizeof(buf)), 0x404040, 0xC0C088);
        ty += LINE;
        const char* type = pd_type_name(pd_move_type(move));
        int tw = pd_text_width(type) + 6;
        pd_round_rect(c, ix, ty + 1, tw, 14, 2, frame);
        pd_text(c, ix + 3, ty, type, 0xFFFFFF, PD_NO_SHADOW);
        ty += LINE;
        char pwr[16], mult[8], ppText[12];
        if (pd_move_power(move) <= 0) snprintf(pwr, sizeof(pwr), "PWR -");
        else snprintf(pwr, sizeof(pwr), "PWR %d", pd_move_power(move));
        int px = ix + pd_text(c, ix, ty, pwr, 0x606060, 0xC0C088) + 4;
        if (foe && pd_move_power(move) > 0) {
            mult_text(pd_type_multiplier_pct(pd_move_type(move), foe->type1, foe->type2), mult, sizeof(mult));
            pd_round_rect(c, px, ty + 1, pd_text_width(mult) + 6, 14, 2, 0x6868A0);
            pd_text(c, px + 3, ty, mult, 0xFFFFFF, PD_NO_SHADOW);
        }
        snprintf(ppText, sizeof(ppText), "PP %d", pp);
        pd_text_right(c, ix + iw, ty, ppText, 0x404040, 0xC0C088);
    }
    int cy = y + gridH + gap;
    plat(ui, c, x, cy, w, cancelH, 0x4890D8, 0x2860A0, HIT_CMD_CANCEL);
    plat_label(c, x, cy, w, cancelH, "CANCEL");
}

static const char* mon_name_battle(const struct pd_mon* m, char* buf, size_t len) {
    if (m->species == PD_SPECIES_EGG) return "EGG";
    if (m->nickname[0]) return m->nickname;
    return pd_species_name(m->species, buf, len);
}

static bool can_send(const struct pd_mon* m, const struct pd_snapshot* s) {
    return m->species && m->species != PD_SPECIES_EGG && m->hp > 0 && !is_out(m, s);
}

static void party_pick(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                       const struct mon_view* foe, const char* prompt) {
    int best = -1;
    if (foe) {
        struct pick picks[PD_PARTY_SIZE];
        int n = rank(s, foe, picks);
        for (int i = 0; i < n; i++) {
            if (can_send(picks[i].mon, s)) {
                best = (int) (picks[i].mon - s->party);
                break;
            }
        }
    }
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int promptH = 32;
    int top = CONTENT_Y;
    int gridH = c->h - MARGIN - promptH - 4 - top;
    int gap = 4;
    int cw = (w - gap) / 2, ch = (gridH - 2 * gap) / 3;
    for (int i = 0; i < PD_PARTY_SIZE; i++) {
        int sx = x + (i % 2) * (cw + gap);
        int sy = top + (i / 2) * (ch + gap);
        const struct pd_mon* m = i < s->partyCount && s->party[i].species ? &s->party[i] : NULL;
        bool send = m && can_send(m, s);
        uint32_t fill = !m ? 0x3A6A78 : m->hp == 0 ? 0xC66B10 : 0x3994DE;
        uint32_t dark = !m ? 0x244854 : m->hp == 0 ? 0xA54A00 : 0x297BB5;
        plat(ui, c, sx, sy, cw, ch, fill, dark, send ? HIT_SWITCH + i : -1);
        if (!m) continue;
        if (pd_draw_mon_icon(c, g, m->species, sx + 4, sy + (ch - PD_ICON) / 2, m->hp == 0)) {
        }
        char buf[40];
        int tx = sx + 4 + PD_ICON + 4;
        pd_text_fit(c, tx, sy + 4, sx + cw - tx - 4, mon_name_battle(m, buf, sizeof(buf)), 0xFFFFFF, 0x404040);
        char lv[12];
        snprintf(lv, sizeof(lv), "Lv%d", m->level);
        pd_text(c, tx, sy + 4 + LINE, lv, 0xFFFFFF, 0x404040);
        char hp[16];
        snprintf(hp, sizeof(hp), "%d/%d", m->hp, m->maxHp);
        pd_text_right(c, sx + cw - 6, sy + ch - LINE - 2, hp, 0xFFFFFF, 0x404040);
        int barW = cw - (tx - sx) - 8;
        if (barW > 20 && m->maxHp > 0) ui_hp_bar(c, tx, sy + 4 + LINE * 2, barW, m->hp, m->maxHp);
        if (i == best) pill(c, sx + cw - pd_text_width("BEST") - 10, sy + 3, "BEST", SUGGEST_GOLD);
        else if (is_out(m, s)) pill(c, sx + cw - pd_text_width("OUT") - 10, sy + 3, "OUT", VERDICT_GREY);
        else if (m->hp == 0) pill(c, sx + cw - pd_text_width("FNT") - 10, sy + 3, "FNT", VERDICT_GREY);
    }
    int py = c->h - MARGIN - promptH;
    pd_title_box(c, x, py, w, promptH, 0xFFFFFF);
    pd_text_fit(c, x + 8, pd_text_y(py, promptH), w - 90, prompt, PD_LABEL, PD_LABEL_SHADOW);
    int bw = 72;
    plat(ui, c, x + w - bw - 4, py + 3, bw, promptH - 6, 0xF8F8F8, 0xA0A0A8, HIT_CMD_CANCEL);
    pd_text(c, x + w - bw - 4 + (bw - pd_text_width("CANCEL")) / 2, pd_text_y(py + 3, promptH - 6), "CANCEL", PD_LABEL,
            PD_LABEL_SHADOW);
}

#define ITEM_BERRY_POUCH 365

static bool item_needs_mon(int id) {
    if (id >= 1 && id <= 12) return false;
    if (id >= 73 && id <= 81) return false;
    return true;
}

// FireRed's battle bag pockets (items, key items, poke balls). Berries are the
// BERRY POUCH in key items. Emerald's screen order matches our pocket ids.
static int game_pocket(const struct pd_game* g, int pocket) {
    if (g->kind == PD_GAME_EMERALD) return pocket;
    if (pocket == PD_POCKET_ITEMS) return 0;
    if (pocket == PD_POCKET_KEY) return 1;
    if (pocket == PD_POCKET_BALLS) return 2;
    return -1;
}

static void bag_pick(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s) {
    static const int POCKETS[] = { PD_POCKET_ITEMS, PD_POCKET_BALLS, PD_POCKET_BERRIES, PD_POCKET_KEY };
    int pocket = ui->bagPocket;
    bool known = false;
    for (int i = 0; i < 4; i++)
        if (POCKETS[i] == pocket) known = true;
    if (!known) pocket = ui->bagPocket = PD_POCKET_ITEMS;
    const struct pd_pocket_items* items = s->bagOk ? &s->bag[pocket] : NULL;
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int cancelH = 26;
    int hy = CONTENT_Y;
    plat(ui, c, x, hy, w, 22, 0xE0A030, 0xA87018, -1);
    const char* name = pd_pocket_name(pocket);
    pd_text(c, x + (w - pd_text_width(name)) / 2, pd_text_y(hy, 22), name, 0xFFFFFF, 0x383838);
    pd_triangle(c, x + 10, hy + 6, 6, PD_LEFT, 0xFFFFFF);
    pd_triangle(c, x + w - 16, hy + 6, 6, PD_RIGHT, 0xFFFFFF);
    ui_add_hit(ui, x, hy, 48, 22, HIT_POCKET_PREV);
    ui_add_hit(ui, x + w - 48, hy, 48, 22, HIT_POCKET_NEXT);
    int ly = hy + 26;
    int lh = c->h - MARGIN - cancelH - 4 - ly;
    int f = pd_list_box(c, x, ly, w, lh);
    int ix = x + f + 2, iy = ly + f + 1, iw = w - 2 * (f + 2), ih = lh - 2 * (f + 1);
    ui_add_scroll_hit(ui, ix, iy, iw, ih, HIT_BAG_LIST);
    int count = items ? items->count : 0;
    int rowH = 18;
    ui->bagScroll = ui_clamp_scroll(ui->bagScroll, count * rowH, ih);
    if (!count) {
        pd_text(c, ix + 8, iy + 2, "The pocket is empty.", PD_MUTED, PD_MUTED_SHADOW);
    } else {
        pd_clip(c, ix, iy, iw, ih);
        int first = ui->bagScroll / rowH;
        for (int i = first; i < count; i++) {
            int ry = iy + i * rowH - ui->bagScroll;
            if (ry >= iy + ih) break;
            const struct pd_item* it = &items->items[i];
            const char* iname = pd_item_name(g, it->id);
            char fallback[16];
            if (!iname || !*iname) {
                snprintf(fallback, sizeof(fallback), "ITEM %d", it->id);
                iname = fallback;
            }
            if (ui->pressedId == HIT_BAG_ROW + i) pd_fill(c, ix, ry, iw, rowH, PD_ROW_SELECTED);
            pd_text_fit(c, ix + 6, pd_text_y(ry, rowH), iw - 48, iname, PD_LABEL, PD_LABEL_SHADOW);
            char qty[12];
            snprintf(qty, sizeof(qty), "x%d", it->quantity);
            pd_text_right(c, ix + iw - 4, pd_text_y(ry, rowH), qty, PD_MUTED, PD_MUTED_SHADOW);
            int top = ry < iy ? iy : ry;
            int bot = ry + rowH > iy + ih ? iy + ih : ry + rowH;
            ui_add_scroll_hit(ui, ix, top, iw, bot - top, HIT_BAG_ROW + i);
        }
        pd_unclip(c);
        ui_scroll_bar(c, x + w - f - 3, iy, ih, ui->bagScroll, count * rowH, PD_MUTED);
    }
    int cy = c->h - MARGIN - cancelH;
    plat(ui, c, x, cy, w, cancelH, 0x4890D8, 0x2860A0, HIT_CMD_CANCEL);
    plat_label(c, x, cy, w, cancelH, "CANCEL");
}

static void controls(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                     const struct mon_view* you) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int y = CONTENT_Y;
    ball_row(c, s, x, y + 2, x + w);
    y += 18;
    int infoW = 78, sugW = 132, bh = 24, gap = 6;
    int rowW = infoW + gap + sugW;
    int rx = x + (w - rowW) / 2;
    plat(ui, c, rx, y, infoW, bh, INFO_PURPLE, INFO_PURPLE_DARK, HIT_BATTLE_INFO);
    plat_label(c, rx, y, infoW, bh, "INFO");
    plat(ui, c, rx + infoW + gap, y, sugW, bh, SUGGEST_GOLD, SUGGEST_GOLD_DARK, HIT_BATTLE_SUGGEST);
    plat_label(c, rx + infoW + gap, y, sugW, bh, "SUGGESTIONS");
    y += bh + 6;
    int botH = 46;
    int botY = c->h - MARGIN - botH;
    plat(ui, c, x + 8, y, w - 16, botY - 6 - y, 0xE8483C, 0xA82820, HIT_CMD_FIGHT);
    int iconX = x + w / 2 - 36;
    int iconY = y + (botY - 6 - y - PD_ICON) / 2;
    if (you && you->species) pd_draw_mon_icon(c, g, you->species, iconX, iconY, false);
    pd_text(c, iconX + PD_ICON + 8, pd_text_y(y, botY - 6 - y), "FIGHT", 0xFFFFFF, 0x383838);
    int gap3 = 6;
    int bw = (w - 2 * gap3) / 3;
    int drop = 8;
    plat(ui, c, x, botY, bw, botH - drop, 0xE0A030, 0xA87018, HIT_CMD_BAG);
    plat_label(c, x, botY, bw, botH - drop, "BAG");
    bool runGrey = s->isTrainer;
    plat(ui, c, x + bw + gap3, botY + drop, bw, botH - drop, runGrey ? 0x686868 : 0x4890D8, runGrey ? 0x404040 : 0x2860A0,
         runGrey ? -1 : HIT_CMD_RUN);
    plat_label(c, x + bw + gap3, botY + drop, bw, botH - drop, "RUN");
    plat(ui, c, x + 2 * (bw + gap3), botY, bw, botH - drop, 0x58A840, 0x307020, HIT_CMD_PKMN);
    plat_label(c, x + 2 * (bw + gap3), botY, bw, botH - drop, "POKéMON");
}

static void header_buttons(struct pd_ui* ui, struct pd_canvas* c, int x, int y, int w) {
    int infoW = 70, sugW = 120, bh = 22, gap = 6;
    int rowW = infoW + gap + sugW;
    int rx = x + (w - rowW) / 2;
    plat(ui, c, rx, y, infoW, bh, INFO_PURPLE, INFO_PURPLE_DARK, HIT_BATTLE_INFO);
    plat_label(c, rx, y, infoW, bh, "INFO");
    plat(ui, c, rx + infoW + gap, y, sugW, bh, SUGGEST_GOLD, SUGGEST_GOLD_DARK, HIT_BATTLE_SUGGEST);
    plat_label(c, rx + infoW + gap, y, sugW, bh, "SUGGESTIONS");
}

void ui_battle_tab(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s) {
    const struct pd_battle_mon* youB = &s->battlers[PD_POS_PLAYER_LEFT];
    if (!s->inBattle || !youB->species) {
        ui_notice(c, "NO BATTLE", "A battle opens this tab by itself,", "and its end takes you back.");
        return;
    }
    // The foe: the FOE TEAM's shown one, else the battler out.
    const char* foeState;
    const struct pd_mon* party = shown_foe(ui, s, &foeState);
    struct mon_view foeView, you = from_battler(youB);
    const struct mon_view* foe = NULL;
    if (party) {
        foeView = from_party(party);
        foe = &foeView;
    } else if (s->battlers[PD_POS_OPPONENT_LEFT].species) {
        foeView = from_battler(&s->battlers[PD_POS_OPPONENT_LEFT]);
        foe = &foeView;
    }

    bool buttons = buttons_on(ui);
    if (buttons && ui->battlePane == 0) {
        if (ui->battlePick == 1) {
            int x = MARGIN, w = c->w - 2 * MARGIN, y = CONTENT_Y;
            ball_row(c, s, x, y + 2, x + w);
            header_buttons(ui, c, x, y + 18, w);
            move_grid(ui, c, &you, foe, x, y + 46, w, c->h - MARGIN - (y + 46));
            return;
        }
        if (ui->battlePick == 2) {
            party_pick(ui, c, g, s, foe, "Choose a POKéMON.");
            return;
        }
        if (ui->battlePick == 4) {
            party_pick(ui, c, g, s, foe, "Use on which POKéMON?");
            return;
        }
        if (ui->battlePick == 3) {
            bag_pick(ui, c, g, s);
            return;
        }
        controls(ui, c, g, s, &you);
        return;
    }

    int infoPane = buttons ? 1 : 0;
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int barY = c->h - MARGIN - BAR_H;
    int h = barY - 4 - CONTENT_Y;
    int f = pd_title_box(c, x, CONTENT_Y, w, h, PD_TITLE_FILL);
    int ix = x + f + 4, iy = CONTENT_Y + f + 2, iw = w - 2 * (f + 4), ih = h - 2 * (f + 2);
    if (ui->battlePane != infoPane) suggestions(c, g, s, foe, foeState, ix, iy, iw, ih);
    else info(c, g, s, foe, foeState, &you, ix, iy, iw, ih);

    if (buttons) {
        nav_button(ui, c, x, barY, 70, BAR_H, "MENU", 0x687078, 0x404850, HIT_BATTLE_MENU);
        if (ui->battlePane == infoPane)
            nav_button(ui, c, x + 76, barY, pd_text_width("SUGGESTIONS") + 16, BAR_H, "SUGGESTIONS", SUGGEST_GOLD,
                       SUGGEST_GOLD_DARK, HIT_BATTLE_SUGGEST);
        else
            nav_button(ui, c, x + 76, barY, pd_text_width("INFO") + 16, BAR_H, "INFO", INFO_PURPLE, INFO_PURPLE_DARK,
                       HIT_BATTLE_INFO);
        foe_strip(ui, c, g, s, x + 76 + pd_text_width("SUGGESTIONS") + 24, barY, x + w);
    } else {
        const char* label = ui->battlePane ? "INFO" : "SUGGESTIONS";
        int bw = pd_text_width("SUGGESTIONS") + 20;
        if (ui->battlePane) nav_button(ui, c, x, barY, bw, BAR_H, label, INFO_PURPLE, INFO_PURPLE_DARK, HIT_BATTLE_INFO);
        else nav_button(ui, c, x, barY, bw, BAR_H, label, SUGGEST_GOLD, SUGGEST_GOLD_DARK, HIT_BATTLE_SUGGEST);
        foe_strip(ui, c, g, s, x + bw + 8, barY, x + w);
    }
}

static int pouch_index(const struct pd_snapshot* s) {
    if (!s->bagOk) return -1;
    const struct pd_pocket_items* keys = &s->bag[PD_POCKET_KEY];
    for (int i = 0; i < keys->count; i++)
        if (keys->items[i].id == ITEM_BERRY_POUCH) return i;
    return -1;
}

bool ui_battle_act(struct pd_ui* ui, int id) {
    bool buttons = buttons_on(ui);
    if (id == HIT_BATTLE_SUGGEST || id == HIT_BATTLE_INFO || id == HIT_BATTLE_MENU) {
        if (!buttons && id == HIT_BATTLE_MENU) return false;
        ui->battlePick = 0;
        if (buttons) ui->battlePane = id == HIT_BATTLE_SUGGEST ? 2 : id == HIT_BATTLE_INFO ? 1 : 0;
        else ui->battlePane = id == HIT_BATTLE_SUGGEST;
        return true;
    }
    if (buttons && ui->battlePane == 0) {
        if (id == HIT_CMD_FIGHT) {
            ui->battlePick = 1;
            return true;
        }
        if (id == HIT_CMD_BAG) {
            ui->battlePick = 3;
            ui->bagPocket = PD_POCKET_ITEMS;
            ui->bagScroll = 0;
            ui->bagSelected = -1;
            return true;
        }
        if (id == HIT_CMD_PKMN) {
            ui->battlePick = 2;
            return true;
        }
        if (id == HIT_CMD_RUN) {
            ui->action = PD_ACTION_BATTLE_RUN;
            return true;
        }
        if (id == HIT_CMD_CANCEL) {
            ui->battlePick = 0;
            return true;
        }
        if (id >= HIT_MOVE && id < HIT_MOVE + PD_NUM_MOVES) {
            ui->actionArg = id - HIT_MOVE;
            ui->action = PD_ACTION_BATTLE_MOVE;
            ui->battlePick = 0;
            return true;
        }
        if (id >= HIT_SWITCH && id < HIT_SWITCH + PD_PARTY_SIZE) {
            int slot = id - HIT_SWITCH;
            if (ui->battlePick == 4) {
                ui->actionArg4 = slot;
                ui->action = PD_ACTION_BATTLE_ITEM;
            } else {
                ui->actionArg = slot;
                ui->action = PD_ACTION_BATTLE_SWITCH;
            }
            ui->battlePick = 0;
            return true;
        }
        if (ui->battlePick == 3 && (id == HIT_POCKET_PREV || id == HIT_POCKET_NEXT)) {
            static const int POCKETS[] = { PD_POCKET_ITEMS, PD_POCKET_BALLS, PD_POCKET_BERRIES, PD_POCKET_KEY };
            int at = 0;
            for (int i = 0; i < 4; i++)
                if (POCKETS[i] == ui->bagPocket) at = i;
            at = (at + (id == HIT_POCKET_NEXT ? 1 : 3)) % 4;
            ui->bagPocket = POCKETS[at];
            ui->bagScroll = 0;
            return true;
        }
        if (ui->battlePick == 3 && ui->snap && ui->game && id >= HIT_BAG_ROW && id < HIT_BAG_ROW + PD_POCKET_SLOTS) {
            int index = id - HIT_BAG_ROW;
            int pocket = ui->bagPocket;
            const struct pd_snapshot* s = ui->snap;
            if (!s->bagOk || pocket < 0 || pocket >= PD_POCKET_COUNT || index >= s->bag[pocket].count) return true;
            int item = s->bag[pocket].items[index].id;
            int berry = -1;
            int gamePocket = game_pocket(ui->game, pocket);
            if (pocket == PD_POCKET_BERRIES && ui->game->kind != PD_GAME_EMERALD) {
                int pouch = pouch_index(s);
                if (pouch < 0) return true;
                berry = index;
                gamePocket = 1;
                index = pouch;
            }
            if (gamePocket < 0) return true;
            ui->actionArg = gamePocket;
            ui->actionArg2 = index;
            ui->actionArg3 = berry;
            ui->actionArg4 = -1;
            if (berry >= 0 || item_needs_mon(item)) ui->battlePick = 4;
            else {
                ui->action = PD_ACTION_BATTLE_ITEM;
                ui->battlePick = 0;
            }
            return true;
        }
    }
    if (id >= HIT_FOE_SLOT && id < HIT_FOE_SLOT + PD_PARTY_SIZE) {
        int i = id - HIT_FOE_SLOT;
        // Tapping the one shown goes back to following the battle.
        ui->foeSelected = i == ui->foeShown && ui->foeSelected >= 0 ? -1 : i;
        return true;
    }
    return false;
}
