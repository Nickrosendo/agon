#include "platform.h"

#include <stdio.h> /* snprintf, fprintf */

/* Without this, SDL renames main on some systems to wrap its own around it.
 * Must come before the include: it changes what the header says. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

/* static at file level: private to this file. There is only one window. */
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int texture_w, texture_h;
static bool stop_requested;

bool platform_open(const char *title, int w, int h, char *err, size_t errlen)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        snprintf(err, errlen, "could not start SDL: %s", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, w, h,
                              SDL_WINDOW_RESIZABLE);
    if (window != NULL) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (renderer == NULL) {
        snprintf(err, errlen, "could not open a window: %s", SDL_GetError());
        platform_close();
        return false;
    }
    return true;
}

void platform_close(void)
{
    /* Each SDL_Destroy refuses NULL with an error, so check first. */
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}

/* SDL's key to ours; 0 for a key we do not use. */
static int key_code(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_KP_PLUS: return '+';
    case SDLK_KP_MINUS: return '-';
    default:
        return sym > 0 && sym < 128 ? (int)sym : 0;
    }
}

/* Fills *e from one SDL event; false for an event we do not use. */
static bool convert_event(const SDL_Event *in, Event *e)
{
    *e = (Event){.kind = EV_QUIT};
    switch (in->type) {
    case SDL_QUIT:
        return true;
    case SDL_KEYDOWN:
        e->kind = EV_KEY;
        e->key = key_code(in->key.keysym.sym);
        return e->key != 0;
    case SDL_MOUSEWHEEL:
        e->kind = EV_WHEEL;
        e->delta = in->wheel.y;
        /* The wheel event has no position: ask where the mouse is. */
        SDL_GetMouseState(&e->x, &e->y);
        return true;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (in->button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        e->kind = in->type == SDL_MOUSEBUTTONDOWN ? EV_DOWN : EV_UP;
        e->x = in->button.x;
        e->y = in->button.y;
        return true;
    case SDL_MOUSEMOTION:
        e->kind = EV_MOVE;
        e->x = in->motion.x;
        e->y = in->motion.y;
        return true;
    case SDL_WINDOWEVENT:
        if (in->window.event != SDL_WINDOWEVENT_SIZE_CHANGED) {
            return false;
        }
        e->kind = EV_RESIZE;
        e->w = in->window.data1;
        e->h = in->window.data2;
        return true;
    default:
        return false;
    }
}

bool platform_poll(Event *e)
{
    SDL_Event in;
    while (SDL_PollEvent(&in)) {
        if (convert_event(&in, e)) {
            return true;
        }
    }
    return false;
}

void platform_present(const Canvas *c)
{
    if (c->w == 0 || c->h == 0) {
        return;
    }
    /* A texture has a fixed size, so a new canvas size needs a new one. */
    if (texture == NULL || texture_w != c->w || texture_h != c->h) {
        if (texture != NULL) {
            SDL_DestroyTexture(texture);
        }
        /* ARGB8888 is SDL's name for a 32-bit number 0xAARRGGBB: exactly
         * how canvas pixels are stored. */
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, c->w, c->h);
        if (texture == NULL) {
            return; /* try again next frame */
        }
        texture_w = c->w;
        texture_h = c->h;
    }
    /* The pitch is the bytes from one row to the next. */
    SDL_UpdateTexture(texture, NULL, c->px, c->w * (int)sizeof(c->px[0]));
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void platform_error(const char *message)
{
#ifdef _WIN32
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "agon", message, NULL);
#else
    fprintf(stderr, "agon-window: %s\n", message);
#endif
}

uint64_t platform_now_ms(void)
{
    return SDL_GetTicks64();
}

void platform_stop(void)
{
    stop_requested = true;
}

void platform_post_quit(void)
{
    SDL_Event quit = {.type = SDL_QUIT};
    SDL_PushEvent(&quit);
}

void platform_run(void (*frame)(void *), void *arg, int frames_per_second)
{
    stop_requested = false;
    uint64_t start = SDL_GetTicks64();
    uint64_t count = 0;
    while (!stop_requested) {
        frame(arg);
        if (frames_per_second <= 0) {
            continue;
        }
        count++;
        /* Each deadline from the start, not from the last frame, so
         * 1000 / 30 rounding down does not build up into a drift. */
        uint64_t due = start + count * 1000u / (uint64_t)frames_per_second;
        uint64_t now = SDL_GetTicks64();
        if (now < due) {
            SDL_Delay((uint32_t)(due - now));
        } else if (now > due + 1000u / (uint64_t)frames_per_second) {
            /* Fell far behind: do not try to catch up. */
            start = now;
            count = 0;
        }
    }
}
