#include "test.h"

#include <stdio.h>  /* remove */
#include <string.h> /* memcmp */

#include "canvas.h"
#include "font.h"

#define RED 0xFFFF0000u
#define BLACK 0xFF000000u

static uint32_t pixel(const Canvas *c, int x, int y)
{
    return c->px[y * c->w + x];
}

static int count_colour(const Canvas *c, uint32_t colour)
{
    int n = 0;
    for (int i = 0; i < c->w * c->h; i++) {
        n += c->px[i] == colour;
    }
    return n;
}

static void test_rgb(void)
{
    CHECK(canvas_rgb(0x12, 0x34, 0x56) == 0xFF123456u);
}

static void test_rect_clips_each_edge(void)
{
    /* A 4 by 4 block used as 2 by 2: the rest of the block is the guard,
     * and must stay untouched whatever is drawn. */
    Canvas c;
    CHECK(canvas_init(&c, 4, 4));
    uint32_t *block = c.px;
    CHECK(canvas_resize(&c, 2, 2));
    CHECK(c.px == block);
    block[4] = 0x12345678u;
    block[15] = 0x12345678u;

    canvas_rect(&c, -5, -5, 100, 100, RED);
    CHECK(count_colour(&c, RED) == 4);
    canvas_fill(&c, BLACK);
    canvas_rect(&c, -1, 0, 2, 1, RED); /* past the left: only (0, 0) */
    CHECK(pixel(&c, 0, 0) == RED && count_colour(&c, RED) == 1);
    canvas_fill(&c, BLACK);
    canvas_rect(&c, 1, 1, 9, 9, RED); /* past right and bottom: only (1, 1) */
    CHECK(pixel(&c, 1, 1) == RED && count_colour(&c, RED) == 1);
    canvas_fill(&c, BLACK);
    canvas_rect(&c, 0, -3, 1, 4, RED); /* past the top: only (0, 0) */
    CHECK(pixel(&c, 0, 0) == RED && count_colour(&c, RED) == 1);

    CHECK(block[4] == 0x12345678u);
    CHECK(block[15] == 0x12345678u);
    canvas_free(&c);
}

static void test_mask_bits_and_flip(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 4, 2));
    /* Binary reads left to right: 1000 is the leftmost column. */
    const uint16_t rows[4] = {0x8, 0x1, 0, 0};
    canvas_mask(&c, rows, 4, 0, 0, 1, false, RED);
    CHECK(pixel(&c, 0, 0) == RED);
    CHECK(pixel(&c, 3, 1) == RED);
    CHECK(count_colour(&c, RED) == 2);

    canvas_fill(&c, BLACK);
    canvas_mask(&c, rows, 4, 0, 0, 1, true, RED);
    CHECK(pixel(&c, 3, 0) == RED);
    CHECK(pixel(&c, 0, 1) == RED);
    CHECK(count_colour(&c, RED) == 2);
    canvas_free(&c);
}

static void test_mask_scale(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 8, 8));
    const uint16_t rows[2] = {0x2, 0x1}; /* binary 10, then 01 */
    canvas_mask(&c, rows, 2, 1, 1, 3, false, RED);
    CHECK(count_colour(&c, RED) == 18); /* two squares of 3 by 3 */
    CHECK(pixel(&c, 1, 1) == RED);
    CHECK(pixel(&c, 3, 3) == RED);
    CHECK(pixel(&c, 4, 4) == RED);
    CHECK(pixel(&c, 6, 6) == RED);
    CHECK(pixel(&c, 4, 1) == BLACK);
    canvas_free(&c);
}

static void test_mask_fully_outside_draws_nothing(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 4, 4));
    const uint16_t rows[2] = {0x3, 0x3};
    canvas_mask(&c, rows, 2, -10, 0, 1, false, RED);
    canvas_mask(&c, rows, 2, 100, 0, 1, false, RED);
    canvas_mask(&c, rows, 2, 0, -10, 1, false, RED);
    canvas_mask(&c, rows, 2, 0, 100, 1, false, RED);
    CHECK(count_colour(&c, RED) == 0);
    canvas_free(&c);
}

static void test_empty_canvas_is_safe(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 0, 0));
    const uint16_t rows[1] = {1};
    canvas_fill(&c, RED);
    canvas_rect(&c, 0, 0, 10, 10, RED);
    canvas_mask(&c, rows, 1, 0, 0, 1, false, RED);
    font_text(&c, 0, 0, "hello", RED, BLACK);
    font_block(&c, 0, 0, 4, RED, BLACK);
    CHECK(canvas_resize(&c, 0, 0));
    CHECK(canvas_resize(&c, 3, 0));
    canvas_fill(&c, RED);
    CHECK(canvas_resize(&c, 2, 2)); /* and back to a real one */
    CHECK(count_colour(&c, BLACK) == 4);
    canvas_free(&c);
    canvas_free(&c); /* safe twice */
}

static void test_resize_keeps_block_when_smaller(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 10, 10));
    uint32_t *block = c.px;
    CHECK(canvas_resize(&c, 5, 5));
    CHECK(c.px == block);
    CHECK(canvas_resize(&c, 10, 10));
    CHECK(c.px == block);
    CHECK(c.capacity == 100);
    canvas_free(&c);
}

static void test_ppm_header(void)
{
    const char *path = "build/test_canvas.ppm";
    Canvas c;
    CHECK(canvas_init(&c, 2, 1));
    canvas_rect(&c, 1, 0, 1, 1, canvas_rgb(1, 2, 3));
    CHECK(canvas_write_ppm(&c, path));
    canvas_free(&c);

    unsigned char data[32];
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f != NULL) {
        size_t n = fread(data, 1, sizeof(data), f);
        fclose(f);
        const char header[] = "P6\n2 1\n255\n";
        size_t header_len = sizeof(header) - 1;
        CHECK(n == header_len + 6);
        CHECK(memcmp(data, header, header_len) == 0);
        CHECK(data[header_len + 3] == 1 && data[header_len + 5] == 3);
    }
    remove(path);
}

static void test_ppm_unwritable_path(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 1, 1));
    CHECK(!canvas_write_ppm(&c, "no/such/folder/x.ppm"));
    canvas_free(&c);
}

static void test_font_draws_inside_its_cell(void)
{
    Canvas c;
    CHECK(canvas_init(&c, FONT_W * 2, FONT_H));
    font_text(&c, 0, 0, "A", RED, BLACK);
    /* 'A' has set pixels, and all of them are in the first cell. */
    int in_first = 0, in_second = 0;
    for (int y = 0; y < FONT_H; y++) {
        for (int x = 0; x < FONT_W; x++) {
            in_first += pixel(&c, x, y) == RED;
            in_second += pixel(&c, x + FONT_W, y) == RED;
        }
    }
    CHECK(in_first > 0);
    CHECK(in_second == 0);
    /* A byte outside ASCII draws as '?': no reading before the table. */
    font_text(&c, 0, 0, "\xC3", RED, BLACK);
    CHECK(count_colour(&c, RED) > 0);
    canvas_free(&c);
}

static void test_font_block_heights(void)
{
    Canvas c;
    CHECK(canvas_init(&c, FONT_W, FONT_H));
    font_block(&c, 0, 0, 0, RED, BLACK);
    CHECK(count_colour(&c, RED) == 0);
    font_block(&c, 0, 0, 4, RED, BLACK);
    CHECK(count_colour(&c, RED) == FONT_W * FONT_H / 2);
    CHECK(pixel(&c, 0, FONT_H - 1) == RED);
    CHECK(pixel(&c, 0, 0) == BLACK);
    font_block(&c, 0, 0, 8, RED, BLACK);
    CHECK(count_colour(&c, RED) == FONT_W * FONT_H);
    canvas_free(&c);
}

static void test_copy_clips(void)
{
    Canvas to, from;
    CHECK(canvas_init(&to, 4, 4));
    CHECK(canvas_init(&from, 2, 2));
    canvas_fill(&from, RED);
    canvas_copy(&to, &from, 1, 1);
    CHECK(to.px[1 * 4 + 1] == RED && to.px[2 * 4 + 2] == RED);
    CHECK(to.px[0] == BLACK && to.px[3 * 4 + 3] == BLACK);
    /* Over each edge, and wholly off: only what lands is written. */
    canvas_fill(&to, BLACK);
    canvas_copy(&to, &from, -1, -1);
    CHECK(to.px[0] == RED && to.px[1] == BLACK && to.px[4] == BLACK);
    canvas_fill(&to, BLACK);
    canvas_copy(&to, &from, 3, 3);
    CHECK(to.px[3 * 4 + 3] == RED && to.px[2 * 4 + 3] == BLACK);
    canvas_fill(&to, BLACK);
    canvas_copy(&to, &from, 100, 0);
    canvas_copy(&to, &from, 0, 100);
    canvas_copy(&to, &from, -100, 0);
    canvas_copy(&to, &from, 0, -100);
    CHECK(to.px[0] == BLACK && to.px[15] == BLACK);
    /* Empty canvases at either end. */
    Canvas empty;
    CHECK(canvas_init(&empty, 0, 0));
    canvas_copy(&empty, &from, 0, 0);
    canvas_copy(&to, &empty, 0, 0);
    canvas_free(&empty);
    canvas_free(&from);
    canvas_free(&to);
}

/* The quick way and the careful way must draw the same pixels: at every position
 * round the canvas's edges, flipped or not, compared with drawing bit by bit. */
static void test_mask_quick_path_matches_rects(void)
{
    enum { CW = 24, CH = 20 };
    uint16_t rows[8] = {0x81, 0x42, 0x24, 0x18, 0x18, 0xE7, 0x00, 0xFF};
    Canvas a, b;
    CHECK(canvas_init(&a, CW, CH));
    CHECK(canvas_init(&b, CW, CH));
    for (int y = -9; y <= CH + 1; y++) {
        for (int x = -9; x <= CW + 1; x++) {
            for (int flip = 0; flip < 2; flip++) {
                canvas_fill(&a, 0);
                canvas_fill(&b, 0);
                canvas_mask(&a, rows, 8, x, y, 1, flip != 0, RED);
                for (int r = 0; r < 8; r++) {
                    for (int i = 0; i < 8; i++) {
                        if ((rows[r] >> (7 - i)) & 1) {
                            int column = flip != 0 ? 7 - i : i;
                            canvas_rect(&b, x + column, y + r, 1, 1, RED);
                        }
                    }
                }
                bool same = true;
                for (int k = 0; k < CW * CH; k++) {
                    same = same && a.px[k] == b.px[k];
                }
                CHECK(same);
            }
        }
    }
    canvas_free(&a);
    canvas_free(&b);
}

static CanvasClip whole(const Canvas *c)
{
    CanvasClip clip = {0, 0, c->w, c->h};
    return clip;
}

static void test_picture_skips_ink_0_and_flips(void)
{
    /* 3 by 2, stride 4 (the 4th column of each row is not part of it). */
    const uint8_t ink[] = {1, 0, 2, 9, 0, 2, 0, 9};
    const uint32_t colours[] = {0, 0xFF111111u, 0xFF222222u};
    Canvas c;
    CHECK(canvas_init(&c, 5, 4));
    canvas_picture(&c, ink, 4, 3, 2, 1, 1, 1, false, colours, whole(&c));
    CHECK(pixel(&c, 1, 1) == 0xFF111111u && pixel(&c, 2, 1) == BLACK);
    CHECK(pixel(&c, 3, 1) == 0xFF222222u && pixel(&c, 2, 2) == 0xFF222222u);
    CHECK(pixel(&c, 4, 1) == BLACK); /* the stride's spare column: not drawn */
    CHECK(count_colour(&c, BLACK) == 20 - 3);
    canvas_fill(&c, BLACK);
    canvas_picture(&c, ink, 4, 3, 2, 1, 1, 1, true, colours, whole(&c));
    CHECK(pixel(&c, 3, 1) == 0xFF111111u && pixel(&c, 1, 1) == 0xFF222222u);
    CHECK(pixel(&c, 2, 2) == 0xFF222222u && count_colour(&c, BLACK) == 17);
    canvas_free(&c);
}

static void test_picture_clips_each_edge(void)
{
    const uint8_t ink[] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    const uint32_t colours[] = {0, 0xFFFF0000u};
    Canvas c;
    CHECK(canvas_init(&c, 8, 8));
    CanvasClip clip = {2, 2, 4, 4}; /* pixels 2..5 both ways */
    /* Over each edge in turn, and the corners: only the clip's inside changes. */
    for (int dy = -3; dy <= 5; dy += 2) {
        for (int dx = -3; dx <= 5; dx += 2) {
            canvas_fill(&c, BLACK);
            canvas_picture(&c, ink, 3, 3, 3, dx, dy, 1, false, colours, clip);
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    bool in_clip = x >= 2 && x < 6 && y >= 2 && y < 6;
                    bool in_picture = x >= dx && x < dx + 3 && y >= dy && y < dy + 3;
                    CHECK(pixel(&c, x, y) == (in_clip && in_picture ? RED : BLACK));
                }
            }
        }
    }
    /* A picture bigger than the canvas, with a clip bigger too. */
    CanvasClip huge = {-50, -50, 200, 200};
    canvas_fill(&c, BLACK);
    canvas_picture(&c, ink, 3, 3, 3, -1, 6, 1, false, colours, huge);
    CHECK(count_colour(&c, RED) == 2 * 2);
    canvas_free(&c);
}

static void test_picture_scale(void)
{
    const uint8_t ink[] = {1, 0, 0, 1};
    const uint32_t colours[] = {0, 0xFFFF0000u};
    Canvas c;
    CHECK(canvas_init(&c, 8, 8));
    canvas_picture(&c, ink, 2, 2, 2, 1, 1, 3, false, colours, whole(&c));
    CHECK(count_colour(&c, RED) == 2 * 9);
    CHECK(pixel(&c, 1, 1) == RED && pixel(&c, 3, 3) == RED);
    CHECK(pixel(&c, 4, 1) == BLACK && pixel(&c, 4, 4) == RED);
    CHECK(pixel(&c, 7, 7) == BLACK);
    canvas_free(&c);
}

static void test_darken_ellipse(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 20, 20));
    uint32_t grey = 0xFFC8C8C8u; /* 200 in every channel */
    canvas_fill(&c, grey);
    canvas_darken_ellipse(&c, 10, 10, 5, 3, 141, whole(&c));
    CHECK(pixel(&c, 10, 10) == 0xFF6E6E6Eu); /* 200 * 141 / 256 = 110 */
    CHECK(pixel(&c, 15, 10) == 0xFF6E6E6Eu && pixel(&c, 5, 10) == 0xFF6E6E6Eu);
    CHECK(pixel(&c, 10, 13) == 0xFF6E6E6Eu);
    CHECK(pixel(&c, 16, 10) == grey && pixel(&c, 10, 14) == grey);
    CHECK(pixel(&c, 15, 13) == grey); /* the box's corner is outside */
    CHECK(pixel(&c, 0, 0) == grey && pixel(&c, 19, 19) == grey);
    /* Clipped: nothing outside the clip changes, nothing off the canvas is touched. */
    canvas_fill(&c, grey);
    CanvasClip clip = {10, 0, 10, 20};
    canvas_darken_ellipse(&c, 10, 10, 5, 3, 141, clip);
    CHECK(pixel(&c, 9, 10) == grey && pixel(&c, 10, 10) == 0xFF6E6E6Eu);
    canvas_fill(&c, grey);
    canvas_darken_ellipse(&c, -2, 21, 6, 6, 141, whole(&c));
    CHECK(pixel(&c, 0, 19) == 0xFF6E6E6Eu && pixel(&c, 10, 10) == grey);
    canvas_free(&c);
}

static void test_pixel_clips(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 6, 6));
    CanvasClip clip = {1, 1, 3, 3};
    canvas_pixel(&c, 0, 0, 1, RED, clip);   /* outside the clip */
    canvas_pixel(&c, 3, 3, 2, RED, clip);   /* a 2 by 2 pixel, only (3, 3) inside */
    canvas_pixel(&c, 1, 1, 1, RED, clip);
    canvas_pixel(&c, -4, -4, 2, RED, clip); /* off the canvas */
    CHECK(count_colour(&c, RED) == 2);
    CHECK(pixel(&c, 3, 3) == RED && pixel(&c, 1, 1) == RED);
    canvas_free(&c);
}

int main(void)
{
    test_rgb();
    test_rect_clips_each_edge();
    test_mask_bits_and_flip();
    test_mask_scale();
    test_mask_fully_outside_draws_nothing();
    test_empty_canvas_is_safe();
    test_resize_keeps_block_when_smaller();
    test_ppm_header();
    test_ppm_unwritable_path();
    test_font_draws_inside_its_cell();
    test_font_block_heights();
    test_mask_quick_path_matches_rects();
    test_copy_clips();
    test_picture_skips_ink_0_and_flips();
    test_picture_clips_each_edge();
    test_picture_scale();
    test_darken_ellipse();
    test_pixel_clips();
    TEST_SUMMARY();
}
