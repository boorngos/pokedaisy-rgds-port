// The launcher's SETTINGS page: the options that apply before a game is
// running, with a search line. Typing filters the rows ("zoom", "fight",
// "lcd"); left and right change the selected one.
#ifndef PD_SETUP_H
#define PD_SETUP_H

#include "pd_canvas.h"
#include "pd_ui.h"

struct pd_setup {
    char query[24];
    int selected; // index into the filtered list
    int pressed;  // filtered index under the stylus, -1 = none
    bool touching;
};

void pd_setup_init(struct pd_setup* s);
void pd_setup_draw(struct pd_setup* s, struct pd_canvas* c, const struct pd_settings* set);
void pd_setup_move(struct pd_setup* s, const struct pd_settings* set, int delta);
// dir -1 / +1 cycles the selected row's value and applies it (theme included).
void pd_setup_adjust(struct pd_setup* s, struct pd_settings* set, int dir);
// ch > 0 appends, -1 deletes one, -2 clears the search.
void pd_setup_type(struct pd_setup* s, int ch);
// Stylus. Returns 1 when a row was tapped (the caller adjusts it).
int pd_setup_touch(struct pd_setup* s, const struct pd_settings* set, int x, int y, bool down);

#endif
