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
    s->raUser[0] = 0;
    s->raToken[0] = 0;
    s->raPass[0] = 0;
    s->raNote[0] = 0;
}

static bool int_in(const char* v, int lo, int hi, int* out) {
    char* end;
    long n = strtol(v, &end, 10);
    if (end == v || n < lo || n > hi) return false;
    *out = (int) n;
    return true;
}

static void copy_str(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    while (src[i] == ' ' || src[i] == '\t') src++;
    for (; src[i] && src[i] != '\n' && src[i] != '\r' && i + 1 < cap; i++) dst[i] = src[i];
    while (i && (dst[i - 1] == ' ' || dst[i - 1] == '\t')) i--;
    dst[i] = 0;
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
        else if (!strcmp(key, "ra_user")) copy_str(s->raUser, sizeof(s->raUser), value);
        else if (!strcmp(key, "ra_token")) copy_str(s->raToken, sizeof(s->raToken), value);
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
    fprintf(f, "ra_user=%s\nra_token=%s\n", s->raUser, s->raToken);
    return fclose(f) == 0;
}
