// The launcher's SETTINGS page: the options that apply before a game is
// running, with a search line. Typing filters the rows ("zoom", "fight",
// "lcd"); left and right change the selected one.
#ifndef PD_SETUP_H
#define PD_SETUP_H

#include "pd_canvas.h"
#include "pd_ra_form.h"
#include "pd_ui.h"

struct pd_setup {
    char query[24];
    int selected; // index into the filtered list
    int pressed;  // filtered index under the stylus, -1 = none
    bool touching;
    bool account; // the RETROACHIEVEMENTS sign-in page
    struct pd_ra_nav ra;
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
// The sign-in page. key's dir matches pd_ra_form_key and returns the same
// 1 / 2. close drops the password.
int pd_setup_ra_open(const struct pd_setup* s);
int pd_setup_ra_key(struct pd_setup* s, struct pd_settings* set, int dir);
void pd_setup_ra_close(struct pd_setup* s, struct pd_settings* set);
void pd_setup_ra_type(struct pd_setup* s, struct pd_settings* set, int ch);

#endif
