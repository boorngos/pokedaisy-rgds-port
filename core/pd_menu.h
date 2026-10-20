// The game list on the bottom screen (an OPTION-style pick-list, D-pad or
// stylus) and the top screen's splash while no game runs.
#ifndef PD_MENU_H
#define PD_MENU_H

#include <stdbool.h>

#include "pd_canvas.h"

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

// The stick-click menu over the companion. n rows, sel highlighted.
void pd_quick_draw(struct pd_canvas* c, const char* const* items, int n, int sel);
// The row under (x, y), or -1. Matches what pd_quick_draw laid out.
int pd_quick_row_at(int x, int y, int n);

#endif
