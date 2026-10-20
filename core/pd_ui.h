// The companion on the 3DS's 320x240 bottom screen: PARTY (with a summary on
// tap), BAG, MAP, GUIDE, POKéDEX, BATTLE (cards + your moves' verdicts) and
// SETTINGS, in the app's
// FireRed OPTION-screen look (app/.../companion/ui/GbaMenu.kt). Immediate
// mode: every pd_ui_draw records where its tappable parts went, and
// pd_ui_touch looks a tap up there - no layout pass to keep in sync.
//
// The host owns the emulator: it shows what pd_ui_draw drew, feeds the stylus
// and buttons in, and carries out what pd_ui_take_action hands back (fast-
// forward, save states, leaving the game, saving changed settings).
#ifndef PD_UI_H
#define PD_UI_H

#include <stdbool.h>

#include "pd_canvas.h"
#include "pd_game.h"
#include "pd_snapshot.h"

#define PD_UI_WIDTH 320
#define PD_UI_HEIGHT 240

enum pd_tab {
    PD_TAB_PARTY,
    PD_TAB_BAG,
    PD_TAB_MAP,
    PD_TAB_GUIDE,
    PD_TAB_DEX,
    PD_TAB_BATTLE,
    PD_TAB_SETTINGS,
    PD_TAB_CARD, // the TRAINER CARD: no chip, it opens from SETTINGS
    PD_TAB_COUNT,
};

// How the game fills the top screen.
enum pd_screen_mode {
    PD_SCREEN_PIXEL,   // integer scale, the game's own shape, square pixels
    PD_SCREEN_SHARP,   // the game's shape, as large as the panel allows
    PD_SCREEN_STRETCH, // fill the panel
    PD_SCREEN_MODES,
};

// Post-process on the game picture. Optional; off by default.
enum pd_shader {
    PD_SHADER_OFF,
    PD_SHADER_LCD,
    PD_SHADER_SCANLINES,
    PD_SHADER_CRT,
    PD_SHADER_GBA,
    PD_SHADERS,
};

#define PD_FF_MIN 2
#define PD_FF_MAX 4

// What the player set; the host loads and saves it (pd_settings.h).
struct pd_settings {
    int screenMode; // enum pd_screen_mode
    int ffSpeed;    // PD_FF_MIN..PD_FF_MAX
    int guideNotice; // a bit per game kind whose GUIDE notice was accepted
    int statusBar;     // game, place, money, clock and battery above the game
    int hud;           // party levels and HP in the margin beside the game
    int battleButtons; // the battle screen's FIGHT / BAG / POKéMON / RUN
    int shader;        // enum pd_shader
    int theme;         // library and settings colours
};

enum pd_action {
    PD_ACTION_NONE,
    PD_ACTION_TOGGLE_FF,
    PD_ACTION_SAVE_STATE,
    PD_ACTION_LOAD_STATE,
    PD_ACTION_LEAVE_GAME,
    PD_ACTION_SETTINGS_CHANGED,
    PD_ACTION_BATTLE_FIGHT,
    PD_ACTION_BATTLE_PKMN,
    PD_ACTION_BATTLE_RUN,
    PD_ACTION_BATTLE_MOVE,   // actionArg = move slot 0..3
    PD_ACTION_BATTLE_SWITCH, // actionArg = party slot
    PD_ACTION_BATTLE_BACK,   // one B, out of the menu the game has open
    // actionArg = the game's bag pocket, actionArg2 = item index in it,
    // actionArg3 = berry index after the BERRY POUCH (-1 = the item itself),
    // actionArg4 = party slot to use it on (-1 = no target).
    PD_ACTION_BATTLE_ITEM,
};

// What the host tells the companion about itself.
#define PD_UI_HOST_LINES 2
struct pd_host_info {
    const char* labels[PD_UI_HOST_LINES]; // extra SETTINGS info rows (FILE, SPEED ...)
    char values[PD_UI_HOST_LINES][40];
    int count;
    bool ffOn;
    bool hasState;      // a save state exists for LOAD STATE
    char stateWhen[24]; // when it was made, shown beside LOAD STATE
};

#define PD_UI_MAX_HITS 64
struct pd_hit {
    int x, y, w, h;
    int id;
    bool scroll; // part of a list that a drag scrolls
};

enum pd_overlay {
    PD_OVERLAY_NONE,
    PD_OVERLAY_PICK_SCREEN,
    PD_OVERLAY_PICK_FF,
    PD_OVERLAY_PICK_SHADER,
    PD_OVERLAY_PICK_THEME,
    PD_OVERLAY_CONFIRM_SAVE,
    PD_OVERLAY_CONFIRM_LOAD,
    PD_OVERLAY_CONFIRM_LEAVE,
    PD_OVERLAY_PLACES,       // the MAP's list of places
    PD_OVERLAY_GUIDE_NOTICE, // the GUIDE's "AI-written, may be wrong" notice
};

struct pd_ui {
    enum pd_tab tab;
    enum pd_tab tabBeforeBattle; // the tab a battle took over from
    int battleSlot;              // the bar slot BATTLE shows in, -1 = none
    bool battleWasOn;
    // BATTLE: INFO (0) or SUGGESTIONS (1); the FOE TEAM's slots seen so far
    // this battle (a bit each), the one tapped (-1 = follow the battle) and
    // the one shown (set by the draw); what the last read said was out / next.
    int battlePane;
    // On the controls (battleButtons, pane 0): 0 the buttons, 1 moves,
    // 2 who to send in, 3 the bag, 4 who an item is used on.
    int battlePick;
    // What the game's own battle menu is waiting for (pd_battle.h). The
    // controls follow it: moves only while it is choosing a move.
    int battleMenu;
    int battleChosen; // party slot highlighted on the battle party pane, -1 = none
    unsigned foeSeen;
    int foeSelected, foeShown;
    int lastFoeActive, lastFoeNext;
    int summarySlot; // party slot whose summary is open, -1 = none
    int bagPocket;   // index into the game's own pocket order
    int bagSelected; // item index in that pocket, -1 = none
    int bagScroll;   // pixels

    // POKéDEX: the list's scroll, the open entry (national number, 0 = the
    // list), and which dex shows (-1 = the save's own: National once it has
    // it, else the regional one).
    int dexScroll, dexOpen, dexNational;
    // MAP: the page shown (-1 = the player's), the tapped place, the cursor's blink.
    int mapPage, mapSel, mapDungeon, mapSelTx, mapSelTy;
    bool mapBlink;
    unsigned blinkMs;
    int placesScroll;
    // GUIDE: the page, its scroll, the open entry and how far it's revealed.
    int guidePage, guideScroll, guideOpen, guideLevel;
    bool guideNoticeShown;
    // TRAINER CARD: the side shown, a flip running (ms into it, 0 = none) and
    // the play time's blinking colon.
    bool cardBack, cardColon;
    int cardFlipMs;
    unsigned cardColonMs;

    enum pd_overlay overlay;
    struct pd_settings* settings;
    enum pd_action action;
    int actionArg, actionArg2, actionArg3, actionArg4;
    // The last draw's game and snapshot, so a tap can read what it showed.
    const struct pd_game* game;
    const struct pd_snapshot* snap;

    bool touching, dragging, touchScroll;
    int touchX0, touchY0, touchLastY;
    int pressedId; // what the stylus went down on (-1 = nothing)
    struct pd_hit hits[PD_UI_MAX_HITS];
    int hitCount;
    int settingsScroll; // SETTINGS list, in pixels
    char settingsQuery[24];
};

void pd_ui_init(struct pd_ui* ui, struct pd_settings* settings);
// Follows the game: a battle opens BATTLE, its end goes back.
void pd_ui_update(struct pd_ui* ui, const struct pd_snapshot* s);
void pd_ui_draw(struct pd_ui* ui, struct pd_canvas* c, const struct pd_game* g,
                const struct pd_snapshot* s, const struct pd_host_info* host);
// Stylus: down / held / up at (x, y). Returns true if the screen needs a redraw.
bool pd_ui_touch(struct pd_ui* ui, int x, int y, bool down);
// Buttons the GBA doesn't have (the 3DS's X / Y): next / previous tab, BACK.
void pd_ui_next_tab(struct pd_ui* ui, int dir);
void pd_ui_open_tab(struct pd_ui* ui, enum pd_tab tab);
bool pd_ui_back(struct pd_ui* ui);
// SETTINGS search: ch > 0 appends, -1 deletes one character.
void pd_ui_search_type(struct pd_ui* ui, int ch);
// The last thing the player asked the host for (once), or PD_ACTION_NONE.
enum pd_action pd_ui_take_action(struct pd_ui* ui);
// The game's battle menu changed. Closes a local pane the game has left.
bool pd_ui_set_battle_menu(struct pd_ui* ui, int menu);
// Time passing (milliseconds since the last call): true when something on
// screen animates and needs a redraw (the MAP cursor's blink, the TRAINER
// CARD's colon and flip).
bool pd_ui_tick(struct pd_ui* ui, unsigned ms);

const char* pd_screen_mode_name(int mode);
const char* pd_shader_name(int shader);

#endif
