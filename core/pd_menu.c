#include "pd_menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pd_dex.h"
#include "pd_game.h"
#include "pd_guide.h"
#include "pd_snapshot.h"
#include "pd_style.h"
#include "pd_tables.h"

#define MARGIN 4
#define TITLE_H 24
#define ROW_H 18
#define LIST_Y (MARGIN + TITLE_H + 4)

// pd_list_box's frame: 1 + 1 + 2 + 1 + 2 px (the list window's layers).
#define LIST_FRAME 7

static int visible_rows(const struct pd_canvas* c) {
    return (c->h - LIST_Y - MARGIN - 2 * LIST_FRAME - 4) / ROW_H;
}

void pd_menu_init(struct pd_menu* m, const char* title, const char* const* items, int count) {
    m->title = title;
    m->items = items;
    m->count = count;
    m->selected = 0;
    m->scroll = 0;
    m->touching = false;
    m->pressed = -1;
}

static void keep_visible(struct pd_menu* m, int rows) {
    if (m->selected < m->scroll) m->scroll = m->selected;
    if (m->selected >= m->scroll + rows) m->scroll = m->selected - rows + 1;
    if (m->scroll < 0) m->scroll = 0;
}

// The same 11x11 gear the companion's tab bar uses.
static const uint16_t GEAR[11] = {
    0x070, 0x376, 0x3FE, 0x1DC, 0x78F, 0x707, 0x78F, 0x1DC, 0x3FE, 0x376, 0x070,
};

static int gear_x(const struct pd_canvas* c) {
    return c->w - MARGIN - 86;
}

void pd_menu_draw(struct pd_menu* m, struct pd_canvas* c) {
    pd_backdrop(c);
    int w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, MARGIN, MARGIN, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, MARGIN + f + 6, pd_text_y(MARGIN, TITLE_H), m->title, PD_TITLE_TEXT, PD_TITLE_SHADOW);
    int gx = gear_x(c);
    bool gearOn = m->pressed == -2;
    pd_bitmap(c, gx, MARGIN + (TITLE_H - 11) / 2, GEAR, 11, gearOn ? PD_VALUE : PD_TITLE_TEXT);
    pd_text(c, gx + 14, pd_text_y(MARGIN, TITLE_H), "SETTINGS", gearOn ? PD_VALUE : PD_TITLE_TEXT,
            gearOn ? PD_VALUE_SHADOW : PD_TITLE_SHADOW);

    pd_list_box(c, MARGIN, LIST_Y, w, c->h - LIST_Y - MARGIN);
    int rows = visible_rows(c);
    keep_visible(m, rows);
    int x = MARGIN + LIST_FRAME, iw = w - 2 * LIST_FRAME;
    int y = LIST_Y + LIST_FRAME + 2;
    if (m->count == 0) {
        pd_text(c, x + 6, y, "No games found.", PD_LABEL, PD_LABEL_SHADOW);
        pd_text(c, x + 6, y + ROW_H, "Put .gba or .zip in", PD_MUTED, PD_MUTED_SHADOW);
        pd_text(c, x + 6, y + 2 * ROW_H, "sdmc:/pokedaisy/roms/", PD_VALUE, PD_VALUE_SHADOW);
        return;
    }
    for (int r = 0; r < rows && m->scroll + r < m->count; r++) {
        int i = m->scroll + r;
        bool cursor = i == m->selected || i == m->pressed;
        if (cursor) pd_fill(c, x, y + r * ROW_H, iw, ROW_H, PD_ROW_SELECTED);
        pd_text_fit(c, x + 6, pd_text_y(y + r * ROW_H, ROW_H), iw - 12, m->items[i],
                    cursor ? PD_VALUE : PD_LABEL, cursor ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
    }
    // A scroll bar when the list runs past the window.
    if (m->count > rows) {
        int track = rows * ROW_H;
        int th = track * rows / m->count;
        int ty = track * m->scroll / m->count;
        pd_fill(c, x + iw - 3, y + ty, 2, th < 6 ? 6 : th, PD_MUTED);
    }
}

void pd_menu_move(struct pd_menu* m, int delta) {
    if (!m->count) return;
    m->selected = (m->selected + delta + m->count) % m->count;
}

static int row_at(const struct pd_menu* m, int x, int y, int rows, int w) {
    int top = LIST_Y + LIST_FRAME + 2;
    if (x < MARGIN + LIST_FRAME || x >= MARGIN + w - LIST_FRAME || y < top) return -1;
    int r = (y - top) / ROW_H;
    if (r >= rows || m->scroll + r >= m->count) return -1;
    return m->scroll + r;
}

static int on_gear(int x, int y) {
    return y >= MARGIN && y < MARGIN + TITLE_H && x >= gear_x(&(struct pd_canvas) { .w = 320 });
}

int pd_menu_touch(struct pd_menu* m, int x, int y, bool down) {
    // The bottom screen is the canvas these rows were drawn on: 320x240.
    int rows = (240 - LIST_Y - MARGIN - 2 * LIST_FRAME - 4) / ROW_H;
    int row = on_gear(x, y) ? -2 : row_at(m, x, y, rows, 320 - 2 * MARGIN);
    if (down) {
        if (!m->touching) {
            m->touching = true;
            m->pressed = row;
        } else if (row != m->pressed) {
            m->pressed = -1;
        }
        return -1;
    }
    if (!m->touching) return -1;
    m->touching = false;
    int picked = m->pressed;
    m->pressed = -1;
    if (picked >= 0) m->selected = picked;
    return picked;
}

static void center_text(struct pd_canvas* c, int y, const char* text, uint32_t color) {
    if (!text || !*text) return;
    int w = pd_text_width(text);
    pd_text(c, (c->w - w) / 2, y, text, color, PD_NO_SHADOW);
}

// The launcher icon from lidor30/pokedaisy (3ds/ctr/icon.png): the daisy on
// the blue field SimpleOS shows for the port. 0 = clear. RGB565.
#include "pd_daisy.inc"

static uint32_t rgb565(uint16_t v) {
    uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

static void daisy(struct pd_canvas* c, int cx, int cy) {
    int box = 96;
    int x = cx - box / 2, y = cy - box / 2;
    pd_round_rect(c, x - 2, y - 2, box + 4, box + 4, 16, 0x1A4A44);
    pd_round_rect(c, x, y, box, box, 14, 0x3396FF);
    int scale = 2;
    int dx = cx - DAISY_W * scale / 2;
    int dy = cy - DAISY_H * scale / 2;
    for (int py = 0; py < DAISY_H; py++) {
        for (int px = 0; px < DAISY_W; px++) {
            uint16_t v = DAISY[py * DAISY_W + px];
            if (!v) continue;
            pd_fill(c, dx + px * scale, dy + py * scale, scale, scale, rgb565(v));
        }
    }
}

void pd_splash_draw(struct pd_canvas* c, const char* line1, const char* line2, const char* line3) {
    pd_fill(c, 0, 0, c->w, c->h, 0x24282C);
    int x = 14, y = 18, w = c->w - 28, h = c->h - 36;
    pd_round_rect(c, x - 3, y - 3, w + 6, h + 6, 8, 0x1A4A44);
    pd_round_rect(c, x, y, w, h, 6, 0xC5E4DC);
    pd_round_rect(c, x + 4, y + 4, w - 8, h - 8, 4, 0xD7F0EA);
    daisy(c, c->w / 2, y + 58);
    if (!line1) line1 = "Your games are on";
    if (!line2) line2 = "the bottom screen.";
    center_text(c, y + 108, line1, 0x1A4A44);
    center_text(c, y + 128, line2, 0x1A4A44);
    center_text(c, y + 152, line3, 0x3E6E68);
}

#define TILE_W 86
#define TILE_H 26

static void tile_at(int i, int n, int* x, int* y) {
    float t = (float) i / (float) (n > 0 ? n : 1) * 6.2831853f;
    float px = 160.f + 108.f * sinf(t);
    float py = 118.f - 80.f * cosf(t);
    *x = (int) (px - TILE_W / 2.f);
    *y = (int) (py - TILE_H / 2.f);
}

void pd_quick_draw(struct pd_canvas* c, const char* const* items, int n, int sel, const char* note,
                   unsigned blocked) {
    pd_dim(c, 0x90);
    for (int i = 0; i < n; i++) {
        int x, y;
        tile_at(i, n, &x, &y);
        int wild = items[i] && !strcmp(items[i], "WILD");
        int on = i == sel;
        uint32_t fill = wild ? (on ? 0x3C8C40 : 0x246B2C) : (on ? 0x3A4652 : 0x242A30);
        pd_round_rect(c, x, y, TILE_W, TILE_H, 4, fill);
        if (on) {
            pd_round_rect(c, x, y, TILE_W, 1, 0, 0xF4F7FA);
            pd_round_rect(c, x, y + TILE_H - 1, TILE_W, 1, 0, 0xF4F7FA);
            pd_round_rect(c, x, y, 1, TILE_H, 0, 0xF4F7FA);
            pd_round_rect(c, x + TILE_W - 1, y, 1, TILE_H, 0, 0xF4F7FA);
        }
        uint32_t ink = (blocked & (1u << i)) ? 0x9AA3AB : 0xFFFFFF;
        uint32_t sh = (blocked & (1u << i)) ? 0x1A1E22 : 0x101418;
        int tw = pd_text_width(items[i]);
        if (tw > TILE_W - 8) tw = TILE_W - 8;
        pd_text_fit(c, x + (TILE_W - tw) / 2, pd_text_y(y, TILE_H), TILE_W - 8, items[i], ink, sh);
    }
    if (note && note[0]) {
        int tw = pd_text_width(note);
        if (tw > 168) tw = 168;
        int w = tw + 16, h = 18;
        int x = 160 - w / 2, y = 118 - h / 2;
        pd_round_rect(c, x, y, w, h, 3, 0x1A2026);
        pd_text_fit(c, x + 8, pd_text_y(y, h), tw, note, 0xFFFFFF, 0x101418);
    }
}

int pd_quick_hit(int x, int y, int n) {
    for (int i = 0; i < n; i++) {
        int tx, ty;
        tile_at(i, n, &tx, &ty);
        if (x >= tx && x < tx + TILE_W && y >= ty && y < ty + TILE_H) return i;
    }
    return -1;
}

int pd_quick_aim(int axisX, int axisY, int n) {
    const int dead = 12000;
    if (n <= 0) return -1;
    if (axisX > -dead && axisX < dead && axisY > -dead && axisY < dead) return -1;
    float ang = atan2f((float) axisX, (float) -axisY);
    if (ang < 0) ang += 6.2831853f;
    int i = (int) (ang / 6.2831853f * (float) n + 0.5f);
    if (i >= n) i = 0;
    return i;
}

// --- WILD: the route's encounters ---

#define WILD_LINES 80

struct wild_line {
    int header;
    char text[28];
    char detail[20];
    int caught; // 1 owned, 0 seen and not owned, -1 hidden or a header
};

static struct wild_line wildLines[WILD_LINES];
static int wildCount;

static int build_wild(const struct pd_game* g, const struct pd_snapshot* s) {
    wildCount = 0;
    if (!g || !s) return 0;
    struct pd_wild_set set;
    if (!pd_wild_here(g, s, &set)) return 0;
    for (int m = 0; m < PD_WILD_METHODS; m++) {
        if (!set.count[m] || wildCount >= WILD_LINES) continue;
        wildLines[wildCount++] = (struct wild_line) { 1, "", "", -1 };
        snprintf(wildLines[wildCount - 1].text, sizeof(wildLines[0].text), "%s", pd_wild_method_name(m));
        for (int i = 0; i < set.count[m] && wildCount < WILD_LINES; i++) {
            const struct pd_wild* w = &set.mons[m][i];
            struct wild_line* e = &wildLines[wildCount++];
            e->header = 0;
            char name[28];
            pd_dex_name(g, w->species, name, sizeof(name));
            int nat = pd_dex_national(g, w->species);
            e->caught = nat && pd_dex_caught(s, nat) ? 1 : 0;
            snprintf(e->text, sizeof(e->text), "%s", name[0] ? name : "???");
            if (w->minLv == w->maxLv) snprintf(e->detail, sizeof(e->detail), "LV%d  %d%%", w->minLv, w->pct);
            else snprintf(e->detail, sizeof(e->detail), "LV%d-%d  %d%%", w->minLv, w->maxLv, w->pct);
        }
    }
    return wildCount;
}

int pd_wild_rows(const struct pd_game* g, const struct pd_snapshot* s) {
    return build_wild(g, s);
}

int pd_wild_move(const struct pd_game* g, const struct pd_snapshot* s, int sel, int dir) {
    int n = build_wild(g, s);
    if (n <= 0) return 0;
    if (dir == 0) {
        if (sel < 0 || sel >= n || wildLines[sel].header) {
            for (int i = 0; i < n; i++)
                if (!wildLines[i].header) return i;
        }
        return sel;
    }
    int i = sel;
    for (int k = 0; k < n; k++) {
        i += dir > 0 ? 1 : -1;
        if (i < 0) i = n - 1;
        if (i >= n) i = 0;
        if (!wildLines[i].header) return i;
    }
    return sel < 0 ? 0 : sel;
}

void pd_wild_draw(struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s, int sel) {
    int n = build_wild(g, s);
    pd_dim(c, 0xC0);
    int x = MARGIN, y = 8, w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, x, y, w, TITLE_H, PD_TITLE_FILL);
    const char* place = s && s->valid ? pd_mapsec_name(g, s->mapsec) : "";
    pd_text(c, x + f + 6, pd_text_y(y, TITLE_H), place && *place ? place : "WILD", PD_TITLE_TEXT, PD_TITLE_SHADOW);
    int owned = 0, mons = 0;
    for (int i = 0; i < n; i++) {
        if (wildLines[i].header) continue;
        mons++;
        if (wildLines[i].caught == 1) owned++;
    }
    char count[24];
    snprintf(count, sizeof(count), "OWN %d OF %d", owned, mons);
    pd_text_right(c, x + w - f - 6, pd_text_y(y, TITLE_H), count, PD_TITLE_TEXT, PD_TITLE_SHADOW);

    int ly = y + TITLE_H + 4;
    int lh = c->h - ly - 8;
    pd_list_box(c, x, ly, w, lh);
    if (!n) {
        pd_text(c, x + 14, ly + 14, "No wild POKéMON on this map.", PD_MUTED, PD_MUTED_SHADOW);
        return;
    }
    int vis = (lh - 16) / ROW_H;
    if (vis < 1) vis = 1;
    int first = 0;
    if (sel >= first + vis) first = sel - vis + 1;
    if (sel < first) first = sel;
    if (first < 0) first = 0;
    int iy = ly + 8;
    for (int i = first; i < n && i < first + vis; i++) {
        int ry = iy + (i - first) * ROW_H;
        if (!wildLines[i].header && i == sel) pd_fill(c, x + 8, ry, w - 16, ROW_H, PD_ROW_SELECTED);
        if (wildLines[i].header) {
            pd_text(c, x + 12, pd_text_y(ry, ROW_H), wildLines[i].text, PD_MUTED, PD_MUTED_SHADOW);
        } else {
            if (wildLines[i].caught >= 0) {
                pd_round_rect(c, x + 12, ry + 4, 10, 10, 5, wildLines[i].caught ? 0xC03030 : 0xA8A8A8);
                pd_fill(c, x + 14, ry + 8, 6, 1, 0xF0F0F0);
            }
            uint32_t ink = i == sel ? PD_VALUE : PD_LABEL;
            uint32_t sh = i == sel ? PD_VALUE_SHADOW : PD_LABEL_SHADOW;
            pd_text_fit(c, x + 26, pd_text_y(ry, ROW_H), w / 2, wildLines[i].text, ink, sh);
            pd_text_right(c, x + w - 14, pd_text_y(ry, ROW_H), wildLines[i].detail, ink, sh);
        }
    }
}
