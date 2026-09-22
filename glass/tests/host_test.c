#define _FORTIFY_SOURCE 0
/*
 * Host-side harness: compiles x3timer.c against the real SDK headers with a
 * stub Host + LVGL table, then drives the engine through the plugin callbacks.
 * Build/run: see run.sh. Not part of the .gmp.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"
#include "gm_plugin_extensions.h"

/* ---- stub LVGL: every object is a small heap record we can inspect ---- */
typedef struct obj { int hidden; int16_t x, y, w, h; char text[96]; } obj_t;
static obj_t root_obj;
static int obj_count;

static gm_plugin_lvgl_obj_t *root_get(void) { return (gm_plugin_lvgl_obj_t *)&root_obj; }
static gm_plugin_lvgl_obj_t *obj_create(gm_plugin_lvgl_obj_t *p) { (void)p; ++obj_count; return calloc(1, sizeof(obj_t)); }
static void obj_delete(gm_plugin_lvgl_obj_t *o) { (void)o; }
static void obj_clean(gm_plugin_lvgl_obj_t *o) { (void)o; }
static void obj_set_pos(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_coord_t x, gm_plugin_lvgl_coord_t y) { ((obj_t *)o)->x = x; ((obj_t *)o)->y = y; }
static void obj_set_size(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_coord_t w, gm_plugin_lvgl_coord_t h) { ((obj_t *)o)->w = w; ((obj_t *)o)->h = h; }
static void obj_align(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_align_t a, gm_plugin_lvgl_coord_t x, gm_plugin_lvgl_coord_t y) { (void)o; (void)a; (void)x; (void)y; }
static void obj_add_flag(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_flag_t f) { if (f == GM_PLUGIN_LVGL_FLAG_HIDDEN) ((obj_t *)o)->hidden = 1; }
static void obj_clear_flag(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_flag_t f) { if (f == GM_PLUGIN_LVGL_FLAG_HIDDEN) ((obj_t *)o)->hidden = 0; }
static void obj_invalidate(const gm_plugin_lvgl_obj_t *o) { (void)o; }
static gm_plugin_lvgl_coord_t obj_get_width(const gm_plugin_lvgl_obj_t *o) { return ((const obj_t *)o)->w; }
static gm_plugin_lvgl_coord_t obj_get_height(const gm_plugin_lvgl_obj_t *o) { return ((const obj_t *)o)->h; }
static void style_set(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_style_prop_t p, gm_plugin_lvgl_style_value_t v, gm_plugin_lvgl_selector_t s) { (void)o; (void)p; (void)v; (void)s; }
static gm_plugin_lvgl_obj_t *label_create(gm_plugin_lvgl_obj_t *p) { return obj_create(p); }
static void label_set_text(gm_plugin_lvgl_obj_t *o, const char *t) { snprintf(((obj_t *)o)->text, sizeof(((obj_t *)o)->text), "%s", t); }
static void label_set_long_mode(gm_plugin_lvgl_obj_t *o, gm_plugin_lvgl_label_mode_t m) { (void)o; (void)m; }
static gm_plugin_lvgl_obj_t *arc_create(gm_plugin_lvgl_obj_t *p) { return obj_create(p); }
static void arc_set_range(gm_plugin_lvgl_obj_t *o, int16_t a, int16_t b) { (void)o; (void)a; (void)b; }
static void arc_set_value(gm_plugin_lvgl_obj_t *o, int16_t v) { (void)o; (void)v; }
static gm_plugin_lvgl_obj_t *line_create(gm_plugin_lvgl_obj_t *p) { return obj_create(p); }
static void line_set_points(gm_plugin_lvgl_obj_t *o, const gm_plugin_lvgl_point_t *pts, uint16_t n) { (void)o; (void)pts; (void)n; }
static gm_plugin_lvgl_coord_t font_get_line_height(const gm_plugin_lvgl_font_t *f) { (void)f; return 26; }
static void text_get_size(gm_plugin_lvgl_point_t *s, const char *t, const gm_plugin_lvgl_font_t *f, gm_plugin_lvgl_coord_t ls, gm_plugin_lvgl_coord_t lsp, gm_plugin_lvgl_coord_t mw, gm_plugin_lvgl_text_flag_t fl) { (void)t; (void)f; (void)ls; (void)lsp; (void)mw; (void)fl; s->x = 0; s->y = 0; }
static uint32_t text_get_next_line(const char *t, const gm_plugin_lvgl_font_t *f, gm_plugin_lvgl_coord_t ls, gm_plugin_lvgl_coord_t mw, gm_plugin_lvgl_coord_t *uw, gm_plugin_lvgl_text_flag_t fl) { (void)t; (void)f; (void)ls; (void)mw; (void)uw; (void)fl; return 0; }
static void label_set_selection_start(gm_plugin_lvgl_obj_t *o, uint32_t i) { (void)o; (void)i; }
static void label_set_selection_end(gm_plugin_lvgl_obj_t *o, uint32_t i) { (void)o; (void)i; }

static gm_plugin_lvgl_api_t lvgl;

/* ---- stub host ---- */
static uint32_t clock_ms;
static int exit_called;
static uint32_t monotonic_ms(void) { return clock_ms; }
static gm_plugin_result_t display_get_info(gm_plugin_display_info_t *i) { memset(i, 0, sizeof(*i)); i->width = 600; i->height = 350; i->refresh_hz = 30; i->pixel_format = GM_PLUGIN_PIXEL_GRAY_4; return GM_PLUGIN_OK; }
static void app_exit(void) { exit_called = 1; }
static void host_log(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
static gm_plugin_libc_extension_api_t libc_api;
static gm_plugin_result_t extension_get(gm_plugin_extension_id_t id, const void **api) { if (id != GM_PLUGIN_EXTENSION_LIBC) return GM_PLUGIN_ENOTSUP; *api = &libc_api; return GM_PLUGIN_OK; }

static gm_plugin_host_api_t host;
static gm_plugin_descriptor_t desc;

/* Pull the plugin in directly so its statics are visible for assertions. */
#include "../x3timer/x3timer.c"

/* ---- driver helpers ---- */
static void tick(uint32_t ms) {
    uint32_t end = clock_ms + ms;
    while (clock_ms < end) { uint32_t step = end - clock_ms < 33 ? end - clock_ms : 33; clock_ms += step; desc.on_loop(desc.context, step); }
}
static void primary(gm_plugin_button_action_t a) { gm_plugin_event_t e; memset(&e, 0, sizeof(e)); e.struct_size = sizeof(e); e.type = GM_PLUGIN_EVENT_BUTTON; e.data.button.button = GM_PLUGIN_BUTTON_PRIMARY; e.data.button.action = a; desc.on_event(desc.context, &e); }
static void trigger(gm_plugin_button_t b) { gm_plugin_event_t e; memset(&e, 0, sizeof(e)); e.struct_size = sizeof(e); e.type = GM_PLUGIN_EVENT_BUTTON; e.data.button.button = b; e.data.button.action = GM_PLUGIN_BUTTON_ACTION_TRIGGER; desc.on_event(desc.context, &e); }
static void nod(void) { gm_plugin_event_t e; memset(&e, 0, sizeof(e)); e.struct_size = sizeof(e); e.type = GM_PLUGIN_EVENT_IMU_GESTURE; e.data.imu_gesture.gesture = GM_PLUGIN_IMU_GESTURE_NOD; e.data.imu_gesture.active = true; desc.on_event(desc.context, &e); }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *overline(void) { return ((obj_t *)app.overline)->text; }
static const char *caption(void) { return ((obj_t *)app.caption)->text; }

int main(void)
{
    memset(&lvgl, 0, sizeof(lvgl));
    lvgl.struct_size = sizeof(lvgl); lvgl.api_version = GM_PLUGIN_LVGL_API_MIN_VERSION;
    lvgl.root_get = root_get; lvgl.obj_create = obj_create; lvgl.obj_delete = obj_delete; lvgl.obj_clean = obj_clean;
    lvgl.obj_set_pos = obj_set_pos; lvgl.obj_set_size = obj_set_size; lvgl.obj_align = obj_align;
    lvgl.obj_add_flag = obj_add_flag; lvgl.obj_clear_flag = obj_clear_flag; lvgl.obj_invalidate = obj_invalidate;
    lvgl.obj_get_width = obj_get_width; lvgl.obj_get_height = obj_get_height; lvgl.style_set = style_set;
    lvgl.label_create = label_create; lvgl.label_set_text = label_set_text; lvgl.label_set_long_mode = label_set_long_mode;
    lvgl.arc_create = arc_create; lvgl.arc_set_range = arc_set_range; lvgl.arc_set_value = arc_set_value;
    lvgl.line_create = line_create; lvgl.line_set_points = line_set_points;
    lvgl.font_get_line_height = font_get_line_height; lvgl.text_get_size = text_get_size; lvgl.text_get_next_line = text_get_next_line;
    lvgl.label_set_selection_start = label_set_selection_start; lvgl.label_set_selection_end = label_set_selection_end;

    memset(&libc_api, 0, sizeof(libc_api));

    libc_api.snprintf = snprintf; libc_api.vsnprintf = vsnprintf; libc_api.memset = memset; libc_api.memcpy = memcpy;

    memset(&host, 0, sizeof(host));
    host.struct_size = sizeof(host); host.abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    host.capabilities = GM_PLUGIN_CAP_BUTTON | GM_PLUGIN_CAP_IMU_EVENTS;
    host.monotonic_ms = monotonic_ms; host.display_get_info = display_get_info; host.app_exit = app_exit;
    host.log = host_log; host.extension_get = extension_get; host.graphics.lvgl = &lvgl;

    memset(&desc, 0, sizeof(desc)); desc.struct_size = sizeof(desc);
    clock_ms = 1000;
    CHECK(gm_plugin_entry(&host, &desc) == GM_PLUGIN_OK, "entry");
    CHECK(desc.on_start(desc.context) == GM_PLUGIN_OK, "start");
    printf("objects created: %d\n", obj_count);

    /* --- Pomodoro --- */
    CHECK(app.state == STATE_IDLE && app.phase == PHASE_FOCUS, "idle focus");
    CHECK(!((obj_t *)app.help1)->hidden, "help visible when idle");
    CHECK(strcmp(overline(), "POMODORO  -  FOCUS") == 0, "overline '%s'", overline());
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    CHECK(app.state == STATE_RUNNING, "running");
    tick(3000);
    CHECK(!app.compact_shown, "not compact before 5s");
    tick(2500);
    CHECK(app.compact_shown, "auto-minimized after 5s idle");
    CHECK(strcmp(overline(), "FOCUS") == 0, "compact overline '%s'", overline());
    nod();                                   /* head motion is ignored mid-session */
    CHECK(app.phase_ms == FOCUS_MS, "nod ignored while running");
    primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE); /* extend +10 min, also expands */
    CHECK(app.phase_ms == FOCUS_MS + EXTEND_MS, "extended");
    CHECK(!app.compact_shown, "expanded on interaction");
    tick(FOCUS_MS + EXTEND_MS);              /* run through the whole block */
    CHECK(app.phase == PHASE_BREAK && app.pomodoros == 1, "break after focus (phase=%d pom=%u)", app.phase, app.pomodoros);
    tick(BREAK_MS + 100);
    CHECK(app.phase == PHASE_FOCUS, "back to focus");
    /* three more pomodoros -> long break */
    { int i; for (i = 0; i < 3; ++i) { tick(FOCUS_MS + 50); if (i < 2) tick(BREAK_MS + 50); } }
    CHECK(app.pomodoros == 4 && app.phase == PHASE_LONG_BREAK, "long break after 4 (pom=%u phase=%d)", app.pomodoros, app.phase);
    printf("caption now: '%s'\n", caption());

    /* pause: colon blinks, state paused */
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    CHECK(app.state == STATE_PAUSED, "paused");
    { uint32_t before = app.elapsed_ms; tick(2000); CHECK(app.elapsed_ms == before, "no progress while paused"); }
    { uint16_t pom = app.pomodoros; primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE);
      CHECK(app.state == STATE_IDLE && app.pomodoros == pom, "double-click while paused resets");
      primary(GM_PLUGIN_BUTTON_ACTION_SINGLE); tick(1000); primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
      CHECK(app.state == STATE_PAUSED, "paused again"); }
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    CHECK(app.state == STATE_RUNNING, "resumed");

    /* switch guard: first long-press only arms, second confirms */
    primary(GM_PLUGIN_BUTTON_ACTION_LONG);
    CHECK(app.program == PROGRAM_POMODORO && app.pending_dir == 1, "armed, not switched");
    CHECK(!((obj_t *)app.prompt)->hidden, "prompt visible");
    tick(6000);
    CHECK(app.pending_dir == 0 && app.program == PROGRAM_POMODORO, "guard expired");
    primary(GM_PLUGIN_BUTTON_ACTION_LONG); tick(500); primary(GM_PLUGIN_BUTTON_ACTION_LONG);
    CHECK(app.program == PROGRAM_HIIT && app.state == STATE_IDLE, "switched to HIIT");

    /* --- HIIT: 10s prep, 8 x (20 work + 10 rest), last round has no rest --- */
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    tick(PREPARE_MS + 10);
    CHECK(app.phase == PHASE_WORK && app.round == 1, "round 1 work");
    CHECK(strcmp(caption(), "ROUND 1 / 8") == 0, "caption '%s'", caption());
    tick(HIIT_WORK_MS);
    CHECK(app.phase == PHASE_REST && app.round == 1, "rest 1");
    trigger(GM_PLUGIN_BUTTON_UP);            /* skip rest */
    CHECK(app.phase == PHASE_WORK && app.round == 2, "skipped to round 2");
    { int r; for (r = 2; r < 8; ++r) { tick(HIIT_WORK_MS + HIIT_REST_MS); } }
    CHECK(app.phase == PHASE_WORK && app.round == 8, "round 8 (round=%u phase=%d)", app.round, app.phase);
    tick(HIIT_WORK_MS + 10);
    CHECK(app.state == STATE_DONE, "HIIT done");
    CHECK(strcmp(caption(), "COMPLETE") == 0, "done caption '%s'", caption());
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE); /* from DONE -> reset to idle */
    CHECK(app.state == STATE_IDLE && app.phase == PHASE_PREPARE, "reset from done");

    /* --- EMOM --- */
    trigger(GM_PLUGIN_BUTTON_RIGHT);
    CHECK(app.program == PROGRAM_EMOM, "EMOM");
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    tick(PREPARE_MS + 10);
    CHECK(app.round == 1 && strcmp(overline(), "EMOM  -  MIN 1") == 0, "emom min 1 '%s'", overline());
    tick(MIN_MS * 9 + 10);
    CHECK(app.round == 10, "min 10 (round=%u)", app.round);
    tick(MIN_MS);
    CHECK(app.state == STATE_DONE, "EMOM done");
    primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE);

    /* --- AMRAP: count up, laps --- */
    trigger(GM_PLUGIN_BUTTON_RIGHT);
    CHECK(app.program == PROGRAM_AMRAP, "AMRAP");
    primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
    tick(PREPARE_MS + 10);
    CHECK(app.phase == PHASE_AMRAP && app.count_up, "amrap counting up");
    tick(90000);
    nod();
    CHECK(app.laps == 0, "nod does not count a lap");
    primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE); primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE);
    CHECK(app.laps == 2 && strcmp(caption(), "LAP 2") == 0, "laps '%s'", caption());
    CHECK(remaining_ms() >= 90000 && remaining_ms() < 91000, "count-up shows elapsed (%u)", remaining_ms());
    /* suspend/resume credits the gap */
    desc.on_suspend(desc.context);
    clock_ms += 60000;
    desc.on_resume(desc.context);
    tick(33);
    CHECK(remaining_ms() >= 150000, "resume credited 60s (%u)", remaining_ms());
    tick(AMRAP_MS);
    CHECK(app.state == STATE_DONE, "AMRAP done");

    /* wrap-around and exit */
    trigger(GM_PLUGIN_BUTTON_RIGHT);
    CHECK(app.program == PROGRAM_POMODORO, "wrapped to pomodoro");
    { gm_plugin_event_t e; memset(&e, 0, sizeof(e)); e.struct_size = sizeof(e); e.type = GM_PLUGIN_EVENT_IMU_GESTURE;
      e.data.imu_gesture.gesture = GM_PLUGIN_IMU_GESTURE_RIGHT; e.data.imu_gesture.active = true;
      desc.on_event(desc.context, &e);
      CHECK(app.program == PROGRAM_HIIT, "head right browses when idle");
      primary(GM_PLUGIN_BUTTON_ACTION_SINGLE);
      desc.on_event(desc.context, &e);
      CHECK(app.program == PROGRAM_HIIT && app.state == STATE_RUNNING, "head right ignored while running");
      primary(GM_PLUGIN_BUTTON_ACTION_SINGLE); primary(GM_PLUGIN_BUTTON_ACTION_DOUBLE);
      trigger(GM_PLUGIN_BUTTON_LEFT);
      CHECK(app.program == PROGRAM_POMODORO, "back to pomodoro"); }
    trigger(GM_PLUGIN_BUTTON_BACK);
    CHECK(exit_called, "exit");

    desc.on_stop(desc.context);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
