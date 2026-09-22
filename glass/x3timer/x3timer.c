/*
 * X3 Timer — a glanceable dual-mode timer for MemoMind glasses.
 *
 * Port of the RayNeo X3 Pro app of the same name. Pomodoro focus mode plus
 * three athletic modalities (HIIT/Tabata, EMOM, AMRAP). The widget occupies
 * < 20% of the panel; everything else stays black (transparent on the
 * waveguide). A thin edge pulse echoes the phase so it reads from the corner
 * of the eye, and a running Pomodoro shrinks to a corner chip after 5 s of no
 * interaction so the view stays clear for reading or coding.
 *
 * Controls (glasses alone). The single button does all in-session work;
 * head motion is deliberately ignored while a session is live so that
 * looking down at a water bottle can never touch the clock.
 *   PRIMARY single      start / pause
 *   PRIMARY double      running: Pomodoro extend +10 min · HIIT/EMOM skip
 *                       interval · AMRAP +1 lap.  Idle / paused / done: reset
 *   PRIMARY long        next program (guarded while a session is in progress:
 *                       long-press again within 5 s to confirm)
 *   HEAD LEFT / RIGHT   previous / next program, only when nothing is running
 *   BACK / HOME         exit
 * Accessory ring, when paired: LEFT/RIGHT program, UP extend/skip/lap,
 *   DOWN pin/unpin the compact chip.
 *
 * Time is drawn as seven-segment digits built from plain LVGL rectangles, so
 * it scales with the panel without depending on Host font sizes.
 */
#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"

/* ---- programme definitions ------------------------------------------- */

#define MIN_MS            60000U
#define FOCUS_MS          (25U * MIN_MS)
#define BREAK_MS          (5U * MIN_MS)
#define LONG_BREAK_MS     (15U * MIN_MS)
#define EXTEND_MS         (10U * MIN_MS)
#define PREPARE_MS        10000U
#define HIIT_ROUNDS       8U
#define HIIT_WORK_MS      20000U
#define HIIT_REST_MS      10000U
#define EMOM_ROUNDS       10U
#define AMRAP_MS          (12U * MIN_MS)

#define CONFIRM_WINDOW_MS 5000U
#define MINIMIZE_DELAY_MS 5000U
#define FLASH_MS          320U
#define MAX_DT_MS         250U

typedef enum {
    PROGRAM_POMODORO = 0,
    PROGRAM_HIIT,
    PROGRAM_EMOM,
    PROGRAM_AMRAP,
    PROGRAM_COUNT
} program_t;

typedef enum {
    PHASE_PREPARE,
    PHASE_FOCUS,
    PHASE_BREAK,
    PHASE_LONG_BREAK,
    PHASE_WORK,
    PHASE_REST,
    PHASE_EMOM_ROUND,
    PHASE_AMRAP,
    PHASE_DONE
} phase_kind_t;

typedef enum { STATE_IDLE, STATE_RUNNING, STATE_PAUSED, STATE_DONE } state_t;

static const char *const program_names[PROGRAM_COUNT] = {
    "POMODORO", "HIIT", "EMOM", "AMRAP"
};

/* ---- seven-segment digits ---------------------------------------------- */

#define SEG_COUNT   7U
#define DIGIT_SLOTS 5U /* m m m : s s -> up to 999 minutes */

/* Segment bit order: a b c d e f g (top, top-right, bottom-right, bottom,
 * bottom-left, top-left, middle). */
static const uint8_t digit_masks[10] = {
    0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B
};

typedef struct {
    gm_plugin_lvgl_obj_t *seg[SEG_COUNT];
    uint8_t shown_mask;
} digit_t;

/* ---- plugin state ------------------------------------------------------ */

typedef struct {
    const gm_plugin_host_api_t *host;
    const gm_plugin_lvgl_api_t *ui;
    const gm_plugin_libc_extension_api_t *libc;
    bool has_imu;

    /* engine */
    program_t program;
    state_t state;
    phase_kind_t phase;
    uint32_t phase_ms;      /* duration; 0 for DONE */
    bool count_up;
    uint8_t round;          /* current WORK / EMOM round, 1-based */
    uint8_t round_total;
    uint32_t elapsed_ms;
    uint32_t last_now;
    bool resync;            /* next update adds the whole gap (after resume) */
    int8_t last_count_sec;
    uint16_t pomodoros;
    uint16_t laps;

    /* interaction */
    uint32_t now;
    int8_t pending_dir;
    uint32_t pending_until;
    uint32_t last_interaction;
    bool pinned_compact;
    uint32_t flash_until;

    /* screen geometry */
    int16_t sw, sh;

    /* UI objects */
    gm_plugin_lvgl_obj_t *root;
    gm_plugin_lvgl_obj_t *edge;
    gm_plugin_lvgl_obj_t *help1;
    gm_plugin_lvgl_obj_t *help2;
    gm_plugin_lvgl_obj_t *grid_lines[9];
    gm_plugin_lvgl_point_t grid_points[9][2];
    gm_plugin_lvgl_obj_t *panel;
    gm_plugin_lvgl_obj_t *overline;
    digit_t digits[DIGIT_SLOTS];
    gm_plugin_lvgl_obj_t *colon[2];
    gm_plugin_lvgl_obj_t *bar_bg;
    gm_plugin_lvgl_obj_t *bar_fill;
    gm_plugin_lvgl_obj_t *caption;
    gm_plugin_lvgl_obj_t *prompt;
    gm_plugin_lvgl_obj_t *prompt_label;
    gm_plugin_lvgl_obj_t *prompt_bar;

    /* render cache so we only touch LVGL when something changed */
    bool compact_shown;
    int16_t panel_x, panel_y, panel_w, panel_h;
    int16_t bar_w, bar_y, bar_h, cap_y, dig_y, dig_h;
    uint8_t shown_slots;
    uint8_t shown_accent;
    uint8_t shown_edge;
    bool edge_shown;
    bool help_shown;
    bool grid_shown;
    bool prompt_shown;
    bool colon_shown;
    int16_t shown_bar;
    int16_t prompt_bar_w;
    char shown_overline[40];
    char shown_caption[48];
    char shown_prompt[64];
} timer_t;

static timer_t app;

#define number gm_plugin_lvgl_style_number
#define color gm_plugin_lvgl_style_color

static void set_style(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_style_prop_t p,
                      gm_plugin_lvgl_style_value_t v)
{
    app.ui->style_set(o, p, v, GM_PLUGIN_LVGL_SELECTOR_MAIN);
}

static void set_hidden(gm_plugin_lvgl_obj_t *o, bool hidden)
{
    if (hidden) app.ui->obj_add_flag(o, GM_PLUGIN_LVGL_FLAG_HIDDEN);
    else app.ui->obj_clear_flag(o, GM_PLUGIN_LVGL_FLAG_HIDDEN);
}

static void style_box(gm_plugin_lvgl_obj_t *o, uint8_t fill, uint8_t fill_opa,
                      uint8_t border, uint8_t border_shade, uint8_t radius)
{
    set_style(o, GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(fill));
    set_style(o, GM_PLUGIN_LVGL_STYLE_BG_OPA, number(fill_opa));
    set_style(o, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(border_shade));
    set_style(o, GM_PLUGIN_LVGL_STYLE_BORDER_OPA, number(255));
    set_style(o, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH, number(border));
    set_style(o, GM_PLUGIN_LVGL_STYLE_RADIUS, number(radius));
    set_style(o, GM_PLUGIN_LVGL_STYLE_PAD_TOP, number(0));
    set_style(o, GM_PLUGIN_LVGL_STYLE_PAD_BOTTOM, number(0));
    set_style(o, GM_PLUGIN_LVGL_STYLE_PAD_LEFT, number(0));
    set_style(o, GM_PLUGIN_LVGL_STYLE_PAD_RIGHT, number(0));
    app.ui->obj_clear_flag(o, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);
}

static void style_text(gm_plugin_lvgl_obj_t *o, uint8_t shade, bool large)
{
    gm_plugin_lvgl_style_value_t font = {0};
    font.ptr = large ? app.ui->font_large : app.ui->font_default;
    set_style(o, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(shade));
    set_style(o, GM_PLUGIN_LVGL_STYLE_TEXT_FONT, font);
    set_style(o, GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN,
              number(GM_PLUGIN_LVGL_TEXT_ALIGN_CENTER));
}

/* ---- engine -------------------------------------------------------------- */

static bool athletic(void) { return app.program != PROGRAM_POMODORO; }

static void enter_phase(phase_kind_t kind, uint32_t ms, bool count_up)
{
    app.phase = kind;
    app.phase_ms = ms;
    app.count_up = count_up;
    app.elapsed_ms = 0;
    app.last_count_sec = -1;
    /* Pop the HUD back to full size briefly so a phase change is noticed. */
    app.last_interaction = app.now;
    if (kind == PHASE_WORK || kind == PHASE_EMOM_ROUND || kind == PHASE_AMRAP)
        app.flash_until = app.now + FLASH_MS;
}

static void load_program(void)
{
    app.state = STATE_IDLE;
    app.round = 0;
    app.laps = 0;
    switch (app.program) {
    case PROGRAM_POMODORO:
        app.round_total = 0;
        enter_phase(PHASE_FOCUS, FOCUS_MS, false);
        break;
    case PROGRAM_HIIT:
        app.round_total = HIIT_ROUNDS;
        enter_phase(PHASE_PREPARE, PREPARE_MS, false);
        break;
    case PROGRAM_EMOM:
        app.round_total = EMOM_ROUNDS;
        enter_phase(PHASE_PREPARE, PREPARE_MS, false);
        break;
    case PROGRAM_AMRAP:
    default:
        app.round_total = 0;
        enter_phase(PHASE_PREPARE, PREPARE_MS, false);
        break;
    }
}

static void finish(void)
{
    app.state = STATE_DONE;
    app.phase = PHASE_DONE;
    app.phase_ms = 0;
    app.count_up = false;
    app.elapsed_ms = 0;
    app.flash_until = app.now + FLASH_MS * 3U;
    app.last_interaction = app.now;
}

static void advance(void)
{
    phase_kind_t finished = app.phase;
    if (finished == PHASE_FOCUS) ++app.pomodoros;

    switch (app.program) {
    case PROGRAM_POMODORO:
        if (finished == PHASE_FOCUS) {
            if (app.pomodoros % 4U == 0U)
                enter_phase(PHASE_LONG_BREAK, LONG_BREAK_MS, false);
            else
                enter_phase(PHASE_BREAK, BREAK_MS, false);
        } else {
            enter_phase(PHASE_FOCUS, FOCUS_MS, false);
        }
        break;
    case PROGRAM_HIIT:
        if (finished == PHASE_PREPARE || finished == PHASE_REST) {
            ++app.round;
            enter_phase(PHASE_WORK, HIIT_WORK_MS, false);
        } else if (app.round >= HIIT_ROUNDS) {
            finish();
        } else {
            enter_phase(PHASE_REST, HIIT_REST_MS, false);
        }
        break;
    case PROGRAM_EMOM:
        if (app.round >= EMOM_ROUNDS) {
            finish();
        } else {
            ++app.round;
            enter_phase(PHASE_EMOM_ROUND, MIN_MS, false);
        }
        break;
    case PROGRAM_AMRAP:
    default:
        if (finished == PHASE_PREPARE) enter_phase(PHASE_AMRAP, AMRAP_MS, true);
        else finish();
        break;
    }
}

static uint32_t remaining_ms(void)
{
    if (app.count_up) return app.elapsed_ms;
    return app.elapsed_ms >= app.phase_ms ? 0U : app.phase_ms - app.elapsed_ms;
}

static void engine_update(void)
{
    uint32_t dt;
    if (app.state != STATE_RUNNING) {
        app.last_now = app.now;
        return;
    }
    dt = app.now - app.last_now;
    /* Clamp a hitch so it cannot fast-forward the clock; a resume from
     * suspend deliberately credits the whole gap so wall time stays honest. */
    if (!app.resync && dt > MAX_DT_MS) dt = MAX_DT_MS;
    app.resync = false;
    app.last_now = app.now;
    app.elapsed_ms += dt;
    if (app.elapsed_ms >= app.phase_ms) {
        /* Carry the overshoot into the next phase unless it would be huge. */
        uint32_t over = app.elapsed_ms - app.phase_ms;
        advance();
        if (app.state == STATE_RUNNING && over < 4U * MAX_DT_MS)
            app.elapsed_ms = over;
    }
}

static void toggle_start_pause(void)
{
    switch (app.state) {
    case STATE_IDLE:
        app.state = STATE_RUNNING;
        app.last_now = app.now;
        app.flash_until = app.now + FLASH_MS;
        break;
    case STATE_RUNNING:
        app.state = STATE_PAUSED;
        break;
    case STATE_PAUSED:
        app.state = STATE_RUNNING;
        app.last_now = app.now;
        break;
    case STATE_DONE:
    default:
        load_program();
        break;
    }
}

/* NOD / UP: pomodoro extends the focus block (flow protection); AMRAP counts a
 * lap; HIIT/EMOM skip to the next interval. */
static void action_up(void)
{
    switch (app.program) {
    case PROGRAM_POMODORO:
        if (app.phase == PHASE_FOCUS) {
            app.phase_ms += EXTEND_MS;
            app.flash_until = app.now + FLASH_MS;
        } else if (app.state == STATE_RUNNING || app.state == STATE_PAUSED) {
            advance();
        }
        break;
    case PROGRAM_AMRAP:
        if (app.state == STATE_RUNNING && app.phase == PHASE_AMRAP) {
            ++app.laps;
            app.flash_until = app.now + FLASH_MS;
        }
        break;
    default:
        if (app.state == STATE_RUNNING || app.state == STATE_PAUSED) advance();
        break;
    }
}

static void cycle_program(int8_t dir)
{
    int8_t next = (int8_t)app.program + dir;
    if (next < 0) next = PROGRAM_COUNT - 1;
    if (next >= (int8_t)PROGRAM_COUNT) next = 0;
    app.program = (program_t)next;
    load_program();
}

/* Accidental-switch guard: while a session is in progress the first request
 * only arms a 5 s confirmation; the second one within the window switches. */
static void request_switch(int8_t dir)
{
    bool in_progress = app.state == STATE_RUNNING || app.state == STATE_PAUSED;
    if (!in_progress) {
        cycle_program(dir);
    } else if (app.pending_dir != 0 && app.now < app.pending_until) {
        app.pending_dir = 0;
        cycle_program(dir);
    } else {
        app.pending_dir = dir;
        app.pending_until = app.now + CONFIRM_WINDOW_MS;
    }
}

/* ---- derived presentation --------------------------------------------- */

static bool final_minute(void)
{
    uint32_t r;
    if (app.program != PROGRAM_POMODORO || app.phase != PHASE_FOCUS ||
        app.state != STATE_RUNNING)
        return false;
    r = remaining_ms();
    return r > 0U && r <= MIN_MS;
}

static bool athletic_warning(void)
{
    uint32_t r;
    if (!athletic() || app.state != STATE_RUNNING || app.count_up) return false;
    r = remaining_ms();
    return r > 0U && r <= 5000U;
}

static const char *phase_label(void)
{
    char *buf;
    switch (app.phase) {
    case PHASE_PREPARE: return "GET READY";
    case PHASE_FOCUS: return "FOCUS";
    case PHASE_BREAK: return "BREAK";
    case PHASE_LONG_BREAK: return "LONG BREAK";
    case PHASE_WORK: return "WORK";
    case PHASE_REST: return "REST";
    case PHASE_AMRAP: return "AMRAP";
    case PHASE_DONE: return "COMPLETE";
    case PHASE_EMOM_ROUND:
    default:
        break;
    }
    /* "MIN n" for EMOM rounds; static so the pointer stays valid. */
    {
        static char emom[12];
        buf = emom;
        app.libc->snprintf(buf, sizeof(emom), "MIN %u", (unsigned)app.round);
        return buf;
    }
}

/* Grey level standing in for the colour accent. Monochrome, so the phase is
 * carried by the overline text and the edge cadence; brightness carries
 * intensity: full for athletic/done, calmer for focus and breaks. */
static uint8_t accent(void)
{
    if (app.now < app.flash_until) return 0xFF;
    if (app.state == STATE_IDLE) return 0xB0;
    if (app.state == STATE_DONE) return 0xFF;
    if (app.state == STATE_PAUSED) return 0x90;
    if (athletic()) return 0xFF;
    if (app.phase == PHASE_FOCUS) return final_minute() ? 0xFF : 0xD0;
    return 0xC0;
}

static bool want_compact(void)
{
    if (app.pinned_compact) return true;
    return app.program == PROGRAM_POMODORO && app.state == STATE_RUNNING &&
           app.now - app.last_interaction > MINIMIZE_DELAY_MS;
}

/* ---- layout -------------------------------------------------------------- */

static int16_t digit_w(int16_t h) { return (int16_t)(h * 55 / 100); }
static int16_t digit_thick(int16_t h)
{
    int16_t t = (int16_t)(h / 7);
    return t < 2 ? 2 : t;
}

/* Position the seven segments of one digit inside a (w x h) box at (x, y). */
static void layout_digit(digit_t *d, int16_t x, int16_t y, int16_t w, int16_t h)
{
    int16_t t = digit_thick(h);
    int16_t half = (int16_t)((h - t) / 2);
    const gm_plugin_lvgl_api_t *ui = app.ui;
    /* a */ ui->obj_set_pos(d->seg[0], (int16_t)(x + t), y);
            ui->obj_set_size(d->seg[0], (int16_t)(w - 2 * t), t);
    /* b */ ui->obj_set_pos(d->seg[1], (int16_t)(x + w - t), (int16_t)(y + t));
            ui->obj_set_size(d->seg[1], t, (int16_t)(half - t));
    /* c */ ui->obj_set_pos(d->seg[2], (int16_t)(x + w - t), (int16_t)(y + half + t));
            ui->obj_set_size(d->seg[2], t, (int16_t)(h - half - 2 * t));
    /* d */ ui->obj_set_pos(d->seg[3], (int16_t)(x + t), (int16_t)(y + h - t));
            ui->obj_set_size(d->seg[3], (int16_t)(w - 2 * t), t);
    /* e */ ui->obj_set_pos(d->seg[4], x, (int16_t)(y + half + t));
            ui->obj_set_size(d->seg[4], t, (int16_t)(h - half - 2 * t));
    /* f */ ui->obj_set_pos(d->seg[5], x, (int16_t)(y + t));
            ui->obj_set_size(d->seg[5], t, (int16_t)(half - t));
    /* g */ ui->obj_set_pos(d->seg[6], (int16_t)(x + t), (int16_t)(y + half));
            ui->obj_set_size(d->seg[6], (int16_t)(w - 2 * t), t);
}

/* Centre the visible digit slots (3, 4 or 5 of "mmm:ss") in the panel. The
 * hidden leading slots are parked off-panel so the time never has a gap. */
static void layout_digits(uint8_t visible)
{
    const gm_plugin_lvgl_api_t *ui = app.ui;
    int16_t dh = app.dig_h, y = app.dig_y;
    int16_t dw = digit_w(dh);
    int16_t t = digit_thick(dh);
    int16_t gap = (int16_t)(t + 2);
    int16_t total = (int16_t)(visible * dw + (visible - 1) * gap + 2 * t + 2 * gap);
    int16_t x = (int16_t)((app.panel_w - total) / 2);
    uint8_t first = (uint8_t)(DIGIT_SLOTS - visible);
    uint8_t i;
    for (i = 0; i < DIGIT_SLOTS; ++i) {
        if (i < first) {
            layout_digit(&app.digits[i], (int16_t)-200, y, dw, dh);
            continue;
        }
        layout_digit(&app.digits[i], x, y, dw, dh);
        x = (int16_t)(x + dw + gap);
        if (i == 2) {
            ui->obj_set_pos(app.colon[0], x, (int16_t)(y + dh / 3 - t / 2));
            ui->obj_set_size(app.colon[0], t, t);
            ui->obj_set_pos(app.colon[1], x, (int16_t)(y + 2 * dh / 3 - t / 2));
            ui->obj_set_size(app.colon[1], t, t);
            x = (int16_t)(x + t + gap);
        }
    }
    app.shown_slots = visible;
}

static void layout_panel(bool compact)
{
    const gm_plugin_lvgl_api_t *ui = app.ui;
    int16_t pw, ph, px, py, pad, dh, y, lh;

    lh = ui->font_get_line_height(ui->font_default);
    if (lh < 14) lh = 14;
    if (compact) {
        pw = (int16_t)(app.sw * 32 / 100);
        ph = (int16_t)(app.sh * 23 / 100);
        px = (int16_t)(app.sw * 3 / 100);
        py = (int16_t)(app.sh * 5 / 100);
    } else {
        pw = (int16_t)(app.sw / 2);
        ph = (int16_t)(app.sh * 42 / 100);
        px = (int16_t)((app.sw - pw) / 2);
        py = (int16_t)(app.sh * 55 / 100);
    }
    app.panel_x = px; app.panel_y = py; app.panel_w = pw; app.panel_h = ph;
    pad = (int16_t)(ph * 12 / 100);

    ui->obj_set_pos(app.panel, px, py);
    ui->obj_set_size(app.panel, pw, ph);

    /* Overline sits inside the top padding. Everything is a child of the
     * panel so coordinates below are panel-relative. */
    ui->obj_set_size(app.overline, (int16_t)(pw - 2 * pad), lh);
    ui->obj_set_pos(app.overline, pad, (int16_t)(compact ? 3 : 6));

    /* Digits: three minute slots (leading one hidden when unused), colon,
     * two second slots. Height comes from the room left between the
     * overline and the caption/bar so nothing can collide whatever the
     * Host font size is. */
    {
        int16_t cap_y = (int16_t)(ph - lh - 4);
        int16_t bar_h = (int16_t)(ph * 45 / 1000 + 1);
        int16_t top = (int16_t)(compact ? 3 + lh + 2 : 6 + lh + 4);
        int16_t room = compact ? (int16_t)(ph - top - 4)
                               : (int16_t)(cap_y - top - bar_h - 10);
        dh = (int16_t)(ph * (compact ? 44 : 40) / 100);
        if (dh > room) dh = room;
        if (dh < 12) dh = 12;
        y = top;
        app.bar_y = (int16_t)(y + dh + 4);
        app.bar_h = bar_h;
        app.cap_y = cap_y;
    }
    app.dig_y = y;
    app.dig_h = dh;
    app.shown_slots = 0;            /* force layout_digits on next render */

    ui->obj_set_pos(app.bar_bg, pad, app.bar_y);
    ui->obj_set_size(app.bar_bg, app.bar_w, app.bar_h);
    ui->obj_set_pos(app.bar_fill, pad, app.bar_y);
    ui->obj_set_size(app.bar_fill, 1, app.bar_h);
    ui->obj_set_size(app.caption, (int16_t)(pw - 2 * pad), lh);
    ui->obj_set_pos(app.caption, pad, app.cap_y);
    set_hidden(app.bar_bg, compact);
    set_hidden(app.bar_fill, compact);
    set_hidden(app.caption, compact);
    app.shown_bar = -1;

    /* Switch-confirmation chip floats above the full panel. */
    ui->obj_set_size(app.prompt, (int16_t)(app.sw * 88 / 100), (int16_t)(lh + 18));
    ui->obj_set_pos(app.prompt, (int16_t)(app.sw * 6 / 100),
                    (int16_t)(app.sh * 55 / 100 - lh - 40));
    ui->obj_set_size(app.prompt_label, (int16_t)(app.sw * 84 / 100), lh);
    ui->obj_set_pos(app.prompt_label, (int16_t)(app.sw * 2 / 100), 5);
    app.prompt_bar_w = (int16_t)(app.sw * 84 / 100);
    ui->obj_set_pos(app.prompt_bar, (int16_t)(app.sw * 2 / 100), (int16_t)(lh + 10));
    ui->obj_set_size(app.prompt_bar, app.prompt_bar_w, 4);
}

static void layout_grid(void)
{
    /* Faint synthwave floor behind the athletic panel: a horizon plus
     * receding horizontals and a fan of perspective lines. */
    int16_t horizon = (int16_t)(app.sh * 55 / 100 - app.sh * 4 / 100);
    int16_t y = horizon;
    int16_t d = (int16_t)(app.sh * 3 / 100);
    uint8_t i;
    for (i = 0; i < 4; ++i) {
        app.grid_points[i][0].x = 0;
        app.grid_points[i][0].y = y;
        app.grid_points[i][1].x = app.sw;
        app.grid_points[i][1].y = y;
        y = (int16_t)(y + d);
        d = (int16_t)(d * 135 / 100);
    }
    for (i = 4; i < 9; ++i) {
        int16_t k = (int16_t)((int16_t)i - 6); /* -2..2 */
        app.grid_points[i][0].x = (int16_t)(app.sw / 2);
        app.grid_points[i][0].y = horizon;
        app.grid_points[i][1].x = (int16_t)(app.sw / 2 + k * (app.sw * 32 / 100));
        app.grid_points[i][1].y = app.sh;
    }
    for (i = 0; i < 9; ++i)
        app.ui->line_set_points(app.grid_lines[i], app.grid_points[i], 2);
}

/* ---- UI construction ----------------------------------------------------- */

static gm_plugin_lvgl_obj_t *make_rect(gm_plugin_lvgl_obj_t *parent, uint8_t shade)
{
    gm_plugin_lvgl_obj_t *o = app.ui->obj_create(parent);
    if (o == 0) return 0;
    style_box(o, shade, 255, 0, shade, 1);
    return o;
}

static gm_plugin_result_t create_ui(void)
{
    const gm_plugin_lvgl_api_t *ui = app.ui;
    gm_plugin_lvgl_obj_t *host_root = ui->root_get();
    gm_plugin_display_info_t display;
    uint8_t i, s;

    if (host_root == 0 || app.host->display_get_info(&display) != GM_PLUGIN_OK)
        return GM_PLUGIN_ESTATE;
    app.sw = (int16_t)display.width;
    app.sh = (int16_t)display.height;

    ui->obj_clean(host_root);
    app.root = ui->obj_create(host_root);
    if (app.root == 0) return GM_PLUGIN_ENOMEM;
    ui->obj_set_pos(app.root, 0, 0);
    ui->obj_set_size(app.root, app.sw, app.sh);
    style_box(app.root, 0x00, 255, 0, 0x00, 0);

    /* Synth grid (behind everything else). */
    for (i = 0; i < 9; ++i) {
        app.grid_lines[i] = ui->line_create(app.root);
        if (app.grid_lines[i] == 0) return GM_PLUGIN_ENOMEM;
        set_style(app.grid_lines[i], GM_PLUGIN_LVGL_STYLE_LINE_COLOR, color(0x30));
        set_style(app.grid_lines[i], GM_PLUGIN_LVGL_STYLE_LINE_OPA, number(255));
        set_style(app.grid_lines[i], GM_PLUGIN_LVGL_STYLE_LINE_WIDTH, number(1));
        set_hidden(app.grid_lines[i], true);
    }
    layout_grid();

    /* Edge pulse: border-only frame inset from the panel edge. */
    app.edge = ui->obj_create(app.root);
    if (app.edge == 0) return GM_PLUGIN_ENOMEM;
    ui->obj_set_pos(app.edge, 2, 2);
    ui->obj_set_size(app.edge, (int16_t)(app.sw - 4), (int16_t)(app.sh - 4));
    style_box(app.edge, 0x00, 0, 4, 0x40, 6);
    set_hidden(app.edge, true);

    /* Idle help. */
    app.help1 = ui->label_create(app.root);
    app.help2 = ui->label_create(app.root);
    if (app.help1 == 0 || app.help2 == 0) return GM_PLUGIN_ENOMEM;
    {
        int16_t lh = ui->font_get_line_height(ui->font_default);
        ui->obj_set_size(app.help1, (int16_t)(app.sw - 40), lh);
        ui->obj_set_size(app.help2, (int16_t)(app.sw - 40), lh);
        ui->obj_set_pos(app.help1, 20, (int16_t)(app.sh * 28 / 100));
        ui->obj_set_pos(app.help2, 20, (int16_t)(app.sh * 28 / 100 + lh + 4));
    }
    style_text(app.help1, 0x90, false);
    style_text(app.help2, 0x90, false);
    ui->label_set_text(app.help1, "CLICK start / pause   2x CLICK reset   HOLD mode");
    ui->label_set_text(app.help2, app.has_imu
        ? "RUNNING: 2x CLICK = lap / skip / extend"
        : "RUNNING: 2x CLICK = lap / skip / extend");

    /* Panel and its children. */
    app.panel = ui->obj_create(app.root);
    if (app.panel == 0) return GM_PLUGIN_ENOMEM;
    style_box(app.panel, 0x10, 255, 2, 0xFF, 12);

    app.overline = ui->label_create(app.panel);
    app.caption = ui->label_create(app.panel);
    if (app.overline == 0 || app.caption == 0) return GM_PLUGIN_ENOMEM;
    style_text(app.overline, 0xFF, false);
    style_text(app.caption, 0xC0, false);

    for (i = 0; i < DIGIT_SLOTS; ++i) {
        for (s = 0; s < SEG_COUNT; ++s) {
            app.digits[i].seg[s] = make_rect(app.panel, 0xFF);
            if (app.digits[i].seg[s] == 0) return GM_PLUGIN_ENOMEM;
            set_hidden(app.digits[i].seg[s], true);
        }
        app.digits[i].shown_mask = 0; /* every segment starts hidden */
    }
    for (i = 0; i < 2; ++i) {
        app.colon[i] = make_rect(app.panel, 0xFF);
        if (app.colon[i] == 0) return GM_PLUGIN_ENOMEM;
    }

    app.bar_bg = make_rect(app.panel, 0x28);
    app.bar_fill = make_rect(app.panel, 0xFF);
    if (app.bar_bg == 0 || app.bar_fill == 0) return GM_PLUGIN_ENOMEM;

    /* Switch-confirmation chip. */
    app.prompt = ui->obj_create(app.root);
    if (app.prompt == 0) return GM_PLUGIN_ENOMEM;
    style_box(app.prompt, 0x10, 255, 2, 0xE0, 8);
    app.prompt_label = ui->label_create(app.prompt);
    app.prompt_bar = make_rect(app.prompt, 0xE0);
    if (app.prompt_label == 0 || app.prompt_bar == 0) return GM_PLUGIN_ENOMEM;
    style_text(app.prompt_label, 0xFF, false);
    set_hidden(app.prompt, true);

    /* Invalidate the render cache and lay out at full size. */
    app.shown_accent = 0;
    app.shown_edge = 0;
    app.edge_shown = true;
    app.help_shown = true;
    app.grid_shown = true;
    app.prompt_shown = true;
    app.colon_shown = false;
    app.shown_overline[0] = '\0';
    app.shown_caption[0] = '\0';
    app.shown_prompt[0] = '\0';
    app.compact_shown = false;
    layout_panel(false);
    return GM_PLUGIN_OK;
}

static void clear_ui(void)
{
    gm_plugin_lvgl_obj_t *host_root = app.ui->root_get();
    if (host_root != 0) app.ui->obj_clean(host_root);
    app.root = 0;
    app.panel = 0;
}

/* ---- render -------------------------------------------------------------- */

static void set_text_if_changed(gm_plugin_lvgl_obj_t *label, char *cache,
                                size_t cap, const char *text)
{
    size_t i;
    for (i = 0; i + 1 < cap; ++i) {
        if (cache[i] != text[i]) break;
        if (text[i] == '\0') return;
    }
    for (i = 0; i + 1 < cap && text[i] != '\0'; ++i) cache[i] = text[i];
    cache[i] = '\0';
    app.ui->label_set_text(label, cache);
}

static void render_digit(digit_t *d, uint8_t mask, uint8_t shade, bool recolor)
{
    uint8_t s;
    for (s = 0; s < SEG_COUNT; ++s) {
        bool on = (mask >> (6 - s)) & 1U;
        bool was = (d->shown_mask >> (6 - s)) & 1U;
        if (on != was) set_hidden(d->seg[s], !on);
        if (on && recolor)
            set_style(d->seg[s], GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(shade));
    }
    d->shown_mask = mask;
}

static void render_time(uint8_t shade, bool recolor)
{
    uint32_t total_sec = remaining_ms() / 1000U;
    uint32_t minutes = total_sec / 60U;
    uint32_t seconds = total_sec % 60U;
    uint8_t masks[DIGIT_SLOTS];
    uint8_t i;
    if (minutes > 999U) minutes = 999U;
    {
        uint8_t visible = minutes >= 100U ? 5 : minutes >= 10U ? 4 : 3;
        if (visible != app.shown_slots) layout_digits(visible);
    }
    masks[0] = minutes >= 100U ? digit_masks[minutes / 100U] : 0;
    masks[1] = minutes >= 10U ? digit_masks[(minutes / 10U) % 10U] : 0;
    masks[2] = digit_masks[minutes % 10U];
    masks[3] = digit_masks[seconds / 10U];
    masks[4] = digit_masks[seconds % 10U];
    for (i = 0; i < DIGIT_SLOTS; ++i)
        render_digit(&app.digits[i], masks[i], shade, recolor);
    /* Blink the colon while paused so a stopped clock is unmistakable. */
    {
        bool show = app.state != STATE_PAUSED || ((app.now / 500U) & 1U) == 0U;
        if (show != app.colon_shown) {
            set_hidden(app.colon[0], !show);
            set_hidden(app.colon[1], !show);
            app.colon_shown = show;
        }
        if (recolor) {
            set_style(app.colon[0], GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(shade));
            set_style(app.colon[1], GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(shade));
        }
    }
}

static void render_edge(void)
{
    bool warn = athletic_warning();
    bool show = warn || app.state == STATE_DONE ||
                (app.state == STATE_RUNNING && (athletic() || final_minute()));
    uint8_t shade;
    if (show != app.edge_shown) {
        set_hidden(app.edge, !show);
        app.edge_shown = show;
    }
    if (!show) return;
    if (warn) {
        /* hard flash in the final five seconds */
        shade = ((app.now / 140U) & 1U) == 0U ? 0xFF : 0x20;
    } else if (app.phase == PHASE_REST || app.phase == PHASE_BREAK ||
               app.phase == PHASE_LONG_BREAK) {
        /* slow, calm breathing for rest phases */
        uint32_t t = app.now % 2400U;
        shade = (uint8_t)(0x30 + (t < 1200U ? t : 2400U - t) * 0x50 / 1200U);
    } else {
        /* brisk pulse for work phases and the pomodoro final minute */
        uint32_t t = app.now % 1200U;
        shade = (uint8_t)(0x50 + (t < 600U ? t : 1200U - t) * 0x80 / 600U);
    }
    if (shade != app.shown_edge) {
        set_style(app.edge, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(shade));
        app.shown_edge = shade;
    }
}

static void render_prompt(void)
{
    bool show = app.pending_dir != 0;
    if (show != app.prompt_shown) {
        set_hidden(app.prompt, !show);
        app.prompt_shown = show;
        app.shown_prompt[0] = '\0';
    }
    if (!show) return;
    {
        char text[64];
        int8_t next = (int8_t)app.program + app.pending_dir;
        int16_t w;
        if (next < 0) next = PROGRAM_COUNT - 1;
        if (next >= (int8_t)PROGRAM_COUNT) next = 0;
        app.libc->snprintf(text, sizeof(text), "%s?  REPEAT TO CONFIRM",
                           program_names[next]);
        set_text_if_changed(app.prompt_label, app.shown_prompt,
                            sizeof(app.shown_prompt), text);
        w = (int16_t)((int32_t)app.prompt_bar_w *
                      (int32_t)(app.pending_until - app.now) / CONFIRM_WINDOW_MS);
        if (w < 1) w = 1;
        app.ui->obj_set_size(app.prompt_bar, w, 4);
    }
}

static void render(void)
{
    bool compact = want_compact();
    uint8_t shade = accent();
    bool recolor = shade != app.shown_accent;
    bool idle = app.state == STATE_IDLE;
    bool grid = athletic() && app.state == STATE_RUNNING;
    char text[48];

    if (compact != app.compact_shown) {
        layout_panel(compact);
        app.compact_shown = compact;
        app.shown_overline[0] = '\0';
        recolor = true;
    }
    if (idle != app.help_shown) {
        set_hidden(app.help1, !idle);
        set_hidden(app.help2, !idle);
        app.help_shown = idle;
    }
    if (grid != app.grid_shown) {
        uint8_t i;
        for (i = 0; i < 9; ++i) set_hidden(app.grid_lines[i], !grid);
        app.grid_shown = grid;
    }
    if (recolor) {
        set_style(app.panel, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(shade));
        set_style(app.overline, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(shade));
        set_style(app.bar_fill, GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(shade));
        app.shown_accent = shade;
    }

    /* Overline: PROGRAM · PHASE, or just PHASE on the compact chip. */
    if (compact)
        app.libc->snprintf(text, sizeof(text), "%s", phase_label());
    else
        app.libc->snprintf(text, sizeof(text), "%s  -  %s",
                           program_names[app.program], phase_label());
    set_text_if_changed(app.overline, app.shown_overline,
                        sizeof(app.shown_overline), text);

    render_time(shade, recolor);

    if (!compact) {
        int16_t fill;
        if (app.phase_ms == 0U) fill = app.count_up ? 0 : app.bar_w;
        else fill = (int16_t)((uint32_t)app.bar_w * (app.elapsed_ms / 64U) / (app.phase_ms / 64U + 1U));
        if (fill < 1) fill = 1;
        if (fill > app.bar_w) fill = app.bar_w;
        if (fill != app.shown_bar) {
            app.ui->obj_set_size(app.bar_fill, fill, app.bar_h);
            app.shown_bar = fill;
        }

        if (app.state == STATE_DONE)
            app.libc->snprintf(text, sizeof(text), "COMPLETE");
        else if (app.program == PROGRAM_POMODORO)
            app.libc->snprintf(text, sizeof(text), "SESSION %u  -  %s",
                               (unsigned)app.pomodoros,
                               app.state == STATE_PAUSED ? "PAUSED" : "TODAY");
        else if (app.phase == PHASE_PREPARE && app.program == PROGRAM_HIIT)
            app.libc->snprintf(text, sizeof(text), "%u x 20s / 10s",
                               (unsigned)HIIT_ROUNDS);
        else if (app.phase == PHASE_PREPARE && app.program == PROGRAM_EMOM)
            app.libc->snprintf(text, sizeof(text), "%u x 1 MIN", (unsigned)EMOM_ROUNDS);
        else if (app.phase == PHASE_PREPARE && app.program == PROGRAM_AMRAP)
            app.libc->snprintf(text, sizeof(text), "%u MIN  -  2x CLICK = LAP",
                               (unsigned)(AMRAP_MS / MIN_MS));
        else if (app.program == PROGRAM_AMRAP)
            app.libc->snprintf(text, sizeof(text), "LAP %u", (unsigned)app.laps);
        else if (app.round_total > 0U)
            app.libc->snprintf(text, sizeof(text), "ROUND %u / %u",
                               (unsigned)app.round, (unsigned)app.round_total);
        else
            text[0] = '\0';
        set_text_if_changed(app.caption, app.shown_caption,
                            sizeof(app.shown_caption), text);
    }

    render_edge();
    render_prompt();
}

/* ---- lifecycle ----------------------------------------------------------- */

static gm_plugin_result_t plugin_start(void *ctx)
{
    gm_plugin_result_t result;
    (void)ctx;
    app.now = app.host->monotonic_ms();
    app.last_interaction = app.now;
    app.last_now = app.now;
    app.pending_dir = 0;
    result = create_ui();
    if (result != GM_PLUGIN_OK) {
        clear_ui();
        return result;
    }
    render();
    return GM_PLUGIN_OK;
}

static void plugin_loop(void *ctx, uint32_t elapsed_ms)
{
    (void)ctx;
    (void)elapsed_ms;
    app.now = app.host->monotonic_ms();
    if (app.pending_dir != 0 &&
        (app.now >= app.pending_until ||
         (app.state != STATE_RUNNING && app.state != STATE_PAUSED)))
        app.pending_dir = 0;
    engine_update();
    if (app.panel != 0) render();
}

static void interacted(void)
{
    app.last_interaction = app.now;
    app.pinned_compact = false;
}

static bool handle_event(const gm_plugin_event_t *event)
{

    if (event->type == GM_PLUGIN_EVENT_BUTTON) {
        gm_plugin_button_action_t action = event->data.button.action;
        gm_plugin_button_t button = event->data.button.button;
        if (button == GM_PLUGIN_BUTTON_PRIMARY) {
            switch (action) {
            case GM_PLUGIN_BUTTON_ACTION_SINGLE:
                interacted();
                toggle_start_pause();
                return true;
            case GM_PLUGIN_BUTTON_ACTION_DOUBLE:
                /* Mid-session the double-click is the lap / skip / extend
                 * control; with nothing running it resets. A running
                 * session is reset by pausing first, so a stray double
                 * click can never wipe a workout. */
                interacted();
                if (app.state == STATE_RUNNING) action_up();
                else load_program();
                return true;
            case GM_PLUGIN_BUTTON_ACTION_LONG:
                interacted();
                request_switch(1);
                return true;
            case GM_PLUGIN_BUTTON_ACTION_VERY_LONG:
                app.host->app_exit();
                return true;
            default:
                return false;
            }
        }
        if (action != GM_PLUGIN_BUTTON_ACTION_TRIGGER) return false;
        switch (button) {
        case GM_PLUGIN_BUTTON_LEFT:
            interacted();
            request_switch(-1);
            return true;
        case GM_PLUGIN_BUTTON_RIGHT:
            interacted();
            request_switch(1);
            return true;
        case GM_PLUGIN_BUTTON_UP:
        case GM_PLUGIN_BUTTON_PAGE_UP:
        case GM_PLUGIN_BUTTON_SCROLL_UP:
            interacted();
            action_up();
            return true;
        case GM_PLUGIN_BUTTON_DOWN:
        case GM_PLUGIN_BUTTON_PAGE_DOWN:
        case GM_PLUGIN_BUTTON_SCROLL_DOWN:
            /* Pin the compact chip so the view clears immediately. */
            app.last_interaction = app.now;
            app.pinned_compact = !app.pinned_compact;
            return true;
        case GM_PLUGIN_BUTTON_BACK:
        case GM_PLUGIN_BUTTON_HOME:
            app.host->app_exit();
            return true;
        default:
            return false;
        }
    }

    if (event->type == GM_PLUGIN_EVENT_IMU_GESTURE) {
        gm_plugin_imu_gesture_t g = event->data.imu_gesture.gesture;
        bool in_session = app.state == STATE_RUNNING || app.state == STATE_PAUSED;
        /* Head motion is never a control while a session is live: looking
         * down at a water bottle or nodding along to music must not touch
         * the clock. Between sessions, turning the head browses programs.
         * Every gesture is reported as handled so the Host does not log it
         * as unhandled input. */
        if (!event->data.imu_gesture.active || in_session) return true;
        switch (g) {
        case GM_PLUGIN_IMU_GESTURE_LEFT:
            interacted();
            cycle_program(-1);
            return true;
        case GM_PLUGIN_IMU_GESTURE_RIGHT:
            interacted();
            cycle_program(1);
            return true;
        default:
            return true;
        }
    }
    return false;
}

/* Redraw right away on a handled event so the HUD answers the press on the
 * same frame instead of waiting for the next on_loop. */
static bool plugin_event(void *ctx, const gm_plugin_event_t *event)
{
    bool handled;
    (void)ctx;
    if (event == 0) return false;
    app.now = app.host->monotonic_ms();
    handled = handle_event(event);
    if (handled && app.panel != 0) render();
    return handled;
}

static void plugin_suspend(void *ctx)
{
    (void)ctx;
}

static void plugin_resume(void *ctx)
{
    (void)ctx;
    /* Credit the whole suspended interval on the next tick: a timer must
     * keep honest wall time while the glasses were showing something else. */
    app.resync = true;
    app.now = app.host->monotonic_ms();
    app.last_interaction = app.now;
}

static void plugin_stop(void *ctx)
{
    (void)ctx;
    clear_ui();
}

gm_plugin_result_t gm_plugin_entry(const gm_plugin_host_api_t *host,
                                   gm_plugin_descriptor_t *plugin)
{
    const gm_plugin_capabilities_t required = GM_PLUGIN_CAP_BUTTON;
    if (host == 0 || plugin == 0 ||
        host->struct_size < GM_PLUGIN_HOST_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(host->abi_version,
                                      GM_PLUGIN_ABI_MIN_VERSION) ||
        host->graphics.lvgl == 0 || host->display_get_info == 0 ||
        host->monotonic_ms == 0 || host->app_exit == 0 ||
        (host->capabilities & required) != required ||
        plugin->struct_size < GM_PLUGIN_DESCRIPTOR_MIN_SIZE)
        return GM_PLUGIN_EVERSION;
    app.host = host;
    app.ui = host->graphics.lvgl;
    if (gm_plugin_libc_get(host, &app.libc) != GM_PLUGIN_OK)
        return GM_PLUGIN_ENOTSUP;
    if (app.ui->struct_size < GM_PLUGIN_LVGL_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(app.ui->api_version,
                                      GM_PLUGIN_LVGL_API_MIN_VERSION))
        return GM_PLUGIN_EVERSION;
    app.has_imu = (host->capabilities & GM_PLUGIN_CAP_IMU_EVENTS) != 0U;
    app.program = PROGRAM_POMODORO;
    app.now = host->monotonic_ms();
    load_program();

    plugin->abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    plugin->context = &app;
    plugin->on_start = plugin_start;
    plugin->on_loop = plugin_loop;
    plugin->on_event = plugin_event;
    plugin->on_suspend = plugin_suspend;
    plugin->on_resume = plugin_resume;
    plugin->on_stop = plugin_stop;
    return GM_PLUGIN_OK;
}
