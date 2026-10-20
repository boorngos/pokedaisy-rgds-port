#include "pd_ra_form.h"

#include <string.h>

#include "pd_style.h"

#define TITLE_H 24

static const char* const ROWS[] = { "abcdefghi", "jklmnopqr", "stuvwxyz0", "123456789", "._-@<" };
#define KEY_ROWS 5
#define BTN_ROW 5

static int row_n(int y) {
    if (y < 0 || y >= KEY_ROWS) return y == BTN_ROW ? 3 : 0;
    return (int) strlen(ROWS[y]);
}

void pd_ra_nav_init(struct pd_ra_nav* n) {
    n->focus = 0;
    n->nav = 0;
    n->kx = n->ky = n->caps = 0;
}

static void type_ch(char* field, int cap, int ch) {
    size_t n = strlen(field);
    if (ch < 0) {
        if (n) field[n - 1] = 0;
    } else if (ch >= 32 && ch < 127 && (int) n + 1 < cap) {
        field[n] = (char) ch;
        field[n + 1] = 0;
    }
}

void pd_ra_form_type(char* user, int userCap, char* pass, int passCap, const struct pd_ra_nav* nav, int ch) {
    if (nav->focus) type_ch(pass, passCap, ch);
    else type_ch(user, userCap, ch);
}

static void clamp_key(struct pd_ra_nav* nav) {
    int n = row_n(nav->ky);
    if (n < 1) n = 1;
    if (nav->kx < 0) nav->kx = n - 1;
    if (nav->kx >= n) nav->kx = 0;
}

int pd_ra_form_key(char* user, int userCap, char* pass, int passCap, struct pd_ra_nav* nav, int dir) {
    if (nav->nav == 0) {
        if (dir == 1) nav->focus = 0;
        else if (dir == 2) {
            if (nav->focus == 0) nav->focus = 1;
            else nav->nav = 1;
        } else if (dir == 5) nav->nav = 1;
        return 0;
    }
    int n = row_n(nav->ky);
    if (dir == 3) {
        nav->kx--;
        clamp_key(nav);
    } else if (dir == 4) {
        nav->kx++;
        clamp_key(nav);
    } else if (dir == 1) {
        if (nav->ky == 0) nav->nav = 0;
        else {
            nav->ky--;
            if (nav->kx >= row_n(nav->ky)) nav->kx = row_n(nav->ky) - 1;
        }
    } else if (dir == 2) {
        if (nav->ky < BTN_ROW) {
            nav->ky++;
            if (nav->kx >= row_n(nav->ky)) nav->kx = row_n(nav->ky) - 1;
        }
    } else if (dir == 5) {
        if (nav->ky == BTN_ROW) {
            if (nav->kx == 0) nav->caps = !nav->caps;
            else if (nav->kx == 1) return 1;
            else return 2;
        } else if (nav->ky >= 0 && nav->ky < KEY_ROWS && nav->kx >= 0 && nav->kx < n) {
            char ch = ROWS[nav->ky][nav->kx];
            if (ch == '<') pd_ra_form_type(user, userCap, pass, passCap, nav, -1);
            else {
                if (nav->caps && ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
                pd_ra_form_type(user, userCap, pass, passCap, nav, ch);
            }
        }
    }
    return 0;
}

static void field_box(struct pd_canvas* c, int x, int y, int w, const char* label, const char* value, int mask,
                      int on) {
    pd_text(c, x, pd_text_y(y, 16), label, on ? PD_VALUE : PD_LABEL, on ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
    int bx = x + 78;
    pd_round_rect(c, bx, y, w - 78, 16, 2, on ? PD_ROW_SELECTED : PD_LIST_FILL);
    char shown[40];
    size_t n = 0;
    if (!value) value = "";
    if (mask) {
        for (; value[n] && n + 1 < sizeof(shown); n++) shown[n] = '*';
    } else {
        for (; value[n] && n + 1 < sizeof(shown); n++) shown[n] = value[n];
    }
    shown[n] = 0;
    pd_text_fit(c, bx + 4, pd_text_y(y, 16), w - 78 - 8, shown, PD_LABEL, PD_LABEL_SHADOW);
}

void pd_ra_form_draw(struct pd_canvas* c, const char* user, const char* pass, const char* note, int signedIn,
                     const struct pd_ra_nav* nav) {
    (void) signedIn;
    int x = 8, w = c->w - 16;
    int f = pd_title_box(c, x, 4, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, x + f + 6, pd_text_y(4, TITLE_H), "RETROACHIEVEMENTS", PD_TITLE_TEXT, PD_TITLE_SHADOW);
    field_box(c, x + 4, 34, w - 8, "USER", user ? user : "", 0, nav->nav == 0 && nav->focus == 0);
    field_box(c, x + 4, 54, w - 8, "PASSWORD", pass ? pass : "", 1, nav->nav == 0 && nav->focus == 1);

    int key = 26, gap = 2;
    int rowW = 9 * (key + gap) - gap;
    int x0 = (c->w - rowW) / 2;
    int y0 = 78;
    for (int r = 0; r < KEY_ROWS; r++) {
        int n = row_n(r);
        int rw = n * (key + gap) - gap;
        int rx = x0 + (rowW - rw) / 2;
        for (int i = 0; i < n; i++) {
            int on = nav->nav && nav->ky == r && nav->kx == i;
            int bx = rx + i * (key + gap), by = y0 + r * 20;
            pd_round_rect(c, bx, by, key, 18, 2, on ? PD_ROW_SELECTED : PD_LIST_FILL);
            char lab[4];
            char ch = ROWS[r][i];
            if (ch == '<') {
                lab[0] = 'D';
                lab[1] = 'E';
                lab[2] = 'L';
                lab[3] = 0;
            } else {
                if (nav->caps && ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
                lab[0] = ch;
                lab[1] = 0;
            }
            int tw = pd_text_width(lab);
            pd_text(c, bx + (key - tw) / 2, pd_text_y(by, 18), lab, PD_LABEL, PD_LABEL_SHADOW);
        }
    }
    static const char* const BTN[] = { "CAPS", "SIGN IN", "SIGN OUT" };
    int bw = 92, by = y0 + KEY_ROWS * 20 + 2;
    int bx0 = (c->w - (3 * bw + 2 * 6)) / 2;
    for (int i = 0; i < 3; i++) {
        int on = nav->nav && nav->ky == BTN_ROW && nav->kx == i;
        int bx = bx0 + i * (bw + 6);
        uint32_t fill = i == 0 && nav->caps ? 0x3C8C40 : on ? PD_ROW_SELECTED : PD_TITLE_FILL;
        pd_round_rect(c, bx, by, bw, 18, 2, fill);
        int tw = pd_text_width(BTN[i]);
        uint32_t ink = (i == 0 && nav->caps) || on ? 0xFFFFFF : PD_TITLE_TEXT;
        pd_text(c, bx + (bw - tw) / 2, pd_text_y(by, 18), BTN[i], ink, PD_TITLE_SHADOW);
    }
    if (note && note[0]) pd_text_fit(c, x + 4, by + 22, w - 8, note, PD_MUTED, PD_MUTED_SHADOW);
}
