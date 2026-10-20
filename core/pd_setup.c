#include "pd_setup.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pd_style.h"
#include "pd_ui_internal.h"

#define TITLE_H 24
#define SEARCH_H 18

enum {
    SET_ASPECT,
    SET_STATUS,
    SET_HUD,
    SET_BATTLE,
    SET_SHADER,
    SET_THEME,
    SET_FF,
    SET_COUNT,
};

static const char* const LABELS[SET_COUNT] = {
    "ASPECT", "STATUS BAR", "HUD", "BATTLE BUTTONS", "SHADER", "THEME", "FAST-FORWARD",
};

// Words the search matches besides the label itself.
static const char* const ALIASES[SET_COUNT] = {
    "screen zoom widescreen stretch original fit shape",
    "location money clock battery",
    "party rail levels hp",
    "fight pokemon run battle",
    "lcd scanlines crt filter color gba",
    "firered leafgreen emerald ruby sapphire colour color",
    "turbo speed fast forward",
};

static int contains(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    if (!n) return 1;
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char) p[i]) == tolower((unsigned char) needle[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int visible(const struct pd_setup* s, int index) {
    if (!s->query[0]) return 1;
    return contains(LABELS[index], s->query) || contains(ALIASES[index], s->query);
}

static int filtered(const struct pd_setup* s, int* out) {
    int n = 0;
    for (int i = 0; i < SET_COUNT; i++) {
        if (visible(s, i)) out[n++] = i;
    }
    return n;
}

static const char* value_of(const struct pd_settings* set, int id, char* buf, size_t len) {
    switch (id) {
    case SET_ASPECT: return pd_screen_mode_name(set->screenMode);
    case SET_STATUS: return set->statusBar ? "ON" : "OFF";
    case SET_HUD: return set->hud ? "ON" : "OFF";
    case SET_BATTLE: return set->battleButtons ? "ON" : "OFF";
    case SET_SHADER: return pd_shader_name(set->shader);
    case SET_THEME: return pd_theme_name(set->theme);
    case SET_FF: snprintf(buf, len, "%dx", set->ffSpeed); return buf;
    default: return "";
    }
}

void pd_setup_init(struct pd_setup* s) {
    memset(s, 0, sizeof(*s));
    s->pressed = -1;
}

static int list_top(void) {
    return MARGIN + TITLE_H + 4 + SEARCH_H + 4;
}

void pd_setup_draw(struct pd_setup* s, struct pd_canvas* c, const struct pd_settings* set) {
    pd_backdrop(c);
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, x, MARGIN, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, x + f + 6, pd_text_y(MARGIN, TITLE_H), "SETTINGS", PD_TITLE_TEXT, PD_TITLE_SHADOW);
    const char* hint = "B back";
    pd_text(c, x + w - f - 6 - pd_text_width(hint), pd_text_y(MARGIN, TITLE_H), hint, PD_MUTED, PD_MUTED_SHADOW);

    int sy = MARGIN + TITLE_H + 4;
    pd_list_box(c, x, sy, w, SEARCH_H + 8);
    char q[40];
    if (s->query[0]) snprintf(q, sizeof(q), "FIND  %s", s->query);
    else snprintf(q, sizeof(q), "FIND  type to filter");
    pd_text_fit(c, x + 12, pd_text_y(sy + 4, SEARCH_H), w - 24, q, s->query[0] ? PD_LABEL : PD_MUTED,
                s->query[0] ? PD_LABEL_SHADOW : PD_MUTED_SHADOW);

    int ids[SET_COUNT];
    int n = filtered(s, ids);
    if (s->selected >= n) s->selected = n ? n - 1 : 0;
    int y = list_top();
    int lh = c->h - y - MARGIN;
    pd_list_box(c, x, y, w, lh);
    int ix = x + 12, iy = y + 10, iw = w - 24;
    if (!n) {
        pd_text(c, ix, iy, "Nothing matches.", PD_MUTED, PD_MUTED_SHADOW);
        return;
    }
    char buf[32];
    for (int i = 0; i < n; i++) {
        int rowY = iy + i * ROW_H;
        if (rowY + ROW_H > y + lh - 6) break;
        bool on = i == s->selected || i == s->pressed;
        if (on) pd_fill(c, ix - 4, rowY, iw + 8, ROW_H, PD_ROW_SELECTED);
        const char* value = value_of(set, ids[i], buf, sizeof(buf));
        ui_row(c, ix, rowY, iw, LABELS[ids[i]], value, on);
    }
}

void pd_setup_move(struct pd_setup* s, const struct pd_settings* set, int delta) {
    int ids[SET_COUNT];
    int n = filtered(s, ids);
    (void) set;
    if (!n) return;
    s->selected = (s->selected + delta + n) % n;
}

void pd_setup_adjust(struct pd_setup* s, struct pd_settings* set, int dir) {
    int ids[SET_COUNT];
    int n = filtered(s, ids);
    if (!n || s->selected < 0 || s->selected >= n || !dir) return;
    int id = ids[s->selected];
    int d = dir < 0 ? -1 : 1;
    switch (id) {
    case SET_ASPECT:
        set->screenMode = (set->screenMode + PD_SCREEN_MODES + d) % PD_SCREEN_MODES;
        break;
    case SET_STATUS: set->statusBar = !set->statusBar; break;
    case SET_HUD: set->hud = !set->hud; break;
    case SET_BATTLE: set->battleButtons = !set->battleButtons; break;
    case SET_SHADER:
        set->shader = (set->shader + PD_SHADERS + d) % PD_SHADERS;
        break;
    case SET_THEME:
        set->theme = (set->theme + PD_THEMES + d) % PD_THEMES;
        pd_style_set_theme(set->theme);
        break;
    case SET_FF: {
        int span = PD_FF_MAX - PD_FF_MIN + 1;
        int i = set->ffSpeed - PD_FF_MIN;
        set->ffSpeed = PD_FF_MIN + (i + span + d) % span;
        break;
    }
    default: break;
    }
}

void pd_setup_type(struct pd_setup* s, int ch) {
    size_t n = strlen(s->query);
    if (ch == -2) s->query[0] = 0;
    else if (ch == -1) {
        if (n) s->query[n - 1] = 0;
    } else if (ch >= 32 && ch < 127 && n + 1 < sizeof(s->query)) {
        s->query[n] = (char) ch;
        s->query[n + 1] = 0;
    }
    s->selected = 0;
}

int pd_setup_touch(struct pd_setup* s, const struct pd_settings* set, int x, int y, bool down) {
    int ids[SET_COUNT];
    int n = filtered(s, ids);
    (void) set;
    int top = list_top() + 10;
    int row = -1;
    if (y >= top && x >= MARGIN && x < 320 - MARGIN) {
        int r = (y - top) / ROW_H;
        if (r >= 0 && r < n) row = r;
    }
    if (down) {
        s->touching = true;
        s->pressed = row;
        return 0;
    }
    if (!s->touching) return 0;
    s->touching = false;
    int hit = s->pressed;
    s->pressed = -1;
    if (hit >= 0 && hit == row) {
        s->selected = hit;
        return 1;
    }
    return 0;
}
