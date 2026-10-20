// The game list on the bottom screen (an OPTION-style pick-list, D-pad or
// stylus) and the top screen's splash while no game runs.
#ifndef PD_MENU_H
#define PD_MENU_H

#include <stdbool.h>

#include "pd_canvas.h"

struct pd_game;
struct pd_snapshot;

struct pd_menu {
    const char* title;
    const char* const* items;
    int count;
    int selected;
    int scroll;
    bool touching;
    int pressed; // row the stylus went down on, -1 = none, -2 = the SETTINGS gear
};

void pd_menu_init(struct pd_menu* m, const char* title, const char* const* items, int count);
void pd_menu_draw(struct pd_menu* m, struct pd_canvas* c);
void pd_menu_move(struct pd_menu* m, int delta);
// Stylus down / up; returns the row a tap picked (on release), -2 for the
// SETTINGS gear, else -1.
int pd_menu_touch(struct pd_menu* m, int x, int y, bool down);

// The top screen while no game is running: a mint card in the app's teal,
// with the PokeDaisy mark where a camera icon would sit.
void pd_splash_draw(struct pd_canvas* c, const char* line1, const char* line2, const char* line3);

// The quick wheel (PokeDaisy's RING): tiles around a circle, first at the
// top, clockwise. `note` is the banner in the middle (the aimed tile). A bit
// in `blocked` dims that tile.
void pd_quick_draw(struct pd_canvas* c, const char* const* items, int n, int sel, const char* note,
                   unsigned blocked);
// The tile under (x, y), or -1.
int pd_quick_hit(int x, int y, int n);
// Left stick, right and down positive (SDL axes). The tile it points at, or
// -1 while the stick is centred.
int pd_quick_aim(int axisX, int axisY, int n);

// WILD: this map's encounters, caught mark and odds. sel is a line (headers
// included); move skips headers. rows is 0 when the map has none.
int pd_wild_rows(const struct pd_game* g, const struct pd_snapshot* s);
int pd_wild_move(const struct pd_game* g, const struct pd_snapshot* s, int sel, int dir);
void pd_wild_draw(struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s, int sel);

#endif
