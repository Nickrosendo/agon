/*
 * agon-window: the simulation in a window. One function, app_frame, does
 * one frame; platform_run calls it. There is no loop here, because a browser
 * has to own the loop.
 */
#include <inttypes.h> /* PRIx64 */
#include <stdio.h>    /* snprintf, printf, fputs */
#include <stdlib.h>   /* abs */
#include <string.h>   /* strcmp */
#include <time.h>     /* time */

#include "app.h"
#include "branch.h"
#include "camera.h"
#include "canvas.h"
#include "cells.h"
#include "cells_canvas.h"
#include "family.h"
#include "font.h"
#include "inspect.h"
#include "lab.h"
#include "lifepage.h"
#include "notices.h"
#include "options.h"
#include "palette.h"
#include "panel.h" /* panel_format_count */
#include "platform.h"
#include "rng.h"
#include "sandbox.h"
#include "save.h"
#include "scene.h"
#include "settings.h"
#include "sim.h"
#include "years.h"

#define FRAMES_PER_SECOND 30
#define WINDOW_W 1280
#define WINDOW_H 720
/* A press that moves further than this, in pixels, is a drag, not a click. */
#define CLICK_SLOP 4
#define DEFAULT_WORLD_W 256
#define DEFAULT_WORLD_H 144
#define NOTICE_FRAMES (2 * FRAMES_PER_SECOND)
/* A notice (notices.h) stays longer, so there is time to press j. */
#define RARE_NOTICE_FRAMES (10 * FRAMES_PER_SECOND)
/* How many notices the e list shows, newest first: one per number key. */
#define NOTICE_LIST_ROWS 9
/* How many screen pixels an arrow key pans by. */
#define ARROW_PAN 48

static const char WINDOW_USAGE[] =
    "usage: agon-window [options]\n"
    "  --seed N          the world's seed\n"
    "  --size WxH        world size in cells (default 256x144)\n"
    "  --speed N         ticks per second (default 2)\n"
    "  --no-floor        turn off a floor a settings file set\n"
    "  --config PATH     read settings from a file\n"
    "  --save PATH       write the world there on quit and on the s key\n"
    "  --load PATH       resume a saved world\n"
    "  --zoom NAME       start at close, normal, far or world\n"
    "  --checksum        print a fingerprint of the world on leaving\n"
    "  --frames N        stop after N frames, without waiting between them\n"
    "  --screenshot PATH write the last frame there as a PPM image\n"
    "  --paused          start paused, as if space were pressed first\n"
    "  --no-art          start with the animals drawn from the skeleton, not their\n"
    "                    drawn frames (the a key switches)\n"
    "  --sandbox         start in Sandbox mode, with the tools (the m key switches)\n"
    "  --keys TEXT       with --frames: press these keys, one a frame, in the\n"
    "                    last frames (\"l\" shows the life story page, \"b\" the\n"
    "                    species lab)\n"
    "  -h, --help        this text\n"
    "  --version         the version\n"
    "keys: space pause, +/- speed, r new seed, s save, q quit, arrows move,\n"
    "      [ and ] zoom, Esc lets go of a creature, n new world after game over,\n"
    "      l opens the life story of the followed creature (or of the most\n"
    "      notable life) and closes it again, j follows who the newest notice\n"
    "      is about (or, with the story open, the creature it tells of).\n"
    "      e lists the notices (rare bodies, giants, tactics first seen, big\n"
    "      families gone, a given trait spreading or gone); 1 to 9 or a\n"
    "      click on one follows who it is about.\n"
    "      b opens the species lab, every animal side by side, and closes it\n"
    "      again; in it 1 to 6 choose the action shown (walk, run, rest,\n"
    "      graze, bite, sleep), and p puts every painted part (plates, a\n"
    "      horn, a fin) on its species.\n"
    "      a switches the animals between their drawn frames and the skeleton.\n"
    "      m switches between Documentary (watching) and Sandbox. In Sandbox,\n"
    "      g gives the followed creature a trait (1 to 7 pick it, , and . set\n"
    "      its value, Enter gives), w starts a weather event for a season and\n"
    "      k skips 1, 5 or 20 years ahead (Esc stops), then says what happened.\n"
    "      x splits the world in two from now, Tab shows the other branch,\n"
    "      and x twice keeps the one shown. A skip moves both branches.\n"
    "mouse: wheel zooms, dragging moves, clicking a creature follows it,\n"
    "       clicking bare ground lets go. On the panel, clicking \"top hunter\",\n"
    "       \"oldest\" or the largest family's body follows that creature.\n";

/* Everything a frame needs, passed through platform_run as a void *. */
typedef struct {
    Sim sim;
    Scene scene;
    Families families;
    Canvas canvas;
    Camera camera;
    Layout layout;
    Inspect inspect;
    LifePage page;     /* the L page, drawn over the world while open */
    bool lab_open;     /* the species lab (b), drawn over the world while open */
    int lab_action;    /* the ACTION_ value it shows */
    bool lab_parts;    /* p: the lab shows every painted part on its species */
    /* Rare things as they happen (notices.h): for the world shown, and for
     * the other branch while split. The screen's, not the world's. */
    Notices notices, other_notices;
    bool notices_open; /* the e list, drawn over the world */
    const Options *opt;
    Rng seed_source;
    int speed;
    int ticks_due; /* ticks owed, in thirtieths, as in the terminal program */
    bool paused, running, failed;
    bool redraw;    /* while paused: something happened that would show */
    int last_ambient; /* the scenery's step in the last frame drawn, paused or running */
    bool pressed;  /* the left button went down over the world */
    bool dragging; /* and has moved far enough not to be a click */
    int press_x, press_y;
    int mouse_x, mouse_y;
    long frames_done;
    int notice_frames;
    char notice[256];
    uint64_t body_ms;  /* a clock that stops while paused */
    uint64_t last_ms;
    /* A new world's ancestors evolving, shown fast-forwarded (app.h), and how
     * many years each new world gets. */
    Prelude prelude;
    int prelude_years;
    /* The mode, Documentary or Sandbox, and the Sandbox menus (sandbox.h).
     * The screen's, not the world's: never saved. */
    Sandbox sandbox;
    /* A world split in two (branch.h): a->sim is always the one on screen,
     * branches.other the one behind it. keep_asked: x was pressed once while
     * split, and a second x keeps the world shown. */
    Branches branches;
    bool keep_asked;
} App;

static void apply_window_size(App *a, int w, int h)
{
    /* If memory runs out the old canvas stays, and is simply stretched. */
    if (canvas_resize(&a->canvas, w, h)) {
        scene_layout(&a->layout, w, h);
        camera_resize(&a->camera, a->layout.view_w, a->layout.view_h);
    }
}

/* True when this window pixel is on the world's part of the window. */
static bool over_view(const App *a, int x, int y)
{
    const Layout *l = &a->layout;
    return x >= l->view_x && x < l->view_x + l->view_w && y >= l->view_y &&
           y < l->view_y + l->view_h;
}

/* Panning means looking elsewhere, which following would undo next frame. */
static void pan(App *a, int dx, int dy)
{
    inspect_stop(&a->inspect);
    camera_pan(&a->camera, dx, dy);
}

static int notice_box(const App *a, int *x, int *y, int *w, int *h);
static void follow_notice(App *a, const Notice *x);

static void click(App *a, int x, int y)
{
    if (a->notices_open) {
        int bx, by, bw, bh;
        int rows = notice_box(a, &bx, &by, &bw, &bh);
        /* Row r of the list starts below the heading and the top margin. */
        int r = (y - by - FONT_H / 2) / FONT_H - 1;
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (r >= 0 && r < rows) {
                follow_notice(a, notices_get(&a->notices, (uint32_t)r));
            }
            return; /* a click on the box never reaches the world behind */
        }
    }
    int sx = x - a->layout.view_x;
    int sy = y - a->layout.view_y;
    if (!camera_on_world(&a->camera, sx, sy)) {
        inspect_stop(&a->inspect);
        return;
    }
    float wx, wy;
    camera_to_world(&a->camera, sx, sy, &wx, &wy);
    scene_click(&a->inspect, &a->sim, wx, wy);
}

/* A click on the panel: follow who it names. While following, the panel lists that
 * creature instead, so these rows are not there to click. */
static void click_panel(App *a, int x, int y)
{
    if (a->inspect.following) {
        return;
    }
    int slot;
    switch (scene_panel_pick(&a->layout, x, y)) {
    case PANEL_PICK_TOP_HUNTER:
        slot = inspect_highest_level(&a->sim);
        break;
    case PANEL_PICK_OLDEST:
        slot = inspect_oldest(&a->sim);
        break;
    case PANEL_PICK_LARGEST_FAMILY:
        slot = inspect_largest_of_family(&a->sim,
                                         family_largest(&a->families, &a->sim));
        break;
    default:
        return;
    }
    if (slot >= 0) {
        inspect_follow(&a->inspect, &a->sim, slot);
    } else {
        snprintf(a->notice, sizeof(a->notice), "nobody to follow");
        a->notice_frames = NOTICE_FRAMES;
    }
}

static void new_world(App *a)
{
    if (!sim_reseed(&a->sim, rng_next(&a->seed_source))) {
        a->failed = true; /* the brain could not be reopened */
        a->running = false;
        return;
    }
    scene_reset(&a->scene);
    inspect_stop(&a->inspect);
    /* Ids start again in a new world: an old id would name a stranger. */
    life_page_close(&a->page);
    notices_reset(&a->notices);
    a->notices_open = false;
    family_reset(&a->families);
    a->ticks_due = 0;
    /* A menu or a summary was about the old world. */
    a->sandbox.menu = SANDBOX_MENU_NONE;
    skip_free(&a->sandbox.skip);
    /* r replaces the world on screen; its other branch goes with it. */
    branch_free(&a->branches);
    a->keep_asked = false;
    app_prelude_start(&a->prelude, &a->sim, a->prelude_years);
    if (a->opt->frames > 0) {
        app_prelude_finish(&a->prelude, &a->sim); /* as at the start */
    }
}

static void say(App *a, const char *text, int frames)
{
    snprintf(a->notice, sizeof(a->notice), "%s", text);
    a->notice_frames = frames;
}

/* l: the followed creature's story, else the most notable one; again, closed. */
static void toggle_page(App *a)
{
    if (a->page.open) {
        life_page_close(&a->page);
    } else if (a->inspect.following) {
        life_page_open(&a->page, &a->sim, a->inspect.id);
    } else if (!life_page_open_most_notable(&a->page, &a->sim)) {
        say(a, "no life recorded yet", NOTICE_FRAMES);
    }
}

/* The newest notice that names a creature, or NULL. */
static const Notice *newest_with_creature(const Notices *n)
{
    for (uint32_t back = 0; back < NOTICES_KEPT; back++) {
        const Notice *x = notices_get(n, back);
        if (x == NULL || x->creature != NOTICE_NOBODY) {
            return x;
        }
    }
    return NULL;
}

/* Follows who notice x is about, or says why not. */
static void follow_notice(App *a, const Notice *x)
{
    int slot = x != NULL && x->creature != NOTICE_NOBODY
                   ? life_find_slot(&a->sim, x->creature) : -1;
    if (slot >= 0) {
        inspect_follow(&a->inspect, &a->sim, slot);
        a->notices_open = false;
    } else {
        say(a, x == NULL ? "no notice there"
               : x->creature == NOTICE_NOBODY ? "that notice is about nobody to follow"
                                  : "that creature is dead",
            NOTICE_FRAMES);
    }
}

/* j: follow the creature the open page tells of, else the newest notice's. */
static void jump(App *a)
{
    if (a->page.open) {
        int slot = a->page.alive ? life_find_slot(&a->sim, a->page.id) : -1;
        if (slot >= 0) {
            inspect_follow(&a->inspect, &a->sim, slot);
        } else {
            say(a, "that creature is dead", NOTICE_FRAMES);
        }
        return;
    }
    follow_notice(a, newest_with_creature(&a->notices));
}

/* The e list's box: where draw_notices puts it, and where a click finds a row.
 * Returns the number of rows, 0 when there is nothing to show or no room. */
static int notice_box(const App *a, int *x, int *y, int *w, int *h)
{
    const Layout *l = &a->layout;
    int rows = (int)(a->notices.total < NOTICE_LIST_ROWS ? a->notices.total
                                                         : NOTICE_LIST_ROWS);
    *x = l->view_x + FONT_W;
    *y = l->view_y + FONT_H / 2;
    *w = l->view_w - 2 * FONT_W;
    /* A heading row, the rows, and half a row of margin above and below. */
    *h = (rows + 2) * FONT_H;
    if (*w < FONT_W * 20 || *h > l->view_h) {
        return 0;
    }
    return rows;
}

/* Puts the other branch on screen. What the window remembers about the old
 * world's bodies and families is about different creatures now, so it starts
 * again, as for a new world; the camera keeps following the same id, which in
 * a branch is the same creature, if it is alive on this side too. */
static void switch_branch(App *a)
{
    if (!a->branches.split) {
        say(a, "one world only: x splits it in two", NOTICE_FRAMES);
        return;
    }
    if (!branch_switch(&a->branches, &a->sim)) {
        say(a, "the other branch is still catching up", NOTICE_FRAMES);
        return;
    }
    scene_reset(&a->scene);
    family_reset(&a->families);
    life_page_close(&a->page);
    /* Each branch keeps its own list. A struct assignment copies every byte,
     * the arrays inside included, and Notices points at nothing, so this is
     * a whole swap. */
    Notices shown = a->notices;
    a->notices = a->other_notices;
    a->other_notices = shown;
    say(a, a->branches.shown == 0 ? "branch A" : "branch B", NOTICE_FRAMES);
}

/* x: splits the world in two; while split, x twice keeps the one shown. */
static void branch_key(App *a)
{
    char err[160];
    if (!a->branches.split) {
        if (branch_make(&a->branches, &a->sim, err, sizeof(err))) {
            a->other_notices = a->notices; /* the same past on both sides */
            say(a, "split in two: this is A, Tab shows B; "
                   "try something in one", 2 * NOTICE_FRAMES);
        } else {
            say(a, err, NOTICE_FRAMES);
        }
    } else if (!a->keep_asked) {
        a->keep_asked = true;
        say(a, "x again keeps this branch and drops the other", NOTICE_FRAMES);
    } else {
        branch_keep_shown(&a->branches);
        a->keep_asked = false;
        say(a, "kept this branch; one world again", NOTICE_FRAMES);
    }
}

static void handle_key(App *a, int key)
{
    /* While the ancestors evolve, any key but q ends that where it got to,
     * and does nothing else: it was pressed to stop the waiting. */
    if (a->prelude.active && key != 'q') {
        app_prelude_stop(&a->prelude);
        a->notice[0] = '\0';
        a->redraw = true;
        return;
    }
    /* The open notice list takes the number keys: each follows a row. */
    if (a->notices_open && key >= '1' && key <= '9') {
        follow_notice(a, notices_get(&a->notices, (uint32_t)(key - '1')));
        a->redraw = true;
        return;
    }
    /* The mode and the Sandbox tools first: an open menu takes the number
     * keys, which the lab would otherwise read. q always reaches the switch. */
    if (key != 'q') {
        char line[256] = "";
        const Creature *followed = inspect_creature(&a->inspect, &a->sim);
        if (sandbox_key(&a->sandbox, &a->sim, key, followed, platform_now_ms(),
                        line, sizeof(line)) == SANDBOX_TAKEN) {
            if (line[0] != '\0') {
                say(a, line, 2 * NOTICE_FRAMES);
            }
            a->redraw = true;
            return;
        }
    }
    /* Any key but a second x takes back a first x's question. */
    if (key != 'x' && key != 'X') {
        a->keep_asked = false;
    }
    /* Branching is a Sandbox tool; Documentary never changes a world. */
    if (a->sandbox.on && (key == 'x' || key == 'X')) {
        branch_key(a);
        a->redraw = true;
        return;
    }
    if (a->sandbox.on && key == '\t') {
        switch_branch(a);
        a->redraw = true;
        return;
    }
    switch (key) {
    case 'q':
        if (!PLATFORM_IS_PAGE) { /* stopping a page would only freeze it */
            a->running = false;
        }
        break;
    case ' ':
        a->paused = !a->paused;
        break;
    case '+':
    case '=': /* the same key as + on most keyboards, without shift */
        if (a->speed < APP_SPEED_COUNT - 1) {
            a->speed++;
        }
        break;
    case '-':
        if (a->speed > 0) {
            a->speed--;
        }
        break;
    case 'r':
        new_world(a);
        break;
    case 'n': /* only here, where it cannot end a living world by a slip */
        if (sim_is_over(&a->sim)) {
            new_world(a);
        }
        break;
    case 's':
        if (!PLATFORM_IS_PAGE) {
            app_save_now(&a->sim, a->opt->save_path, a->notice,
                         sizeof(a->notice));
            a->notice_frames = NOTICE_FRAMES;
        }
        break;
    case ']':
        camera_zoom(&a->camera, 1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case '[':
        camera_zoom(&a->camera, -1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case KEY_LEFT: pan(a, -ARROW_PAN, 0); break;
    case KEY_RIGHT: pan(a, ARROW_PAN, 0); break;
    case KEY_UP: pan(a, 0, -ARROW_PAN); break;
    case KEY_DOWN: pan(a, 0, ARROW_PAN); break;
    case 'l':
    case 'L':
        toggle_page(a);
        break;
    case 'j':
        jump(a);
        break;
    case 'e':
    case 'E':
        a->notices_open = !a->notices_open;
        if (a->notices_open && a->notices.total == 0) {
            a->notices_open = false;
            say(a, "no notices yet", NOTICE_FRAMES);
        }
        break;
    case 'b':
    case 'B':
        a->lab_open = !a->lab_open;
        break;
    case 'p':
    case 'P':
        if (a->lab_open) {
            a->lab_parts = !a->lab_parts;
        }
        break;
    case 'a':
    case 'A':
        /* The drawn frames (art.h) on or off: the pictures kept are
         * forgotten, and the next frame draws them the other way. */
        sprite_cache_set_art(&a->scene.sprites, !a->scene.sprites.art);
        a->redraw = true;
        break;
    case '1': case '2': case '3': case '4': case '5': case '6':
        /* The action keys only mean something on the lab page. Characters are
         * numbers in C, so key - '0' is the key's number: '3' - '0' is 3. */
        if (a->lab_open) {
            a->lab_action = lab_action_of_key(key - '0');
        }
        break;
    case KEY_ESCAPE:
        /* The pages first, so Esc never lets go of a creature behind them. */
        if (a->lab_open) {
            a->lab_open = false;
        } else if (a->notices_open) {
            a->notices_open = false;
        } else if (a->page.open) {
            life_page_close(&a->page);
        } else {
            inspect_stop(&a->inspect);
        }
        break;
    default: break;
    }
}

static void handle_event(App *a, const Event *e)
{
    a->redraw = true;
    switch (e->kind) {
    case EV_QUIT:
        a->running = false;
        break;
    case EV_KEY:
        handle_key(a, e->key);
        break;
    case EV_WHEEL:
        if (over_view(a, e->x, e->y)) {
            /* The camera counts from the view's own corner. */
            camera_zoom(&a->camera, e->delta, e->x - a->layout.view_x,
                        e->y - a->layout.view_y);
        }
        break;
    case EV_DOWN:
        a->pressed = over_view(a, e->x, e->y);
        if (!a->pressed) {
            click_panel(a, e->x, e->y);
        }
        a->dragging = false;
        a->press_x = a->mouse_x = e->x;
        a->press_y = a->mouse_y = e->y;
        break;
    case EV_UP:
        if (a->pressed && !a->dragging) {
            click(a, e->x, e->y);
        }
        a->pressed = false;
        a->dragging = false;
        break;
    case EV_MOVE:
        if (a->pressed && !a->dragging &&
            (abs(e->x - a->press_x) > CLICK_SLOP ||
             abs(e->y - a->press_y) > CLICK_SLOP)) {
            a->dragging = true;
        }
        if (a->dragging) {
            /* Dragging pulls the world along: the view moves the other way. */
            pan(a, a->mouse_x - e->x, a->mouse_y - e->y);
            a->mouse_x = e->x;
            a->mouse_y = e->y;
        }
        break;
    case EV_RESIZE:
        apply_window_size(a, e->w, e->h);
        break;
    }
}

static void draw_title(App *a, const Creature *followed)
{
    char year[32];
    panel_format_count(year, sizeof(year),
                       years_whole(a->sim.tick, a->sim.set.ticks_per_year));
    char who[48] = "";
    if (followed != NULL) {
        char name[16];
        name_from_id(followed->id, name, sizeof(name));
        snprintf(who, sizeof(who), " | following %s #%u", name,
                 (unsigned)followed->id);
    }
    char weather[32] = "";
    if (weather_active(&a->sim.weather, a->sim.tick)) {
        snprintf(weather, sizeof(weather), " | %s",
                 weather_name(a->sim.weather.kind));
    }
    char branch[160] = "";
    if (a->branches.split) {
        branch[0] = ' ';
        branch[1] = '|';
        branch[2] = ' ';
        /* Written after the three characters: out points into the middle. */
        branch_status_text(&a->branches, &a->sim, branch + 3, sizeof(branch) - 3);
    }
    char text[600];
    snprintf(text, sizeof(text),
             " agon %s | seed 0x%" PRIx64 " | year %s, %s%s | creatures %d | %d/s%s%s%s%s%s",
             a->sandbox.on ? "Sandbox" : "Documentary", a->sim.seed, year,
             years_season(a->sim.tick, a->sim.set.ticks_per_year), weather,
             a->sim.pool.count, APP_SPEEDS[a->speed],
             sim_is_over(&a->sim) ? " | game over" : a->paused ? " | paused" : "",
             branch, who,
             a->notice[0] != '\0' ? " | " : "", a->notice);
    cells_canvas_set(&a->canvas, 0, 0);
    render_text(0, 0, text, a->canvas.w / FONT_W + 1);
}

static void draw_help(App *a)
{
    cells_canvas_set(&a->canvas, 0, a->layout.help_y);
    /* Adjacent strings are one string, so each of these is one piece of text. */
    const char *help =
        PLATFORM_IS_PAGE
            ? " space pause | +/- speed | [ ] zoom | click follow | Esc let go"
              " | l life story | j jump | e notices | b lab | r new world"
            : " space pause | +/- speed | [ ] zoom | click follow | Esc let go"
              " | l life story | j jump | e notices | b lab | r new world | s save | q quit";
    /* Sandbox has four keys more, so it drops the ones --help still lists. */
    if (a->sandbox.on) {
        help = " space pause | +/- speed | [ ] zoom | click follow | l story";
    }
    char text[300];
    /* sandbox_help starts with a space and the mode's name: "| Sandbox | ...". */
    snprintf(text, sizeof(text), "%s |%s", help, sandbox_help(&a->sandbox));
    render_text(0, 0, text, a->canvas.w / FONT_W + 1);
}

/* The enlarged body, with a caption above it. */
static void draw_enlarged(App *a, const Creature *followed, uint32_t lineage,
                          const FamilySample *newest)
{
    const Layout *l = &a->layout;
    char caption[32] = " no family yet";
    if (followed != NULL) {
        snprintf(caption, sizeof(caption), " creature #%u", (unsigned)followed->id);
    } else if (lineage != FAMILY_NONE) {
        snprintf(caption, sizeof(caption), " largest: #%u", (unsigned)lineage);
    }
    cells_canvas_set(&a->canvas, l->panel_x, l->family_y);
    render_text(0, 0, caption, PANEL_COLS);
    /* In the middle of the panel's width, below the caption. */
    int x = l->panel_x + (SCENE_PANEL_W - SCENE_ENLARGED_W) / 2;
    int y = l->family_y + FONT_H;
    if (followed != NULL) {
        scene_draw_followed(&a->scene, &a->canvas, &a->sim, followed, a->body_ms,
                            x, y);
    } else if (newest != NULL) {
        scene_draw_sample(&a->scene, &a->canvas, newest, lineage, a->body_ms, x, y);
    }
}

/* The family's bodies over time, with a caption above them. A family outside the
 * recorded ones has no strip, and the caption says so. */
static void draw_strip(App *a, uint32_t lineage, int samples)
{
    const Layout *l = &a->layout;
    int y = l->family_y + (SCENE_ENLARGED_ROWS * FONT_H);
    cells_canvas_set(&a->canvas, l->panel_x, y);
    char caption[32];
    /* The longest id, 10 digits, would still only lose the end of the words. */
    snprintf(caption, sizeof(caption), samples == 0 ? " #%u: no record" : " family #%u",
             (unsigned)lineage);
    render_text(0, 0, lineage != FAMILY_NONE ? caption : "", PANEL_COLS);
    scene_draw_strip(&a->scene, &a->canvas, &a->families, lineage, l->panel_x,
                     y + FONT_H, SCENE_PANEL_W);
}

/* The bottom of the panel: the followed creature and its family, or else the
 * largest family and its newest body. */
static void draw_family(App *a, const Creature *followed)
{
    if (!a->layout.show_enlarged) {
        return;
    }
    uint32_t lineage = followed != NULL ? followed->lineage
                                        : family_largest(&a->families, &a->sim);
    FamilySample samples[FAMILY_STRIP];
    int n = family_strip(&a->families, lineage, samples);
    draw_enlarged(a, followed, lineage, n > 0 ? &samples[n - 1] : NULL);
    if (a->layout.show_strip) {
        draw_strip(a, lineage, n);
    }
}

static void draw_panel(App *a, const Creature *followed)
{
    const Layout *l = &a->layout;
    if (!l->has_panel) {
        return;
    }
    /* Rows are whole; this clears what is left below the last of them. */
    canvas_rect(&a->canvas, l->panel_x, l->panel_y, SCENE_PANEL_W, l->view_h,
                canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                           PALETTE_TEXT_BACKGROUND.b));
    cells_canvas_set(&a->canvas, l->panel_x, l->panel_y);
    if (followed != NULL) {
        panel_draw_inspect(&a->sim, &a->inspect, 0, 0, l->panel_rows);
    } else {
        panel_draw(&a->sim, 0, 0, l->panel_rows);
    }
    draw_family(a, followed);
}

/* The L page over the world's area, inset by a character all round, so the
 * world keeps showing (and running) at its edges. */
static void draw_page(App *a)
{
    const Layout *l = &a->layout;
    life_page_update(&a->page, &a->sim);
    int x = l->view_x + FONT_W, y = l->view_y + FONT_H / 2;
    int w = l->view_w - 2 * FONT_W, h = l->view_h - FONT_H;
    if (w < FONT_W * 20 || h < FONT_H * 4) {
        return; /* too small to read; the world shows instead */
    }
    canvas_rect(&a->canvas, x, y, w, h,
                canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                           PALETTE_TEXT_BACKGROUND.b));
    int cols = w / FONT_W;
    int rows = h / FONT_H;
    if (rows > LIFE_PAGE_MAX_LINES) {
        rows = LIFE_PAGE_MAX_LINES;
    }
    /* static: about 7 KB, kept off the stack, which is small in the web
     * build. Only this function uses it, one frame at a time. */
    static char lines[LIFE_PAGE_MAX_LINES][LIFE_PAGE_LINE_BYTES];
    int n = life_page_text(&a->page, a->sim.set.ticks_per_year, cols, lines,
                           rows);
    cells_canvas_set(&a->canvas, x, y);
    for (int r = 0; r < n; r++) {
        render_text(0, r, lines[r], cols);
    }
}

/* The open Sandbox menu, a skip's progress or its summary, in a box at the
 * top left of the world. */
static void draw_sandbox(App *a)
{
    static char lines[SKIP_LINES][SKIP_COLS]; /* static: off the small web stack */
    int n = sandbox_menu_lines(&a->sandbox, &a->sim, platform_now_ms(), lines,
                               SKIP_LINES);
    if (n == 0) {
        return;
    }
    const Layout *l = &a->layout;
    int cols = 0;
    for (int r = 0; r < n; r++) {
        int len = (int)strlen(lines[r]);
        cols = len > cols ? len : cols;
    }
    int w = (cols + 2) * FONT_W, h = (n + 1) * FONT_H;
    if (w > l->view_w) {
        w = l->view_w;
    }
    int x = l->view_x + FONT_W, y = l->view_y + FONT_H / 2;
    canvas_rect(&a->canvas, x, y, w, h,
                canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                           PALETTE_TEXT_BACKGROUND.b));
    cells_canvas_set(&a->canvas, x + FONT_W, y + FONT_H / 2);
    for (int r = 0; r < n; r++) {
        render_text(0, r, lines[r], w / FONT_W - 1);
    }
}

/* Too small for anything else: a minimised window lands here too. */
static void draw_too_small(App *a)
{
    canvas_fill(&a->canvas, canvas_rgb(0, 0, 0));
    cells_canvas_set(&a->canvas, 0, 0);
    render_too_small(a->canvas.w / FONT_W, a->canvas.h / FONT_H, SCENE_MIN_COLS,
                     SCENE_MIN_ROWS);
}

/* The creature being followed, moving the camera; a death here tells the user
 * and drops back to the ordinary panel. */
static const Creature *update_following(App *a, float fraction)
{
    bool was_following = a->inspect.following;
    const Creature *followed =
        scene_follow(&a->scene, &a->camera, &a->inspect, &a->sim, fraction);
    if (was_following && followed == NULL) {
        inspect_stop(&a->inspect);
        snprintf(a->notice, sizeof(a->notice), "the creature you followed died");
        a->notice_frames = NOTICE_FRAMES;
    }
    return followed;
}

/* Takes a look for new notices and puts the newest in the title line. While
 * the ancestors evolve, the list stays empty: its first look after them takes
 * the world as it is then as known, so their years are not news. */
static void update_notices(App *a)
{
    if (a->prelude.active) {
        notices_reset(&a->notices);
        return;
    }
    uint32_t before = a->notices.total;
    notices_check(&a->notices, &a->sim);
    uint32_t fresh = a->notices.total - before;
    if (fresh == 0) {
        return;
    }
    char line[NOTICE_TEXT + 48];
    const Notice *x = notices_newest(&a->notices);
    if (fresh > 1) {
        snprintf(line, sizeof(line), "%s (and %u more: e lists)", x->text,
                 (unsigned)(fresh - 1));
    } else {
        snprintf(line, sizeof(line), "%s%s", x->text,
                 x->creature != NOTICE_NOBODY ? " (j follows)" : "");
    }
    say(a, line, RARE_NOTICE_FRAMES);
    a->redraw = true;
}

/* The e list, newest first, numbered for the keys that follow. */
static void draw_notices(App *a)
{
    int x, y, w, h;
    int rows = notice_box(a, &x, &y, &w, &h);
    if (!a->notices_open || rows == 0) {
        return;
    }
    canvas_rect(&a->canvas, x, y, w, h,
                canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                           PALETTE_TEXT_BACKGROUND.b));
    int cols = w / FONT_W - 1;
    cells_canvas_set(&a->canvas, x + FONT_W, y + FONT_H / 2);
    render_text(0, 0, "Notices, newest first: 1-9 or a click follows, e or Esc closes",
                cols);
    for (int r = 0; r < rows; r++) {
        const Notice *n = notices_get(&a->notices, (uint32_t)r);
        char line[NOTICE_TEXT + 32];
        int year = years_whole(n->tick, a->sim.set.ticks_per_year) + 1;
        snprintf(line, sizeof(line), "%d  y%-4d %s%s", r + 1, year, n->text,
                 n->creature != NOTICE_NOBODY && life_find_slot(&a->sim, n->creature) < 0
                     ? " (dead)" : "");
        render_text(0, r + 1, line, cols);
    }
}

static void draw_frame(App *a, float fraction)
{
    const Creature *followed = update_following(a, fraction);
    if (a->layout.too_small) {
        draw_too_small(a);
        return;
    }
    const Layout *l = &a->layout;
    scene_draw_world(&a->scene, &a->canvas, &a->camera, &a->sim, fraction,
                     a->last_ms, a->body_ms, l->view_x, l->view_y);
    scene_draw_cursor(&a->scene, &a->canvas, &a->camera, &a->sim, followed,
                      fraction, l->view_x, l->view_y);
    scene_draw_names(&a->scene, &a->canvas, &a->camera, &a->sim, followed,
                     fraction, l->view_x, l->view_y);
    if (sim_is_over(&a->sim)) {
        scene_draw_game_over(&a->canvas, &a->sim,
                             PLATFORM_IS_PAGE ? "n new world"
                                              : "n new world | q quit",
                             l->view_x, l->view_y, l->view_w, l->view_h);
    }
    if (a->lab_open) {
        lab_draw(&a->scene.sprites, &a->canvas, a->lab_action, a->lab_parts, a->body_ms,
                 l->view_x, l->view_y, l->view_w, l->view_h);
    }
    if (a->page.open) {
        draw_page(a);
    }
    draw_sandbox(a);
    draw_notices(a);
    /* After the world: a body near the edge may have been drawn over these. */
    draw_title(a, followed);
    draw_help(a);
    draw_panel(a, followed);
}

static void advance_clocks(App *a)
{
    uint64_t now = platform_now_ms();
    if (!a->paused && !sim_is_over(&a->sim)) {
        a->body_ms += now - a->last_ms;
    }
    a->last_ms = now;
    if (a->notice_frames > 0 && --a->notice_frames == 0) {
        a->notice[0] = '\0';
        a->redraw = true;
    }
}

/*
 * Whether this frame looks different from the last. Bodies stop moving while
 * paused and only the water, grass and trees go on, and they step only every
 * fifth of a second while paused, so a paused window draws five times a second
 * instead of thirty. Events are still read every frame.
 */
static bool frame_needs_drawing(App *a)
{
    /* A dead world is as still as a paused one, so it costs as little. */
    bool still = a->paused || sim_is_over(&a->sim);
    /* Running, every frame is drawn anyway; the step only matters when still.
     * Switching between the two clocks can cost one extra frame, no more. */
    int ambient = still ? scene_paused_frame(a->last_ms)
                        : scene_ambient_frame(a->last_ms);
    bool needed = !still || a->redraw || ambient != a->last_ambient;
    if (needed) {
        a->last_ambient = ambient;
        a->redraw = false;
    }
    return needed;
}

/* --keys: in the last strlen(keys) frames of --frames, one key a frame, so the
 * frame the screenshot keeps comes after the last of them. */
static void pretend_keys(App *a)
{
    const char *keys = a->opt->keys;
    if (keys == NULL || a->opt->frames <= 0) {
        return;
    }
    /* strlen gives a size_t; the cast is safe for any text a shell passes. */
    long len = (long)strlen(keys);
    long k = a->frames_done - (a->opt->frames - 1 - len);
    if (k >= 0 && k < len) {
        handle_key(a, keys[k]);
        a->redraw = true;
    }
}

/* One frame. void * because platform_run knows nothing about App. */
static void app_frame(void *arg)
{
    App *a = arg; /* in C, void * converts to any pointer without a cast */
    Event e;
    while (platform_poll(&e)) {
        handle_event(a, &e);
    }
    pretend_keys(a);
    /* Before any ticks: a quit means the world stays as it was shown. */
    if (!a->running) {
        platform_stop();
        return;
    }
    if (a->prelude.active) {
        /* As many prelude ticks as fit in 28 ms of the frame's 33, so the
         * window and the keys still keep up; pausing does not hold it. */
        app_prelude_step(&a->prelude, &a->sim, platform_now_ms,
                         APP_PRELUDE_FRAME_MS);
        if (a->prelude.active) {
            app_prelude_text(&a->prelude, &a->sim, a->notice, sizeof(a->notice));
            a->notice_frames = 0; /* set again every frame */
        } else {
            a->notice[0] = '\0';
        }
        a->redraw = true;
    } else if (sandbox_skipping(&a->sandbox)) {
        /* A skip runs like the prelude, a slice a frame, paused or not. */
        /* Split, the other branch gets half the frame (below), so both
         * move ahead together and the skip covers both. */
        sandbox_skip_step(&a->sandbox, &a->sim, platform_now_ms,
                          a->branches.split ? APP_PRELUDE_FRAME_MS / 2
                                            : APP_PRELUDE_FRAME_MS);
        a->ticks_due = 0;
        a->redraw = true;
    } else if (!a->paused && !sim_is_over(&a->sim)) {
        a->ticks_due += APP_SPEEDS[a->speed];
        /* Checked on every tick: at high speed a frame runs several, and the
         * world may end during the first. */
        while (a->ticks_due >= FRAMES_PER_SECOND && !sim_is_over(&a->sim)) {
            sim_step(&a->sim);
            a->ticks_due -= FRAMES_PER_SECOND;
        }
        if (sim_is_over(&a->sim)) {
            a->ticks_due = 0;  /* nothing is on its way any more */
            a->redraw = true;  /* the frame that shows the screen */
        }
    }
    /* The world behind the one on screen keeps level with it: the ticks the
     * shown world ran this frame, or as many as half a frame allows. */
    if (!branch_caught_up(&a->branches, &a->sim)) {
        branch_catch_up(&a->branches, &a->sim, platform_now_ms,
                        APP_PRELUDE_FRAME_MS / 2);
    }
    family_update(&a->families, &a->sim);
    update_notices(a);
    /* The bodies' motion is worked out on the body clock as it was before
     * this frame's step, as it always has been. */
    uint64_t body_before = a->body_ms;
    advance_clocks(a);

    if (frame_needs_drawing(a)) {
        /* Only for a frame that is drawn: nothing else reads the motion.
         * Running, every frame is drawn, so this changes nothing there;
         * paused, it saves two updates in three. */
        scene_update(&a->scene, &a->sim, APP_SPEEDS[a->speed], body_before);
        float fraction = (float)a->ticks_due / (float)FRAMES_PER_SECOND;
        draw_frame(a, fraction);
        platform_present(&a->canvas);
    }

    /* The same way out as the window's X button, so --frames tests it. */
    a->frames_done++;
    if (a->opt->frames > 0 && a->frames_done == a->opt->frames) {
        platform_post_quit();
    }
}

/* The zoom step with this name; false when there is none. */
static bool zoom_from_name(const char *name, int *level)
{
    static const char *const NAMES[CAMERA_LEVELS] = {"close", "normal", "far",
                                                     "world"};
    for (int i = 0; i < CAMERA_LEVELS; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *level = i;
            return true;
        }
    }
    return false;
}

/* A seed for when none is given: the clock, mixed. time() has only
 * seconds, which is enough for a program a person starts by hand. */
static uint64_t seed_from_clock(void)
{
    uint64_t state = (uint64_t)time(NULL);
    return rng_splitmix64(&state);
}

/* Settings from the file and options; false with err filled in. */
static bool read_settings(const Options *o, Settings *settings, char *err,
                          size_t errlen)
{
    *settings = settings_default();
    if (o->config_path != NULL &&
        !settings_load(settings, o->config_path, err, errlen)) {
        return false;
    }
    if (o->no_floor) {
        settings->min_creatures = 0;
    }
    return true;
}

/* What happens on leaving, in the order that keeps a failure visible. */
static int app_leave(App *a)
{
    int result = EXIT_FINE;
    char err[512];
    if (a->failed) {
        platform_error("out of memory while building a new world");
        result = EXIT_FAILED;
    }
    if (a->opt->screenshot_path != NULL &&
        !canvas_write_ppm(&a->canvas, a->opt->screenshot_path)) {
        platform_error("could not write the screenshot");
        result = EXIT_FAILED;
    }
    if (!a->failed && a->opt->save_path != NULL &&
        !save_write(&a->sim, a->opt->save_path, err, sizeof(err))) {
        platform_error(err);
        result = EXIT_FAILED;
    }
    if (a->opt->checksum) {
        printf("checksum 0x%016" PRIx64 "\n", sim_checksum(&a->sim));
    }
    return result;
}

int main(int argc, char **argv)
{
    /* static, like app below: in a browser page main never finishes and its
     * stack is reused, while frames keep reading the options through app.opt.
     * A plain local here would be a pointer to memory that changes under us. */
    static Options opt;
    char err[512];
    if (!options_parse(argc, argv, &opt, err, sizeof(err))) {
        char message[600];
        snprintf(message, sizeof(message), "%s (see agon-window --help)", err);
        platform_error(message);
        return EXIT_USAGE;
    }
    if (opt.help) {
        fputs(WINDOW_USAGE, stdout);
        return EXIT_FINE;
    }
    if (opt.version) {
        printf("agon-window %s\n", AGON_VERSION);
        return EXIT_FINE;
    }
    if (opt.once || opt.list_settings) {
        platform_error("--once and --settings are for agon");
        return EXIT_USAGE;
    }
    if (PLATFORM_IS_PAGE && (opt.save_path != NULL || opt.load_path != NULL)) {
        platform_error("a browser page has nowhere to save or load from");
        return EXIT_USAGE;
    }
    int zoom = CAMERA_NORMAL;
    if (opt.zoom_name != NULL && !zoom_from_name(opt.zoom_name, &zoom)) {
        platform_error("--zoom needs close, normal, far or world");
        return EXIT_USAGE;
    }
    Settings settings;
    if (!read_settings(&opt, &settings, err, sizeof(err))) {
        platform_error(err);
        return EXIT_USAGE;
    }

    /* static: this big struct lives for the whole program, not on main's
     * stack, and starts as all zeros. */
    static App app;
    app.opt = &opt;
    app.paused = opt.paused;
    app.sandbox.on = opt.sandbox;
    uint64_t seed = opt.seed_given ? opt.seed : seed_from_clock();
    int world_w = opt.size_given ? opt.width : DEFAULT_WORLD_W;
    int world_h = opt.size_given ? opt.height : DEFAULT_WORLD_H;
    if (!app_open_world(&app.sim, &opt, &settings, seed, world_w, world_h, err,
                        sizeof(err))) {
        platform_error(err);
        return EXIT_FAILED;
    }
    rng_seed(&app.seed_source, app.sim.seed);
    /* With --frames (tests and screenshots) the prelude runs whole before the
     * first frame, and only when --ancestors asks for it: a picture must come
     * out the same every time, which a prelude cut by a clock would not. */
    app.prelude_years = app_prelude_years(&opt, &settings, opt.frames == 0);
    app_prelude_start(&app.prelude, &app.sim, app.prelude_years);
    if (opt.frames > 0) {
        app_prelude_finish(&app.prelude, &app.sim);
    }
    app.speed = opt.speed > 0 ? app_speed_step(opt.speed) : APP_DEFAULT_SPEED;
    app.running = true;
    app.redraw = true;

    if (!scene_init(&app.scene, &app.sim) ||
        !family_init(&app.families, app.sim.pool.capacity) ||
        !canvas_init(&app.canvas, WINDOW_W, WINDOW_H)) {
        platform_error("out of memory");
        return EXIT_FAILED;
    }
    sprite_cache_set_art(&app.scene.sprites, !opt.no_art);
    scene_layout(&app.layout, WINDOW_W, WINDOW_H);
    camera_init(&app.camera, app.sim.world.w, app.sim.world.h,
                app.layout.view_w, app.layout.view_h);
    app.camera.level = zoom;
    if (!platform_open("agon", WINDOW_W, WINDOW_H, err, sizeof(err))) {
        platform_error(err);
        return EXIT_FAILED;
    }
    app.last_ms = platform_now_ms();

    platform_run(app_frame, &app, opt.frames > 0 ? 0 : FRAMES_PER_SECOND);

    int result = app_leave(&app);
    platform_close();
    canvas_free(&app.canvas);
    family_free(&app.families);
    scene_free(&app.scene);
    sandbox_free(&app.sandbox);
    branch_free(&app.branches);
    sim_free(&app.sim);
    return result;
}
