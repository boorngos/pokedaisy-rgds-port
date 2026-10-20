#include "pd_menu.h"

#include "pd_style.h"

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

static void quick_box(int n, int* x, int* y, int* w, int* h) {
    *w = 220;
    *h = TITLE_H + n * ROW_H + 18;
    if (*h > 220) *h = 220;
    *x = (320 - *w) / 2;
    *y = (240 - *h) / 2;
}

void pd_quick_draw(struct pd_canvas* c, const char* const* items, int n, int sel) {
    pd_dim(c, 0xC0);
    int x, y, w, h;
    quick_box(n, &x, &y, &w, &h);
    int f = pd_title_box(c, x, y, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, x + f + 6, pd_text_y(y, TITLE_H), "QUICK", PD_TITLE_TEXT, PD_TITLE_SHADOW);
    int ly = y + TITLE_H + 2;
    pd_list_box(c, x, ly, w, h - TITLE_H - 2);
    int ix = x + 12, iy = ly + 8;
    for (int i = 0; i < n; i++) {
        int ry = iy + i * ROW_H;
        if (ry + ROW_H > y + h - 6) break;
        if (i == sel) pd_fill(c, ix - 4, ry, w - 16, ROW_H, PD_ROW_SELECTED);
        pd_text(c, ix + 8, pd_text_y(ry, ROW_H), items[i], i == sel ? PD_VALUE : PD_LABEL,
                i == sel ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
    }
}

int pd_quick_row_at(int x, int y, int n) {
    int bx, by, bw, bh;
    quick_box(n, &bx, &by, &bw, &bh);
    int iy = by + TITLE_H + 2 + 8;
    if (x < bx || x >= bx + bw || y < iy) return -1;
    int r = (y - iy) / ROW_H;
    if (r < 0 || r >= n) return -1;
    return r;
}
