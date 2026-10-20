// SETTINGS: what the companion knows about the game, then the options - and
// the black-out overlays they open: a pick-list for a multi-choice setting
// (the app's OptionSelector: never a row cycling through more than two
// values) and a YES / NO confirm (OptionConfirm).
#include <stdio.h>
#include <string.h>

#include "pd_style.h"
#include "pd_ui_internal.h"

#define TITLE_H 24
#define LIST_Y (CONTENT_Y + TITLE_H + 4)

static const char* const SCREEN_NAMES[PD_SCREEN_MODES] = { "ORIGINAL", "FIT", "STRETCH" };
static const char* const SHADER_NAMES[PD_SHADERS] = { "OFF", "LCD", "SCANLINES", "CRT", "GBA COLOR" };

const char* pd_screen_mode_name(int mode) {
    return mode >= 0 && mode < PD_SCREEN_MODES ? SCREEN_NAMES[mode] : "";
}

const char* pd_shader_name(int shader) {
    return shader >= 0 && shader < PD_SHADERS ? SHADER_NAMES[shader] : "";
}

static int contains_ci(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    if (!n) return 1;
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && ((p[i] | 32) == (needle[i] | 32))) i++;
        if (i == n) return 1;
    }
    return 0;
}

static int row_matches(const struct pd_ui* ui, const char* label, const char* alias) {
    if (!ui->settingsQuery[0]) return 1;
    return contains_ci(label, ui->settingsQuery) || (alias && contains_ci(alias, ui->settingsQuery));
}

static void setting(struct pd_ui* ui, struct pd_canvas* c, int x, int* y, int w, int viewTop, int viewBot,
                    enum setting_row row, const char* label, const char* value, const char* alias) {
    if (!row_matches(ui, label, alias)) return;
    int ry = *y;
    *y += ROW_H;
    if (ry + ROW_H <= viewTop || ry >= viewBot) return;
    ui_row(c, x, ry, w, label, value, ui->pressedId == HIT_SETTING + (int) row);
    ui_add_scroll_hit(ui, x - 4, ry, w + 8, ROW_H, HIT_SETTING + (int) row);
}

void ui_settings_tab(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g, const struct pd_snapshot* s,
                     const struct pd_host_info* host) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, x, CONTENT_Y, w, TITLE_H, PD_TITLE_FILL);
    int ty = pd_text_y(CONTENT_Y, TITLE_H);
    pd_text(c, x + f + 6, ty, "SETTINGS", PD_TITLE_TEXT, PD_TITLE_SHADOW);
    int right = x + w - f - 6;
    // The TRAINER CARD opens from here (the list below is full): a grey chip at
    // the title window's right, white while pressed, like the tab bar's.
    if (g->cfg && g->cfg->cardStyle && g->rom) {
        const char* label = "TRAINER CARD";
        int bw = pd_text_width(label) + 12, bh = TITLE_H - 6;
        int bx = x + w - f - 2 - bw, by = CONTENT_Y + 3;
        pd_title_box(c, bx, by, bw, bh, ui->pressedId == HIT_OPEN_CARD ? PD_TITLE_FILL : PD_LIST_FILL);
        pd_text(c, bx + 6, pd_text_y(by, bh), label, PD_TITLE_TEXT, PD_TITLE_SHADOW);
        ui_add_hit(ui, bx, by, bw, bh, HIT_OPEN_CARD);
        right = bx - 8;
    }
    if (g->title) {
        int tx = x + f + 6 + pd_text_width("SETTINGS") + 12;
        int tw = pd_text_width(g->title);
        if (tw > right - tx) tw = right - tx;
        pd_text_fit(c, right - tw, ty, tw, g->title, PD_TITLE_TEXT, PD_TITLE_SHADOW);
    }

    int listH = c->h - LIST_Y - MARGIN;
    int lf = pd_list_box(c, x, LIST_Y, w, listH);
    int ix = x + lf + 4, iw = w - 2 * (lf + 4);
    int viewTop = LIST_Y + lf;
    int viewBot = LIST_Y + listH - lf;
    ui_add_scroll_hit(ui, x, viewTop, w, viewBot - viewTop, HIT_SETTINGS_LIST);
    pd_clip(c, ix - 4, viewTop, iw + 8, viewBot - viewTop);
    int y = viewTop - ui->settingsScroll;
    int contentTop = y;
    char buf[48];
    if (ui->settingsQuery[0]) {
        snprintf(buf, sizeof(buf), "FIND  %s", ui->settingsQuery);
        pd_text_fit(c, ix, pd_text_y(y, ROW_H), iw, buf, PD_LABEL, PD_LABEL_SHADOW);
        y += ROW_H;
    }
    if (s->valid && row_matches(ui, "PLACE", "location")) {
        const char* place = pd_mapsec_name(g, s->mapsec);
        if (y + ROW_H > viewTop && y < viewBot) ui_row(c, ix, y, iw, "PLACE", *place ? place : "-", false);
        y += ROW_H;
    }
    if (s->valid && row_matches(ui, "MONEY", NULL)) {
        ui_format_money(s->money, buf, sizeof(buf));
        if (y + ROW_H > viewTop && y < viewBot) ui_row(c, ix, y, iw, "MONEY", buf, false);
        y += ROW_H;
    }
    for (int i = 0; host && i < host->count && i < PD_UI_HOST_LINES; i++) {
        if (!row_matches(ui, host->labels[i], NULL)) continue;
        if (y + ROW_H > viewTop && y < viewBot) ui_row(c, ix, y, iw, host->labels[i], host->values[i], false);
        y += ROW_H;
    }
    if (y + 4 > viewTop && y < viewBot) pd_fill(c, ix, y + 1, iw, 1, PD_DIVIDER);
    y += 4;

    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_SCREEN, "ASPECT",
            pd_screen_mode_name(ui->settings->screenMode), "screen zoom widescreen stretch original fit");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_STATUS, "STATUS BAR",
            ui->settings->statusBar ? "ON" : "OFF", "location money clock battery");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_HUD, "HUD", ui->settings->hud ? "ON" : "OFF",
            "party levels hp rail");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_BATTLE_BTNS, "BATTLE BUTTONS",
            ui->settings->battleButtons ? "ON" : "OFF", "fight bag pokemon run moves");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_SHADER, "SHADER", pd_shader_name(ui->settings->shader),
            "lcd scanlines crt filter color gba");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_THEME, "THEME", pd_theme_name(ui->settings->theme),
            "firered leafgreen emerald ruby sapphire colour");
    snprintf(buf, sizeof(buf), "%dx", ui->settings->ffSpeed);
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_FF, "FAST-FORWARD", buf, "turbo speed");
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_SAVE_STATE, "SAVE STATE", NULL, "savestate");
    if (host && host->hasState) {
        setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_LOAD_STATE, "LOAD STATE", host->stateWhen, "savestate");
    } else if (row_matches(ui, "LOAD STATE", "savestate")) {
        if (y + ROW_H > viewTop && y < viewBot) {
            pd_text(c, ix, pd_text_y(y, ROW_H), "LOAD STATE", PD_MUTED, PD_MUTED_SHADOW);
            pd_text(c, ix + iw * 42 / 100, pd_text_y(y, ROW_H), "none yet", PD_MUTED, PD_MUTED_SHADOW);
        }
        y += ROW_H;
    }
    setting(ui, c, ix, &y, iw, viewTop, viewBot, SETTING_LEAVE, "LEAVE GAME", NULL, "quit exit");
    pd_unclip(c);
    int contentH = y - contentTop;
    int viewH = viewBot - viewTop;
    if (ui->settingsScroll > contentH - viewH) ui->settingsScroll = contentH > viewH ? contentH - viewH : 0;
    if (ui->settingsScroll < 0) ui->settingsScroll = 0;
    if (contentH > viewH) ui_scroll_bar(c, x + w - lf - 4, viewTop, viewH, ui->settingsScroll, contentH, PD_MUTED);
}

// --- overlays ---

static void title_window(struct pd_canvas* c, int y, const char* title) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int f = pd_title_box(c, x, y, w, TITLE_H, PD_TITLE_FILL);
    pd_text(c, x + f + 6, pd_text_y(y, TITLE_H), title, PD_TITLE_TEXT, PD_TITLE_SHADOW);
}

static void pick_list(struct pd_ui* ui, struct pd_canvas* c, const char* title, const char* const* options, int n,
                      int current) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int y = CONTENT_Y + 8;
    title_window(c, y, title);
    y += TITLE_H + 4;
    int lf = pd_list_box(c, x, y, w, n * ROW_H + 2 * 7 + 4);
    int ix = x + lf + 4, iw = w - 2 * (lf + 4);
    int ry = y + lf + 2;
    for (int i = 0; i < n; i++) {
        bool on = i == current || ui->pressedId == HIT_OPTION + i;
        if (on) pd_fill(c, ix - 4, ry, iw + 8, ROW_H, PD_ROW_SELECTED);
        pd_text(c, ix + 10, pd_text_y(ry, ROW_H), options[i], on ? PD_VALUE : PD_LABEL,
                on ? PD_VALUE_SHADOW : PD_LABEL_SHADOW);
        if (i == current) pd_triangle(c, ix, ry + 4, 5, PD_RIGHT, PD_VALUE);
        ui_add_hit(ui, ix - 4, ry, iw + 8, ROW_H, HIT_OPTION + i);
        ry += ROW_H;
    }
}

static void confirm(struct pd_ui* ui, struct pd_canvas* c, const char* title, const char* text, const char* yes) {
    int x = MARGIN, w = c->w - 2 * MARGIN;
    int y = CONTENT_Y + 8;
    title_window(c, y, title);
    y += TITLE_H + 4;
    int h = 3 * 16 + 2 * 7 + 6;
    int lf = pd_list_box(c, x, y, w, h);
    pd_text_wrap(c, x + lf + 6, y + lf + 2, w - 2 * (lf + 6), 16, 3, text, PD_LABEL, PD_LABEL_SHADOW);
    y += h + 6;
    int bw = 96;
    ui_button(ui, c, c->w / 2 - bw - 6, y, bw, 24, yes, HIT_YES);
    ui_button(ui, c, c->w / 2 + 6, y, bw, 24, "CANCEL", HIT_NO);
}

void ui_overlay(struct pd_ui* ui, struct pd_canvas* c) {
    if (ui->overlay == PD_OVERLAY_NONE) return;
    pd_dim(c, 0xE6);
    // Swallows taps beside the windows (they close the overlay).
    ui_add_hit(ui, 0, 0, c->w, c->h, HIT_SCRIM);
    static const char* const FF[PD_FF_MAX - PD_FF_MIN + 1] = { "2x", "3x", "4x" };
    switch (ui->overlay) {
    case PD_OVERLAY_PICK_SCREEN:
        pick_list(ui, c, "ASPECT", SCREEN_NAMES, PD_SCREEN_MODES, ui->settings->screenMode);
        break;
    case PD_OVERLAY_PICK_SHADER:
        pick_list(ui, c, "SHADER", SHADER_NAMES, PD_SHADERS, ui->settings->shader);
        break;
    case PD_OVERLAY_PICK_THEME: {
        const char* names[PD_THEMES];
        for (int i = 0; i < PD_THEMES; i++) names[i] = pd_theme_name(i);
        pick_list(ui, c, "THEME", (const char* const*) names, PD_THEMES, ui->settings->theme);
        break;
    }
    case PD_OVERLAY_PICK_FF:
        pick_list(ui, c, "FAST-FORWARD", FF, PD_FF_MAX - PD_FF_MIN + 1, ui->settings->ffSpeed - PD_FF_MIN);
        break;
    case PD_OVERLAY_CONFIRM_SAVE:
        confirm(ui, c, "SAVE STATE",
                "Save a state of the game as it is now? It replaces the last one. The save file isn't touched.",
                "SAVE");
        break;
    case PD_OVERLAY_CONFIRM_LOAD:
        confirm(ui, c, "LOAD STATE",
                "Go back to the saved state? Play since then is lost; the save file isn't touched.", "LOAD");
        break;
    case PD_OVERLAY_CONFIRM_LEAVE:
        confirm(ui, c, "LEAVE GAME",
                "Back to the game list? What the game saved stays saved; anything since is lost.", "LEAVE");
        break;
    default:
        break;
    }
}
