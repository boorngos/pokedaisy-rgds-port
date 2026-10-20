#include "pd_settings.h"

#include <stdio.h>
#include "pd_style.h"
#include <stdlib.h>
#include <string.h>

void pd_settings_defaults(struct pd_settings* s) {
    s->screenMode = PD_SCREEN_SHARP;
    s->ffSpeed = PD_FF_MIN;
    s->guideNotice = 0;
    s->statusBar = 0;
    s->hud = 1;
    s->battleButtons = 1;
    s->shader = PD_SHADER_OFF;
    s->theme = 0;
}

static bool int_in(const char* v, int lo, int hi, int* out) {
    char* end;
    long n = strtol(v, &end, 10);
    if (end == v || n < lo || n > hi) return false;
    *out = (int) n;
    return true;
}

bool pd_settings_load(struct pd_settings* s, const char* path) {
    pd_settings_defaults(s);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* key = line;
        const char* value = eq + 1;
        if (!strcmp(key, "screen")) int_in(value, 0, PD_SCREEN_MODES - 1, &s->screenMode);
        else if (!strcmp(key, "ff")) int_in(value, PD_FF_MIN, PD_FF_MAX, &s->ffSpeed);
        else if (!strcmp(key, "guide")) int_in(value, 0, 0xFFFF, &s->guideNotice);
        else if (!strcmp(key, "statusbar")) int_in(value, 0, 1, &s->statusBar);
        else if (!strcmp(key, "hud")) int_in(value, 0, 1, &s->hud);
        else if (!strcmp(key, "battlebuttons")) int_in(value, 0, 1, &s->battleButtons);
        else if (!strcmp(key, "shader")) int_in(value, 0, PD_SHADERS - 1, &s->shader);
        else if (!strcmp(key, "theme")) int_in(value, 0, PD_THEMES - 1, &s->theme);
    }
    fclose(f);
    return true;
}

bool pd_settings_save(const struct pd_settings* s, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "screen=%d\nff=%d\nguide=%d\nstatusbar=%d\nhud=%d\nbattlebuttons=%d\nshader=%d\ntheme=%d\n",
            s->screenMode, s->ffSpeed, s->guideNotice, s->statusBar, s->hud, s->battleButtons, s->shader,
            s->theme);
    return fclose(f) == 0;
}
