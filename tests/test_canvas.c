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
    TEST_SUMMARY();
}
