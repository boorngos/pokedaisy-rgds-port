// The FireRed OPTION screen's look, shared by the companion and the ROM list:
// OptionColors (app/.../companion/ui/GbaMenu.kt) and the window boxes built
// from them.
#ifndef PD_STYLE_H
#define PD_STYLE_H

#include "pd_canvas.h"
#include "pd_font.h"

#define PD_THEMES 9

struct pd_palette {
    uint32_t titleFill, titleText, titleShadow;
    uint32_t listFill, rowSelected;
    uint32_t label, labelShadow, value, valueShadow;
    uint32_t muted, mutedShadow, divider;
    uint32_t backdropA, backdropB, backdropBand;
    const char* name;
};

const struct pd_palette* pd_palette(void);
void pd_style_set_theme(int theme);
const char* pd_theme_name(int theme);

#define PD_TITLE_FILL (pd_palette()->titleFill)
#define PD_TITLE_TEXT (pd_palette()->titleText)
#define PD_TITLE_SHADOW (pd_palette()->titleShadow)
#define PD_LIST_FILL (pd_palette()->listFill)
#define PD_ROW_SELECTED (pd_palette()->rowSelected)
#define PD_LABEL (pd_palette()->label)
#define PD_LABEL_SHADOW (pd_palette()->labelShadow)
#define PD_VALUE (pd_palette()->value)
#define PD_VALUE_SHADOW (pd_palette()->valueShadow)
#define PD_MUTED (pd_palette()->muted)
#define PD_MUTED_SHADOW (pd_palette()->mutedShadow)
#define PD_DIVIDER (pd_palette()->divider)
// Text straight on the backdrop: white with the hint bar's grey shadow.
#define PD_ON_BACKDROP 0xFFFFFF
#define PD_ON_BACKDROP_SHADOW 0x404850
// FireRed's party backdrop stripes (FIRERED_BACKDROP in theme/Theme.kt) -
// the plain stripes the app shows until a FireRed ROM supplied the art.
#define PD_BACKDROP_A (pd_palette()->backdropA)
#define PD_BACKDROP_B (pd_palette()->backdropB)
#define PD_BACKDROP_BAND (pd_palette()->backdropBand)

// The white title window and the grey list window; each returns its frame's width.
int pd_title_box(struct pd_canvas* c, int x, int y, int w, int h, uint32_t fill);
int pd_list_box(struct pd_canvas* c, int x, int y, int w, int h);
void pd_backdrop(struct pd_canvas* c);
// Text centred in a row of height h at y (the cell is 16 px, caps at rows 4-12).
static inline int pd_text_y(int y, int h) {
    return y + (h - PD_FONT_HEIGHT) / 2 + 1;
}

#endif
