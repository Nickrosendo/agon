#include "term.h"

#include <errno.h>
#include <signal.h> /* sigaction, raise, sig_atomic_t */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* atexit, getenv */
#include <string.h> /* memcpy, strcmp, strlen */
#include <sys/ioctl.h> /* ioctl, TIOCGWINSZ, struct winsize */
#include <termios.h>   /* tcgetattr, tcsetattr, struct termios */
#include <unistd.h>    /* read, write, isatty */

/* gcc defines __SANITIZE_ADDRESS__ when building with
 * -fsanitize=address, so this part only exists in the debug build. */
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/common_interface_defs.h>
#endif

/* 256 KiB holds a full frame on large screens. A bigger frame is written
 * in several pieces, which can cost a little flicker, not correctness. */
#define FRAME_CAPACITY (256 * 1024)

/*
 * Everything below is "static": visible only inside this file, like a
 * module-level variable that is not exported. Outside variables also live
 * for the whole run of the program, not on the stack, and start at zero.
 */

/* The terminal settings found at start-up, restored by term_close. */
static struct termios saved_mode;

/* Read and written from signal handlers too. sig_atomic_t is an integer
 * the CPU reads and writes in one step, so a handler never sees half a
 * value. volatile tells the compiler the value can change behind its
 * back, so it must reread it every time instead of keeping a copy. */
static volatile sig_atomic_t is_open = 0;
static volatile sig_atomic_t resize_pending = 0;

static bool cleanup_installed = false;
static bool truecolor = false;

/* The frame being built. A fixed array, not malloc: it exists for the
 * whole run and is never freed. */
static char frame[FRAME_CAPACITY];
static size_t frame_len = 0;

/* The colours the terminal is currently set to, packed as 0xRRGGBB, or
 * -1 for unknown. Lets term_fg and term_bg skip repeating a colour. */
static int32_t current_fg = -1;
static int32_t current_bg = -1;

/* write() may write fewer bytes than asked, or be interrupted by a signal
 * before writing anything (EINTR); keep going until all is written.
 * Only uses write(), so it is safe to call from a signal handler. */
static void write_all(int fd, const char *bytes, size_t len)
{
    while (len > 0) {
        ssize_t written = write(fd, bytes, len);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return; /* nothing sensible to do if the terminal is gone */
        }
        bytes += written;
        len -= (size_t)written;
    }
}

static void write_text(int fd, const char *text)
{
    write_all(fd, text, strlen(text));
}

/* Interrupt, terminate, hang-up, quit and crash signals all land here.
 * SA_RESETHAND (below) has already put the signal back to its default
 * action, the one that ends the program, so raising it again after
 * cleaning up ends the program the way the signal intended, with the
 * exit status the shell expects. */
static void on_exit_signal(int sig)
{
    int saved_errno = errno;
    term_close();
    raise(sig);
    errno = saved_errno;
}

/* A handler may only touch sig_atomic_t flags and call the short list of
 * "async-signal-safe" functions (write, tcsetattr, raise, ...). printf,
 * malloc and most of the library are not on it. */
static void on_resize_signal(int sig)
{
    (void)sig; /* unused; the cast silences -Wunused-parameter */
    resize_pending = 1;
}

#ifdef __SANITIZE_ADDRESS__
/* The memory checker ends the program with its own exit, which skips
 * atexit. It calls this first, so the terminal is still restored. Its
 * report was printed on the alternate screen, which term_close throws
 * away, so say where to find it next time. */
static void on_checker_death(void)
{
    if (!is_open) {
        return; /* a report after a normal exit (a leak) is still visible */
    }
    term_close();
    if (!isatty(STDERR_FILENO)) {
        return; /* the report went to a file and is safe there */
    }
    write_text(STDERR_FILENO,
               "agon: the memory checker stopped the program. Its report "
               "was on the full screen and is lost; run again with "
               "2>build/report.txt to keep it.\n");
}
#endif

static void install_cleanup(void)
{
    if (cleanup_installed) {
        return;
    }
    cleanup_installed = true;

    atexit(term_close);

    struct sigaction exit_action = {0};
    exit_action.sa_handler = on_exit_signal;
    sigemptyset(&exit_action.sa_mask);
    /* glibc defines SA_RESETHAND as the unsigned 0x80000000, but sa_flags
     * is a signed int. The cast says the bit pattern is what we want. */
    exit_action.sa_flags = (int)SA_RESETHAND;

    const int exit_signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGABRT};
    /* sizeof works on a real array (not on a pointer): total bytes divided
     * by the bytes of one element gives the count. */
    size_t count = sizeof(exit_signals) / sizeof(exit_signals[0]);
    for (size_t i = 0; i < count; i++) {
        sigaction(exit_signals[i], &exit_action, NULL);
    }

#ifdef __SANITIZE_ADDRESS__
    /* The memory checker catches crashes itself and prints a report;
     * taking these signals from it would lose the report. */
    __sanitizer_set_death_callback(on_checker_death);
#else
    sigaction(SIGSEGV, &exit_action, NULL);
    sigaction(SIGBUS, &exit_action, NULL);
    sigaction(SIGFPE, &exit_action, NULL);
#endif

    struct sigaction resize_action = {0};
    resize_action.sa_handler = on_resize_signal;
    sigemptyset(&resize_action.sa_mask);
    /* Resume a read() or write() the signal interrupted instead of
     * failing it with EINTR. */
    resize_action.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &resize_action, NULL);
}

static bool colorterm_says_truecolor(void)
{
    const char *value = getenv("COLORTERM");
    if (value == NULL) {
        return false;
    }
    /* == on two strings compares addresses; strcmp compares characters
     * and returns 0 when they match. */
    return strcmp(value, "truecolor") == 0 || strcmp(value, "24bit") == 0;
}

bool term_open(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &saved_mode) != 0) {
        return false;
    }
    install_cleanup();
    truecolor = colorterm_says_truecolor();

    struct termios raw = saved_mode; /* a struct assignment copies it */
    /* Turn off: echoing keys (ECHO), waiting for Enter (ICANON), Ctrl-V
     * (IEXTEN), Ctrl-S/Ctrl-Q pausing output (IXON), Enter arriving as
     * '\n' instead of '\r' (ICRNL). ISIG stays on, so Ctrl-C still sends
     * the interrupt signal. The flags are unsigned; casting before ~
     * keeps -Wconversion quiet. */
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    /* read() returns at once, with 0 bytes if no key is waiting. */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    /* Marked open before the change, so a signal arriving in between
     * still restores the saved settings. */
    is_open = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        is_open = 0;
        return false;
    }
    /* Alternate screen (the shell's screen comes back on close), hidden
     * cursor, cleared screen. */
    write_text(STDOUT_FILENO, "\033[?1049h\033[?25l\033[2J");
    return true;
}

void term_close(void)
{
    if (!is_open) {
        return;
    }
    is_open = 0;
    /* Default colours, visible cursor, back to the shell's screen. */
    write_text(STDOUT_FILENO, "\033[0m\033[?25h\033[?1049l");
    /* TCSAFLUSH also drops keys typed but not read, so they do not
     * spill into the shell. */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_mode);
}

void term_size(int *cols, int *rows)
{
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 &&
        size.ws_row > 0) {
        *cols = size.ws_col;
        *rows = size.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

bool term_resized(void)
{
    if (resize_pending) {
        resize_pending = 0;
        return true;
    }
    return false;
}

int term_key(void)
{
    unsigned char byte;
    if (read(STDIN_FILENO, &byte, 1) == 1) {
        return byte;
    }
    return -1;
}

bool term_truecolor(void)
{
    return truecolor;
}

static void frame_append(const char *bytes, size_t len)
{
    if (frame_len + len > FRAME_CAPACITY) {
        write_all(STDOUT_FILENO, frame, frame_len);
        frame_len = 0;
    }
    if (len > FRAME_CAPACITY) {
        write_all(STDOUT_FILENO, bytes, len);
        return;
    }
    memcpy(frame + frame_len, bytes, len);
    frame_len += len;
}

void term_begin(void)
{
    frame_len = 0;
    /* Something else may have changed the colours since the last frame;
     * forget what we think they are. */
    current_fg = -1;
    current_bg = -1;
}

void term_clear(void)
{
    /* Reset colours first: the terminal clears with the current
     * background colour. */
    frame_append("\033[0m\033[2J", 8);
    current_fg = -1;
    current_bg = -1;
}

void term_move(int col, int row)
{
    char sequence[32];
    /* The terminal counts from 1. snprintf writes at most
     * sizeof(sequence) bytes, including the final '\0', and returns how
     * many characters the text has. */
    int len = snprintf(sequence, sizeof(sequence), "\033[%d;%dH", row + 1,
                       col + 1);
    frame_append(sequence, (size_t)len);
}

/* 256-colour terminals have a 6x6x6 colour cube at codes 16 to 231.
 * (v + 25) / 51 maps 0..255 to the nearest of the six levels 0..5. */
static int cube_index(uint8_t r, uint8_t g, uint8_t b)
{
    return 16 + 36 * ((r + 25) / 51) + 6 * ((g + 25) / 51) + (b + 25) / 51;
}

/* layer is 38 for the foreground (text) colour, 48 for the background. */
static void set_colour(int layer, uint8_t r, uint8_t g, uint8_t b,
                       int32_t *current)
{
    int32_t packed = (r << 16) | (g << 8) | b;
    if (packed == *current) {
        return;
    }
    *current = packed;

    char sequence[32];
    int len;
    if (truecolor) {
        len = snprintf(sequence, sizeof(sequence), "\033[%d;2;%d;%d;%dm",
                       layer, r, g, b);
    } else {
        len = snprintf(sequence, sizeof(sequence), "\033[%d;5;%dm", layer,
                       cube_index(r, g, b));
    }
    frame_append(sequence, (size_t)len);
}

void term_fg(uint8_t r, uint8_t g, uint8_t b)
{
    set_colour(38, r, g, b, &current_fg);
}

void term_bg(uint8_t r, uint8_t g, uint8_t b)
{
    set_colour(48, r, g, b, &current_bg);
}

void term_put(const char *utf8)
{
    frame_append(utf8, strlen(utf8));
}

void term_end(void)
{
    write_all(STDOUT_FILENO, frame, frame_len);
    frame_len = 0;
}
