// The RETROACHIEVEMENTS sign-in page: username, password and a gamepad
// keyboard. The password stays in memory only until the host has tried it.
#ifndef PD_RA_FORM_H
#define PD_RA_FORM_H

#include "pd_canvas.h"

struct pd_ra_nav {
    int focus; // 0 username, 1 password
    int nav;   // 0 the two fields, 1 the keys
    int kx, ky;
    int caps;
};

void pd_ra_nav_init(struct pd_ra_nav* n);
// Draws the page. signedIn shows SIGN OUT.
void pd_ra_form_draw(struct pd_canvas* c, const char* user, const char* pass, const char* note, int signedIn,
                     const struct pd_ra_nav* nav);
// dir: 1 up, 2 down, 3 left, 4 right, 5 accept. Returns 1 for SIGN IN, 2 for
// SIGN OUT, else 0. Typed characters land in user or pass.
int pd_ra_form_key(char* user, int userCap, char* pass, int passCap, struct pd_ra_nav* nav, int dir);
// A keyboard character (ch > 0) or backspace (ch < 0) into the focused field.
void pd_ra_form_type(char* user, int userCap, char* pass, int passCap, const struct pd_ra_nav* nav, int ch);

#endif
